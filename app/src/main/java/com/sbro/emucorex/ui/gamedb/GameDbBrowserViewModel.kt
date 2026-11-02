package com.sbro.emucorex.ui.gamedb

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.gamedb.GameDbCatalogEntry
import com.sbro.emucorex.data.gamedb.GameDbCatalogRepository
import com.sbro.emucorex.data.gamedb.GameDbFixId
import com.sbro.emucorex.data.gamedb.GameDbGameOverride
import com.sbro.emucorex.data.gamedb.GameDbOverrideRenderer
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.GameDbSettingItem
import com.sbro.emucorex.data.gamedb.GameDbUserOverrides
import com.sbro.emucorex.data.gamedb.category
import java.util.Locale
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.mapLatest
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.time.Duration.Companion.milliseconds

enum class GameDbFilter {
    ALL,
    CORE,
    GRAPHICS
}

data class GameDbBrowserUiState(
    val loading: Boolean = true,
    val query: String = "",
    val filter: GameDbFilter = GameDbFilter.ALL,
    val totalCount: Int = 0,
    val entries: List<GameDbEntryUi> = emptyList(),
    val loadFailed: Boolean = false,
    val coreFixesEnabled: Boolean = true,
    val graphicsFixesEnabled: Boolean = true,
    val customizedGames: Int = 0
)

private data class GameDbSearch(
    val rawQuery: String,
    val debouncedQuery: String,
    val filter: GameDbFilter
)

private data class GameDbContent(
    val entries: List<GameDbEntryUi>,
    val coreFixesEnabled: Boolean,
    val graphicsFixesEnabled: Boolean,
    val loading: Boolean,
    val loadFailed: Boolean,
    val customizedGames: Int
)

@OptIn(FlowPreview::class, ExperimentalCoroutinesApi::class)
class GameDbBrowserViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = GameDbCatalogRepository(application)
    private val overrides = GameDbUserOverrides(application)
    private val preferences = AppPreferences(application)

    private val allEntries = MutableStateFlow<List<GameDbCatalogEntry>>(emptyList())
    private val query = MutableStateFlow("")
    private val filter = MutableStateFlow(GameDbFilter.ALL)
    private val loading = MutableStateFlow(true)
    private val loadFailed = MutableStateFlow(false)
    private var appliedInitialQuery: String? = null
    private var renderJob: Job? = null

    val queryText = query.asStateFlow()

    private val fixSwitches = combine(
        preferences.enableGameFixes,
        preferences.autoGsHardwareFixes
    ) { core, graphics -> core to graphics }

    private val searchState = combine(
        query,
        query.debounce(120.milliseconds),
        filter
    ) { rawQuery, debouncedQuery, currentFilter ->
        GameDbSearch(rawQuery, debouncedQuery, currentFilter)
    }

    private val displayEntries = combine(
        allEntries,
        overrides.games,
        fixSwitches
    ) { entries, games, switches ->
        withContext(Dispatchers.Default) {
            entries.map { entry ->
                buildEntryUi(entry, games[entry.serial.uppercase()] ?: GameDbGameOverride())
            }
        }
    }

    private val content = combine(
        displayEntries,
        fixSwitches,
        loading,
        loadFailed,
        overrides.games
    ) { entries, switches, isLoading, failed, games ->
        GameDbContent(
            entries = entries,
            coreFixesEnabled = switches.first,
            graphicsFixesEnabled = switches.second,
            loading = isLoading,
            loadFailed = failed,
            customizedGames = games.size
        )
    }

    val uiState = combine(content, searchState) { current, search -> current to search }
        .mapLatest { (current, search) ->
            withContext(Dispatchers.Default) { buildUiState(current, search) }
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(5_000),
            initialValue = GameDbBrowserUiState()
        )

    init {
        viewModelScope.launch {
            runCatching {
                withContext(Dispatchers.IO) { repository.loadEntries() }
            }.onSuccess {
                allEntries.value = it
            }.onFailure {
                loadFailed.value = true
            }
            loading.value = false
        }
    }

    fun setQuery(value: String) {
        query.value = value
    }

    fun setFilter(value: GameDbFilter) {
        filter.value = value
    }

    fun openQuery(value: String) {
        val normalized = value.trim()
        if (normalized.isBlank() || appliedInitialQuery == normalized) return
        appliedInitialQuery = normalized
        filter.value = GameDbFilter.ALL
        query.value = normalized
    }

    fun toggleFix(serial: String, id: String, enabled: Boolean) {
        overrides.setEnabled(serial, id, enabled)
        scheduleRender()
    }

    fun setFixValue(serial: String, id: String, value: String?) {
        overrides.setValue(serial, id, value)
        scheduleRender()
    }

    fun addFix(serial: String, id: String, value: String?) {
        overrides.addFix(serial, id, value)
        scheduleRender()
    }

    fun resetFix(serial: String, id: String) {
        overrides.removeFix(serial, id)
        scheduleRender()
    }

    fun resetGame(serial: String) {
        overrides.resetGame(serial)
        scheduleRender()
    }

    fun resetAll() {
        overrides.resetAll()
        scheduleRender()
    }

    /**
     * The emulator only reads GameDB when a game boots, so the overlay is rebuilt as soon as an edit
     * lands. A short debounce coalesces bursts of switches, and the write runs on an app-level scope
     * so leaving the screen cannot cancel it before the file reaches the disk.
     */
    private fun scheduleRender() {
        renderJob?.cancel()
        renderJob = renderScope.launch {
            delay(RENDER_DEBOUNCE_MS)
            val context = getApplication<Application>()
            runCatching {
                GameDbOverrideRenderer.render(context, NativeApp.dataRoot(context), force = true)
            }
        }
    }

    private fun buildUiState(content: GameDbContent, search: GameDbSearch): GameDbBrowserUiState {
        val normalizedQuery = search.debouncedQuery.trim().lowercase(Locale.ROOT)
        val filtered = content.entries.filter { entry ->
            val matchesQuery = normalizedQuery.isBlank() || entry.searchableText.contains(normalizedQuery)
            val matchesFilter = when (search.filter) {
                GameDbFilter.ALL -> true
                GameDbFilter.CORE -> entry.coreSettingCount > 0
                GameDbFilter.GRAPHICS -> entry.graphicsSettingCount > 0
            }
            matchesQuery && matchesFilter
        }
        return GameDbBrowserUiState(
            loading = content.loading,
            query = search.rawQuery,
            filter = search.filter,
            totalCount = content.entries.size,
            entries = filtered,
            loadFailed = content.loadFailed,
            coreFixesEnabled = content.coreFixesEnabled,
            graphicsFixesEnabled = content.graphicsFixesEnabled,
            customizedGames = content.customizedGames
        )
    }

    private fun buildEntryUi(base: GameDbCatalogEntry, game: GameDbGameOverride): GameDbEntryUi {
        val settings = ArrayList<GameDbSettingUi>(base.settings.size + game.addedCount)
        val known = HashSet<String>(base.settings.size)

        base.settings.forEach { item ->
            known += item.id
            settings += item.toUi(game)
        }

        game.addedKeys().sorted().forEach { id ->
            if (id in known) return@forEach
            val section = GameDbFixId.sectionOf(id) ?: return@forEach
            val key = GameDbFixId.keyOf(id)
            val category = section.category
            val item = GameDbSettingItem(
                category = category,
                key = key,
                value = game.valueOf(id) ?: GameDbFixCatalog.defaultValue(section, key),
                note = null,
                controlledByGameFixesToggle = category != GameDbSettingCategory.GRAPHICS_FIX
            )
            settings += item.toUi(game)
        }

        val addedKeys = game.addedKeys().joinToString(" ") {
            GameDbFixId.keyOf(it).lowercase(Locale.ROOT)
        }
        return GameDbEntryUi(
            entry = base,
            settings = settings,
            enabledCount = settings.count { it.enabled },
            customizedCount = game.customizedCount,
            disabledCount = game.disabledCount,
            searchableText = if (addedKeys.isBlank()) base.searchableText else "${base.searchableText} $addedKeys"
        )
    }

    private fun GameDbSettingItem.toUi(game: GameDbGameOverride): GameDbSettingUi {
        val override = game.valueOf(id)
        val effectiveValue = override ?: value
        val effective = if (override == null) this else copy(value = override)
        return GameDbSettingUi(
            id = id,
            category = category,
            enabled = game.isEnabled(id),
            added = game.isAdded(id),
            value = effectiveValue,
            valueEdited = override != null,
            presentation = GameDbSettingPresentationMapper.map(effective),
            valueOptions = GameDbValueCatalog.optionsFor(category, key)
        )
    }

    private companion object {
        private const val RENDER_DEBOUNCE_MS = 120L
        private val renderScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    }
}

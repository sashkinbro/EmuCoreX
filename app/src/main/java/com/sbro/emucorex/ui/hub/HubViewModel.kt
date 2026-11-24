package com.sbro.emucorex.ui.hub

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.hub.HubItem
import com.sbro.emucorex.data.hub.HubKind
import com.sbro.emucorex.data.hub.HubLocaleResolver
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.hub.HubRepository
import com.sbro.emucorex.data.hub.HubThemeEntry
import com.sbro.emucorex.data.hub.HubThemeSnapshot
import com.sbro.emucorex.data.theme.InstalledThemePackage
import com.sbro.emucorex.data.theme.ThemePackageRepository
import com.sbro.emucorex.core.ProPurchaseManager
import com.sbro.emucorex.R
import com.sbro.emucorex.ui.theme.ThemeMode
import android.app.Activity
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.text.Normalizer
import java.util.Locale

enum class HubTab {
    NEWS,
    VIDEOS,
    HISTORY,
    MANUALS,
    THEMES,
    FAVORITES
}

enum class HubSortOrder {
    NEWEST_FIRST,
    OLDEST_FIRST,
    TITLE_ASCENDING
}

enum class HubProductFilter {
    ALL,
    EMUCOREX,
    EMUCOREV,
    PLAYSTATION_2,
    OTHER_EMULATORS
}

enum class HubThemeAccessFilter { ALL, FREE, PRO }

enum class HubThemeToneFilter { ALL, DARK, LIGHT }

data class HubThemeFilters(
    val access: HubThemeAccessFilter = HubThemeAccessFilter.ALL,
    val tone: HubThemeToneFilter = HubThemeToneFilter.ALL,
    val categoryId: String? = null,
    val installedOnly: Boolean = false
)

data class HubThemeActionState(
    val busyIds: Set<String> = emptySet(),
    val message: String? = null
)

data class HubUiState(
    val items: List<HubItem> = emptyList(),
    val selectedTab: HubTab = HubTab.NEWS,
    val searchQuery: String = "",
    val sortOrder: HubSortOrder = HubSortOrder.NEWEST_FIRST,
    val productFilter: HubProductFilter = HubProductFilter.ALL,
    val featuredOnly: Boolean = false,
    val isInitialLoading: Boolean = true,
    val isRefreshing: Boolean = false,
    val showRefreshIndicator: Boolean = false,
    val isOffline: Boolean = false,
    val error: Throwable? = null,
    val fallbackLocale: String? = null
) {
    val hasActiveFilters: Boolean
        get() = productFilter != HubProductFilter.ALL || featuredOnly

    val visibleItems: List<HubItem>
        get() {
            var selected = when (selectedTab) {
                HubTab.NEWS -> items.filter { it.kind == HubKind.NEWS }
                HubTab.VIDEOS -> items.filter { it.kind == HubKind.VIDEOS }
                HubTab.HISTORY -> items.filter { it.kind == HubKind.HISTORY }
                HubTab.MANUALS -> items.filter { it.kind == HubKind.MANUALS }
                HubTab.THEMES -> emptyList()
                HubTab.FAVORITES -> items.filter(HubItem::isFavorite)
            }
            selected = selected.filter { it.matchesProductFilter(productFilter) }
            if (featuredOnly) selected = selected.filter(HubItem::featured)

            val normalizedQuery = searchQuery.normalizedSearchText()
            if (normalizedQuery.isNotBlank()) {
                selected = selected.filter { item ->
                    buildString {
                        append(item.title)
                        append(' ')
                        append(item.summary)
                        append(' ')
                        append(item.categories.joinToString(" "))
                        append(' ')
                        append(item.tags.joinToString(" "))
                        append(' ')
                        append(item.channelTitle.orEmpty())
                        append(' ')
                        append(item.year?.toString().orEmpty())
                    }.normalizedSearchText().contains(normalizedQuery)
                }
            }
            return selected.sortedWith(sortOrder.comparator())
        }
}

class HubViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = HubRepository.get(application)
    private val locale = HubLocaleResolver.resolve(
        application.resources.configuration.locales.get(0)?.toLanguageTag()
    )
    private val _uiState = MutableStateFlow(HubUiState())
    val uiState: StateFlow<HubUiState> = _uiState.asStateFlow()
    private var itemsJob: Job? = null
    var hasShownInitialSkeleton: Boolean = false
        private set

    init {
        observeItems()
        refresh(force = false, showIndicator = false)
    }

    fun selectTab(tab: HubTab) {
        _uiState.update { it.copy(selectedTab = tab) }
    }

    fun selectRelativeTab(offset: Int) {
        val tabs = HubTab.entries
        val current = tabs.indexOf(_uiState.value.selectedTab).coerceAtLeast(0)
        selectTab(tabs[(current + offset + tabs.size) % tabs.size])
    }

    fun setSearchQuery(query: String) {
        _uiState.update { it.copy(searchQuery = query) }
    }

    fun clearSearch() = setSearchQuery("")

    fun setSortOrder(sortOrder: HubSortOrder) {
        _uiState.update { it.copy(sortOrder = sortOrder) }
    }

    fun setProductFilter(productFilter: HubProductFilter) {
        _uiState.update { it.copy(productFilter = productFilter) }
    }

    fun setFeaturedOnly(featuredOnly: Boolean) {
        _uiState.update { it.copy(featuredOnly = featuredOnly) }
    }

    fun clearFilters() {
        _uiState.update {
            it.copy(
                productFilter = HubProductFilter.ALL,
                featuredOnly = false
            )
        }
    }

    private val themePackages = ThemePackageRepository.getInstance(application)
    private val preferences = AppPreferences(application)
    private val proPurchaseManager = ProPurchaseManager.getInstance(application)

    val themes: StateFlow<HubThemeSnapshot?> = repository.themes
    val installedThemes: StateFlow<List<InstalledThemePackage>> = themePackages.installed
    val appliedThemeId: StateFlow<String?> = combine(
        preferences.themeMode,
        preferences.themePackageId
    ) { mode, id -> if (mode == ThemeMode.PACKAGE) id else null }
        .stateIn(viewModelScope, SharingStarted.Eagerly, null)
    val isProUnlocked: StateFlow<Boolean> = proPurchaseManager.state
        .map { it.isProUnlocked }
        .stateIn(viewModelScope, SharingStarted.Eagerly, proPurchaseManager.state.value.isProUnlocked)

    private val _themeFilters = MutableStateFlow(HubThemeFilters())
    val themeFilters: StateFlow<HubThemeFilters> = _themeFilters.asStateFlow()
    private val _themeActions = MutableStateFlow(HubThemeActionState())
    val themeActions: StateFlow<HubThemeActionState> = _themeActions.asStateFlow()

    fun updateThemeFilters(transform: (HubThemeFilters) -> HubThemeFilters) {
        _themeFilters.update(transform)
    }

    fun consumeThemeMessage() {
        _themeActions.update { it.copy(message = null) }
    }

    fun purchasePro(activity: Activity) {
        proPurchaseManager.purchase(activity, source = "hub_themes")
    }

    /** Installs (or updates) [entry]; when [apply] is set, applies it right after a successful install. */
    fun installTheme(entry: HubThemeEntry, apply: Boolean) {
        runThemeAction(entry.id) {
            val bytes = repository.downloadThemePackage(entry)
            val installed = themePackages.install(bytes)
            val pkg = installed.getOrNull()
            if (pkg == null) {
                postThemeMessage(R.string.hub_theme_install_failed, installed.exceptionOrNull()?.message)
            } else if (apply) {
                applyInstalled(entry)
            } else {
                postThemeMessage(R.string.hub_theme_installed_message, null)
            }
        }
    }

    fun applyTheme(entry: HubThemeEntry) {
        runThemeAction(entry.id) { applyInstalled(entry) }
    }

    fun removeTheme(entry: HubThemeEntry) {
        runThemeAction(entry.id) {
            if (themePackages.remove(entry.id)) preferences.clearThemePackage(entry.id)
        }
    }

    private suspend fun applyInstalled(entry: HubThemeEntry) {
        if (!preferences.applyThemePackage(entry.id, entry.requiresPro)) {
            postThemeMessage(R.string.hub_theme_requires_pro, null)
        }
    }

    private fun postThemeMessage(resId: Int, detail: String?) {
        val base = getApplication<Application>().getString(resId)
        _themeActions.update { it.copy(message = if (detail.isNullOrBlank()) base else "$base: $detail") }
    }

    private fun runThemeAction(id: String, block: suspend () -> Unit) {
        if (id in _themeActions.value.busyIds) return
        _themeActions.update { it.copy(busyIds = it.busyIds + id) }
        viewModelScope.launch {
            try {
                block()
            } catch (cancel: kotlinx.coroutines.CancellationException) {
                throw cancel
            } catch (error: Throwable) {
                postThemeMessage(R.string.hub_theme_install_failed, error.message)
            } finally {
                _themeActions.update { it.copy(busyIds = it.busyIds - id) }
            }
        }
    }

    fun markInitialSkeletonShown() {
        hasShownInitialSkeleton = true
    }

    fun refresh(force: Boolean = true, showIndicator: Boolean = true) {
        if (_uiState.value.isRefreshing) return
        viewModelScope.launch {
            _uiState.update {
                it.copy(
                    isRefreshing = true,
                    showRefreshIndicator = showIndicator,
                    error = null
                )
            }
            runCatching { repository.sync(locale, forceRefresh = force) }
                .onSuccess { result ->
                    _uiState.update {
                        it.copy(
                            isInitialLoading = false,
                            isRefreshing = false,
                            showRefreshIndicator = false,
                            isOffline = false,
                            error = null,
                            fallbackLocale = result.fallbackLocale
                        )
                    }
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isInitialLoading = false,
                            isRefreshing = false,
                            showRefreshIndicator = false,
                            isOffline = it.items.isNotEmpty(),
                            error = error
                        )
                    }
                }
        }
    }

    fun toggleFavorite(item: HubItem) {
        viewModelScope.launch { repository.toggleFavorite(item) }
    }

    private fun observeItems() {
        itemsJob?.cancel()
        itemsJob = viewModelScope.launch {
            repository.observeItems(locale).collectLatest { items ->
                _uiState.update {
                    it.copy(
                        items = items,
                        isInitialLoading = items.isEmpty() && it.isInitialLoading
                    )
                }
            }
        }
    }
}

private fun String.normalizedSearchText(): String = Normalizer.normalize(this, Normalizer.Form.NFKC)
    .lowercase(Locale.ROOT)
    .replace(Regex("[\\p{Punct}\\s]+"), " ")
    .trim()

private fun HubItem.matchesProductFilter(filter: HubProductFilter): Boolean {
    val products = relatedProductIds.mapTo(mutableSetOf()) { it.lowercase(Locale.ROOT) }
    return when (filter) {
        HubProductFilter.ALL -> true
        HubProductFilter.EMUCOREX -> "emucorex" in products
        HubProductFilter.EMUCOREV -> "emucorev" in products
        HubProductFilter.PLAYSTATION_2 -> products.any { it in PS2_PRODUCT_IDS }
        HubProductFilter.OTHER_EMULATORS -> products.none {
            it == "emucorex" || it == "emucorev" || it in PS2_PRODUCT_IDS
        }
    }
}

private fun HubSortOrder.comparator(): Comparator<HubItem> = when (this) {
    HubSortOrder.NEWEST_FIRST -> compareByDescending<HubItem> { it.sortDateKey() }
        .thenByDescending(HubItem::updatedAt)
        .thenBy { it.title.lowercase(Locale.ROOT) }
        .thenBy(HubItem::id)
    HubSortOrder.OLDEST_FIRST -> compareBy<HubItem> { it.sortDateKey() }
        .thenBy(HubItem::updatedAt)
        .thenBy { it.title.lowercase(Locale.ROOT) }
        .thenBy(HubItem::id)
    HubSortOrder.TITLE_ASCENDING -> compareBy<HubItem> { it.title.lowercase(Locale.ROOT) }
        .thenByDescending { it.sortDateKey() }
        .thenBy(HubItem::id)
}

private fun HubItem.sortDateKey(): String = when (kind) {
    HubKind.HISTORY -> eventDate?.takeIf(String::isNotBlank) ?: publishedAt
    else -> publishedAt
}

private val PS2_PRODUCT_IDS = setOf(
    "ps2",
    "playstation-2",
    "pcsx2",
    "aethersx2",
    "nethersx2",
    "armsx2"
)

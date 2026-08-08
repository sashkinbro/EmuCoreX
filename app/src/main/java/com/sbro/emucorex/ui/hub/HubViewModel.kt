package com.sbro.emucorex.ui.hub

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.hub.HubItem
import com.sbro.emucorex.data.hub.HubKind
import com.sbro.emucorex.data.hub.HubLocaleResolver
import com.sbro.emucorex.data.hub.HubRepository
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
    FAVORITES
}

data class HubUiState(
    val items: List<HubItem> = emptyList(),
    val selectedTab: HubTab = HubTab.NEWS,
    val searchQuery: String = "",
    val isInitialLoading: Boolean = true,
    val isRefreshing: Boolean = false,
    val isOffline: Boolean = false,
    val error: Throwable? = null,
    val fallbackLocale: String? = null
) {
    val visibleItems: List<HubItem>
        get() {
            val selected = when (selectedTab) {
                HubTab.NEWS -> items.filter { it.kind == HubKind.NEWS }
                HubTab.VIDEOS -> items.filter { it.kind == HubKind.VIDEOS }
                HubTab.HISTORY -> items.filter { it.kind == HubKind.HISTORY }
                HubTab.MANUALS -> items.filter { it.kind == HubKind.MANUALS }
                HubTab.FAVORITES -> items.filter(HubItem::isFavorite)
            }
            val normalizedQuery = searchQuery.normalizedSearchText()
            if (normalizedQuery.isBlank()) return selected
            return selected.filter { item ->
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
        refresh(force = false)
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

    fun markInitialSkeletonShown() {
        hasShownInitialSkeleton = true
    }

    fun refresh(force: Boolean = true) {
        if (_uiState.value.isRefreshing) return
        viewModelScope.launch {
            _uiState.update { it.copy(isRefreshing = true, error = null) }
            runCatching { repository.sync(locale, forceRefresh = force) }
                .onSuccess { result ->
                    _uiState.update {
                        it.copy(
                            isInitialLoading = false,
                            isRefreshing = false,
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

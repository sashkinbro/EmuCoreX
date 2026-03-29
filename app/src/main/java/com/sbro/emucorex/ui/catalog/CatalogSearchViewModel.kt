package com.sbro.emucorex.ui.catalog

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.ps2.Ps2CatalogRepository
import com.sbro.emucorex.data.ps2.Ps2CatalogSummary
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class CatalogSearchUiState(
    val query: String = "",
    val isLoading: Boolean = true,
    val isLoadingMore: Boolean = false,
    val hasCatalog: Boolean = false,
    val hasMore: Boolean = false,
    val results: List<Ps2CatalogSummary> = emptyList(),
    val availableGenres: List<String> = emptyList(),
    val availableYears: List<Int> = emptyList(),
    val selectedGenre: String? = null,
    val selectedYear: Int? = null,
    val minRating: Double? = null
)

class CatalogSearchViewModel(application: Application) : AndroidViewModel(application) {
    companion object {
        private const val PAGE_SIZE = 60
    }

    private val repository = Ps2CatalogRepository(application)
    private var refreshJob: Job? = null
    private var loadMoreJob: Job? = null

    private val _uiState = MutableStateFlow(CatalogSearchUiState())
    val uiState: StateFlow<CatalogSearchUiState> = _uiState.asStateFlow()

    init {
        refresh(showFullscreenLoader = true)
    }

    fun updateQuery(query: String) {
        _uiState.value = _uiState.value.copy(query = query)
        scheduleRefresh()
    }

    fun updateGenre(genre: String?) {
        _uiState.value = _uiState.value.copy(selectedGenre = genre)
        refresh(showFullscreenLoader = false)
    }

    fun updateYear(year: Int?) {
        _uiState.value = _uiState.value.copy(selectedYear = year)
        refresh(showFullscreenLoader = false)
    }

    fun updateMinRating(minRating: Double?) {
        _uiState.value = _uiState.value.copy(minRating = minRating)
        refresh(showFullscreenLoader = false)
    }

    fun clearFilters() {
        _uiState.value = _uiState.value.copy(
            selectedGenre = null,
            selectedYear = null,
            minRating = null
        )
        refresh(showFullscreenLoader = false)
    }

    private fun scheduleRefresh() {
        refreshJob?.cancel()
        refreshJob = viewModelScope.launch {
            delay(220)
            refresh(showFullscreenLoader = false)
        }
    }

    fun refresh(showFullscreenLoader: Boolean = false) {
        refreshJob?.cancel()
        val currentState = _uiState.value
        _uiState.value = currentState.copy(
            isLoading = showFullscreenLoader,
            isLoadingMore = false,
            hasMore = false
        )
        refreshJob = viewModelScope.launch(Dispatchers.IO) {
            val snapshot = _uiState.value
            val hasCatalog = repository.hasCatalog()
            val availableGenres = if (hasCatalog && snapshot.availableGenres.isEmpty()) {
                repository.getAvailableGenres()
            } else {
                snapshot.availableGenres
            }
            val availableYears = if (hasCatalog && snapshot.availableYears.isEmpty()) {
                repository.getAvailableYears()
            } else {
                snapshot.availableYears
            }
            val results = if (hasCatalog) {
                repository.search(
                    query = snapshot.query,
                    genre = snapshot.selectedGenre,
                    year = snapshot.selectedYear,
                    minRating = snapshot.minRating,
                    limit = PAGE_SIZE,
                    offset = 0
                )
            } else {
                emptyList()
            }
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                hasCatalog = hasCatalog,
                hasMore = hasCatalog && results.size >= PAGE_SIZE,
                results = results,
                availableGenres = availableGenres,
                availableYears = availableYears
            )
        }
    }

    fun loadMoreIfNeeded(lastVisibleIndex: Int) {
        val state = _uiState.value
        if (state.isLoading || state.isLoadingMore || !state.hasCatalog || !state.hasMore) return
        if (lastVisibleIndex < state.results.lastIndex - 8) return

        loadMoreJob?.cancel()
        loadMoreJob = viewModelScope.launch(Dispatchers.IO) {
            val snapshot = _uiState.value
            _uiState.value = snapshot.copy(isLoadingMore = true)
            val nextPage = repository.search(
                query = snapshot.query,
                genre = snapshot.selectedGenre,
                year = snapshot.selectedYear,
                minRating = snapshot.minRating,
                limit = PAGE_SIZE,
                offset = snapshot.results.size
            )
            val merged = snapshot.results + nextPage.filterNot { next ->
                snapshot.results.any { it.igdbId == next.igdbId }
            }
            _uiState.value = _uiState.value.copy(
                isLoadingMore = false,
                results = merged,
                hasMore = nextPage.size >= PAGE_SIZE
            )
        }
    }
}

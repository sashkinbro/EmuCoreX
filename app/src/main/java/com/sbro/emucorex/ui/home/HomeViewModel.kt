package com.sbro.emucorex.ui.home

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameRepository
import com.sbro.emucorex.data.RecentGameEntry
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import androidx.core.net.toUri
import com.sbro.emucorex.data.ps2.Ps2CatalogRepository
import kotlinx.coroutines.sync.withPermit
import java.text.Normalizer

enum class HomeSortOption {
    TITLE, SIZE_DESC, SIZE_ASC
}

data class HomeUiState(
    val games: List<GameItem> = emptyList(),
    val recentGames: List<GameItem> = emptyList(),
    val isLoading: Boolean = true,
    val isRefreshing: Boolean = false,
    val isBootstrapping: Boolean = true,
    val gameFolderSet: Boolean = false,
    val biosConfigured: Boolean = false,
    val biosValid: Boolean = false,
    val setupComplete: Boolean = false,
    val searchQuery: String = "",
    val sortOption: HomeSortOption = HomeSortOption.TITLE
)

class HomeViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = GameRepository()
    private val preferences = AppPreferences(application)
    private val catalogRepository = Ps2CatalogRepository(application)
    private var allGames: List<GameItem> = emptyList()
    private var recentEntries: List<RecentGameEntry> = emptyList()
    private var coverSyncJob: Job? = null
    private var searchJob: Job? = null
    private var biosInitialized = false
    private var libraryInitialized = false

    private val _uiState = MutableStateFlow(HomeUiState())
    val uiState: StateFlow<HomeUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            preferences.gamePath.collect { path ->
                val isAccessible = SetupValidator.isGameFolderAccessible(getApplication(), path)
                _uiState.value = _uiState.value.copy(
                    gameFolderSet = isAccessible,
                    isLoading = isAccessible
                )
                if (isAccessible && path != null) {
                    if (path.startsWith("content://")) {
                        scanGamesFromUri(path.toUri(), isInitialLoad = !libraryInitialized)
                    } else {
                        scanGames(path, isInitialLoad = !libraryInitialized)
                    }
                } else {
                    allGames = emptyList()
                    libraryInitialized = true
                    publishVisibleGames()
                    updateBootstrapState()
                    _uiState.value = _uiState.value.copy(isLoading = false, isRefreshing = false)
                }
            }
        }
        viewModelScope.launch {
            preferences.biosPath.collect { path ->
                _uiState.value = _uiState.value.copy(
                    biosConfigured = path != null,
                    biosValid = BiosValidator.hasUsableBiosFiles(getApplication(), path)
                )
                biosInitialized = true
                updateBootstrapState()
            }
        }
        viewModelScope.launch {
            preferences.onboardingCompleted.collect { completed ->
                _uiState.value = _uiState.value.copy(setupComplete = completed)
            }
        }
        viewModelScope.launch {
            preferences.recentGames.collect { entries ->
                recentEntries = entries
                publishVisibleGames()
            }
        }
    }

    fun onFolderSelected(uri: Uri) {
        val context = getApplication<Application>()
        context.contentResolver.takePersistableUriPermission(
            uri,
            android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
        )

        viewModelScope.launch {
            preferences.setGamePath(uri.toString())
            scanGamesFromUri(uri)
        }
    }

    fun refreshGames() {
        viewModelScope.launch {
            val path = preferences.gamePath.first() ?: return@launch
            val uri = path.toUri()
            if (path.startsWith("content://")) {
                scanGamesFromUri(uri)
            } else {
                scanGames(path)
            }
        }
    }

    fun updateSearchQuery(query: String) {
        _uiState.value = _uiState.value.copy(searchQuery = query)
        searchJob?.cancel()
        searchJob = viewModelScope.launch {
            delay(180)
            publishVisibleGames()
        }
    }

    fun updateSortOption(option: HomeSortOption) {
        _uiState.value = _uiState.value.copy(sortOption = option)
        publishVisibleGames()
    }

    private fun scanGames(path: String, isInitialLoad: Boolean = false) {
        viewModelScope.launch(Dispatchers.IO) {
            val showFullScreenLoader = isInitialLoad || allGames.isEmpty()
            _uiState.value = _uiState.value.copy(
                isLoading = showFullScreenLoader,
                isRefreshing = !showFullScreenLoader
            )
            allGames = repository.scanDirectory(path, getApplication())
            if (isInitialLoad) {
                libraryInitialized = true
            }
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                isRefreshing = false
            )
            publishVisibleGames()
            updateBootstrapState()
            syncMissingCovers()
        }
    }

    private fun scanGamesFromUri(uri: Uri, isInitialLoad: Boolean = false) {
        viewModelScope.launch(Dispatchers.IO) {
            val showFullScreenLoader = isInitialLoad || allGames.isEmpty()
            _uiState.value = _uiState.value.copy(
                isLoading = showFullScreenLoader,
                isRefreshing = !showFullScreenLoader
            )
            val context = getApplication<Application>()
            allGames = repository.scanDirectoryFromUri(uri, context)
            if (isInitialLoad) {
                libraryInitialized = true
            }
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                isRefreshing = false
            )
            publishVisibleGames()
            updateBootstrapState()
            syncMissingCovers()
        }
    }

    private fun publishVisibleGames() {
        val state = _uiState.value
        val query = normalizeSearchToken(state.searchQuery)
        val filtered = allGames.filter { game ->
            query.isBlank() ||
                normalizeSearchToken(game.title).contains(query) ||
                normalizeSearchToken(game.fileName).contains(query) ||
                normalizeSearchToken(game.serial).contains(query)
        }
        val sorted = when (state.sortOption) {
            HomeSortOption.TITLE -> filtered.sortedBy { it.title.lowercase(Locale.ROOT) }
            HomeSortOption.SIZE_DESC -> filtered.sortedByDescending { it.fileSize }
            HomeSortOption.SIZE_ASC -> filtered.sortedBy { it.fileSize }
        }
        val gamesByPath = allGames.associateBy { it.path }
        val recentGames = recentEntries.mapNotNull { entry ->
            gamesByPath[entry.path]
        }.filter { game ->
            query.isBlank() ||
                normalizeSearchToken(game.title).contains(query) ||
                normalizeSearchToken(game.fileName).contains(query) ||
                normalizeSearchToken(game.serial).contains(query)
        }
        _uiState.value = _uiState.value.copy(
            games = sorted,
            recentGames = recentGames
        )
    }

    private fun updateBootstrapState() {
        _uiState.value = _uiState.value.copy(
            isBootstrapping = !(biosInitialized && libraryInitialized)
        )
    }

    private fun syncMissingCovers() {
        coverSyncJob?.cancel()
        val context = getApplication<Application>()
        coverSyncJob = viewModelScope.launch(Dispatchers.IO) {
            val gamesToProcess = allGames.filter { it.coverArtPath == null || it.coverArtPath.startsWith("http") }
            if (gamesToProcess.isEmpty()) return@launch
            val semaphore = kotlinx.coroutines.sync.Semaphore(3)
            
            gamesToProcess.forEach { game ->
                launch {
                    semaphore.withPermit {
                        val downloadedCover = repository.downloadCoverForGame(game, context)
                        if (downloadedCover != null && downloadedCover != game.coverArtPath) {
                            synchronized(this@HomeViewModel) {
                                allGames = allGames.map { 
                                    if (it.path == game.path) it.copy(coverArtPath = downloadedCover) else it 
                                }
                            }
                            publishVisibleGames()
                        }
                    }
                }
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        catalogRepository.close()
    }

    private fun normalizeSearchToken(value: String?): String {
        if (value.isNullOrBlank()) return ""
        val normalized = Normalizer.normalize(value, Normalizer.Form.NFD)
        return normalized
            .replace(Regex("\\p{InCombiningDiacriticalMarks}+"), "")
            .lowercase(Locale.ROOT)
            .replace(Regex("[^\\p{L}\\p{N}]+"), " ")
            .trim()
    }
}

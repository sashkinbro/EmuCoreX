package com.sbro.emucorex.ui.home

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameLibraryCacheRepository
import com.sbro.emucorex.data.GameLibraryCacheSnapshot
import com.sbro.emucorex.data.GameRepository
import com.sbro.emucorex.data.RecentGameEntry
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import androidx.core.net.toUri
import kotlinx.coroutines.sync.withPermit
import java.text.Normalizer

enum class HomeSortOption {
    TITLE_ASC,
    TITLE_DESC,
    RECENT_DESC,
    RECENT_ASC,
    SIZE_DESC,
    SIZE_ASC
}

enum class HomeLibraryViewMode {
    GRID, LIST
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
    val showRecentGames: Boolean = true,
    val showHomeSearch: Boolean = true,
    val searchQuery: String = "",
    val sortOption: HomeSortOption = HomeSortOption.TITLE_ASC,
    val libraryViewMode: HomeLibraryViewMode = HomeLibraryViewMode.GRID
)

class HomeViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val AUTO_REFRESH_INTERVAL_MS = 15 * 60 * 1000L
    }

    private val repository = GameRepository()
    private val libraryCacheRepository = GameLibraryCacheRepository(application)
    private val preferences = AppPreferences(application)
    private var allGames: List<GameItem> = emptyList()
    private var recentEntries: List<RecentGameEntry> = emptyList()
    private var coverSyncJob: Job? = null
    private var searchJob: Job? = null
    private var biosInitialized = false
    private var libraryInitialized = false
    private var currentLibraryRoot: String? = null

    private val _uiState = MutableStateFlow(HomeUiState())
    val uiState: StateFlow<HomeUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            preferences.gamePath.distinctUntilChanged().collect { path ->
                val context = getApplication<Application>()
                val migratedPath = path
                    ?.takeIf { !it.startsWith("content://") }
                    ?.let { DocumentPathResolver.findAccessibleTreeUriForRawPath(context, it)?.toString() }

                if (migratedPath != null && migratedPath != path) {
                    preferences.setGamePath(migratedPath)
                    return@collect
                }

                val effectivePath = migratedPath ?: path
                currentLibraryRoot = effectivePath
                val isAccessible = SetupValidator.isGameFolderAccessible(context, effectivePath)
                _uiState.value = _uiState.value.copy(
                    gameFolderSet = isAccessible,
                    isLoading = isAccessible
                )
                if (isAccessible && effectivePath != null) {
                    if (effectivePath.startsWith("content://")) {
                        scanGamesFromUri(effectivePath.toUri(), isInitialLoad = !libraryInitialized)
                    } else {
                        scanGames(effectivePath, isInitialLoad = !libraryInitialized)
                    }
                } else {
                    allGames = emptyList()
                    currentLibraryRoot = null
                    libraryInitialized = true
                    publishVisibleGames()
                    updateBootstrapState()
                    _uiState.value = _uiState.value.copy(isLoading = false, isRefreshing = false)
                }
            }
        }
        viewModelScope.launch {
            preferences.biosPath.distinctUntilChanged().collect { path ->
                _uiState.value = _uiState.value.copy(
                    biosConfigured = path != null,
                    biosValid = BiosValidator.hasUsableBiosFiles(getApplication(), path)
                )
                biosInitialized = true
                updateBootstrapState()
            }
        }
        viewModelScope.launch {
            preferences.onboardingCompleted.distinctUntilChanged().collect { completed ->
                _uiState.value = _uiState.value.copy(setupComplete = completed)
            }
        }
        viewModelScope.launch {
            preferences.recentGames.distinctUntilChanged().collect { entries ->
                recentEntries = entries
                publishVisibleGames()
            }
        }
        viewModelScope.launch {
            preferences.showRecentGames.distinctUntilChanged().collect { enabled ->
                _uiState.value = _uiState.value.copy(showRecentGames = enabled)
                publishVisibleGames()
            }
        }
        viewModelScope.launch {
            preferences.showHomeSearch.distinctUntilChanged().collect { enabled ->
                _uiState.value = _uiState.value.copy(showHomeSearch = enabled)
            }
        }
        viewModelScope.launch {
            preferences.homeLibraryViewMode.distinctUntilChanged().collect { mode ->
                _uiState.value = _uiState.value.copy(
                    libraryViewMode = if (mode == 1) HomeLibraryViewMode.LIST else HomeLibraryViewMode.GRID
                )
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
        }
    }

    fun onBiosFolderSelected(uri: Uri) {
        val context = getApplication<Application>()
        context.contentResolver.takePersistableUriPermission(
            uri,
            android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
        )

        viewModelScope.launch {
            preferences.setBiosPath(uri.toString())
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

    fun toggleLibraryViewMode() {
        viewModelScope.launch {
            preferences.setHomeLibraryViewMode(
                if (_uiState.value.libraryViewMode == HomeLibraryViewMode.GRID) 1 else 0
            )
        }
    }

    private fun scanGames(path: String, isInitialLoad: Boolean = false) {
        viewModelScope.launch(Dispatchers.IO) {
            val cacheSnapshot = resolveCacheSnapshot(path)
            val cachedGames = cacheSnapshot.games
            val showFullScreenLoader = isInitialLoad && cachedGames.isEmpty()
            _uiState.value = _uiState.value.copy(
                isLoading = showFullScreenLoader,
                isRefreshing = !showFullScreenLoader
            )
            publishCachedGamesIfAvailable(path, cachedGames)
            if (shouldSkipAutoRescan(isInitialLoad, cacheSnapshot)) {
                libraryInitialized = true
                _uiState.value = _uiState.value.copy(
                    isLoading = false,
                    isRefreshing = false
                )
                updateBootstrapState()
                syncMissingCovers()
                return@launch
            }
            allGames = repository.scanDirectory(path, getApplication(), cachedGames.associateBy { it.path })
            currentLibraryRoot = path
            libraryInitialized = true
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                isRefreshing = false
            )
            publishVisibleGames()
            updateBootstrapState()
            libraryCacheRepository.save(path, allGames)
            syncMissingCovers()
        }
    }

    private fun scanGamesFromUri(uri: Uri, isInitialLoad: Boolean = false) {
        viewModelScope.launch(Dispatchers.IO) {
            val rootPath = uri.toString()
            val cacheSnapshot = resolveCacheSnapshot(rootPath)
            val cachedGames = cacheSnapshot.games
            val showFullScreenLoader = isInitialLoad && cachedGames.isEmpty()
            _uiState.value = _uiState.value.copy(
                isLoading = showFullScreenLoader,
                isRefreshing = !showFullScreenLoader
            )
            publishCachedGamesIfAvailable(rootPath, cachedGames)
            if (shouldSkipAutoRescan(isInitialLoad, cacheSnapshot)) {
                libraryInitialized = true
                _uiState.value = _uiState.value.copy(
                    isLoading = false,
                    isRefreshing = false
                )
                updateBootstrapState()
                syncMissingCovers()
                return@launch
            }
            val context = getApplication<Application>()
            allGames = repository.scanDirectoryFromUri(uri, context, cachedGames.associateBy { it.path })
            currentLibraryRoot = rootPath
            libraryInitialized = true
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                isRefreshing = false
            )
            publishVisibleGames()
            updateBootstrapState()
            libraryCacheRepository.save(rootPath, allGames)
            syncMissingCovers()
        }
    }

    private fun resolveCacheSnapshot(rootPath: String): GameLibraryCacheSnapshot {
        val inMemoryGames = allGames.takeIf { currentLibraryRoot == rootPath && it.isNotEmpty() }
        return if (inMemoryGames != null) {
            GameLibraryCacheSnapshot(inMemoryGames, System.currentTimeMillis())
        } else {
            libraryCacheRepository.loadSnapshot(rootPath)
        }
    }

    private fun shouldSkipAutoRescan(isInitialLoad: Boolean, cacheSnapshot: GameLibraryCacheSnapshot): Boolean {
        if (!isInitialLoad) return false
        if (cacheSnapshot.games.isEmpty()) return false
        val cacheAge = System.currentTimeMillis() - cacheSnapshot.savedAt
        return cacheAge in 0 until AUTO_REFRESH_INTERVAL_MS
    }

    private fun publishCachedGamesIfAvailable(rootPath: String, cachedGames: List<GameItem>) {
        if (cachedGames.isEmpty()) return
        if (currentLibraryRoot == rootPath && allGames.isNotEmpty()) return
        allGames = cachedGames
        currentLibraryRoot = rootPath
        libraryInitialized = true
        publishVisibleGames()
        updateBootstrapState()
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
            HomeSortOption.TITLE_ASC -> filtered.sortedWith(
                compareBy<GameItem> { normalizeSortToken(it.title) }
                    .thenBy { normalizeSortToken(it.fileName) }
            )
            HomeSortOption.TITLE_DESC -> filtered.sortedWith(
                compareByDescending<GameItem> { normalizeSortToken(it.title) }
                    .thenByDescending { normalizeSortToken(it.fileName) }
            )
            HomeSortOption.RECENT_DESC -> filtered.sortedWith(
                compareByDescending<GameItem> { it.lastModified }
                    .thenBy { normalizeSortToken(it.title) }
            )
            HomeSortOption.RECENT_ASC -> filtered.sortedWith(
                compareBy<GameItem> { it.lastModified }
                    .thenBy { normalizeSortToken(it.title) }
            )
            HomeSortOption.SIZE_DESC -> filtered.sortedWith(
                compareByDescending<GameItem> { it.fileSize }
                    .thenBy { normalizeSortToken(it.title) }
            )
            HomeSortOption.SIZE_ASC -> filtered.sortedWith(
                compareBy<GameItem> { it.fileSize }
                    .thenBy { normalizeSortToken(it.title) }
            )
        }
        val gamesByPath = allGames.associateBy { it.path }
        val recentGames = recentEntries.mapNotNull { entry ->
            gamesByPath[entry.path]
        }.filter { game ->
            query.isBlank() ||
                normalizeSearchToken(game.title).contains(query) ||
                normalizeSearchToken(game.fileName).contains(query) ||
                normalizeSearchToken(game.serial).contains(query)
        }.takeIf { state.showRecentGames }.orEmpty()
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
            currentLibraryRoot?.let { rootPath ->
                libraryCacheRepository.save(rootPath, allGames)
            }
        }
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

    private fun normalizeSortToken(value: String?): String {
        if (value.isNullOrBlank()) return ""
        val normalized = Normalizer.normalize(value, Normalizer.Form.NFD)
        return normalized
            .replace(Regex("\\p{InCombiningDiacriticalMarks}+"), "")
            .lowercase(Locale.ROOT)
            .trim()
    }
}

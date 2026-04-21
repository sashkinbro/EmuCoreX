package com.sbro.emucorex.ui.home

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CoverArtRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameLibraryCacheRepository
import com.sbro.emucorex.data.GameLibraryCacheSnapshot
import com.sbro.emucorex.data.GameRepository
import com.sbro.emucorex.data.RecentGameEntry
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import androidx.core.net.toUri
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.sync.withLock
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
    val libraryViewMode: HomeLibraryViewMode = HomeLibraryViewMode.GRID,
    val isCoverArtDisabled: Boolean = true
)

private data class DeferredLibraryScan(
    val rootPath: String,
    val isInitialLoad: Boolean
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
    private var preferEnglishGameTitles = false
    private var titlesPreferenceInitialized = false
    private var coverArtStyleInitialized = false
    private var coverBaseUrlInitialized = false
    private var currentCoverArtStyle = AppPreferences.COVER_ART_STYLE_DISABLED
    private var currentCoverDownloadBaseUrl: String? = null
    private val scanMutex = Mutex()
    private var deferredLibraryScan: DeferredLibraryScan? = null
    private var deferredWorkJob: Job? = null
    private var deferredCoverSync = false

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
                if (isAccessible) {
                    libraryInitialized = true
                    updateBootstrapState()
                }
                _uiState.value = _uiState.value.copy(
                    gameFolderSet = isAccessible,
                    isLoading = false,
                    isRefreshing = isAccessible
                )
                if (isAccessible && effectivePath != null) {
                    requestLibraryScan(effectivePath, isInitialLoad = !libraryInitialized)
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
            preferences.preferEnglishGameTitles.distinctUntilChanged().collect { enabled ->
                val shouldRefreshLibrary = titlesPreferenceInitialized && preferEnglishGameTitles != enabled
                preferEnglishGameTitles = enabled
                titlesPreferenceInitialized = true
                if (!shouldRefreshLibrary) return@collect
                val rootPath = currentLibraryRoot ?: return@collect
                allGames = emptyList()
                requestLibraryScan(rootPath)
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
        viewModelScope.launch {
            preferences.coverArtStyle.distinctUntilChanged().collect { style ->
                _uiState.value = _uiState.value.copy(
                    isCoverArtDisabled = style == AppPreferences.COVER_ART_STYLE_DISABLED
                )
                val shouldRefreshLibrary = coverArtStyleInitialized && currentCoverArtStyle != style
                currentCoverArtStyle = style
                coverArtStyleInitialized = true
                if (shouldRefreshLibrary) {
                    handleCoverSourceChanged()
                }
            }
        }
        viewModelScope.launch {
            preferences.coverDownloadBaseUrl.distinctUntilChanged().collect { baseUrl ->
                val shouldRefreshLibrary = coverBaseUrlInitialized && currentCoverDownloadBaseUrl != baseUrl
                currentCoverDownloadBaseUrl = baseUrl
                coverBaseUrlInitialized = true
                if (shouldRefreshLibrary) {
                    handleCoverSourceChanged()
                }
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
            requestLibraryScan(path)
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
            scanMutex.withLock {
                if (shouldDeferLibraryWork(path, isInitialLoad)) return@withLock
                val cacheSnapshot = resolveCacheSnapshot(path)
                val cachedGames = cacheSnapshot.games
                val showFullScreenLoader = false
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
                    return@withLock
                }
                val scannedGames = repository.scanDirectory(
                    path,
                    getApplication(),
                    cachedGames.associateBy { it.path },
                    shouldAbort = { EmulatorBridge.isVmActive() }
                )
                if (EmulatorBridge.isVmActive()) {
                    queueDeferredLibraryWork(path, isInitialLoad = false)
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        isRefreshing = false
                    )
                    return@withLock
                }
                allGames = scannedGames
                currentLibraryRoot = path
                libraryInitialized = true
                _uiState.value = _uiState.value.copy(
                    isLoading = false,
                    isRefreshing = false
                )
                publishVisibleGames()
                updateBootstrapState()
                libraryCacheRepository.save(path, allGames, preferEnglishGameTitles)
                syncMissingCovers()
            }
        }
    }

    private fun scanGamesFromUri(uri: Uri, isInitialLoad: Boolean = false) {
        viewModelScope.launch(Dispatchers.IO) {
            scanMutex.withLock {
                val rootPath = uri.toString()
                if (shouldDeferLibraryWork(rootPath, isInitialLoad)) return@withLock
                val cacheSnapshot = resolveCacheSnapshot(rootPath)
                val cachedGames = cacheSnapshot.games
                val showFullScreenLoader = false
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
                    return@withLock
                }
                val context = getApplication<Application>()
                val scannedGames = repository.scanDirectoryFromUri(
                    uri,
                    context,
                    cachedGames.associateBy { it.path },
                    shouldAbort = { EmulatorBridge.isVmActive() }
                )
                if (EmulatorBridge.isVmActive()) {
                    queueDeferredLibraryWork(rootPath, isInitialLoad = false)
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        isRefreshing = false
                    )
                    return@withLock
                }
                allGames = scannedGames
                currentLibraryRoot = rootPath
                libraryInitialized = true
                _uiState.value = _uiState.value.copy(
                    isLoading = false,
                    isRefreshing = false
                )
                publishVisibleGames()
                updateBootstrapState()
                libraryCacheRepository.save(rootPath, allGames, preferEnglishGameTitles)
                syncMissingCovers()
            }
        }
    }

    private fun requestLibraryScan(rootPath: String, isInitialLoad: Boolean = false) {
        if (shouldDeferLibraryWork(rootPath, isInitialLoad)) return
        if (rootPath.startsWith("content://")) {
            scanGamesFromUri(rootPath.toUri(), isInitialLoad)
        } else {
            scanGames(rootPath, isInitialLoad)
        }
    }

    private fun shouldDeferLibraryWork(rootPath: String, isInitialLoad: Boolean): Boolean {
        if (!EmulatorBridge.isVmActive()) return false
        val cacheSnapshot = resolveCacheSnapshot(rootPath)
        publishCachedGamesIfAvailable(rootPath, cacheSnapshot.games)
        queueDeferredLibraryWork(rootPath, isInitialLoad)
        _uiState.value = _uiState.value.copy(
            isLoading = false,
            isRefreshing = false
        )
        updateBootstrapState()
        return true
    }

    private fun queueDeferredLibraryWork(rootPath: String, isInitialLoad: Boolean) {
        val existing = deferredLibraryScan
        deferredLibraryScan = DeferredLibraryScan(
            rootPath = rootPath,
            isInitialLoad = isInitialLoad || existing?.isInitialLoad == true
        )
        startDeferredWorkMonitor()
    }

    private fun startDeferredWorkMonitor() {
        if (deferredWorkJob?.isActive == true) return
        deferredWorkJob = viewModelScope.launch {
            try {
                while (true) {
                    if (EmulatorBridge.isVmActive()) {
                        delay(1500)
                        continue
                    }

                    val pendingScan = deferredLibraryScan
                    if (pendingScan != null) {
                        deferredLibraryScan = null
                        requestLibraryScan(pendingScan.rootPath, pendingScan.isInitialLoad)
                        return@launch
                    }

                    if (deferredCoverSync) {
                        deferredCoverSync = false
                        syncMissingCovers()
                        return@launch
                    }

                    return@launch
                }
            } finally {
                deferredWorkJob = null
                if (!EmulatorBridge.isVmActive() && (deferredLibraryScan != null || deferredCoverSync)) {
                    startDeferredWorkMonitor()
                }
            }
        }
    }

    private fun resolveCacheSnapshot(rootPath: String): GameLibraryCacheSnapshot {
        val inMemoryGames = allGames.takeIf { currentLibraryRoot == rootPath && it.isNotEmpty() }
        return if (inMemoryGames != null) {
            GameLibraryCacheSnapshot(inMemoryGames, System.currentTimeMillis())
        } else {
            libraryCacheRepository.loadSnapshot(rootPath, preferEnglishGameTitles)
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
            !BiosValidator.isLikelyBiosLibraryEntry(
                fileName = game.fileName,
                title = game.title,
                serial = game.serial,
                fileSize = game.fileSize
            ) && (
            query.isBlank() ||
                normalizeSearchToken(game.title).contains(query) ||
                normalizeSearchToken(game.fileName).contains(query) ||
                normalizeSearchToken(game.serial).contains(query)
            )
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
        if (EmulatorBridge.isVmActive()) {
            deferredCoverSync = true
            startDeferredWorkMonitor()
            return
        }
        coverSyncJob?.cancel()
        val context = getApplication<Application>()
        coverSyncJob = viewModelScope.launch(Dispatchers.IO) {
            val gamesToProcess = allGames.filter { it.coverArtPath == null || it.coverArtPath.startsWith("http") }
            if (gamesToProcess.isEmpty()) return@launch
            val semaphore = kotlinx.coroutines.sync.Semaphore(3)

            val shouldReschedule = AtomicBoolean(false)
            coroutineScope {
                gamesToProcess.forEach { game ->
                    launch {
                        semaphore.withPermit {
                            if (EmulatorBridge.isVmActive()) {
                                shouldReschedule.set(true)
                                return@withPermit
                            }
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
            if (shouldReschedule.get()) {
                deferredCoverSync = true
                startDeferredWorkMonitor()
            }
            currentLibraryRoot?.let { rootPath ->
                libraryCacheRepository.save(rootPath, allGames, preferEnglishGameTitles)
            }
        }
    }

    private fun handleCoverSourceChanged() {
        val rootPath = currentLibraryRoot ?: return
        val context = getApplication<Application>()
        val coverRepository = CoverArtRepository(context)
        val cachePrefix = java.io.File(context.cacheDir, "game-covers").absolutePath

        synchronized(this) {
            allGames = allGames.map { game ->
                val currentPath = game.coverArtPath
                if (currentPath != null && currentPath.startsWith(cachePrefix, ignoreCase = true)) {
                    game.copy(coverArtPath = coverRepository.findCachedCoverPath(game.serial))
                } else {
                    game
                }
            }
        }
        publishVisibleGames()
        libraryCacheRepository.save(rootPath, allGames, preferEnglishGameTitles)

        requestLibraryScan(rootPath)
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

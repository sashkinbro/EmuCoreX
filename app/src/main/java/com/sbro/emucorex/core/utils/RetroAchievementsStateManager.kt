package com.sbro.emucorex.core.utils

import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.data.AchievementsProfileCache
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.RetroAchievementsRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.File
import java.util.concurrent.atomic.AtomicLong

enum class RetroAchievementsLoginRequestReason {
    USER_INITIATED,
    TOKEN_INVALID,
    UNKNOWN
}

data class RetroAchievementsUserState(
    val username: String,
    val displayName: String,
    val avatarPath: String?,
    val points: Int,
    val softcorePoints: Int,
    val unreadMessages: Int
)

data class RetroAchievementsGameState(
    val title: String,
    val richPresence: String?,
    val iconPath: String?,
    val gameId: Int,
    val earnedAchievements: Int,
    val totalAchievements: Int,
    val earnedPoints: Int,
    val totalPoints: Int,
    val hardcoreMode: Boolean,
    val leaderboardEnabled: Boolean,
    val richPresenceEnabled: Boolean
)

data class RetroAchievementsUiState(
    val isSupported: Boolean = EmulatorBridge.isNativeLoaded,
    val isLoading: Boolean = false,
    val isAuthenticating: Boolean = false,
    val enabled: Boolean = false,
    val hardcorePreference: Boolean = false,
    val hardcoreSessionPreference: Boolean = false,
    val hardcoreActive: Boolean = false,
    val notifications: Boolean = true,
    val leaderboardNotifications: Boolean = true,
    val achievementIndicators: Boolean = true,
    val leaderboardTrackers: Boolean = true,
    val soundEffects: Boolean = true,
    val unlockSoundName: String? = null,
    val storedUsername: String? = null,
    val user: RetroAchievementsUserState? = null,
    val game: RetroAchievementsGameState? = null,
    val errorMessage: String? = null,
    val loginRequestReason: RetroAchievementsLoginRequestReason? = null,
    val sessionRevision: Long = 0L
)

object RetroAchievementsStateManager {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val presentationSettingsMutex = Mutex()
    private val preferenceSettingsMutex = Mutex()
    private val enabledPreferenceRevision = AtomicLong()
    private val hardcorePreferenceRevision = AtomicLong()

    @Volatile
    private var activeGameKey: String? = null

    fun setActiveGame(gameKey: String?) {
        activeGameKey = gameKey
    }

    fun requestedHardcoreForCurrentGame(globalDefault: Boolean = loadStoredHardcore()): Boolean {
        val context = EmulatorBridge.getContext()
        val override = if (context != null) activeGameKey
            ?.let { PerGameSettingsRepository(context).get(it)?.achievementsHardcore } else null
        return RetroAchievementsHardcorePolicy.resolve(globalDefault, override, hostOverrideActive = false)
    }

    fun effectiveHardcoreForCurrentGame(globalDefault: Boolean = loadStoredHardcore()): Boolean {
        val context = EmulatorBridge.getContext()
        return RetroAchievementsHardcorePolicy.resolve(
            requestedHardcoreForCurrentGame(globalDefault),
            gameOverride = null,
            hostOverrideActive = context?.let(RetroAchievementsHostOverrideReceiver::isOverrideActive) == true
        )
    }
    private val _state = MutableStateFlow(RetroAchievementsUiState())
    val state: StateFlow<RetroAchievementsUiState> = _state.asStateFlow()

    @Volatile
    private var initialized = false

    fun initialize() {
        if (initialized) return
        initialized = true
        val prefsEnabled = loadStoredEnabled()
        val prefsHardcore = loadStoredHardcore()
        _state.update {
            it.copy(
                isSupported = EmulatorBridge.isNativeLoaded,
                storedUsername = loadStoredUsername(),
                user = loadStoredProfile(),
                enabled = prefsEnabled,
                hardcorePreference = prefsHardcore,
                hardcoreSessionPreference = effectiveHardcoreForCurrentGame(prefsHardcore),
                notifications = loadStoredNotifications(),
                leaderboardNotifications = loadStoredLeaderboardNotifications(),
                achievementIndicators = loadStoredAchievementIndicators(),
                leaderboardTrackers = loadStoredLeaderboardTrackers(),
                soundEffects = loadStoredSoundEffects(),
                unlockSoundName = loadStoredUnlockSoundName(),
                isLoading = true
            )
        }
        refreshState(invalidateCaches = false)
    }

    fun refreshState(invalidateCaches: Boolean = true) {
        if (invalidateCaches) {
            RetroAchievementsRepository.invalidateUnlockedAchievementsCache()
        }
        if (!EmulatorBridge.isNativeLoaded) {
            _state.update {
                it.copy(
                    isSupported = false,
                    isLoading = false,
                    isAuthenticating = false,
                    errorMessage = null
                )
            }
            return
        }

        _state.update {
            it.copy(
                isSupported = true,
                isLoading = true,
                enabled = loadStoredEnabled(),
                hardcorePreference = loadStoredHardcore(),
                notifications = loadStoredNotifications(),
                leaderboardNotifications = loadStoredLeaderboardNotifications(),
                achievementIndicators = loadStoredAchievementIndicators(),
                leaderboardTrackers = loadStoredLeaderboardTrackers(),
                soundEffects = loadStoredSoundEffects(),
                unlockSoundName = loadStoredUnlockSoundName(),
                storedUsername = loadStoredUsername() ?: it.storedUsername
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching {
                // A cold-started Android hub has no VM to initialize rcheevos for us.
                // Recreate the persistent client whenever the stored feature is enabled.
                preferenceSettingsMutex.withLock {
                    // Hydrate the request before initializing rcheevos in a cold hub.
                    RetroAchievementsBridge.nativeSetHardcore(effectiveHardcoreForCurrentGame())
                    RetroAchievementsBridge.nativeSetEnabled(loadStoredEnabled())
                }
            }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun login(username: String, password: String, rememberPassword: Boolean = false) {
        val cleanUsername = username.trim()
        if (cleanUsername.isBlank() || password.isBlank()) {
            _state.update {
                it.copy(errorMessage = "Username and password are required.")
            }
            return
        }

        _state.update {
            it.copy(
                isAuthenticating = true,
                isLoading = true,
                errorMessage = null,
                loginRequestReason = null,
                storedUsername = cleanUsername
            )
        }
        RetroAchievementsRepository.invalidateUnlockedAchievementsCache(forceAccountRefresh = true)
        scope.launch(Dispatchers.IO) {
            val preferences = EmulatorBridge.getContext()?.let(::AppPreferences)
            val wasEnabled = preferences?.getAchievementsEnabledSync() ?: _state.value.enabled
            val previousUsername = preferences?.getAchievementsUsernameSync()
            if (!previousUsername.equals(cleanUsername, ignoreCase = true)) {
                // Account-owned data must never leak into a new login attempt.
                preferences?.setAchievementsProfileCache(null)
                preferences?.setAchievementsAccountProgressCache(null)
                _state.update { it.copy(user = null) }
            }
            // Login against the persistent client, not the disposable client used
            // while RA is disabled. Roll the preference back if authentication fails.
            preferences?.setAchievementsEnabled(true)
            runCatching {
                RetroAchievementsBridge.nativeSetHardcore(effectiveHardcoreForCurrentGame())
                RetroAchievementsBridge.nativeSetEnabled(true)
            }
                .onFailure {
                    preferences?.setAchievementsEnabled(wasEnabled)
                    handleNativeFailure(it)
                    return@launch
                }
            runCatching { RetroAchievementsBridge.nativeLogin(cleanUsername, password) }
                .onSuccess { error ->
                    if (!error.isNullOrBlank()) {
                        preferences?.setAchievementsEnabled(wasEnabled)
                        if (!wasEnabled) runCatching { RetroAchievementsBridge.nativeSetEnabled(false) }
                        _state.update {
                            it.copy(
                                isAuthenticating = false,
                                isLoading = false,
                                enabled = wasEnabled,
                                errorMessage = error
                            )
                        }
                    } else {
                        preferences?.let {
                            preferences.setAchievementsUsername(cleanUsername)
                            preferences.setAchievementsRememberPassword(rememberPassword)
                            preferences.setAchievementsPassword(password.takeIf { rememberPassword })
                            preferences.setAchievementsEnabled(true)
                        }
                        // nativeLogin already publishes the authenticated state before
                        // returning. Do not request and then overwrite it with another
                        // loading transition: that used to cancel the Compose account
                        // fetch immediately after a successful login.
                        _state.update { current ->
                            current.copy(
                                isAuthenticating = false,
                                isLoading = false,
                                enabled = true,
                                errorMessage = null,
                                loginRequestReason = null,
                                storedUsername = cleanUsername,
                                user = current.user?.takeIf { it.username.equals(cleanUsername, ignoreCase = true) }
                            )
                        }
                    }
                }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun logout() {
        RetroAchievementsRepository.invalidateUnlockedAchievementsCache(forceAccountRefresh = true)
        _state.update {
            it.copy(
                isLoading = true,
                isAuthenticating = false,
                user = null,
                game = null,
                errorMessage = null,
                loginRequestReason = null
            )
        }
        scope.launch(Dispatchers.IO) {
            EmulatorBridge.getContext()?.let { context ->
                AppPreferences(context).run {
                    setAchievementsProfileCache(null)
                    setAchievementsAccountProgressCache(null)
                    setAchievementsToken(null)
                    setAchievementsLoginTimestamp(null)
                }
            }
            runCatching { RetroAchievementsBridge.nativeLogout() }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun setEnabled(enabled: Boolean) {
        val revision = enabledPreferenceRevision.incrementAndGet()
        RetroAchievementsRepository.invalidateUnlockedAchievementsCache()
        _state.update {
            it.copy(
                enabled = enabled,
                isLoading = true,
                errorMessage = null
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching {
                preferenceSettingsMutex.withLock {
                    // IO launches can arrive out of order after rapid UI toggles.
                    if (revision != enabledPreferenceRevision.get()) return@withLock
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsEnabled(enabled) }
                    RetroAchievementsBridge.nativeSetHardcore(effectiveHardcoreForCurrentGame())
                    RetroAchievementsBridge.nativeSetEnabled(enabled)
                }
            }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun setHardcore(enabled: Boolean) {
        val revision = hardcorePreferenceRevision.incrementAndGet()
        _state.update {
            it.copy(
                hardcorePreference = enabled,
                isLoading = true,
                errorMessage = null
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching {
                preferenceSettingsMutex.withLock {
                    if (revision != hardcorePreferenceRevision.get()) return@withLock
                    // Persist the user's request, independently of runtime suspension,
                    // per-game overrides, or the temporary loopback test host.
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsHardcore(enabled) }
                    RetroAchievementsBridge.nativeSetHardcore(effectiveHardcoreForCurrentGame(enabled))
                }
            }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun setAchievementIndicators(enabled: Boolean) {
        _state.update { it.copy(achievementIndicators = enabled, errorMessage = null) }
        scope.launch(Dispatchers.IO) {
            runCatching {
                presentationSettingsMutex.withLock {
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsIndicators(enabled) }
                    RetroAchievementsBridge.nativeSetAchievementIndicators(enabled)
                }
            }.onFailure { handleNativeFailure(it) }
        }
    }

    fun setNotifications(enabled: Boolean) {
        _state.update { it.copy(notifications = enabled, errorMessage = null) }
        scope.launch(Dispatchers.IO) {
            runCatching {
                presentationSettingsMutex.withLock {
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsNotifications(enabled) }
                    RetroAchievementsBridge.nativeSetNotifications(enabled)
                }
            }.onFailure { handleNativeFailure(it) }
        }
    }

    fun setLeaderboardNotifications(enabled: Boolean) {
        _state.update { it.copy(leaderboardNotifications = enabled, errorMessage = null) }
        scope.launch(Dispatchers.IO) {
            runCatching {
                presentationSettingsMutex.withLock {
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsLeaderboardNotifications(enabled) }
                    RetroAchievementsBridge.nativeSetLeaderboardNotifications(enabled)
                }
            }.onFailure { handleNativeFailure(it) }
        }
    }

    fun setLeaderboardTrackers(enabled: Boolean) {
        _state.update { it.copy(leaderboardTrackers = enabled, errorMessage = null) }
        scope.launch(Dispatchers.IO) {
            runCatching {
                presentationSettingsMutex.withLock {
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsLeaderboardTrackers(enabled) }
                    RetroAchievementsBridge.nativeSetLeaderboardTrackers(enabled)
                }
            }.onFailure { handleNativeFailure(it) }
        }
    }

    fun setSoundEffects(enabled: Boolean) {
        _state.update { it.copy(soundEffects = enabled, errorMessage = null) }
        scope.launch(Dispatchers.IO) {
            runCatching {
                presentationSettingsMutex.withLock {
                    EmulatorBridge.getContext()?.let { AppPreferences(it).setAchievementsSoundEffects(enabled) }
                    RetroAchievementsBridge.nativeSetSoundEffects(enabled)
                }
            }.onFailure { handleNativeFailure(it) }
        }
    }

    fun setUnlockSound(path: String?, displayName: String?) {
        val resolvedPath = path?.takeIf { it.isNotBlank() }
        val resolvedName = displayName?.takeIf { it.isNotBlank() }
        _state.update { it.copy(unlockSoundName = if (resolvedPath != null) resolvedName else null, errorMessage = null) }
        scope.launch(Dispatchers.IO) {
            runCatching {
                presentationSettingsMutex.withLock {
                    EmulatorBridge.getContext()?.let {
                        AppPreferences(it).setAchievementsUnlockSound(resolvedPath, resolvedName)
                    }
                    RetroAchievementsBridge.nativeSetUnlockSound(resolvedPath.orEmpty())
                }
            }.onFailure { handleNativeFailure(it) }
        }
    }

    fun clearTransientState() {
        _state.update {
            it.copy(
                errorMessage = null,
                loginRequestReason = null
            )
        }
    }

    internal fun onLoginRequested(reason: Int) {
        val resolvedReason = when (reason) {
            0 -> RetroAchievementsLoginRequestReason.USER_INITIATED
            1 -> RetroAchievementsLoginRequestReason.TOKEN_INVALID
            else -> RetroAchievementsLoginRequestReason.UNKNOWN
        }
        if (resolvedReason == RetroAchievementsLoginRequestReason.TOKEN_INVALID) {
            RetroAchievementsRepository.invalidateUnlockedAchievementsCache(forceAccountRefresh = true)
            EmulatorBridge.getContext()?.let { context ->
                scope.launch(Dispatchers.IO) {
                    AppPreferences(context).run {
                        setAchievementsProfileCache(null)
                        setAchievementsAccountProgressCache(null)
                        setAchievementsToken(null)
                        setAchievementsLoginTimestamp(null)
                    }
                }
            }
        }
        _state.update {
            it.copy(
                isAuthenticating = false,
                isLoading = false,
                user = if (resolvedReason == RetroAchievementsLoginRequestReason.TOKEN_INVALID) null else it.user,
                loginRequestReason = resolvedReason
            )
        }
    }

    internal fun onLoginSuccess(username: String?, points: Int, scPoints: Int, unreadMessages: Int) {
        RetroAchievementsRepository.invalidateUnlockedAchievementsCache(forceAccountRefresh = true)
        val storedAvatar = loadStoredAvatarPath()
        var verifiedUser: RetroAchievementsUserState? = null
        _state.update { current ->
            val resolvedUsername = username?.takeIf { it.isNotBlank() }
                ?: current.user?.username
                ?: current.storedUsername
                ?: loadStoredUsername()
            verifiedUser = if (!resolvedUsername.isNullOrBlank()) {
                current.user?.copy(
                    username = resolvedUsername,
                    displayName = username?.takeIf { it.isNotBlank() } ?: current.user.displayName,
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                ) ?: RetroAchievementsUserState(
                    username = resolvedUsername,
                    displayName = username?.takeIf { it.isNotBlank() } ?: resolvedUsername,
                    avatarPath = storedAvatar,
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                )
            } else {
                current.user?.copy(
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                )
            }
            current.copy(
                isAuthenticating = false,
                isLoading = false,
                errorMessage = null,
                loginRequestReason = null,
                sessionRevision = current.sessionRevision + 1L,
                storedUsername = resolvedUsername ?: current.storedUsername,
                user = verifiedUser
            )
        }
        verifiedUser?.let(::persistStoredProfile)
    }

    internal fun onStateChanged(
        haveUser: Boolean,
        username: String?,
        displayName: String?,
        avatar: String?,
        points: Int,
        scPoints: Int,
        unreadMessages: Int,
        hardcorePreference: Boolean,
        hardcoreActive: Boolean,
        haveGame: Boolean,
        gameTitle: String?,
        richPresence: String?,
        iconPath: String?,
        gameId: Int,
        numAchievements: Int,
        earnedAchievements: Int,
        score: Int,
        scScore: Int,
        hardcoreMode: Boolean,
        leaderboardEnabled: Boolean,
        richPresenceEnabled: Boolean
    ) {
        val resolvedStoredUsername = username ?: _state.value.storedUsername ?: loadStoredUsername()
        val storedEnabled = loadStoredEnabled()
        val storedHardcore = loadStoredHardcore()
        val storedAvatar = loadStoredAvatarPath()
        val resolvedAvatar = avatar?.takeIf { it.isNotBlank() } ?: storedAvatar
        if (!avatar.isNullOrBlank()) {
            persistStoredAvatarPath(avatar)
        }
        var verifiedUser: RetroAchievementsUserState? = null
        _state.update { current ->
            verifiedUser = if (haveUser && !username.isNullOrBlank()) {
                RetroAchievementsUserState(
                    username = username,
                    displayName = displayName?.takeIf { it.isNotBlank() } ?: username,
                    avatarPath = resolvedAvatar,
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                )
            } else if (current.loginRequestReason != RetroAchievementsLoginRequestReason.TOKEN_INVALID) {
                current.user?.takeIf { user ->
                    resolvedStoredUsername != null && user.username.equals(resolvedStoredUsername, ignoreCase = true)
                } ?: loadStoredProfile(resolvedStoredUsername)
            } else {
                null
            }
            current.copy(
                isSupported = true,
                isLoading = false,
                // State notifications also occur while login is preparing the
                // persistent client; only the login callback may finish that flow.
                isAuthenticating = current.isAuthenticating,
                enabled = storedEnabled,
                hardcorePreference = storedHardcore,
                hardcoreSessionPreference = hardcorePreference,
                notifications = loadStoredNotifications(),
                leaderboardNotifications = loadStoredLeaderboardNotifications(),
                achievementIndicators = loadStoredAchievementIndicators(),
                leaderboardTrackers = loadStoredLeaderboardTrackers(),
                soundEffects = loadStoredSoundEffects(),
                unlockSoundName = loadStoredUnlockSoundName(),
                hardcoreActive = hardcoreActive,
                storedUsername = resolvedStoredUsername,
                user = verifiedUser,
                game = if (haveGame && !gameTitle.isNullOrBlank()) {
                    RetroAchievementsGameState(
                        title = gameTitle,
                        richPresence = richPresence,
                        iconPath = iconPath,
                        gameId = gameId,
                        earnedAchievements = earnedAchievements,
                        totalAchievements = numAchievements,
                        earnedPoints = score,
                        totalPoints = scScore,
                        hardcoreMode = hardcoreMode,
                        leaderboardEnabled = leaderboardEnabled,
                        richPresenceEnabled = richPresenceEnabled
                    )
                } else {
                    null
                },
                errorMessage = null,
                loginRequestReason = null
            )
        }
        if (haveUser) verifiedUser?.let(::persistStoredProfile)
    }

    internal fun onHardcoreModeChanged(enabled: Boolean) {
        _state.update {
            it.copy(
                hardcoreActive = enabled,
                game = it.game?.copy(hardcoreMode = enabled),
                isLoading = false
            )
        }
    }

    private fun loadStoredUsername(): String? {
        val context = EmulatorBridge.getContext() ?: return null
        return AppPreferences(context).getAchievementsUsernameSync()
    }

    private fun loadStoredEnabled(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.enabled
        return AppPreferences(context).getAchievementsEnabledSync()
    }

    private fun loadStoredHardcore(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.hardcorePreference
        return AppPreferences(context).getAchievementsHardcoreSync()
    }

    private fun loadStoredNotifications(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.notifications
        return AppPreferences(context).getAchievementsNotificationsSync()
    }

    private fun loadStoredLeaderboardNotifications(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.leaderboardNotifications
        return AppPreferences(context).getAchievementsLeaderboardNotificationsSync()
    }

    private fun loadStoredAchievementIndicators(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.achievementIndicators
        return AppPreferences(context).getAchievementsIndicatorsSync()
    }

    private fun loadStoredLeaderboardTrackers(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.leaderboardTrackers
        return AppPreferences(context).getAchievementsLeaderboardTrackersSync()
    }

    private fun loadStoredSoundEffects(): Boolean {
        val context = EmulatorBridge.getContext() ?: return _state.value.soundEffects
        return AppPreferences(context).getAchievementsSoundEffectsSync()
    }

    private fun loadStoredUnlockSoundName(): String? {
        val context = EmulatorBridge.getContext() ?: return _state.value.unlockSoundName
        val preferences = AppPreferences(context)
        val path = preferences.getAchievementsUnlockSoundPathSync() ?: return null
        return preferences.getAchievementsUnlockSoundNameSync()?.takeIf { File(path).isFile }
    }

    private fun loadStoredAvatarPath(): String? {
        val context = EmulatorBridge.getContext() ?: return null
        return AppPreferences(context).getAchievementsAvatarPathSync()
    }

    private fun loadStoredProfile(username: String? = loadStoredUsername()): RetroAchievementsUserState? {
        val context = EmulatorBridge.getContext() ?: return null
        val preferences = AppPreferences(context)
        val expectedUsername = username?.trim().orEmpty()
        // A cached card is not an authenticated session. Requiring the persisted
        // token prevents a half-signed-in UI with a username, zero games and no
        // possible server refresh after reinstall/migration failures.
        if (!RetroAchievementsSessionPolicy.hasStoredSession(
                expectedUsername,
                preferences.getAchievementsTokenSync()
            )
        ) return null
        val cached = preferences.getAchievementsProfileCacheSync() ?: return null
        if (!cached.username.equals(expectedUsername, ignoreCase = true)) return null
        return RetroAchievementsUserState(
            username = cached.username,
            displayName = cached.displayName,
            avatarPath = cached.avatarPath,
            points = cached.points,
            softcorePoints = cached.softcorePoints,
            unreadMessages = cached.unreadMessages
        )
    }

    private fun persistStoredProfile(user: RetroAchievementsUserState) {
        val context = EmulatorBridge.getContext() ?: return
        scope.launch(Dispatchers.IO) {
            AppPreferences(context).run {
                setAchievementsUsername(user.username)
                setAchievementsProfileCache(AchievementsProfileCache(
                    username = user.username,
                    displayName = user.displayName,
                    avatarPath = user.avatarPath,
                    points = user.points,
                    softcorePoints = user.softcorePoints,
                    unreadMessages = user.unreadMessages,
                    updatedAtMillis = System.currentTimeMillis()
                ))
            }
        }
    }

    private fun persistStoredAvatarPath(avatarPath: String) {
        val context = EmulatorBridge.getContext() ?: return
        scope.launch(Dispatchers.IO) {
            AppPreferences(context).setAchievementsAvatarPath(avatarPath)
        }
    }

    private fun handleNativeFailure(error: Throwable) {
        _state.update {
            it.copy(
                isLoading = false,
                isAuthenticating = false,
                errorMessage = error.message ?: "RetroAchievements is unavailable right now."
            )
        }
    }
}

internal object RetroAchievementsSessionPolicy {
    fun hasStoredSession(username: String?, token: String?): Boolean =
        !username.isNullOrBlank() && !token.isNullOrBlank()
}

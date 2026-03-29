package com.sbro.emucorex.core.utils

import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.NativeApp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

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
    val hardcoreActive: Boolean = false,
    val storedUsername: String? = null,
    val user: RetroAchievementsUserState? = null,
    val game: RetroAchievementsGameState? = null,
    val errorMessage: String? = null,
    val loginRequestReason: RetroAchievementsLoginRequestReason? = null
)

object RetroAchievementsStateManager {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val _state = MutableStateFlow(RetroAchievementsUiState())
    val state: StateFlow<RetroAchievementsUiState> = _state.asStateFlow()

    @Volatile
    private var initialized = false

    fun initialize() {
        if (initialized) return
        initialized = true
        _state.update {
            it.copy(
                isSupported = EmulatorBridge.isNativeLoaded,
                storedUsername = loadStoredUsername(),
                isLoading = true
            )
        }
        refreshState()
    }

    fun refreshState() {
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
                storedUsername = loadStoredUsername() ?: it.storedUsername
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching { RetroAchievementsBridge.nativeRequestState() }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun login(username: String, password: String) {
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
        scope.launch(Dispatchers.IO) {
            runCatching { RetroAchievementsBridge.nativeLogin(cleanUsername, password) }
                .onSuccess { error ->
                    if (!error.isNullOrBlank()) {
                        _state.update {
                            it.copy(
                                isAuthenticating = false,
                                isLoading = false,
                                errorMessage = error
                            )
                        }
                    }
                }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun logout() {
        _state.update {
            it.copy(
                isLoading = true,
                isAuthenticating = false,
                errorMessage = null,
                loginRequestReason = null
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching { RetroAchievementsBridge.nativeLogout() }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun setEnabled(enabled: Boolean) {
        _state.update {
            it.copy(
                enabled = enabled,
                isLoading = true,
                errorMessage = null
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching { RetroAchievementsBridge.nativeSetEnabled(enabled) }
                .onFailure { handleNativeFailure(it) }
        }
    }

    fun setHardcore(enabled: Boolean) {
        _state.update {
            it.copy(
                hardcorePreference = enabled,
                isLoading = true,
                errorMessage = null
            )
        }
        scope.launch(Dispatchers.IO) {
            runCatching { RetroAchievementsBridge.nativeSetHardcore(enabled) }
                .onFailure { handleNativeFailure(it) }
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
        _state.update {
            it.copy(
                isAuthenticating = false,
                isLoading = false,
                loginRequestReason = when (reason) {
                    0 -> RetroAchievementsLoginRequestReason.USER_INITIATED
                    1 -> RetroAchievementsLoginRequestReason.TOKEN_INVALID
                    else -> RetroAchievementsLoginRequestReason.UNKNOWN
                }
            )
        }
    }

    internal fun onLoginSuccess(displayName: String?, points: Int, scPoints: Int, unreadMessages: Int) {
        _state.update { current ->
            current.copy(
                isAuthenticating = false,
                isLoading = false,
                errorMessage = null,
                user = current.user?.copy(
                    displayName = displayName ?: current.user.displayName,
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                ) ?: current.storedUsername?.let { username ->
                    RetroAchievementsUserState(
                        username = username,
                        displayName = displayName ?: username,
                        avatarPath = null,
                        points = points,
                        softcorePoints = scPoints,
                        unreadMessages = unreadMessages
                    )
                }
            )
        }
    }

    internal fun onStateChanged(
        enabled: Boolean,
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
        _state.update { current ->
            current.copy(
                isSupported = true,
                isLoading = false,
                isAuthenticating = false,
                enabled = enabled,
                hardcorePreference = hardcorePreference,
                hardcoreActive = hardcoreActive,
                storedUsername = username ?: current.storedUsername ?: loadStoredUsername(),
                user = if (haveUser && !username.isNullOrBlank()) {
                    RetroAchievementsUserState(
                        username = username,
                        displayName = displayName?.takeIf { it.isNotBlank() } ?: username,
                        avatarPath = avatar,
                        points = points,
                        softcorePoints = scPoints,
                        unreadMessages = unreadMessages
                    )
                } else {
                    null
                },
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
    }

    internal fun onHardcoreModeChanged(enabled: Boolean) {
        _state.update {
            it.copy(
                hardcoreActive = enabled,
                isLoading = false
            )
        }
    }

    private fun loadStoredUsername(): String? {
        if (!EmulatorBridge.isNativeLoaded) return null
        return runCatching { NativeApp.getSetting("Achievements", "Username", "string") }
            .getOrNull()
            ?.takeIf { it.isNotBlank() }
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

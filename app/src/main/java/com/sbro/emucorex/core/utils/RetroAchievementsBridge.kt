package com.sbro.emucorex.core.utils

import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking

object RetroAchievementsBridge {
    @JvmStatic
    external fun nativeRequestState()

    @JvmStatic
    external fun nativeLogin(username: String, password: String): String?

    @JvmStatic
    external fun nativeLogout()

    @JvmStatic
    external fun nativeSetEnabled(enabled: Boolean)

    @JvmStatic
    external fun nativeSetHardcore(enabled: Boolean)

    @JvmStatic
    external fun nativeSetNotifications(enabled: Boolean)

    @JvmStatic
    external fun nativeSetLeaderboardNotifications(enabled: Boolean)

    @JvmStatic
    external fun nativeSetAchievementIndicators(enabled: Boolean)

    @JvmStatic
    external fun nativeSetLeaderboardTrackers(enabled: Boolean)

    @JvmStatic
    external fun nativeSetSoundEffects(enabled: Boolean)

    @JvmStatic
    external fun nativeSetUnlockSound(path: String)

    @JvmStatic
    external fun nativePreviewSound(path: String): Boolean

    @JvmStatic
    fun notifyLoginRequested(reason: Int) {
        RetroAchievementsStateManager.onLoginRequested(reason)
    }

    @JvmStatic
    fun notifyLoginSuccess(username: String?, points: Int, scPoints: Int, unreadMessages: Int) {
        RetroAchievementsLiveStateManager.onLoginSuccess(username, points, scPoints, unreadMessages)
        RetroAchievementsStateManager.onLoginSuccess(username, points, scPoints, unreadMessages)
    }

    @JvmStatic
    fun notifyStateChanged(
        enabled: Boolean, haveUser: Boolean, username: String?, displayName: String?,
        avatar: String?, points: Int, scPoints: Int, unreadMessages: Int,
        hardcorePreference: Boolean, hardcoreActive: Boolean, haveGame: Boolean,
        gameTitle: String?, richPresence: String?, iconPath: String?,
        gameId: Int, numAchievements: Int, earnedAchievements: Int, score: Int, scScore: Int,
        hardcoreMode: Boolean, leaderboardEnabled: Boolean, richPresenceEnabled: Boolean
    ) {
        RetroAchievementsLiveStateManager.onStateChanged(
            enabled = enabled,
            haveUser = haveUser,
            username = username,
            displayName = displayName,
            avatar = avatar,
            points = points,
            scPoints = scPoints,
            unreadMessages = unreadMessages,
            hardcorePreference = hardcorePreference,
            hardcoreActive = hardcoreActive,
            haveGame = haveGame,
            gameTitle = gameTitle,
            richPresence = richPresence,
            iconPath = iconPath,
            gameId = gameId,
            numAchievements = numAchievements,
            earnedAchievements = earnedAchievements,
            score = score,
            scScore = scScore,
            hardcoreMode = hardcoreMode,
            leaderboardEnabled = leaderboardEnabled,
            richPresenceEnabled = richPresenceEnabled
        )
        RetroAchievementsStateManager.onStateChanged(
            haveUser = haveUser,
            username = username,
            displayName = displayName,
            avatar = avatar,
            points = points,
            scPoints = scPoints,
            unreadMessages = unreadMessages,
            hardcorePreference = hardcorePreference,
            hardcoreActive = hardcoreActive,
            haveGame = haveGame,
            gameTitle = gameTitle,
            richPresence = richPresence,
            iconPath = iconPath,
            gameId = gameId,
            numAchievements = numAchievements,
            earnedAchievements = earnedAchievements,
            score = score,
            scScore = scScore,
            hardcoreMode = hardcoreMode,
            leaderboardEnabled = leaderboardEnabled,
            richPresenceEnabled = richPresenceEnabled
        )
    }

    @JvmStatic
    fun notifyHardcoreModeChanged(enabled: Boolean) {
        RetroAchievementsLiveStateManager.onHardcoreModeChanged(enabled)
        RetroAchievementsStateManager.onHardcoreModeChanged(enabled)
    }

    @JvmStatic
    fun notifyNotification(kind: String?, title: String?, message: String?, imagePath: String?) {
        RetroAchievementsLiveStateManager.onNotification(kind, title, message, imagePath)
    }

    @JvmStatic
    fun notifySettingsChanged(section: String, key: String, value: String) {
        // NativeApp's JNI context is not guaranteed to exist in the library hub,
        // before a VM is started. EmulatorBridge owns the application context there.
        val context = NativeApp.getContext() ?: EmulatorBridge.getContext() ?: return
        val prefs = AppPreferences(context)
        if (section == "Achievements" && (key == "Username" || key == "Token" || key == "LoginTimestamp")) {
            // The library hub can commit native settings before a VM/settings layer
            // exists. Those commits contain empty credential values and must not erase
            // a token which rcheevos has already authenticated and saved to DataStore.
            // Explicit logout and a verified TOKEN_INVALID response own credential
            // removal; native synchronization only accepts usable values.
            if (!RetroAchievementsCredentialSyncPolicy.shouldPersistNativeValue(value)) return
            runBlocking(Dispatchers.IO) {
                when (key) {
                    "Username" -> prefs.setAchievementsUsername(value)
                    "Token" -> prefs.setAchievementsToken(value)
                    "LoginTimestamp" -> prefs.setAchievementsLoginTimestamp(value)
                }
            }
            return
        }

        // Enabled/ChallengeMode are Android-owned user preferences. Native commits
        // include defaults during startup and effective per-game/session values;
        // neither may write back into the global DataStore preferences.
    }
}

internal object RetroAchievementsCredentialSyncPolicy {
    fun shouldPersistNativeValue(value: String?): Boolean = !value.isNullOrBlank()
}

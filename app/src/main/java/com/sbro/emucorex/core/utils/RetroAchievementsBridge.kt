package com.sbro.emucorex.core.utils

import android.util.Log

object RetroAchievementsBridge {
    private const val TAG = "RetroAchievementsBridge"

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
    fun notifyLoginRequested(reason: Int) {
        Log.d(TAG, "notifyLoginRequested reason=$reason")
        RetroAchievementsStateManager.onLoginRequested(reason)
    }

    @JvmStatic
    fun notifyLoginSuccess(username: String?, points: Int, scPoints: Int, unreadMessages: Int) {
        Log.d(TAG, "notifyLoginSuccess user=$username")
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
        RetroAchievementsStateManager.onStateChanged(
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
    }

    @JvmStatic
    fun notifyHardcoreModeChanged(enabled: Boolean) {
        Log.d(TAG, "notifyHardcoreModeChanged enabled=$enabled")
        RetroAchievementsStateManager.onHardcoreModeChanged(enabled)
    }
}

package com.sbro.emucorex.core.utils

import android.content.Context
import com.sbro.emucorex.R
import com.sbro.emucorex.core.AppLocaleManager
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class RetroAchievementsNotification(
    val id: Long,
    val kind: String,
    val title: String,
    val message: String,
    val imagePath: String?,
    val durationMs: Long = 0L
)

/** Kinds of the structured notifications sent by the native client (onNotificationV2). */
object RetroAchievementsNotificationKind {
    const val GENERIC = 0
    const val ACHIEVEMENT_UNLOCKED = 1
    const val GAME_MASTERED = 2
    const val GAME_SUMMARY = 3
    const val HARDCORE_INFO = 4
    const val LEADERBOARD_STARTED = 5
    const val LEADERBOARD_FAILED = 6
    const val LEADERBOARD_SUBMITTED = 7
    const val LOGIN_SUCCESS = 8
    const val ERROR = 9
    const val SERVER_DISCONNECTED = 10
    const val SERVER_RECONNECTED = 11
}

/** Kinds of the persistent in-game indicators (onIndicator). */
object RetroAchievementsIndicatorKind {
    const val CHALLENGE = 0
    const val PROGRESS = 1
    const val LEADERBOARD_TRACKER = 2
}

data class RetroAchievementsIndicator(
    val kind: Int,
    val id: Int,
    val title: String,
    val text: String,
    val badgePath: String?
)

/** Pure FIFO queue rules for notifications, kept separate so they can be unit tested. */
internal object RetroAchievementsNotificationQueue {
    const val MAX_SIZE = 10

    fun enqueue(
        queue: List<RetroAchievementsNotification>,
        item: RetroAchievementsNotification,
        maxSize: Int = MAX_SIZE
    ): List<RetroAchievementsNotification> {
        // Coalesce duplicates (e.g. the same unlock reported by two native paths).
        if (queue.any { it.kind == item.kind && it.title == item.title && it.message == item.message }) return queue
        // The head is already on screen and must not be replaced; overflow drops the newest.
        if (queue.size >= maxSize) return queue
        return queue + item
    }

    fun dismiss(queue: List<RetroAchievementsNotification>, id: Long): List<RetroAchievementsNotification> =
        if (queue.any { it.id == id }) queue.filterNot { it.id == id } else queue
}

data class RetroAchievementsLiveUiState(
    val isSupported: Boolean = EmulatorBridge.isNativeLoaded,
    val enabled: Boolean = false,
    val hardcorePreference: Boolean = false,
    val hardcoreActive: Boolean = false,
    val user: RetroAchievementsUserState? = null,
    val game: RetroAchievementsGameState? = null,
    val notificationQueue: List<RetroAchievementsNotification> = emptyList()
) {
    /** The notification currently on screen (head of the FIFO queue). */
    val notification: RetroAchievementsNotification? get() = notificationQueue.firstOrNull()
}

object RetroAchievementsLiveStateManager {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val _state = MutableStateFlow(RetroAchievementsLiveUiState())
    val state: StateFlow<RetroAchievementsLiveUiState> = _state.asStateFlow()

    // Active indicators keyed by kind+id; the progress indicator is a single slot (id 0).
    private val _indicators = MutableStateFlow<List<RetroAchievementsIndicator>>(emptyList())
    val indicators: StateFlow<List<RetroAchievementsIndicator>> = _indicators.asStateFlow()

    @Volatile
    private var seeded = false

    // RetroAchievements rule: the player is always told when achievements become active. The native
    // summary notification can be lost (sent before the callback path is ready, or filtered), so the
    // Kotlin side checks that one arrived for the game and posts it from the live state otherwise.
    @Volatile
    private var lastSummaryTitle: String? = null

    @Volatile
    private var lastSummaryAtMs = 0L
    private const val SUMMARY_FALLBACK_DELAY_MS = 2500L
    private const val SUMMARY_RECENT_WINDOW_MS = 30_000L

    /**
     * Cold start: until the first native state callback arrives the hardcore flag must already
     * reflect the stored preference, otherwise restricted actions are allowed for the first frames.
     */
    fun seedFromPreferences(context: Context?) {
        if (seeded || context == null) return
        val prefs = AppPreferences(context.applicationContext)
        val seed = runCatching {
            Triple(
                prefs.getAchievementsEnabledSync(),
                prefs.getAchievementsHardcoreSync(),
                RetroAchievementsSessionPolicy.hasStoredSession(
                    prefs.getAchievementsUsernameSync(),
                    prefs.getAchievementsTokenSync()
                )
            )
        }.getOrNull() ?: return
        seeded = true
        _state.update { current ->
            if (current.user != null || current.game != null) current
            else current.copy(
                enabled = seed.first,
                hardcorePreference = seed.second,
                hardcoreActive = seed.first && seed.second && seed.third
            )
        }
    }

    fun refreshFromNative() {
        if (!EmulatorBridge.isNativeLoaded) {
            _state.update { it.copy(isSupported = false) }
            return
        }
        seedFromPreferences(EmulatorBridge.getContext())

        scope.launch(Dispatchers.IO) {
            runCatching { RetroAchievementsBridge.nativeRequestState() }
        }
    }

    fun clearIndicators() {
        _indicators.value = emptyList()
    }

    /** Native does not hide live indicators when a toggle is switched off mid-game. */
    fun clearIndicatorKinds(vararg kinds: Int) {
        _indicators.update { list -> list.filterNot { it.kind in kinds } }
    }

    internal fun onIndicator(kind: Int, id: Int, active: Boolean, title: String, text: String, badgePath: String) {
        if (kind !in 0..2) return
        val slotId = if (kind == RetroAchievementsIndicatorKind.PROGRESS) 0 else id
        _indicators.update { list ->
            val without = list.filterNot { it.kind == kind && it.id == slotId }
            if (!active) without
            else without + RetroAchievementsIndicator(kind, slotId, title, text, badgePath.takeIf { it.isNotBlank() })
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
        seeded = true
        val previousGameId = _state.value.game?.gameId
        _state.update { current ->
            val newGameId = if (haveGame && !gameTitle.isNullOrBlank()) gameId else null
            if (newGameId != current.game?.gameId || !haveUser) clearIndicators()
            current.copy(
                isSupported = true,
                enabled = enabled,
                hardcorePreference = hardcorePreference,
                hardcoreActive = hardcoreActive,
                user = if (haveUser && !username.isNullOrBlank()) {
                    RetroAchievementsUserState(
                        username = username,
                        displayName = displayName?.takeIf { it.isNotBlank() } ?: username,
                        // Only fall back to the previous avatar for the same account.
                        avatarPath = avatar?.takeIf { it.isNotBlank() }
                            ?: current.user?.avatarPath?.takeIf { current.user.username.equals(username, ignoreCase = true) },
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
                }
            )
        }
        if (haveUser && haveGame && !gameTitle.isNullOrBlank() && gameId != previousGameId && numAchievements > 0) {
            scheduleSummaryFallback(gameId, gameTitle)
        }
    }

    private fun summaryReceivedFor(title: String): Boolean =
        lastSummaryTitle == title && System.currentTimeMillis() - lastSummaryAtMs < SUMMARY_RECENT_WINDOW_MS

    private fun scheduleSummaryFallback(gameId: Int, gameTitle: String) {
        if (summaryReceivedFor(gameTitle)) return
        scope.launch {
            delay(SUMMARY_FALLBACK_DELAY_MS)
            val game = _state.value.game ?: return@launch
            if (game.gameId != gameId || summaryReceivedFor(game.title)) return@launch
            if (!RetroAchievementsStateManager.state.value.notifications) return@launch
            if (game.totalAchievements <= 0) return@launch
            lastSummaryTitle = game.title
            lastSummaryAtMs = System.currentTimeMillis()
            val extra = "hardcore=${if (game.hardcoreMode) 1 else 0}\nunlocked=${game.earnedAchievements}" +
                "\ntotal=${game.totalAchievements}\npoints_unlocked=${game.earnedPoints}\npoints_total=${game.totalPoints}"
            enqueue(
                buildNotification(
                    RetroAchievementsNotificationKind.GAME_SUMMARY, game.title, "", game.earnedPoints, extra,
                    game.iconPath.orEmpty(), 5000L
                )
            )
        }
    }

    internal fun onLoginSuccess(username: String?, points: Int, scPoints: Int, unreadMessages: Int) {
        if (username.isNullOrBlank()) return
        _state.update { current ->
            current.copy(
                user = current.user?.takeIf { it.username.equals(username, ignoreCase = true) }?.copy(
                    username = username,
                    displayName = current.user.displayName.ifBlank { username },
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                ) ?: RetroAchievementsUserState(
                    username = username,
                    displayName = username,
                    avatarPath = null,
                    points = points,
                    softcorePoints = scPoints,
                    unreadMessages = unreadMessages
                )
            )
        }
    }

    internal fun onHardcoreModeChanged(enabled: Boolean) {
        _state.update { current ->
            current.copy(
                hardcoreActive = enabled,
                game = current.game?.copy(hardcoreMode = enabled)
            )
        }
    }

    internal fun onNotification(kind: String?, title: String?, message: String?, imagePath: String?) {
        val cleanTitle = title?.trim().orEmpty()
        val cleanMessage = message?.trim().orEmpty()
        if (cleanTitle.isBlank() && cleanMessage.isBlank()) return
        enqueue(
            RetroAchievementsNotification(
                id = System.nanoTime(),
                kind = kind?.trim()?.takeIf { it.isNotBlank() } ?: "info",
                title = cleanTitle.ifBlank { text(R.string.ra_notif_app_name, "RetroAchievements") },
                message = cleanMessage,
                imagePath = imagePath?.trim()?.takeIf { it.isNotBlank() }
            )
        )
    }

    internal fun onNotificationV2(
        kind: Int,
        title: String,
        description: String,
        points: Int,
        extra: String,
        iconPath: String,
        durationMs: Long
    ) {
        if (kind == RetroAchievementsNotificationKind.GAME_SUMMARY) {
            lastSummaryTitle = title.trim()
            lastSummaryAtMs = System.currentTimeMillis()
        }
        enqueue(buildNotification(kind, title.trim(), description.trim(), points, extra.trim(), iconPath, durationMs))
    }

    private fun buildNotification(
        kind: Int,
        title: String,
        description: String,
        points: Int,
        extra: String,
        iconPath: String,
        durationMs: Long
    ): RetroAchievementsNotification {
        val pointsText = if (points > 0) text(R.string.ra_notif_points, "%1\$d points", points) else ""
        val kindName: String
        val resolvedTitle: String
        val resolvedMessage: String
        when (kind) {
            RetroAchievementsNotificationKind.ACHIEVEMENT_UNLOCKED -> {
                kindName = "achievement"
                resolvedTitle = title.ifBlank { text(R.string.ra_notif_achievement_unlocked, "Achievement unlocked") }
                resolvedMessage = listOf(description, pointsText).filter { it.isNotBlank() }.joinToString("\n")
            }
            RetroAchievementsNotificationKind.GAME_MASTERED -> {
                kindName = "mastery"
                // RetroAchievements wording: only hardcore earns "Mastered"; softcore is "Completed".
                resolvedTitle = if (parseExtra(extra)["hardcore"] == "1") {
                    text(R.string.ra_notif_game_mastered, "Game mastered")
                } else {
                    text(R.string.ra_notif_game_completed, "Game completed")
                }
                resolvedMessage = listOf(title, description).filter { it.isNotBlank() }.joinToString("\n")
            }
            RetroAchievementsNotificationKind.GAME_SUMMARY -> {
                kindName = "summary"
                resolvedTitle = title.ifBlank { text(R.string.ra_notif_app_name, "RetroAchievements") }
                val fields = parseExtra(extra)
                val unlocked = fields["unlocked"]?.toIntOrNull()
                val total = fields["total"]?.toIntOrNull()
                val pointsUnlocked = fields["points_unlocked"]?.toIntOrNull()
                val pointsTotal = fields["points_total"]?.toIntOrNull()
                resolvedMessage = if (unlocked != null && total != null) {
                    val counts = if (pointsUnlocked != null && pointsTotal != null) {
                        text(R.string.ra_notif_summary_full, "%1\$d of %2\$d achievements · %3\$d/%4\$d points", unlocked, total, pointsUnlocked, pointsTotal)
                    } else {
                        text(R.string.ra_notif_summary_counts, "%1\$d of %2\$d achievements", unlocked, total)
                    }
                    val mode = when (fields["hardcore"]) {
                        "1" -> text(R.string.ra_notif_mode_hardcore, "Hardcore")
                        "0" -> text(R.string.ra_notif_mode_softcore, "Softcore")
                        else -> ""
                    }
                    listOf(counts, mode).filter { it.isNotBlank() }.joinToString("\n")
                } else {
                    description.ifBlank { pointsText }
                }
            }
            RetroAchievementsNotificationKind.HARDCORE_INFO -> {
                kindName = "hardcore"
                val pending = extra.equals("pending", ignoreCase = true)
                val unrecognized = extra.equals("unrecognized", ignoreCase = true)
                val enabled = extra == "1" || extra.equals("true", ignoreCase = true) || extra.equals("on", ignoreCase = true)
                val disabled = extra == "0" || extra.equals("false", ignoreCase = true) || extra.equals("off", ignoreCase = true)
                resolvedTitle = text(R.string.ra_notif_hardcore_title, "Hardcore mode")
                resolvedMessage = when {
                    unrecognized -> text(R.string.ra_notif_hardcore_unrecognized_media, "Hardcore disabled. Unrecognized media inserted.")
                    pending -> text(R.string.ra_notif_hardcore_pending, "Hardcore mode will be enabled after the game restarts.")
                    enabled -> text(R.string.ra_notif_hardcore_on, "Hardcore mode is on. Save states and cheats are disabled.")
                    disabled -> text(R.string.ra_notif_hardcore_off, "Hardcore mode is off.")
                    else -> description.ifBlank { title }
                }
            }
            RetroAchievementsNotificationKind.LEADERBOARD_STARTED -> {
                kindName = "leaderboard"
                resolvedTitle = text(R.string.ra_notif_lb_started, "Leaderboard attempt started")
                resolvedMessage = title.ifBlank { description }
            }
            RetroAchievementsNotificationKind.LEADERBOARD_FAILED -> {
                kindName = "leaderboard"
                resolvedTitle = text(R.string.ra_notif_lb_failed, "Leaderboard attempt failed")
                resolvedMessage = title.ifBlank { description }
            }
            RetroAchievementsNotificationKind.LEADERBOARD_SUBMITTED -> {
                kindName = "leaderboard"
                resolvedTitle = text(R.string.ra_notif_lb_submitted, "Leaderboard score submitted")
                val result = if (extra.isNotBlank()) text(R.string.ra_notif_lb_result, "Result: %1\$s", extra) else ""
                resolvedMessage = listOf(title.ifBlank { description }, result).filter { it.isNotBlank() }.joinToString("\n")
            }
            RetroAchievementsNotificationKind.LOGIN_SUCCESS -> {
                kindName = "login"
                resolvedTitle = text(R.string.ra_notif_login_success, "Signed in to RetroAchievements")
                val who = title.ifBlank { description }
                val fields = parseExtra(extra)
                val softcore = fields["softcore_points"]?.toIntOrNull()?.takeIf { it > 0 }
                    ?.let { text(R.string.ra_notif_softcore_points, "Softcore: %1\$d points", it) }.orEmpty()
                val unread = fields["unread"]?.toIntOrNull()?.takeIf { it > 0 }
                    ?.let { text(R.string.ra_notif_unread_messages, "Unread messages: %1\$d", it) }.orEmpty()
                resolvedMessage = listOf(
                    if (who.isNotBlank()) text(R.string.ra_notif_login_welcome, "Welcome, %1\$s", who) else "",
                    pointsText,
                    softcore,
                    unread
                ).filter { it.isNotBlank() }.joinToString("\n")
            }
            RetroAchievementsNotificationKind.ERROR -> {
                kindName = "error"
                resolvedTitle = text(R.string.ra_notif_error_title, "RetroAchievements error")
                resolvedMessage = RetroAchievementsErrorMessages.resolve(
                    AppLocaleManager.localized(EmulatorBridge.getContext()), extra, description.ifBlank { title }
                )
            }
            RetroAchievementsNotificationKind.SERVER_DISCONNECTED -> {
                kindName = "error"
                resolvedTitle = text(R.string.ra_notif_server_disconnected_title, "Connection lost")
                resolvedMessage = text(R.string.ra_notif_server_disconnected_desc, "Cannot reach RetroAchievements. Unlocks are saved and will be sent when the connection returns.")
            }
            RetroAchievementsNotificationKind.SERVER_RECONNECTED -> {
                kindName = "info"
                resolvedTitle = text(R.string.ra_notif_server_reconnected_title, "Connection restored")
                resolvedMessage = text(R.string.ra_notif_server_reconnected_desc, "Pending unlocks were submitted to RetroAchievements.")
            }
            else -> {
                kindName = "info"
                resolvedTitle = title.ifBlank { text(R.string.ra_notif_app_name, "RetroAchievements") }
                resolvedMessage = description
            }
        }
        // A V2 notification is never dropped: the legacy callback is not sent when V2 exists.
        return RetroAchievementsNotification(
            id = System.nanoTime(),
            kind = kindName,
            title = resolvedTitle.ifBlank { text(R.string.ra_notif_app_name, "RetroAchievements") },
            message = resolvedMessage,
            imagePath = iconPath.trim().takeIf { it.isNotBlank() },
            durationMs = durationMs.coerceIn(0L, 30_000L)
        )
    }

    private fun parseExtra(extra: String): Map<String, String> =
        extra.lineSequence().mapNotNull { line ->
            val index = line.indexOf('=')
            if (index <= 0) null else line.substring(0, index).trim() to line.substring(index + 1).trim()
        }.toMap()

    private fun text(resId: Int, fallback: String, vararg args: Any): String {
        // App-selected language, not the system locale of the application context.
        val context = AppLocaleManager.localized(EmulatorBridge.getContext())
        return runCatching { context?.getString(resId, *args) }.getOrNull()
            ?: if (args.isEmpty()) fallback else String.format(fallback, *args)
    }

    private fun enqueue(notification: RetroAchievementsNotification) {
        _state.update { current ->
            val next = RetroAchievementsNotificationQueue.enqueue(current.notificationQueue, notification)
            if (next === current.notificationQueue) current else current.copy(notificationQueue = next)
        }
    }

    fun dismissNotification(id: Long) {
        _state.update { current ->
            val next = RetroAchievementsNotificationQueue.dismiss(current.notificationQueue, id)
            if (next === current.notificationQueue) current else current.copy(notificationQueue = next)
        }
    }

    /** Logout / account switch: drop everything that belonged to the previous session. */
    fun onLogout() {
        clearIndicators()
        _state.update { it.copy(notificationQueue = emptyList(), user = null, game = null, hardcoreActive = false) }
    }
}

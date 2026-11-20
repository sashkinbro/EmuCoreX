package com.sbro.emucorex.core.utils

import kotlin.math.ceil

/** Requested settings only. Native rc_client state alone controls current-session restrictions. */
internal object RetroAchievementsHardcorePolicy {
    fun resolve(globalRequested: Boolean, gameOverride: Boolean?, hostOverrideActive: Boolean): Boolean =
        (gameOverride ?: globalRequested) && !hostOverrideActive

    /** Emulated frames per second used to turn rc_client_can_pause() frames into a message (NTSC rate). */
    private const val PAUSE_FRAMES_PER_SECOND = 60.0

    /** Whole seconds (at least 1) the user still has to wait, from the frames rc_client_can_pause() reports. */
    fun pauseCooldownSeconds(framesRemaining: Int): Int =
        if (framesRemaining <= 0) 0 else ceil(framesRemaining / PAUSE_FRAMES_PER_SECOND).toInt().coerceAtLeast(1)

    /**
     * Opacity of the scrim drawn behind the paused/menu screen. RetroAchievements requires the paused
     * game to be obscured by at least 70% in Hardcore so that it cannot be studied while paused.
     */
    const val HARDCORE_PAUSE_SCRIM_ALPHA = 0.8f

    /**
     * RetroAchievements User-Agent: `<product>/<numeric semver> (<system info>) <extensions>`.
     * The product has no spaces and the version is digits and dots only (padded to major.minor.patch).
     */
    fun userAgent(appVersionName: String?, androidRelease: String?, coreVersion: String = "2.8.0"): String {
        val system = androidRelease?.trim()?.takeIf { it.isNotEmpty() }
            ?.filter { it.isLetterOrDigit() || it == '.' || it == '-' }
            ?.takeIf { it.isNotEmpty() }
            ?.let { "Android $it" } ?: "Android"
        return "EmuCoreX/${numericVersion(appVersionName)} ($system) pcsx2/${numericVersion(coreVersion)}"
    }

    internal fun numericVersion(raw: String?): String {
        var text = raw?.trim().orEmpty()
        if (text.startsWith("v") || text.startsWith("V")) text = text.substring(1)
        text = text.takeWhile { it.isDigit() || it == '.' }.trimEnd('.')
        if (text.isEmpty() || text.startsWith(".") || text.contains("..")) text = "0"
        val parts = text.split('.').toMutableList()
        while (parts.size < 3) parts += "0"
        return parts.joinToString(".")
    }
}

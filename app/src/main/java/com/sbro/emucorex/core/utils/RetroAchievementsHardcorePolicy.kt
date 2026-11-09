package com.sbro.emucorex.core.utils

/** Requested settings only. Native rc_client state alone controls current-session restrictions. */
internal object RetroAchievementsHardcorePolicy {
    fun resolve(globalRequested: Boolean, gameOverride: Boolean?, hostOverrideActive: Boolean): Boolean =
        (gameOverride ?: globalRequested) && !hostOverrideActive
}

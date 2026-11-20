package com.sbro.emucorex.core

/**
 * A startup performance preset. Every value here is also a real, individually stored setting;
 * the profile id is only a convenience marker. The profile shown to the user is always derived
 * from the effective values via [PerformanceProfiles.detect], never trusted from the stored id.
 *
 * @property forcedUpscale when non-null the preset owns the upscale multiplier. Presets that leave
 * it null never touch upscale, so picking SAFE/FAST after ULTRA keeps the user's 1x.
 */
data class PerformanceProfileConfig(
    val id: Int,
    val eeCycleRate: Int,
    val eeCycleSkip: Int,
    val hwDownloadMode: Int,
    val fpuCorrectAddSub: Boolean,
    val forcedUpscale: Float? = null
)

object PerformanceProfiles {
    const val SAFE = 0
    const val FAST = 1
    const val ULTRA = 2

    /** Returned by [detect] when the effective values match no preset. */
    const val CUSTOM = -1

    val safeConfig = PerformanceProfileConfig(
        id = SAFE,
        eeCycleRate = 0,
        eeCycleSkip = 0,
        hwDownloadMode = GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT,
        fpuCorrectAddSub = true
    )

    val fastConfig = PerformanceProfileConfig(
        id = FAST,
        eeCycleRate = -1,
        eeCycleSkip = 2,
        hwDownloadMode = GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT,
        fpuCorrectAddSub = false
    )

    val ultraConfig = PerformanceProfileConfig(
        id = ULTRA,
        eeCycleRate = -2,
        eeCycleSkip = 3,
        hwDownloadMode = GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT,
        fpuCorrectAddSub = false,
        forcedUpscale = 1f
    )

    fun normalize(profileId: Int): Int = when (profileId) {
        FAST -> FAST
        ULTRA -> ULTRA
        else -> SAFE
    }

    fun configFor(profileId: Int): PerformanceProfileConfig = when (normalize(profileId)) {
        FAST -> fastConfig
        ULTRA -> ultraConfig
        else -> safeConfig
    }

    /**
     * Derives the profile from the effective global values. ULTRA is checked before FAST
     * (it is a strict superset of FAST's flags). Returns [CUSTOM] when nothing matches.
     */
    fun detect(
        eeCycleRate: Int,
        eeCycleSkip: Int,
        fpuCorrectAddSub: Boolean,
        upscaleMultiplier: Float
    ): Int {
        for (config in listOf(ultraConfig, fastConfig, safeConfig)) {
            if (config.eeCycleRate != eeCycleRate) continue
            if (config.eeCycleSkip != eeCycleSkip) continue
            if (config.fpuCorrectAddSub != fpuCorrectAddSub) continue
            val forced = config.forcedUpscale
            if (forced != null && forced != upscaleMultiplier) continue
            return config.id
        }
        return CUSTOM
    }
}

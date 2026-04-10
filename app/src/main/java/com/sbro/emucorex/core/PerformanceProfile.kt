package com.sbro.emucorex.core

data class PerformanceProfileConfig(
    val id: Int,
    val renderer: Int = 14,
    val eeCycleRate: Int,
    val eeCycleSkip: Int,
    val hwDownloadMode: Int,
    val fpuClampMode: Int,
    val disableHardwareReadbacks: Boolean,
    val fpuCorrectAddSub: Boolean
)

object PerformanceProfiles {
    const val SAFE = 0
    const val FAST = 1

    val safeConfig = PerformanceProfileConfig(
        id = SAFE,
        eeCycleRate = 0,
        eeCycleSkip = 0,
        hwDownloadMode = 0,
        fpuClampMode = 1,
        disableHardwareReadbacks = false,
        fpuCorrectAddSub = true
    )

    val fastConfig = PerformanceProfileConfig(
        id = FAST,
        eeCycleRate = -1,
        eeCycleSkip = 2,
        hwDownloadMode = 2,
        fpuClampMode = 0,
        disableHardwareReadbacks = true,
        fpuCorrectAddSub = false
    )

    fun normalize(profileId: Int): Int {
        return if (profileId == FAST) FAST else SAFE
    }

    fun configFor(profileId: Int): PerformanceProfileConfig {
        return if (normalize(profileId) == FAST) fastConfig else safeConfig
    }
}

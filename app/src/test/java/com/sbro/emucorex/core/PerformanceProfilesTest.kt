package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class PerformanceProfilesTest {
    private fun detect(c: PerformanceProfileConfig, upscale: Float = 1f) =
        PerformanceProfiles.detect(c.eeCycleRate, c.eeCycleSkip, c.fpuCorrectAddSub, upscale)

    @Test
    fun presetsDetectThemselves() {
        assertEquals(PerformanceProfiles.SAFE, detect(PerformanceProfiles.safeConfig, 3f))
        assertEquals(PerformanceProfiles.FAST, detect(PerformanceProfiles.fastConfig, 3f))
        assertEquals(PerformanceProfiles.ULTRA, detect(PerformanceProfiles.ultraConfig, 1f))
    }

    @Test
    fun ultraRequiresNativeUpscale() {
        assertEquals(PerformanceProfiles.CUSTOM, detect(PerformanceProfiles.ultraConfig, 2f))
    }

    @Test
    fun manualEditsBecomeCustom() {
        assertEquals(PerformanceProfiles.CUSTOM, PerformanceProfiles.detect(-1, 2, true, 1f))
        assertEquals(PerformanceProfiles.CUSTOM, PerformanceProfiles.detect(-1, 1, false, 1f))
    }

    @Test
    fun normalizeAcceptsUltra() {
        assertEquals(PerformanceProfiles.ULTRA, PerformanceProfiles.normalize(2))
        assertEquals(PerformanceProfiles.SAFE, PerformanceProfiles.normalize(9))
    }
}

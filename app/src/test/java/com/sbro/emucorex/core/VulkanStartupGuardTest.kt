package com.sbro.emucorex.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class VulkanStartupGuardTest {
    @Test
    fun acceptsMarkerInsideCrashWindow() {
        val startedAt = 1_000L
        assertTrue(VulkanStartupGuard.isWithinCrashWindow(startedAt, startedAt + 50))
    }

    @Test
    fun rejectsMarkerOlderThanCrashWindow() {
        val startedAt = 1_000L
        assertFalse(
            VulkanStartupGuard.isWithinCrashWindow(
                startedAt,
                startedAt + VulkanStartupGuard.MAX_CRASH_AGE_MS + 1
            )
        )
    }

    @Test
    fun rejectsMissingMarker() {
        assertFalse(VulkanStartupGuard.isWithinCrashWindow(0L, 5_000L))
    }
}

package com.sbro.emucorex.ui.common

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class ProPromoImpressionThrottleTest {
    private val tenMin = ProPromoImpressionThrottle.INTERVAL_MS

    @Before
    fun setUp() = ProPromoImpressionThrottle.reset()

    @Test
    fun firstImpressionLogs_repeatWithinWindowDoesNot() {
        assertTrue(ProPromoImpressionThrottle.shouldLog("library", 1_000L))
        assertFalse(ProPromoImpressionThrottle.shouldLog("library", 1_000L + tenMin - 1))
    }

    @Test
    fun logsAgainAfterWindow() {
        assertTrue(ProPromoImpressionThrottle.shouldLog("library", 1_000L))
        assertTrue(ProPromoImpressionThrottle.shouldLog("library", 1_000L + tenMin))
    }

    @Test
    fun placementsAreIndependent() {
        assertTrue(ProPromoImpressionThrottle.shouldLog("library", 1_000L))
        assertTrue(ProPromoImpressionThrottle.shouldLog("themes", 1_001L))
    }
}

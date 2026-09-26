package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class UpscaleConfigTest {
    @Test
    fun buildUpscaleOptions_includesSubNativeAndQuarterStepsThroughTenX() {
        val options = buildUpscaleOptions(nativeLabel = "Native", maxMultiplier = 10)

        assertEquals(40, options.size)
        assertEquals(25 to "0.25x", options.first())
        assertEquals(50 to "0.5x", options[1])
        assertEquals(75 to "0.75x", options[2])
        assertEquals(100 to "Native", options[3])
        assertEquals(1000 to "10x", options.last())
        assertTrue(options.contains(525 to "5.25x"))
        assertTrue(options.contains(550 to "5.5x"))
        assertTrue(options.contains(575 to "5.75x"))
    }

    @Test
    fun normalizeUpscale_clampsBetweenSubNativeAndTenX() {
        assertEquals(10.0f, normalizeUpscale(12.0f))
        assertEquals(0.25f, normalizeUpscale(0.1f))
        assertEquals(0.5f, normalizeUpscale(0.51f))
        assertEquals(5.25f, normalizeUpscale(5.26f))
    }
}

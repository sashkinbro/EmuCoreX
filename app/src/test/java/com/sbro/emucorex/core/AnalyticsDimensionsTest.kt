package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class AnalyticsDimensionsTest {
    @Test
    fun launchTypeUsesOnlyBoundedTechnicalCategories() {
        assertEquals("game", AnalyticsDimensions.launchType(false, false, false))
        assertEquals("bios", AnalyticsDimensions.launchType(true, false, false))
        assertEquals("autotest", AnalyticsDimensions.launchType(false, false, true))
        assertEquals("smoke_test", AnalyticsDimensions.launchType(false, true, true))
        assertEquals("unknown", AnalyticsDimensions.sanitizeLaunchType("content://private/game.iso"))
    }

    @Test
    fun backendAndRendererValuesAreNormalized() {
        assertEquals("aaudio", AnalyticsDimensions.audioBackend(AudioDefaults.BACKEND_AAUDIO))
        assertEquals("opensles", AnalyticsDimensions.audioBackend(AudioDefaults.BACKEND_OPENSLES))
        assertEquals("aaudio", AnalyticsDimensions.audioBackend(Int.MAX_VALUE))
        assertEquals("opengl", AnalyticsDimensions.renderer(RendererDefaults.OPENGL))
        assertEquals("software", AnalyticsDimensions.renderer(RendererDefaults.SOFTWARE))
        assertEquals("vulkan", AnalyticsDimensions.renderer(RendererDefaults.VULKAN))
        assertEquals("unknown", AnalyticsDimensions.renderer(Int.MIN_VALUE))
    }

    @Test
    fun durationAndUpscaleUseLowCardinalityBuckets() {
        assertEquals("under_1m", AnalyticsDimensions.durationBucket(59_999L))
        assertEquals("1_5m", AnalyticsDimensions.durationBucket(60_000L))
        assertEquals("over_60m", AnalyticsDimensions.durationBucket(60 * 60_000L))
        assertEquals("native", AnalyticsDimensions.upscale(Float.NaN))
        assertEquals("native", AnalyticsDimensions.upscale(1f))
        assertEquals("2x", AnalyticsDimensions.upscale(2f))
        assertEquals("3x", AnalyticsDimensions.upscale(3f))
        assertEquals("4x_plus", AnalyticsDimensions.upscale(4f))
    }

    @Test
    fun arbitraryFailureAndActionValuesAreRejected() {
        assertEquals("bios_missing", AnalyticsDimensions.sanitizeFailureReason("bios_missing"))
        assertEquals("unknown", AnalyticsDimensions.sanitizeFailureReason("/storage/emulated/0"))
        assertEquals("save", AnalyticsDimensions.sanitizeSaveStateAction("save"))
        assertEquals("unknown", AnalyticsDimensions.sanitizeSaveStateAction("user supplied"))
    }
}

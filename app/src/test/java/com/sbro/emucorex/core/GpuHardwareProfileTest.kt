package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GpuHardwareProfileTest {
    @Test
    fun classifiesKnownSocVendorsWithoutBroadSubstringMatches() {
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("MediaTek MT6989"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("board mt6897"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("Qualcomm SM8650"))
        assertEquals(GpuHardwareProfiles.POWERVR, GpuHardwareProfiles.classifyHardwareProfile("IMGTEC PowerVR"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("Comtech generic board"))
    }

    @Test
    fun identifiesMediaTekSocIndependentlyFromGpuFamily() {
        assertTrue(GpuHardwareProfiles.hasMediaTekSocHints("MediaTek MT6989 IMGTEC PowerVR"))
        assertTrue(GpuHardwareProfiles.hasMediaTekSocHints("Dimensity 9300"))
        assertFalse(GpuHardwareProfiles.hasMediaTekSocHints("Samsung Exynos Mali-G78"))
        assertFalse(GpuHardwareProfiles.hasMediaTekSocHints("Qualcomm Snapdragon 8 Gen 3"))
    }

    @Test
    fun nativeProfileAlwaysUsesRendererAutoDetection() {
        assertEquals("auto", GpuHardwareProfiles.coreOverrideFor(GpuHardwareProfiles.ADRENO))
        assertEquals("auto", GpuHardwareProfiles.coreOverrideFor(GpuHardwareProfiles.MALI))
        assertEquals("auto", GpuHardwareProfiles.coreOverrideFor(GpuHardwareProfiles.POWERVR))
    }

    @Test
    fun rendererNormalizationRejectsUnsupportedPersistedValues() {
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.OPENGL))
        assertEquals(RendererDefaults.SOFTWARE, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.SOFTWARE))
        assertEquals(RendererDefaults.VULKAN, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.VULKAN))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(999, false))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.AUTO, false))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(999, true))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.AUTO, true))
        // An explicit user selection must survive regardless of the hardware default.
        assertEquals(RendererDefaults.VULKAN, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.VULKAN, true))
    }

    @Test
    fun rendererDefaultsToOpenGlForSnapdragonAndMediaTek() {
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.defaultForHardware(false))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.defaultForHardware(true))
    }
}

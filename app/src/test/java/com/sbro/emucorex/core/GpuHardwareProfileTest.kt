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
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("Comtech generic board"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("Samsung Exynos 2200"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("ARM Mali-G78"))
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
        assertEquals("auto", GpuHardwareProfiles.coreOverrideFor())
    }

    @Test
    fun rendererNormalizationRejectsUnsupportedPersistedValues() {
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.OPENGL))
        assertEquals(RendererDefaults.SOFTWARE, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.SOFTWARE))
        assertEquals(RendererDefaults.VULKAN, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.VULKAN))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(999))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.AUTO))
        // An explicit user selection must survive regardless of the hardware default.
        assertEquals(RendererDefaults.VULKAN, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.VULKAN))
    }

    @Test
    fun rendererDefaultsToOpenGlForSnapdragonAndMediaTek() {
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.defaultForHardware())
    }
}

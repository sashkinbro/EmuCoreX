package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GpuHardwareProfileTest {
    @Test
    fun classifiesAllwinnerAndEliteFromTheSocCatalog() {
        assertEquals(GpuHardwareProfiles.POWERVR, GpuHardwareProfiles.classifyHardwareProfile("Allwinner A133P"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("sun55iw3p1"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("sun"))
    }

    @Test
    fun classifiesKnownSocVendorsWithoutBroadSubstringMatches() {
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("MediaTek MT6989"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("board mt6897"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("Qualcomm SM8650"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("sm7635-ac vulcano"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("SM4450"))
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
    fun familyNameMatchesHardwareProfile() {
        assertEquals("adreno", GpuHardwareProfiles.familyName(GpuHardwareProfiles.ADRENO))
        assertEquals("powervr", GpuHardwareProfiles.familyName(GpuHardwareProfiles.POWERVR))
        assertEquals("mali", GpuHardwareProfiles.familyName(GpuHardwareProfiles.MALI))
        assertEquals("mali", GpuHardwareProfiles.familyName(999))
    }

    @Test
    fun rendererDefaultsAreSelectedByGpuFamily() {
        assertEquals(RendererDefaults.VULKAN, RendererDefaults.defaultForProfile(GpuHardwareProfiles.ADRENO))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.defaultForProfile(GpuHardwareProfiles.MALI))
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.defaultForProfile(GpuHardwareProfiles.POWERVR))
    }

    @Test
    fun rendererNormalizationPreservesSupportedSelectionsAndUsesHardwareFallback() {
        assertEquals(RendererDefaults.OPENGL, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.OPENGL))
        assertEquals(RendererDefaults.SOFTWARE, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.SOFTWARE))
        assertEquals(RendererDefaults.VULKAN, RendererDefaults.normalizeAndroidRenderer(RendererDefaults.VULKAN))
        assertEquals(RendererDefaults.defaultForHardware(), RendererDefaults.normalizeAndroidRenderer(999))
        assertEquals(RendererDefaults.defaultForHardware(), RendererDefaults.normalizeAndroidRenderer(RendererDefaults.AUTO))
    }
}

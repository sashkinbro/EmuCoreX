package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class GpuDriverCompatibilityTest {
    @Test
    fun `maps current flagship Snapdragon generations to their Adreno families`() {
        assertEquals("Adreno 840", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 5")?.adrenoName)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite")?.family)
        assertEquals("Adreno 750", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Gen 3")?.adrenoName)
        assertEquals(AdrenoFamily.A6XX, GpuDriverRecommendations.profileForSoc("Snapdragon 865 series")?.family)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 6 Gen 4")?.family)
        assertEquals(AdrenoFamily.A7XX, GpuDriverRecommendations.profileForSoc("Snapdragon 6 Gen 1")?.family)
        assertEquals("Adreno 810", GpuDriverRecommendations.profileForSoc("Snapdragon 7s Gen 4")?.adrenoName)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 7s Gen 3")?.family)
        assertEquals("Adreno 845", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 6")?.adrenoName)
        assertEquals("Adreno 850", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Extreme Gen 6")?.adrenoName)
    }

    @Test
    fun `prefers the catalog mapping over a stale renderer string`() {
        val staleRenderer = GpuModelInfo(GpuVendor.ADRENO, "Adreno 710")
        assertEquals(
            "Adreno 810",
            GpuDriverRecommendations.profileForGpu("Snapdragon 7s Gen 3", staleRenderer)?.adrenoName
        )
    }

    @Test
    fun `falls back to the detected GPU when the SoC name is unknown`() {
        val detected = GpuModelInfo(GpuVendor.ADRENO, "Adreno 810")
        assertEquals(
            "Adreno 810",
            GpuDriverRecommendations.profileForGpu("Unknown SoC", detected)?.adrenoName
        )
        assertEquals(
            AdrenoFamily.A8XX,
            GpuDriverRecommendations.profileForGpu("Unknown SoC", detected)?.family
        )
        assertEquals(null, GpuDriverRecommendations.profileForGpu("Unknown SoC", null))
    }

    @Test
    fun `maps budget Snapdragon models exactly instead of a broad family token`() {
        assertEquals("Adreno 619", GpuDriverRecommendations.profileForSoc("Snapdragon 695")?.adrenoName)
        assertEquals("Adreno 619L", GpuDriverRecommendations.profileForSoc("Snapdragon 690")?.adrenoName)
        assertEquals("Adreno 610", GpuDriverRecommendations.profileForSoc("Snapdragon 680 series")?.adrenoName)
        assertEquals("Adreno 610", GpuDriverRecommendations.profileForSoc("Snapdragon 665")?.adrenoName)
        assertEquals("Adreno 613", GpuDriverRecommendations.profileForSoc("Snapdragon 4 Gen 2")?.adrenoName)
        assertEquals("Adreno 642L", GpuDriverRecommendations.profileForSoc("Snapdragon 778G series")?.adrenoName)
    }

    @Test
    fun `does not produce a recommendation for a non Snapdragon device`() {
        assertEquals(null, GpuDriverRecommendations.profileForSoc("Dimensity 9400"))
        // Qualcomm markets G-series GPUs as A11/A12/A21/A32/A33. Do not guess that these
        // use the same driver family numbering as phone Adreno 6xx/7xx/8xx packages.
        assertEquals(null, GpuDriverRecommendations.profileForSoc("Snapdragon G3x Gen 2"))
    }

    @Test
    fun `matches broad and explicitly limited driver packages`() {
        val elite = GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite")
        val gen5 = GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 5")
        val broad = driver(gpu = "Adreno 6xx/7xx/8xx")
        val limited = driver(gpu = "Adreno 8xx (830/840)")
        val a7 = driver(gpu = "Adreno 6xx/7xx")

        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(broad, elite))
        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(limited, elite))
        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(limited, gen5))
        assertEquals(GpuDriverMatch.OTHER_FAMILY, GpuDriverRecommendations.match(a7, elite))
        assertTrue(GpuDriverRecommendations.supportedFamilies(broad.gpu).contains(AdrenoFamily.A7XX))
    }

    @Test
    fun `classifies device vendors from build strings`() {
        assertEquals(
            GpuDriverVendor.ADRENO,
            GpuDriverCompatibility.deviceVendor("Qualcomm Technologies, Inc SM8650 for arm64")
        )
        assertEquals(GpuDriverVendor.ADRENO, GpuDriverCompatibility.deviceVendor("sm7635-ac volcano"))
        assertEquals(GpuDriverVendor.ADRENO, GpuDriverCompatibility.deviceVendor("SM4450"))
        assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.deviceVendor("MediaTek MT6989"))
        assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.deviceVendor("mt6993"))
        assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.deviceVendor("s5e8835"))
        assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.deviceVendor("samsung exynos 1380 mali-g68"))
        assertEquals(
            GpuDriverVendor.XCLIPSE,
            GpuDriverCompatibility.deviceVendor("samsung exynos 2400 xclipse 940")
        )
        assertEquals(GpuDriverVendor.XCLIPSE, GpuDriverCompatibility.deviceVendor("Samsung Exynos 1680"))
        assertEquals(GpuDriverVendor.XCLIPSE, GpuDriverCompatibility.deviceVendor("s5e8865"))
        assertEquals(GpuDriverVendor.POWERVR, GpuDriverCompatibility.deviceVendor("IMG PowerVR GE8320"))
        assertEquals(GpuDriverVendor.UNKNOWN, GpuDriverCompatibility.deviceVendor("generic board"))
    }

    @Test
    fun `classifies detected GPU vendors`() {
        assertEquals(
            GpuDriverVendor.ADRENO,
            GpuDriverCompatibility.vendorOfGpu(GpuModelInfo(GpuVendor.ADRENO, "Adreno 810"))
        )
        assertEquals(
            GpuDriverVendor.MALI,
            GpuDriverCompatibility.vendorOfGpu(GpuModelInfo(GpuVendor.MALI, "Immortalis-G925 MC12"))
        )
        assertEquals(
            GpuDriverVendor.XCLIPSE,
            GpuDriverCompatibility.vendorOfGpu(GpuModelInfo(GpuVendor.XCLIPSE, "Xclipse 960"))
        )
        assertEquals(
            GpuDriverVendor.POWERVR,
            GpuDriverCompatibility.vendorOfGpu(GpuModelInfo(GpuVendor.POWERVR, "PowerVR CXTP-48-1536"))
        )
        assertEquals(GpuDriverVendor.UNKNOWN, GpuDriverCompatibility.vendorOfGpu(null))
    }

    @Test
    fun `classifies catalog gpu strings`() {
        assertEquals(GpuDriverVendor.ADRENO, GpuDriverCompatibility.vendorOfCatalogGpu("Adreno 6xx/7xx"))
        assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.vendorOfCatalogGpu("Mali PanVK (Valhall)"))
        assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.vendorOfCatalogGpu("Panfrost"))
        assertEquals(GpuDriverVendor.XCLIPSE, GpuDriverCompatibility.vendorOfCatalogGpu("Xclipse (Exynos 2400)"))
        assertEquals(GpuDriverVendor.POWERVR, GpuDriverCompatibility.vendorOfCatalogGpu("PowerVR"))
        assertEquals(GpuDriverVendor.UNKNOWN, GpuDriverCompatibility.vendorOfCatalogGpu(""))
    }

    @Test
    fun `detects vendor markers inside driver libraries`() {
        val turnip = File.createTempFile("turnip", ".so")
        val panvk = File.createTempFile("panvk", ".so")
        val xclipse = File.createTempFile("xclipse", ".so")
        try {
            turnip.writeBytes("libvulkan_freedreno.so tu_clear".toByteArray())
            assertEquals(GpuDriverVendor.ADRENO, GpuDriverCompatibility.vendorOfDriverLibrary(turnip))

            // PanVK packs ship under the freedreno name; ELF markers must win.
            panvk.writeBytes("../src/panfrost/vulkan/panvk_v10_device.c".toByteArray())
            assertEquals(GpuDriverVendor.MALI, GpuDriverCompatibility.vendorOfDriverLibrary(panvk))

            xclipse.writeBytes("libVkLayer_VortekXclipse.so".toByteArray())
            assertEquals(GpuDriverVendor.XCLIPSE, GpuDriverCompatibility.vendorOfDriverLibrary(xclipse))
        } finally {
            turnip.delete()
            panvk.delete()
            xclipse.delete()
        }
    }

    @Test
    fun `refuses cross vendor driver and device pairs`() {
        assertTrue(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.MALI, GpuDriverVendor.MALI))
        assertTrue(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.UNKNOWN, GpuDriverVendor.ADRENO))
        assertFalse(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.MALI, GpuDriverVendor.ADRENO))
        assertFalse(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.ADRENO, GpuDriverVendor.MALI))
        assertFalse(GpuDriverCompatibility.isVendorCompatible(GpuDriverVendor.XCLIPSE, GpuDriverVendor.MALI))
    }

    @Test
    fun `matches non Adreno packages only against their own vendor`() {
        val mali = driver(gpu = "Mali PanVK (Valhall)")
        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(mali, null, GpuDriverVendor.MALI))
        assertEquals(GpuDriverMatch.OTHER_FAMILY, GpuDriverRecommendations.match(mali, null, GpuDriverVendor.ADRENO))
        assertEquals(GpuDriverMatch.UNKNOWN, GpuDriverRecommendations.match(mali, null, null))

        val adreno = driver(gpu = "Adreno 6xx/7xx")
        val elite = GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite")
        assertEquals(
            GpuDriverMatch.OTHER_FAMILY,
            GpuDriverRecommendations.match(adreno, elite, GpuDriverVendor.MALI)
        )
    }

    private fun driver(gpu: String) = RemoteGpuDriver(
        id = "test",
        name = "Test",
        variant = "Auto",
        gpu = gpu,
        description = "",
        recommended = false,
        downloadUrl = "https://example.com/test.zip",
        sourceUrl = "",
        credits = "",
        sizeBytes = null
    )
}

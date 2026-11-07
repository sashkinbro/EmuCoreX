package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MaliSx2DriverCompatibilityTest {
    private val maliDevice = GpuDriverVendor.MALI
    private val sx2Driver = driver(id = "mali-sx2-0.1.4", gpu = "Mali SX2")
    private val ordinaryMaliDriver = driver(id = "panvk-generic", gpu = "Mali PanVK (Valhall)")

    @Test
    fun supportedMaliV9AndV11RendererModelsAllowTheSx2CatalogEntry() {
        listOf(
            "Mali-G57 MC4", "Mali-G68 MP5", "Mali-G77 MC9", "Mali-G78 MC5",
            "Mali-G78AE MC4", "Mali-G615 MC6", "Mali-G715 MP7", "Immortalis-G715"
        )
            .forEach { renderer ->
                val model = DeviceGpuInfoResolver.resolve(renderer, null)
                assertTrue("$renderer should support the Mali SX2 catalog row", GpuDriverCompatibility.isCatalogDriverCompatible(
                    sx2Driver, maliDevice, model
                ))
            }
    }

    @Test
    fun sx2FailsClosedForUnknownOrUnsupportedMaliModelsWhileOtherMaliDriversKeepExistingBehavior() {
        val unknown = DeviceGpuInfoResolver.resolve(null, null)
        val unsupported = DeviceGpuInfoResolver.resolve("Mali-G610 MC4", null)
        for (model in listOf(unknown, unsupported)) {
            assertFalse(GpuDriverCompatibility.isCatalogDriverCompatible(sx2Driver, maliDevice, model))
            assertTrue(GpuDriverCompatibility.isCatalogDriverCompatible(ordinaryMaliDriver, maliDevice, model))
        }
    }

    @Test
    fun onlyTheSx2InstalledLibraryGetsRendererSpecificValidation() {
        val supported = DeviceGpuInfoResolver.resolve("ARM Mali-G78AE", null)
        val unsupported = DeviceGpuInfoResolver.resolve("ARM Mali-G610", null)
        assertTrue(GpuDriverCompatibility.isInstalledDriverCompatible(
            "libvulkan_malisx2.so", maliDevice, maliDevice, supported
        ))
        // An installed 0.3.1-or-newer package must stay selectable on the Bifrost models.
        assertTrue(GpuDriverCompatibility.isInstalledDriverCompatible(
            "libvulkan_malisx2.so", maliDevice, maliDevice,
            DeviceGpuInfoResolver.resolve("Mali-G52 MC1", null)
        ))
        assertTrue(GpuDriverCompatibility.isInstalledDriverCompatible(
            "libvulkan_malisx2.so", maliDevice, maliDevice,
            DeviceGpuInfoResolver.resolve("Mali-G76 MC4", null)
        ))
        assertTrue(GpuDriverCompatibility.isInstalledDriverCompatible(
            "drivers/vendor/libvulkan_malisx2.so", maliDevice, maliDevice, supported
        ))
        assertFalse(GpuDriverCompatibility.isInstalledDriverCompatible(
            "libvulkan_malisx2.so", maliDevice, maliDevice, unsupported
        ))
        assertFalse(GpuDriverCompatibility.isInstalledDriverCompatible(
            "libvulkan_malisx2.so", maliDevice, maliDevice, null
        ))
        assertTrue(GpuDriverCompatibility.isInstalledDriverCompatible(
            "libvulkan_panvk.so", maliDevice, maliDevice, null
        ))
    }

    @Test
    fun sx2RequiresAValidatedGlRendererRatherThanASocCatalogGuess() {
        val socOnly = DeviceGpuInfoResolver.resolve(null, "MediaTek Dimensity 9400")
        assertTrue(socOnly.model != null)
        assertFalse(GpuDriverCompatibility.isCatalogDriverCompatible(sx2Driver, maliDevice, socOnly))
    }

    @Test
    fun mediatekMaliDevicesAreEligibleForTheCustomDriverManager() {
        assertTrue(GpuDriverCompatibility.supportsCustomDrivers("MediaTek MT6989 Mali-G610"))
        assertTrue(GpuDriverCompatibility.supportsCustomDrivers("Samsung Exynos 2400 Xclipse 940"))
        assertFalse(GpuDriverCompatibility.supportsCustomDrivers("generic board"))
    }

    @Test
    fun catalogDeclaredModelsOpenSx2ToMaliG52AndG76() {
        val newest = driver(
            id = "mali-sx2-0.4.0",
            gpu = "Mali Bifrost/Valhall (G52, G52 r1, G76, G57, G68, G77, G78, G78AE, G615, G715)",
            models = listOf("G52", "G76", "G57", "G68", "G77", "G78", "G78AE", "G615", "G715")
        )
        listOf(
            "Mali-G52", "Mali-G52 MC1", "Mali-G76 MC4", "Mali-G57 MC4", "Immortalis-G715"
        )
            .forEach { renderer ->
                val model = DeviceGpuInfoResolver.resolve(renderer, null)
                assertTrue(
                    "$renderer should support the catalog-declared 0.4.0 row",
                    GpuDriverCompatibility.isCatalogDriverCompatible(newest, maliDevice, model)
                )
            }
    }

    @Test
    fun catalogModelsKeepOlderSx2EntriesOffMaliG52AndG76() {
        val legacy = driver(
            id = "mali-sx2-0.1.4",
            gpu = "Mali Bifrost/Valhall",
            models = listOf("G57", "G68", "G77", "G78", "G78AE", "G615", "G715")
        )
        listOf("Mali-G52", "Mali-G76 MC4").forEach { renderer ->
            val model = DeviceGpuInfoResolver.resolve(renderer, null)
            assertFalse(
                "$renderer must not offer a package that does not declare it",
                GpuDriverCompatibility.isCatalogDriverCompatible(legacy, maliDevice, model)
            )
        }
        val model57 = DeviceGpuInfoResolver.resolve("Mali-G57 MC4", null)
        assertTrue(GpuDriverCompatibility.isCatalogDriverCompatible(legacy, maliDevice, model57))
    }

    @Test
    fun entriesWithoutCatalogModelsUseTheFallbackModelList() {
        val model52 = DeviceGpuInfoResolver.resolve("Mali-G52 MC1", null)
        val model76 = DeviceGpuInfoResolver.resolve("Mali-G76 MC4", null)
        val model57 = DeviceGpuInfoResolver.resolve("Mali-G57 MC4", null)
        val model610 = DeviceGpuInfoResolver.resolve("Mali-G610 MC4", null)
        assertTrue(GpuDriverCompatibility.isCatalogDriverCompatible(sx2Driver, maliDevice, model52))
        assertTrue(GpuDriverCompatibility.isCatalogDriverCompatible(sx2Driver, maliDevice, model76))
        assertTrue(GpuDriverCompatibility.isCatalogDriverCompatible(sx2Driver, maliDevice, model57))
        assertFalse(GpuDriverCompatibility.isCatalogDriverCompatible(sx2Driver, maliDevice, model610))
    }

    @Test
    fun modelTokensFollowTheCatalogSpelling() {
        assertEquals("G52", GpuDriverCompatibility.maliSx2ModelToken(
            DeviceGpuInfoResolver.resolve("Mali-G52 r1", null).model
        ))
        assertEquals("G76", GpuDriverCompatibility.maliSx2ModelToken(
            DeviceGpuInfoResolver.resolve("Mali-G76 MC4", null).model
        ))
        // The renderer parser drops the "AE" suffix, so the token is G78 either way.
        assertEquals("G78", GpuDriverCompatibility.maliSx2ModelToken(
            DeviceGpuInfoResolver.resolve("Mali-G78AE MC4", null).model
        ))
        assertEquals("G715", GpuDriverCompatibility.maliSx2ModelToken(
            DeviceGpuInfoResolver.resolve("Immortalis-G715", null).model
        ))
    }

    @Test
    fun aDeclaredG78aeModelStillMatchesTheParsedG78Renderer() {
        val newest = driver(
            id = "mali-sx2-0.4.0",
            gpu = "Mali Bifrost/Valhall",
            models = listOf("G52", "G76", "G57", "G68", "G77", "G78", "G78AE", "G615", "G715")
        )
        listOf("Mali-G78 MC5", "Mali-G78AE MC4").forEach { renderer ->
            val model = DeviceGpuInfoResolver.resolve(renderer, null)
            assertTrue(
                "$renderer must match the declared G78/G78AE models",
                GpuDriverCompatibility.isCatalogDriverCompatible(newest, maliDevice, model)
            )
        }
    }

    private fun driver(id: String, gpu: String, models: List<String> = emptyList()) = RemoteGpuDriver(
        id = id,
        name = id,
        variant = "Mali SX2",
        gpu = gpu,
        description = "",
        recommended = false,
        downloadUrl = "https://example.com/$id.zip",
        sourceUrl = "",
        credits = "",
        sizeBytes = null,
        supportedGpuModels = models
    )
}

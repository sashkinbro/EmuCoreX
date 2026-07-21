package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class GsHackDefaultsTest {
    @Test
    fun defaultsMatchNativeGsEnums() {
        assertEquals(2, GsHackDefaults.BILINEAR_FILTERING_DEFAULT)
        assertEquals(-1, GsHackDefaults.TRILINEAR_FILTERING_DEFAULT)
        assertEquals(1, GsHackDefaults.BLENDING_ACCURACY_DEFAULT)
        assertEquals(2, GsHackDefaults.TEXTURE_PRELOADING_DEFAULT)
        assertEquals(4, GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT) // Disabled for Android
        assertEquals(0, GsHackDefaults.ANISOTROPIC_FILTERING_DEFAULT)
    }

    @Test
    fun hardwareDownloadModesCoverTheCompleteNativeEnum() {
        assertEquals(0, GsHackDefaults.coerceHardwareDownloadMode(-1))
        assertEquals(1, GsHackDefaults.coerceHardwareDownloadMode(1))
        assertEquals(2, GsHackDefaults.coerceHardwareDownloadMode(2))
        assertEquals(3, GsHackDefaults.coerceHardwareDownloadMode(3))
        assertEquals(4, GsHackDefaults.coerceHardwareDownloadMode(4))
        assertEquals(4, GsHackDefaults.coerceHardwareDownloadMode(5))
        assertEquals(0, GsHackDefaults.coerceFrameSkip(-1))
        assertEquals(4, GsHackDefaults.coerceFrameSkip(5))
    }

    @Test
    fun invalidPersistedGsEnumsAreSanitized() {
        assertEquals(0, GsHackDefaults.coerceBilinearFiltering(-20))
        assertEquals(3, GsHackDefaults.coerceBilinearFiltering(20))
        assertEquals(-1, GsHackDefaults.coerceTrilinearFiltering(-20))
        assertEquals(2, GsHackDefaults.coerceTrilinearFiltering(20))
        assertEquals(0, GsHackDefaults.coerceBlendingAccuracy(-20))
        assertEquals(5, GsHackDefaults.coerceBlendingAccuracy(20))
        assertEquals(0, GsHackDefaults.coerceTexturePreloading(-20))
        assertEquals(2, GsHackDefaults.coerceTexturePreloading(20))
        assertEquals(0, GsHackDefaults.coerceNativeScaling(-20))
        assertEquals(4, GsHackDefaults.coerceNativeScaling(20))
        assertEquals(16, GsHackDefaults.coerceAnisotropicFiltering(16))
        assertEquals(0, GsHackDefaults.coerceAnisotropicFiltering(12))
    }
}

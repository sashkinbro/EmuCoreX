package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class GsImageProcessingSettingsTest {
    @Test
    fun deinterlacingAcceptsEveryCoreMode() {
        (GsHackDefaults.DEINTERLACE_MODE_MIN..GsHackDefaults.DEINTERLACE_MODE_MAX).forEach { mode ->
            assertEquals(mode, GsHackDefaults.coerceDeinterlaceMode(mode))
        }
    }

    @Test
    fun invalidDeinterlacingFallsBackToAutomatic() {
        assertEquals(
            GsHackDefaults.DEINTERLACE_MODE_DEFAULT,
            GsHackDefaults.coerceDeinterlaceMode(Int.MIN_VALUE)
        )
        assertEquals(
            GsHackDefaults.DEINTERLACE_MODE_DEFAULT,
            GsHackDefaults.coerceDeinterlaceMode(Int.MAX_VALUE)
        )
    }

    @Test
    fun ditheringAcceptsEveryCoreMode() {
        (GsHackDefaults.DITHERING_MIN..GsHackDefaults.DITHERING_MAX).forEach { mode ->
            assertEquals(mode, GsHackDefaults.coerceDithering(mode))
        }
    }

    @Test
    fun invalidDitheringFallsBackToAccurateDefault() {
        assertEquals(2, GsHackDefaults.DITHERING_DEFAULT)
        assertEquals(
            GsHackDefaults.DITHERING_DEFAULT,
            GsHackDefaults.coerceDithering(-1)
        )
        assertEquals(
            GsHackDefaults.DITHERING_DEFAULT,
            GsHackDefaults.coerceDithering(4)
        )
    }
}

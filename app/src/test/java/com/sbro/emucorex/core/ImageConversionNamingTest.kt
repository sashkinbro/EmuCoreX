package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class ImageConversionNamingTest {

    @Test
    fun `iso names become chd output names`() {
        assertEquals("game.chd", ImageConversionManager.buildOutputName("game.iso"))
        assertEquals("game.chd", ImageConversionManager.buildOutputName("game.ISO"))
        assertEquals("game.chd", ImageConversionManager.buildOutputName("game"))
        assertEquals("disc 1.chd", ImageConversionManager.buildOutputName("disc 1.iso"))
    }

    @Test
    fun `chd names become iso output names`() {
        assertEquals("game.iso", ImageConversionManager.buildIsoOutputName("game.chd"))
        assertEquals("game.iso", ImageConversionManager.buildIsoOutputName("game.CHD"))
        assertEquals("game.iso", ImageConversionManager.buildIsoOutputName("game"))
        assertEquals("disc 1.iso", ImageConversionManager.buildIsoOutputName("disc 1.chd"))
    }

    @Test
    fun `bin names become iso output names`() {
        assertEquals("game.iso", ImageConversionManager.buildIsoFromBinOutputName("game.bin"))
        assertEquals("game.iso", ImageConversionManager.buildIsoFromBinOutputName("game.BIN"))
        assertEquals("game.iso", ImageConversionManager.buildIsoFromBinOutputName("game"))
        assertEquals("disc 1.iso", ImageConversionManager.buildIsoFromBinOutputName("disc 1.bin"))
    }

    @Test
    fun `progress fraction is clamped and safe with unknown totals`() {
        assertEquals(
            0.5f,
            ImageConversionManager.ConversionProgress(
                stage = ImageConversionManager.ConversionStage.CONVERTING,
                processedBytes = 50L,
                totalBytes = 100L
            ).fraction,
            0.0001f
        )
        assertEquals(
            1f,
            ImageConversionManager.ConversionProgress(
                stage = ImageConversionManager.ConversionStage.SAVING,
                processedBytes = 150L,
                totalBytes = 100L
            ).fraction,
            0.0001f
        )
        assertEquals(
            0f,
            ImageConversionManager.ConversionProgress(
                stage = ImageConversionManager.ConversionStage.STAGING,
                processedBytes = 25L,
                totalBytes = 0L
            ).fraction,
            0.0001f
        )
    }
}

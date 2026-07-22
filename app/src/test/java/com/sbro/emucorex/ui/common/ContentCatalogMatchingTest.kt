package com.sbro.emucorex.ui.common

import org.junit.Assert.assertEquals
import org.junit.Test

class ContentCatalogMatchingTest {
    @Test
    fun textureTitlesIgnorePunctuationAndCase() {
        assertEquals(
            "gran turismo 3 a spec",
            "Gran Turismo 3: A-Spec".contentCatalogTitleKey()
        )
        assertEquals(
            "gran turismo 3 a spec",
            "GRAN TURISMO 3 - A SPEC".contentCatalogTitleKey()
        )
    }

    @Test
    fun cheatTitlesIgnoreRegionSerialAndDiscMetadata() {
        assertEquals(
            "call of duty 3",
            "Call of Duty 3 NTSC-U SLUS-21426".cheatCatalogGameTitleKey()
        )
        assertEquals(
            "grandia iii",
            "Grandia III PAL SLES-50715 Disc 1 of 2".cheatCatalogGameTitleKey()
        )
    }

    @Test
    fun differentGamesStayDifferent() {
        assertEquals("persona 4", "Persona 4".cheatCatalogGameTitleKey())
        assertEquals("persona 4 arena", "Persona 4 Arena".cheatCatalogGameTitleKey())
    }
}

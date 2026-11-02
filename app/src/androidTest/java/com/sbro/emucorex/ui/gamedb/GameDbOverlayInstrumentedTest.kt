package com.sbro.emucorex.ui.gamedb

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import android.content.Context
import com.sbro.emucorex.data.gamedb.GameDbCatalogRepository
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.pcsx2.GameIndexOverlay
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GameDbOverlayInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun overlayReplacesGraphicsBlockAndAddsCompatibilityMetadataOverPrecomputedIndex() {
        val overlay = GameIndexOverlay.load(context)
        assertTrue("The mobile GameDB overlay must be bundled", overlay.isNotEmpty())

        val rogueGalaxy = GameDbCatalogRepository(context).loadEntries()
            .single { it.serial == "SCAJ-20162" }
        val graphics = rogueGalaxy.settings
            .filter { it.category == GameDbSettingCategory.GRAPHICS_FIX }
            .associate { it.key to it.value }
        assertEquals("0", graphics["autoFlush"])
        assertEquals("5", graphics["hwDownloadMode"])
        assertFalse("The overlay's GS block replaces upstream as a whole", "preloadFrameData" in graphics)

        val arcadeCompatibility = Pcsx2CompatibilityRepository(context).findBySerial("NM00020")
        assertNotNull("Overlay-only arcade serials must survive the preferred JSON-index path", arcadeCompatibility)
        assertEquals("Dragon Chronicle Online - Great Sky Battle", arcadeCompatibility?.title)
        assertEquals("System246", arcadeCompatibility?.region)
    }

}

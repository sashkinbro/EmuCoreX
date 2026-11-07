package com.sbro.emucorex.ui.gamedb

import com.sbro.emucorex.data.gamedb.GameDbFixSection
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GameDbFixCatalogTest {
    @Test
    fun clampModesCanBeAddedOneKeyAtATime() {
        assertTrue(GameDbFixCatalog.supportsAdding(GameDbFixSection.CLAMP_MODES))
        assertEquals(
            listOf("eeClampMode", "vu0ClampMode", "vu1ClampMode"),
            GameDbFixCatalog.addable(GameDbFixSection.CLAMP_MODES).map { it.key }
        )
        assertEquals("1", GameDbFixCatalog.defaultValue(GameDbFixSection.CLAMP_MODES, "vu1ClampMode"))
    }

    @Test
    fun everyAddableClampModeStartsWithAValueThePickerCanShow() {
        GameDbFixCatalog.addable(GameDbFixSection.CLAMP_MODES).forEach { fix ->
            val options = GameDbValueCatalog.optionsFor(GameDbSettingCategory.CLAMP_MODE, fix.key)
            assertNotNull("${fix.key} must offer a picker", options)
            assertTrue(
                "${fix.key} default '${fix.defaultValue}' is not one of its options",
                options.orEmpty().any { it.value.toString() == fix.defaultValue }
            )
        }
    }

    @Test
    fun sectionsWithoutAddSupportStayClosed() {
        assertFalse(GameDbFixCatalog.supportsAdding(GameDbFixSection.SPEED_HACKS))
        assertFalse(GameDbFixCatalog.supportsAdding(GameDbFixSection.ROUND_MODES))
        assertTrue(GameDbFixCatalog.addable(GameDbFixSection.SPEED_HACKS).isEmpty())
    }
}

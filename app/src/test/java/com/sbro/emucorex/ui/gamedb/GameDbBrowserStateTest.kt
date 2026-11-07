package com.sbro.emucorex.ui.gamedb

import com.sbro.emucorex.data.gamedb.GameDbCatalogEntry
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.GameDbSettingItem
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GameDbBrowserStateTest {
    @Test
    fun anEmptyClampSectionStaysVisibleSoTheFirstLineCanBeAdded() {
        val state = entryUi(setting(GameDbSettingCategory.CORE_FIX, "EETimingHack", "Enabled"))

        assertEquals(
            listOf(GameDbSettingCategory.CORE_FIX, GameDbSettingCategory.CLAMP_MODE),
            state.sections.map { it.category }
        )
        assertTrue(state.sections.last().settings.isEmpty())
    }

    @Test
    fun emptySectionsWithoutTheAddButtonStayHidden() {
        val state = entryUi(setting(GameDbSettingCategory.GRAPHICS_FIX, "autoFlush", "1"))

        assertEquals(
            listOf(GameDbSettingCategory.CLAMP_MODE, GameDbSettingCategory.GRAPHICS_FIX),
            state.sections.map { it.category }
        )
        assertEquals(1, state.sections.last().settings.size)
    }

    @Test
    fun anExistingClampSectionIsShownOnceInGameDbOrder() {
        val state = entryUi(
            setting(GameDbSettingCategory.CLAMP_MODE, "vu0ClampMode", "2"),
            setting(GameDbSettingCategory.CORE_FIX, "EETimingHack", "Enabled")
        )

        assertEquals(
            listOf(GameDbSettingCategory.CORE_FIX, GameDbSettingCategory.CLAMP_MODE),
            state.sections.map { it.category }
        )
        assertEquals("vu0ClampMode", state.sections.last().settings.single().id.substringAfter('|'))
    }

    private fun entryUi(vararg settings: GameDbSettingUi) = GameDbEntryUi(
        entry = GameDbCatalogEntry(
            serial = SERIAL,
            title = "Test Game",
            region = "NTSC-U",
            coverUrl = null,
            settings = emptyList()
        ),
        settings = settings.toList(),
        enabledCount = settings.count { it.enabled },
        customizedCount = 0,
        disabledCount = settings.count { !it.enabled },
        searchableText = ""
    )

    private fun setting(category: GameDbSettingCategory, key: String, value: String): GameDbSettingUi {
        val item = GameDbSettingItem(
            category = category,
            key = key,
            value = value,
            note = null,
            controlledByGameFixesToggle = category != GameDbSettingCategory.GRAPHICS_FIX
        )
        return GameDbSettingUi(
            id = item.id,
            category = category,
            enabled = true,
            added = false,
            value = value,
            valueEdited = false,
            presentation = GameDbSettingPresentationMapper.map(item),
            valueOptions = GameDbValueCatalog.optionsFor(category, key)
        )
    }

    private companion object {
        private const val SERIAL = "SLUS-20000"
    }
}

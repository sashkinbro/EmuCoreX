package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class UiSoundPresetsTest {
    @Test
    fun everyKindStartsWithDefaultAndHasUniqueIds() {
        UiSoundKind.entries.forEach { kind ->
            val presets = UiSoundPresets.forKind(kind)
            assertEquals(UiSoundPresets.DEFAULT_ID, presets.first().id)
            assertEquals(presets.size, presets.map { it.id }.toSet().size)
        }
    }

    @Test
    fun menuPresetsProvideOpenCloseSelect() {
        UiSoundPresets.forKind(UiSoundKind.GAME_MENU).forEach { assertEquals(3, it.raws.size) }
        UiSoundPresets.forKind(UiSoundKind.BUTTON_CLICK).forEach { assertEquals(1, it.raws.size) }
        UiSoundPresets.forKind(UiSoundKind.BACKGROUND_MUSIC).forEach { assertEquals(1, it.raws.size) }
    }

    @Test
    fun unknownOrMissingIdsFallBackToDefault() {
        UiSoundKind.entries.forEach { kind ->
            assertEquals(UiSoundPresets.DEFAULT_ID, UiSoundPresets.sanitizeId(kind, null))
            assertEquals(UiSoundPresets.DEFAULT_ID, UiSoundPresets.sanitizeId(kind, ""))
            assertEquals(UiSoundPresets.DEFAULT_ID, UiSoundPresets.sanitizeId(kind, "does-not-exist"))
        }
        assertEquals("console", UiSoundPresets.sanitizeId(UiSoundKind.BACKGROUND_MUSIC, "console"))
        assertTrue(UiSoundPresets.forKind(UiSoundKind.BACKGROUND_MUSIC).size >= 4)
    }
}

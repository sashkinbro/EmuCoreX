package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class KeyboardMousePerGamePersistenceTest {
    @Test
    fun keyboardMouseOverrideAddsAndRemovesItsSelectiveKey() {
        val profile = PerGameSettings(
            gameKey = "game.iso",
            gameTitle = "PS2 game",
            renderer = 14,
            providedKeys = setOf("renderer")
        )
        val settings = KeyboardMouseSettings(
            keyboardEnabled = true,
            mouseEnabled = true,
            keyboardBindingsByPad = mapOf(0 to mapOf("cross" to 39)),
            mouseBindingsByPad = mapOf(1 to mapOf("circle" to 1))
        )

        val added = profile.withKeyboardMouseSettings(settings)
        val cleared = added.withKeyboardMouseSettings(null)

        assertEquals(settings, added.keyboardMouse)
        assertEquals(setOf("renderer", "keyboardMouse"), added.providedKeys)
        assertNull(cleared.keyboardMouse)
        assertEquals(setOf("renderer"), cleared.providedKeys)
        assertEquals(14, cleared.renderer)
    }

    @Test
    fun oldFullProfilesKeepFullProfileSemanticsAfterKeyboardMouseChange() {
        val oldProfile = PerGameSettings(
            gameKey = "old.iso",
            gameTitle = "Old game",
            providedKeys = null
        )

        assertNull(oldProfile.withKeyboardMouseSettings(KeyboardMouseSettings(keyboardEnabled = true)).providedKeys)
        assertNull(oldProfile.withKeyboardMouseSettings(null).providedKeys)
    }
}

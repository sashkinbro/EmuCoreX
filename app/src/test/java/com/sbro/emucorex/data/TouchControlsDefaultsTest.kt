package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class TouchControlsDefaultsTest {
    @Test
    fun `touchscreen right stick is disabled by default`() {
        val global = SettingsSnapshot()
        val perGame = PerGameSettings(gameKey = "game", gameTitle = "Game")

        assertFalse(global.touchscreenRightStick)
        assertFalse(perGame.touchscreenRightStick)
        assertEquals(100, global.touchscreenRightStickSensitivity)
        assertEquals(100, perGame.touchscreenRightStickSensitivity)
    }

    @Test
    fun `overlay opacity keeps existing default and supports zero`() {
        assertEquals(80, AppPreferences.DEFAULT_OVERLAY_OPACITY)
        assertEquals(0, AppPreferences.OVERLAY_OPACITY_MIN)
        assertEquals(100, AppPreferences.OVERLAY_OPACITY_MAX)
    }
}

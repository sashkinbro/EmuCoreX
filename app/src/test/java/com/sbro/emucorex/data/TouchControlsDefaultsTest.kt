package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchControlsDefaultsTest {
    @Test
    fun `touchscreen gesture sticks are disabled by default`() {
        val global = SettingsSnapshot()
        val perGame = PerGameSettings(gameKey = "game", gameTitle = "Game")

        assertFalse(global.touchscreenRightStick)
        assertFalse(perGame.touchscreenRightStick)
        assertFalse(global.touchscreenLeftStick)
        assertFalse(perGame.touchscreenLeftStick)
        assertEquals(100, global.touchscreenRightStickSensitivity)
        assertEquals(100, perGame.touchscreenRightStickSensitivity)
        assertEquals(100, global.touchscreenLeftStickSensitivity)
        assertEquals(100, perGame.touchscreenLeftStickSensitivity)
    }

    @Test
    fun `right stick and stick click buttons are visible in the default layout`() {
        val controls = AppPreferences.defaultOverlayControlLayouts()

        assertTrue(requireNotNull(controls["right_stick"]).visible)
        assertTrue(requireNotNull(controls["l3"]).visible)
        assertTrue(requireNotNull(controls["r3"]).visible)
    }

    @Test
    fun `overlay opacity keeps existing default and supports zero`() {
        assertEquals(80, AppPreferences.DEFAULT_OVERLAY_OPACITY)
        assertEquals(0, AppPreferences.OVERLAY_OPACITY_MIN)
        assertEquals(100, AppPreferences.OVERLAY_OPACITY_MAX)
    }
}

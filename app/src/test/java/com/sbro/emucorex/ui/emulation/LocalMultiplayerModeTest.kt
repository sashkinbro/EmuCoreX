package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.SettingsSnapshot
import org.junit.Assert.assertEquals
import org.junit.Test

class LocalMultiplayerModeTest {
    @Test
    fun `local multiplayer is opt in globally and per game`() {
        assertEquals(AppPreferences.LOCAL_MULTIPLAYER_OFF, SettingsSnapshot().localMultiplayerMode)
        assertEquals(
            AppPreferences.LOCAL_MULTIPLAYER_OFF,
            PerGameSettings(gameKey = "game", gameTitle = "Game").localMultiplayerMode
        )
    }

    @Test
    fun `swapped crop routes touch zones to the matching players`() {
        assertEquals(0 to 1, localMultiplayerPadOrder(AppPreferences.LOCAL_MULTIPLAYER_SIDE_BY_SIDE))
        assertEquals(0 to 1, localMultiplayerPadOrder(AppPreferences.LOCAL_MULTIPLAYER_STACKED))
        assertEquals(0 to 1, localMultiplayerPadOrder(AppPreferences.LOCAL_MULTIPLAYER_HORIZONTAL_CROP))
        assertEquals(1 to 0, localMultiplayerPadOrder(AppPreferences.LOCAL_MULTIPLAYER_HORIZONTAL_CROP_SWAPPED))
    }
}

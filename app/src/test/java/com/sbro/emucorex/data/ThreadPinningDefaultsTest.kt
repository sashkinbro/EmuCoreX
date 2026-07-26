package com.sbro.emucorex.data

import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.settings.SettingsUiState
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ThreadPinningDefaultsTest {
    @Test
    fun threadPinningIsDisabledAcrossFreshGlobalAndGameStates() {
        assertFalse(AppPreferences.DEFAULT_THREAD_PINNING)
        assertFalse(SettingsSnapshot().enableThreadPinning)
        assertFalse(SettingsUiState().enableThreadPinning)
        assertFalse(EmulationUiState().enableThreadPinning)
        assertFalse(PerGameSettings(gameKey = "game", gameTitle = "Game").enableThreadPinning)
    }

    @Test
    fun explicitUserOptInRemainsSupported() {
        assertTrue(
            PerGameSettings(
                gameKey = "game",
                gameTitle = "Game",
                enableThreadPinning = true
            ).enableThreadPinning
        )
    }
}

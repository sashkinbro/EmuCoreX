package com.sbro.emucorex.data

import com.sbro.emucorex.ui.settings.SettingsUiState
import org.junit.Assert.assertTrue
import org.junit.Test

class FastmemDefaultsTest {
    @Test
    fun fastmemIsEnabledAcrossFreshGlobalAndSettingsStates() {
        assertTrue(SettingsSnapshot().enableFastmem)
        assertTrue(SettingsUiState().enableFastmem)
    }
}

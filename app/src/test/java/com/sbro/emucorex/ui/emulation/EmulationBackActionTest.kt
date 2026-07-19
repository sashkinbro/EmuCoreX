package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.SettingsSnapshot
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class EmulationBackActionTest {
    @Test
    fun defaultSettingKeepsOpeningTheGameMenu() {
        val defaults = SettingsSnapshot()

        assertFalse(defaults.backButtonExitsGame)
        assertEquals(
            EmulationBackAction.OpenGameMenu,
            resolveEmulationBackAction(defaults.backButtonExitsGame)
        )
    }

    @Test
    fun enabledSettingUsesTheSharedExitRequest() {
        assertEquals(
            EmulationBackAction.RequestExit,
            resolveEmulationBackAction(backButtonExitsGame = true)
        )
    }
}

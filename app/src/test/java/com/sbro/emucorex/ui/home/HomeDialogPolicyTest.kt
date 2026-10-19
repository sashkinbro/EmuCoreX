package com.sbro.emucorex.ui.home

import org.junit.Assert.assertEquals
import org.junit.Test

class HomeDialogPolicyTest {
    @Test
    fun `keeps dialogs in priority order`() {
        assertEquals(
            HomeActiveDialog.WELCOME,
            HomeDialogPolicy.activeDialog(
                canPresent = true,
                welcomePending = true,
                hardwareWarningPending = true
            )
        )
        assertEquals(
            HomeActiveDialog.HARDWARE_WARNING,
            HomeDialogPolicy.activeDialog(
                canPresent = true,
                welcomePending = false,
                hardwareWarningPending = true
            )
        )
    }

    @Test
    fun `presents nothing while the screen is busy`() {
        assertEquals(
            HomeActiveDialog.NONE,
            HomeDialogPolicy.activeDialog(
                canPresent = false,
                welcomePending = true,
                hardwareWarningPending = true
            )
        )
    }

    @Test
    fun `presents nothing when no dialog is pending`() {
        assertEquals(
            HomeActiveDialog.NONE,
            HomeDialogPolicy.activeDialog(
                canPresent = true,
                welcomePending = false,
                hardwareWarningPending = false
            )
        )
    }
}

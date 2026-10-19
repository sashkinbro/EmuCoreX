package com.sbro.emucorex.ui.home

enum class HomeActiveDialog {
    NONE,
    WELCOME,
    HARDWARE_WARNING
}

object HomeDialogPolicy {
    fun activeDialog(
        canPresent: Boolean,
        welcomePending: Boolean,
        hardwareWarningPending: Boolean
    ): HomeActiveDialog = when {
        !canPresent -> HomeActiveDialog.NONE
        welcomePending -> HomeActiveDialog.WELCOME
        hardwareWarningPending -> HomeActiveDialog.HARDWARE_WARNING
        else -> HomeActiveDialog.NONE
    }
}

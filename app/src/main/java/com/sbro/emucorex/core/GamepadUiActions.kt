package com.sbro.emucorex.core

object GamepadUiActions {
    @Volatile
    private var toggleDrawerAction: (() -> Unit)? = null

    fun setToggleDrawerAction(action: (() -> Unit)?) {
        toggleDrawerAction = action
    }

    fun toggleDrawer(): Boolean {
        val action = toggleDrawerAction ?: return false
        action()
        return true
    }
}

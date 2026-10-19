package com.sbro.emucorex.ui.common

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager

@StringRes
fun gamepadActionLabelRes(actionId: String): Int = when (actionId) {
    "cross" -> R.string.settings_gamepad_action_cross
    "circle" -> R.string.settings_gamepad_action_circle
    "square" -> R.string.settings_gamepad_action_square
    "triangle" -> R.string.settings_gamepad_action_triangle
    "l1" -> R.string.settings_gamepad_action_l1
    "r1" -> R.string.settings_gamepad_action_r1
    "l2" -> R.string.settings_gamepad_action_l2
    "r2" -> R.string.settings_gamepad_action_r2
    "l3" -> R.string.settings_gamepad_action_l3
    "r3" -> R.string.settings_gamepad_action_r3
    "select" -> R.string.settings_gamepad_action_select
    "start" -> R.string.settings_gamepad_action_start
    "pressure" -> R.string.settings_gamepad_action_pressure
    "gun_trigger" -> R.string.settings_gamepad_action_gun_trigger
    "gun_pedal" -> R.string.settings_gamepad_action_gun_pedal
    "gun_reload" -> R.string.settings_gamepad_action_gun_reload
    "gun_recalibrate" -> R.string.settings_gamepad_action_gun_recalibrate
    "coin" -> R.string.settings_gamepad_action_coin
    "service" -> R.string.settings_gamepad_action_service
    GamepadManager.ACTION_QUICK_SAVE -> R.string.emulation_quick_save
    GamepadManager.ACTION_QUICK_LOAD -> R.string.emulation_quick_load
    "dpad_up" -> R.string.settings_gamepad_action_dpad_up
    "dpad_down" -> R.string.settings_gamepad_action_dpad_down
    "dpad_left" -> R.string.settings_gamepad_action_dpad_left
    "dpad_right" -> R.string.settings_gamepad_action_dpad_right
    "left_stick_up" -> R.string.settings_gamepad_action_left_stick_up
    "left_stick_down" -> R.string.settings_gamepad_action_left_stick_down
    "left_stick_left" -> R.string.settings_gamepad_action_left_stick_left
    "left_stick_right" -> R.string.settings_gamepad_action_left_stick_right
    "right_stick_up" -> R.string.settings_gamepad_action_right_stick_up
    "right_stick_down" -> R.string.settings_gamepad_action_right_stick_down
    "right_stick_left" -> R.string.settings_gamepad_action_right_stick_left
    "right_stick_right" -> R.string.settings_gamepad_action_right_stick_right
    else -> R.string.settings_gamepad_section
}

@Composable
fun gamepadActionLabel(actionId: String): String = when (actionId) {
    "cross" -> "\u2715"
    "circle" -> "\u25cb"
    "square" -> "\u25a1"
    "triangle" -> "\u25b3"
    else -> stringResource(gamepadActionLabelRes(actionId))
}

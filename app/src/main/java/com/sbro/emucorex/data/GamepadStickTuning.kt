package com.sbro.emucorex.data

/**
 * Gamepad stick tuning grouped into one value so SettingsUiState stays below the DEX 255-register
 * limit for constructor/copy$default calls (see SettingsStateArityTest).
 */
data class GamepadStickTuning(
    val stickDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_DEADZONE,
    val leftStickSensitivity: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY,
    val rightStickSensitivity: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY,
    val leftStickNegativeDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_NEGATIVE_DEADZONE,
    val rightStickNegativeDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_NEGATIVE_DEADZONE,
    val leftStickAntiDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_ANTI_DEADZONE,
    val rightStickAntiDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_ANTI_DEADZONE,
    val leftStickCurve: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_CURVE,
    val rightStickCurve: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_CURVE
)

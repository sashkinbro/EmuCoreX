package com.sbro.emucorex.data

/**
 * Gyroscope and light-gun preferences grouped into one nested value so the settings
 * snapshot and UI state stay well below the JVM 255-argument limit of data-class copy().
 *
 * Stored under the same flat DataStore keys as before; this grouping is internal only.
 */
data class GyroSettings(
    val mode: Int = AppPreferences.GYRO_MODE_OFF,
    val sensitivity: Int = AppPreferences.DEFAULT_GYRO_SENSITIVITY,
    val smoothing: Int = AppPreferences.DEFAULT_GYRO_SMOOTHING,
    val invertX: Boolean = false,
    val invertY: Boolean = false,
    val stickTarget: Int = AppPreferences.DEFAULT_GYRO_STICK_TARGET,
    val lightGunAim: Int = AppPreferences.DEFAULT_LIGHT_GUN_AIM,
    val lightGunCursorEnabled: Boolean = true
)

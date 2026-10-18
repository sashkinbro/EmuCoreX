package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.PerGameInterfaceSettings
import com.sbro.emucorex.data.toInterfaceSettings
import kotlinx.coroutines.flow.first

internal fun EmulationUiState.toInterfaceSettings(): PerGameInterfaceSettings = PerGameInterfaceSettings(
    overlayShow = controlsVisible,
    overlayScale = overlayScale,
    overlayOpacity = overlayOpacity,
    hideOverlayOnGamepad = hideOverlayOnGamepad,
    compactControls = compactControls,
    keepScreenOn = keepScreenOn,
    leftStickSensitivity = leftStickSensitivity,
    rightStickSensitivity = rightStickSensitivity,
    invertLeftStick = invertLeftStick,
    invertRightStick = invertRightStick,
    invertLeftStickHorizontal = invertLeftStickHorizontal,
    invertRightStickHorizontal = invertRightStickHorizontal,
    fpsOverlayCorner = fpsOverlayCorner,
    fpsOverlayScale = fpsOverlayScale,
    fpsOverlayMetrics = fpsOverlayMetrics,
    autoSaveEnabled = autoSaveEnabled,
    autoSaveIntervalMinutes = autoSaveIntervalMinutes,
)

internal fun EmulationUiState.withInterfaceSettings(settings: PerGameInterfaceSettings): EmulationUiState = copy(
    controlsVisible = settings.overlayShow,
    overlayScale = settings.overlayScale,
    overlayOpacity = settings.overlayOpacity,
    hideOverlayOnGamepad = settings.hideOverlayOnGamepad,
    compactControls = settings.compactControls,
    keepScreenOn = settings.keepScreenOn,
    leftStickSensitivity = settings.leftStickSensitivity,
    rightStickSensitivity = settings.rightStickSensitivity,
    invertLeftStick = settings.invertLeftStick,
    invertRightStick = settings.invertRightStick,
    invertLeftStickHorizontal = settings.invertLeftStickHorizontal,
    invertRightStickHorizontal = settings.invertRightStickHorizontal,
    fpsOverlayCorner = settings.fpsOverlayCorner,
    fpsOverlayScale = settings.fpsOverlayScale,
    fpsOverlayMetrics = settings.fpsOverlayMetrics,
    autoSaveEnabled = settings.autoSaveEnabled,
    autoSaveIntervalMinutes = settings.autoSaveIntervalMinutes,
)

internal suspend fun AppPreferences.readInterfaceSettings(): PerGameInterfaceSettings {
    val settings = settingsSnapshot.first()
    return settings.toInterfaceSettings(autoSaveEnabled.first(), autoSaveIntervalMinutes.first()).sanitized()
}

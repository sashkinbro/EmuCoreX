package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE

data class EmulationPerformanceState(
    val fps: String = "0.0",
    val speedPercent: Float = 100f,
    val overlayText: String = ""
)

internal fun parseEmulationPerformanceSnapshot(
    raw: String,
    overlayMode: Int,
    cpuName: String
): EmulationPerformanceState? {
    val parts = raw.split('\n', limit = 3)
    if (parts.size < 3) return null
    val fps = parts[0].toFloatOrNull() ?: return null
    val speedPercent = parts[1].toFloatOrNull() ?: return null
    val overlayText = replacePerformanceCpuName(parts[2], cpuName)
    val displayedFps = if (overlayMode == FPS_OVERLAY_MODE_SIMPLE) {
        effectivePerformanceFps(fps, overlayText)
    } else {
        fps
    }
    return EmulationPerformanceState(
        fps = "%.1f".format(displayedFps),
        speedPercent = speedPercent,
        overlayText = overlayText
    )
}

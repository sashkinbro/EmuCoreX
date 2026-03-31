package com.sbro.emucorex.core

import java.util.Locale
import kotlin.math.roundToInt

const val UPSCALE_MIN = 0.5f
const val UPSCALE_MAX = 4.0f
const val UPSCALE_STEP = 0.25f
const val UPSCALE_SLIDER_STEPS = 13

fun normalizeUpscale(value: Float): Float {
    val snapped = (value / UPSCALE_STEP).roundToInt() * UPSCALE_STEP
    return snapped.coerceIn(UPSCALE_MIN, UPSCALE_MAX)
}

fun formatUpscaleLabel(value: Float): String {
    val normalized = normalizeUpscale(value)
    val whole = normalized.roundToInt().toFloat()
    return if (normalized == whole) {
        "${whole.toInt()}x"
    } else {
        String.format(Locale.US, "%.2fx", normalized)
    }
}

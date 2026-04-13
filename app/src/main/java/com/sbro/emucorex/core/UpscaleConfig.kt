package com.sbro.emucorex.core

import kotlin.math.roundToInt

const val UPSCALE_MIN = 1.0f
const val UPSCALE_MAX = 6.0f

private const val UPSCALE_NATIVE_MULTIPLIER = 1
private const val UPSCALE_MAX_MULTIPLIER = 6

val UPSCALE_MULTIPLIER_OPTIONS: List<Int> = (UPSCALE_NATIVE_MULTIPLIER..UPSCALE_MAX_MULTIPLIER).toList()

fun normalizeUpscale(value: Float): Float {
    return value.roundToInt().coerceIn(UPSCALE_NATIVE_MULTIPLIER, UPSCALE_MAX_MULTIPLIER).toFloat()
}

fun upscaleMultiplierValue(value: Float): Int = normalizeUpscale(value).roundToInt()

fun formatUpscaleLabel(value: Float, nativeLabel: String): String {
    val normalized = upscaleMultiplierValue(value)
    return if (normalized == UPSCALE_NATIVE_MULTIPLIER) nativeLabel else "${normalized}x"
}

fun buildUpscaleOptions(nativeLabel: String): List<Pair<Int, String>> {
    return UPSCALE_MULTIPLIER_OPTIONS.map { multiplier ->
        multiplier to if (multiplier == UPSCALE_NATIVE_MULTIPLIER) nativeLabel else "${multiplier}x"
    }
}

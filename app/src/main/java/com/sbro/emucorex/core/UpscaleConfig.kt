package com.sbro.emucorex.core

import kotlin.math.roundToInt

// Sub-native steps (0.25x/0.5x/0.75x) let weak devices trade image sharpness for speed;
// the GS core has always accepted fractional upscale multipliers and only clamps the top.
const val UPSCALE_MIN = 0.25f
const val UPSCALE_MAX = 10.0f

private const val UPSCALE_STEP = 0.25f
private const val UPSCALE_NATIVE_MULTIPLIER = 1.0f
private const val UPSCALE_MAX_MULTIPLIER = UPSCALE_MAX

fun normalizeUpscale(value: Float, maxMultiplier: Int = UPSCALE_MAX_MULTIPLIER.roundToInt()): Float {
    val max = maxMultiplier.coerceAtLeast(UPSCALE_NATIVE_MULTIPLIER.roundToInt())
        .coerceAtMost(UPSCALE_MAX_MULTIPLIER.roundToInt())
        .toFloat()
    val stepped = (value / UPSCALE_STEP).roundToInt() * UPSCALE_STEP
    return stepped.coerceIn(UPSCALE_MIN, max)
}

fun upscaleMultiplierValue(value: Float): Int = upscaleMultiplierKey(normalizeUpscale(value))

fun upscaleMultiplierKey(value: Float): Int = (normalizeUpscale(value) * 100f).roundToInt()

fun upscaleKeyToMultiplier(value: Int): Float = normalizeUpscale(value.toFloat() / 100f)

fun formatUpscaleLabel(value: Float, nativeLabel: String): String {
    val normalized = normalizeUpscale(value)
    return when {
        normalized == UPSCALE_NATIVE_MULTIPLIER -> nativeLabel
        normalized == normalized.roundToInt().toFloat() -> "${normalized.roundToInt()}x"
        else -> {
            val formatted = "%.2f".format(java.util.Locale.US, normalized)
                .trimEnd('0')
                .trimEnd('.')
            "${formatted}x"
        }
    }
}

fun buildUpscaleOptions(nativeLabel: String, maxMultiplier: Int = UPSCALE_MAX_MULTIPLIER.roundToInt()): List<Pair<Int, String>> {
    val max = maxMultiplier.coerceAtLeast(UPSCALE_NATIVE_MULTIPLIER.roundToInt())
        .coerceAtMost(UPSCALE_MAX_MULTIPLIER.roundToInt())
        .toFloat()
    val steps = ((max - UPSCALE_MIN) / UPSCALE_STEP).roundToInt()
    return (0..steps).map { index ->
        val multiplier = UPSCALE_MIN + (index * UPSCALE_STEP)
        upscaleMultiplierKey(multiplier) to formatUpscaleLabel(multiplier, nativeLabel)
    }
}

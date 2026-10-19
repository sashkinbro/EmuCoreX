package com.sbro.emucorex.ui.emulation

import kotlin.math.sqrt

internal data class RightStickGestureValue(
    val x: Float,
    val y: Float
)

internal enum class TouchscreenStickGestureTarget { NONE, LEFT, RIGHT }

/**
 * Picks the virtual stick controlled by a drag that started at [startX].
 *
 * When both gesture sticks are enabled, the free screen space is split in half so the
 * left half drives the left stick and the right half drives the right stick. A single
 * enabled stick keeps using the whole free area.
 */
internal fun resolveTouchscreenStickGestureTarget(
    leftEnabled: Boolean,
    rightEnabled: Boolean,
    startX: Float,
    widthPx: Float
): TouchscreenStickGestureTarget {
    if (!leftEnabled && !rightEnabled) return TouchscreenStickGestureTarget.NONE
    if (leftEnabled && rightEnabled) {
        return if (startX < widthPx / 2f) {
            TouchscreenStickGestureTarget.LEFT
        } else {
            TouchscreenStickGestureTarget.RIGHT
        }
    }
    return if (leftEnabled) TouchscreenStickGestureTarget.LEFT else TouchscreenStickGestureTarget.RIGHT
}

internal fun calculateRightStickGestureValue(
    deltaX: Float,
    deltaY: Float,
    radiusPx: Float
): RightStickGestureValue {
    if (!deltaX.isFinite() || !deltaY.isFinite() || !radiusPx.isFinite() || radiusPx <= 0f) {
        return RightStickGestureValue(0f, 0f)
    }

    var x = deltaX / radiusPx
    var y = deltaY / radiusPx
    val magnitude = sqrt((x * x) + (y * y))
    if (magnitude > 1f) {
        x /= magnitude
        y /= magnitude
    }
    return RightStickGestureValue(x = x, y = y)
}

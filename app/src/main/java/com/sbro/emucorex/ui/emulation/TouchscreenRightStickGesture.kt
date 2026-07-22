package com.sbro.emucorex.ui.emulation

import kotlin.math.sqrt

internal data class RightStickGestureValue(
    val x: Float,
    val y: Float
)

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

package com.sbro.emucorex.ui.common

import androidx.compose.ui.unit.Dp

/**
 * Shrinks the user's requested overlay scale only when the safe content area cannot contain
 * the default controller groups. All values are linear in [requestedScale], so the result is
 * density-independent and stable across resolutions with the same physical Compose bounds.
 */
internal fun calculateOverlayResponsiveScale(
    canvasWidth: Dp,
    canvasHeight: Dp,
    safeLeftInset: Dp,
    safeRightInset: Dp,
    safeTopInset: Dp,
    safeBottomInset: Dp,
    requestedScale: Float,
    stickScale: Float,
    leftStickVisible: Boolean,
    leftStickFootprintScale: Float,
    rightStickVisible: Boolean,
    rightStickFootprintScale: Float,
    isLandscape: Boolean
): Float {
    val requested = requestedScale.coerceAtLeast(0.01f)
    val availableWidth = (
        canvasWidth.value - safeLeftInset.value.coerceAtLeast(0f) -
            safeRightInset.value.coerceAtLeast(0f)
        ).coerceAtLeast(1f)
    val availableHeight = (
        canvasHeight.value - safeTopInset.value.coerceAtLeast(0f) -
            safeBottomInset.value.coerceAtLeast(0f)
        ).coerceAtLeast(1f)

    val dpadBase = if (isLandscape) 130f else 150f
    val actionBase = if (isLandscape) 130f else 150f
    val analogBase = (if (isLandscape) 120f else 140f) * stickScale.coerceAtLeast(0.1f)
    val leftAnalogWidth = if (leftStickVisible) analogBase * leftStickFootprintScale else dpadBase
    val rightAnalogWidth = analogBase * rightStickFootprintScale
    val shoulderHeight = if (isLandscape) 32f else 36f
    val centerHeight = if (isLandscape) 26f else 30f
    val verticalPadding = if (isLandscape) 22f else 16f
    val clusterGap = if (isLandscape) 32f else 34f
    val actionGap = if (isLandscape) 48f else 52f
    val dpadExtent = dpadBase * (2f / 3f) + clusterGap
    val actionExtent = actionBase * (2f / 3.1f) + actionGap
    val primaryExtent = maxOf(dpadExtent, actionExtent, analogBase)

    val horizontalGap = if (isLandscape) 18f else 12f
    val widthNeeded = if (isLandscape && rightStickVisible) {
        maxOf(dpadExtent, leftAnalogWidth) + rightAnalogWidth + actionExtent + horizontalGap * 2f + 32f
    } else {
        maxOf(dpadExtent, leftAnalogWidth) + actionExtent + horizontalGap + 20f
    }
    val stackedRightStickHeight = if (!isLandscape && rightStickVisible) {
        analogBase + 18f
    } else {
        0f
    }
    val heightNeeded = verticalPadding * 2f + shoulderHeight * 2f + 8f + 12f +
        stackedRightStickHeight + primaryExtent + 12f + centerHeight

    val widthFit = availableWidth / (widthNeeded * requested)
    val heightFit = availableHeight / (heightNeeded * requested)
    return requested * minOf(1f, widthFit, heightFit).coerceAtLeast(0.01f)
}

package com.sbro.emucorex.ui.common

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

val OverlayShoulderTopPadding = 40.dp
val OverlaySideAnchorPadding = 28.dp
val OverlayBottomAnchorPadding = 24.dp
val OverlayCenterBottomPadding = 18.dp
val OverlayRightShoulderBaseOffset = 0.dp
val OverlayRightShoulderGapOffset = 40.dp
val OverlayLeftStickBaseOffset = 32.dp
val OverlayRightStickBaseOffset = (-24).dp
val OverlayClusterGapLandscape = 32.dp
val OverlayClusterGapPortrait = 34.dp
val OverlayActionGapLandscape = 104.dp
val OverlayActionGapPortrait = 112.dp
val OverlayCenterBaseShiftX = 24.dp
val OverlayCenterColumnGapLandscape = 34.dp
val OverlayCenterColumnGapPortrait = 36.dp
val OverlayCenterRowGapLandscape = 8.dp
val OverlayCenterRowGapPortrait = 10.dp
val OverlayShoulderVerticalGap = 40.dp

fun overlayCenterButtonOffset(
    buttonWidth: Dp,
    columnWidth: Dp,
    gap: Dp,
    leftColumn: Boolean
): Dp {
    val columnCenter = if (leftColumn) {
        0.dp - (columnWidth / 2f + gap / 2f)
    } else {
        columnWidth / 2f + gap / 2f
    }
    return columnCenter - buttonWidth / 2f
}

fun overlayCenterSecondRowOffset(
    buttonHeight: Dp,
    rowGap: Dp
): Dp = buttonHeight + rowGap

fun overlayClusterStep(buttonSize: Dp, gap: Dp): Dp = buttonSize + gap

fun overlayDpadOffset(step: Dp, direction: String): Pair<Dp, Dp> = when (direction) {
    "up" -> (step / 2f) to (0.dp - step)
    "down" -> (step / 2f) to 0.dp
    "left" -> 0.dp to (0.dp - step / 2f)
    "right" -> step to (0.dp - step / 2f)
    else -> 0.dp to 0.dp
}

fun overlayActionOffset(step: Dp, button: String): Pair<Dp, Dp> = when (button) {
    "triangle" -> (0.dp - step / 2f) to (0.dp - step)
    "cross" -> (0.dp - step / 2f) to 0.dp
    "square" -> (0.dp - step) to (0.dp - step / 2f)
    "circle" -> 0.dp to (0.dp - step / 2f)
    else -> 0.dp to 0.dp
}

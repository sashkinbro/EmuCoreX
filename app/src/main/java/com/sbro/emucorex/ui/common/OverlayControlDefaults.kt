package com.sbro.emucorex.ui.common

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

val OverlayShoulderTopPadding = 18.dp
val OverlaySideAnchorPadding = 28.dp
val OverlayBottomAnchorPadding = 24.dp
val OverlayCenterBottomPadding = 82.dp
val OverlayRightShoulderBaseOffset = (-112).dp
val OverlayRightShoulderGapOffset = (-39).dp
val OverlayLeftStickBaseOffset = 136.dp
val OverlayRightStickBaseOffset = (-136).dp
val OverlayClusterGapLandscape = 38.dp
val OverlayClusterGapPortrait = 40.dp
val OverlayCenterBaseShiftX = 34.dp
val OverlayCenterColumnGapLandscape = 16.dp
val OverlayCenterColumnGapPortrait = 18.dp
val OverlayCenterRowGapLandscape = 12.dp
val OverlayCenterRowGapPortrait = 14.dp

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

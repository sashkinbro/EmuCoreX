package com.sbro.emucorex.ui.common

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

val OverlayShoulderTopPadding = 40.dp
val OverlayBottomAnchorPadding = 24.dp
val OverlayCenterBottomPadding = 18.dp
val OverlayRightShoulderGapOffset = 40.dp
val OverlayRightStickBaseLift = 24.dp
val OverlayClusterGapLandscape = 32.dp
val OverlayClusterGapPortrait = 34.dp
val OverlayActionGapLandscape = 48.dp
val OverlayActionGapPortrait = 52.dp
val OverlayCenterBaseShiftX = 0.dp
val OverlayCenterInlineGapLandscape = 10.dp
val OverlayCenterInlineGapPortrait = 12.dp
val OverlayCenterSelectOpticalNudgeX = (-2).dp
val OverlayCenterToggleOpticalNudgeY = 0.dp
val OverlayCenterStartOpticalNudgeX = 8.dp
val OverlayShoulderVerticalGap = 40.dp

fun overlayInlineGroupOffset(
    widths: List<Dp>,
    gap: Dp,
    index: Int
): Dp {
    val totalWidth = widths.fold(0.dp) { acc, width -> acc + width } +
        gap * (widths.size - 1).coerceAtLeast(0)
    val precedingWidth = widths.take(index).fold(0.dp) { acc, width -> acc + width } +
        gap * index.coerceAtLeast(0)
    return -(totalWidth / 2f) + precedingWidth
}

fun overlayClusterStep(buttonSize: Dp, gap: Dp): Dp = buttonSize + gap

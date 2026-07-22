package com.sbro.emucorex.ui.home

internal data class HomeWindowMetrics(
    val widthDp: Int,
    val heightDp: Int
)

internal fun resolveHomeWindowMetrics(
    configurationWidthDp: Int,
    configurationHeightDp: Int,
    measuredWidthDp: Int,
    measuredHeightDp: Int
): HomeWindowMetrics {
    val hasValidMeasuredSize = measuredWidthDp > 0 && measuredHeightDp > 0
    return HomeWindowMetrics(
        widthDp = if (hasValidMeasuredSize) measuredWidthDp else configurationWidthDp.coerceAtLeast(1),
        heightDp = if (hasValidMeasuredSize) measuredHeightDp else configurationHeightDp.coerceAtLeast(1)
    )
}

internal fun calculateHomeGridColumnCount(
    screenWidthDp: Int,
    screenHeightDp: Int,
    smallestScreenWidthDp: Int,
    gridScale: Float,
    contentReservedWidthDp: Int? = null
): Int {
    val isLandscape = screenWidthDp > screenHeightDp
    val isWide = smallestScreenWidthDp >= 600 && screenWidthDp >= 900
    val reservedWidthDp = contentReservedWidthDp ?: if (isWide) 332 else 0
    val contentWidthDp = (screenWidthDp - reservedWidthDp).coerceAtLeast(1)
    val baseCellSizeDp = if (isLandscape) 94 else 102
    val minCellSizeDp = (baseCellSizeDp * gridScale).toInt().coerceAtLeast(1)
    return maxOf(1, (contentWidthDp + 12) / (minCellSizeDp + 12))
}

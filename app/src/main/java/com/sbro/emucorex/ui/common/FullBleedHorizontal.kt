package com.sbro.emucorex.ui.common

import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Lets one item inside a horizontally padded list (for example a LazyColumn item
 * with [horizontalPadding] in its contentPadding) span the full screen width.
 * The scrollable content then re-applies the same padding through its own
 * contentPadding, which keeps the first/last item aligned with the rest of the
 * screen while still allowing it to scroll under the edges.
 *
 * Apply this to the scroll container itself — not to a wrapper — and pass the
 * padding to the container's contentPadding instead of the parent.
 */
internal fun Modifier.fullBleedHorizontal(horizontalPadding: Dp): Modifier {
    if (horizontalPadding == 0.dp) return this
    return layout { measurable, constraints ->
        val sidePadding = horizontalPadding.roundToPx()
        val expandedConstraints = constraints.copy(
            minWidth = (constraints.minWidth + sidePadding * 2).coerceAtLeast(0),
            maxWidth = (constraints.maxWidth + sidePadding * 2).coerceAtLeast(0)
        )
        val placeable = measurable.measure(expandedConstraints)
        layout(constraints.maxWidth, placeable.height) {
            placeable.placeRelative(-sidePadding, 0)
        }
    }
}

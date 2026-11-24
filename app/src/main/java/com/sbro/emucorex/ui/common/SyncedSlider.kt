package com.sbro.emucorex.ui.common

import androidx.annotation.IntRange
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderColors
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.SliderState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.ui.theme.LocalThemeExtras
import com.sbro.emucorex.data.theme.ThemeShapeStyle
import com.sbro.emucorex.ui.theme.neon.neonShape

/**
 * Value-driven slider on top of the SliderState overload. Same behaviour as the legacy
 * Slider(value, ...) overload: the state is recreated when steps/valueRange change and always
 * follows the incoming [value].
 *
 * Rounded themes keep the stock Material track and thumb. Square/cut themes (Classic, Pixel, Terminal,
 * Neon...) get a flat track and a block thumb in the theme's corner style instead of Material's pill.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SyncedSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    @IntRange(from = 0) steps: Int = 0,
    onValueChangeFinished: (() -> Unit)? = null,
    colors: SliderColors = SliderDefaults.colors(),
    interactionSource: MutableInteractionSource = remember { MutableInteractionSource() }
) {
    val state = remember(steps, valueRange) { SliderState(value, steps, valueRange) }
    state.value = value
    if (LocalThemeExtras.current.shapeStyle == ThemeShapeStyle.ROUNDED) {
        Slider(
            state = state,
            onValueChange = onValueChange,
            modifier = modifier,
            enabled = enabled,
            onValueChangeFinished = onValueChangeFinished,
            colors = colors,
            interactionSource = interactionSource
        )
        return
    }
    val scheme = MaterialTheme.colorScheme
    val active = if (enabled) scheme.primary else scheme.onSurface.copy(alpha = 0.38f)
    val inactive = scheme.onSurface.copy(alpha = if (enabled) 0.22f else 0.12f)
    val edge = scheme.onSurface.copy(alpha = 0.45f)
    val thumbShape = neonShape(3.dp)
    Slider(
        state = state,
        onValueChange = onValueChange,
        modifier = modifier,
        enabled = enabled,
        onValueChangeFinished = onValueChangeFinished,
        colors = colors,
        interactionSource = interactionSource,
        thumb = { _ ->
            Box(
                Modifier
                    .size(width = 14.dp, height = 26.dp)
                    .clip(thumbShape)
                    .background(active, thumbShape)
                    .border(1.dp, edge, thumbShape)
            )
        },
        track = { sliderState: SliderState ->
            val fraction = sliderState.coercedValueAsFraction
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(8.dp)
                    .drawBehind {
                        drawRect(inactive)
                        drawRect(active, size = Size(size.width * fraction, size.height))
                        drawRect(edge, topLeft = Offset.Zero, size = Size(size.width, 1.dp.toPx()))
                    }
            )
        }
    )
}

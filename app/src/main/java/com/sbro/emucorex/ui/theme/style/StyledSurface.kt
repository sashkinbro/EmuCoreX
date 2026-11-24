package com.sbro.emucorex.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.contentColorFor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.NonRestartableComposable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.theme.UiStyle
import com.sbro.emucorex.ui.theme.LocalThemeExtras
import androidx.compose.material3.Surface as M3Surface

/*
 * Drop-in replacements for material3.Surface. Screens import these instead of the Material ones,
 * so the active theme's UI style (bevels, gloss, glass, pixel frames, ...) applies to every panel
 * without touching each call site. With UiStyle.STANDARD / NEON they forward unchanged.
 */

/** How a styled panel should be drawn for a given base color. */
internal class PanelLook(
    val modifier: Modifier,
    val color: Color,
    val border: BorderStroke?,
    val shadowElevation: Dp,
    val tonalElevation: Dp
)

@Composable
internal fun panelLook(
    shape: Shape,
    color: Color,
    border: BorderStroke?,
    tonalElevation: Dp,
    shadowElevation: Dp,
    pressed: Boolean = false
): PanelLook {
    val extras = LocalThemeExtras.current
    val scheme = MaterialTheme.colorScheme
    val visible = color.alpha > 0.04f
    return when (extras.uiStyle) {
        UiStyle.STANDARD, UiStyle.NEON -> PanelLook(Modifier, color, border, shadowElevation, tonalElevation)
        UiStyle.CLASSIC -> PanelLook(
            modifier = if (visible) Modifier.drawWithContent {
                drawContent()
                drawBevel(light = Color.White.copy(alpha = 0.95f), dark = Color(0xFF404040), shade = Color(0xFF808080), sunken = pressed)
            } else Modifier,
            color = color, border = null, shadowElevation = 0.dp, tonalElevation = 0.dp
        )
        UiStyle.LUNA -> PanelLook(
            modifier = if (visible) Modifier.background(
                Brush.verticalGradient(listOf(lerp(color, Color.White, 0.22f), color, lerp(color, Color.Black, 0.04f))), shape
            ) else Modifier,
            color = if (visible) Color.Transparent else color,
            border = border ?: BorderStroke(1.dp, lerp(scheme.outline, scheme.primary, 0.35f).copy(alpha = 0.8f)),
            shadowElevation = maxOf(shadowElevation, 1.dp), tonalElevation = 0.dp
        )
        UiStyle.S60 -> PanelLook(
            modifier = if (visible) Modifier.background(
                Brush.verticalGradient(listOf(lerp(color, Color.White, 0.16f), color)), shape
            ) else Modifier,
            color = if (visible) Color.Transparent else color,
            border = border ?: BorderStroke(1.dp, scheme.outlineVariant),
            shadowElevation = 0.dp, tonalElevation = 0.dp
        )
        UiStyle.AERO -> PanelLook(
            modifier = if (visible) Modifier.drawWithContent {
                drawContent()
                drawRect(
                    Brush.verticalGradient(0f to Color.White.copy(alpha = 0.20f), 0.45f to Color.Transparent),
                    size = size
                )
            } else Modifier,
            color = color.copy(alpha = color.alpha * 0.62f),
            border = BorderStroke(1.dp, Color.White.copy(alpha = 0.30f)),
            shadowElevation = 0.dp, tonalElevation = 0.dp
        )
        UiStyle.METRO -> PanelLook(Modifier, color, null, 0.dp, 0.dp)
        UiStyle.OSD -> PanelLook(
            modifier = Modifier,
            color = color.copy(alpha = color.alpha * 0.66f),
            border = BorderStroke(1.dp, scheme.primary.copy(alpha = 0.45f)),
            shadowElevation = 0.dp, tonalElevation = 0.dp
        )
        UiStyle.PIXEL -> PanelLook(
            modifier = if (visible) Modifier.drawWithContent {
                drawContent()
                drawPixelFrame(if (pressed) scheme.primary else scheme.outline)
            } else Modifier,
            color = color, border = null, shadowElevation = 0.dp, tonalElevation = 0.dp
        )
        UiStyle.TERMINAL -> PanelLook(
            modifier = if (visible) Modifier.drawWithContent {
                drawContent()
                drawRect(
                    color = scheme.primary.copy(alpha = 0.7f),
                    style = Stroke(width = 1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 6f)))
                )
            } else Modifier,
            color = color, border = null, shadowElevation = 0.dp, tonalElevation = 0.dp
        )
        UiStyle.PAPER -> PanelLook(Modifier, color, null, maxOf(shadowElevation, if (visible) 3.dp else 0.dp), 0.dp)
    }
}

/** Win9x 3D frame: light top/left, dark bottom/right (swapped when sunken/pressed). */
internal fun DrawScope.drawBevel(light: Color, dark: Color, shade: Color, sunken: Boolean) {
    val w = size.width
    val h = size.height
    val s = 1.5.dp.toPx()
    val topLeft = if (sunken) dark else light
    val bottomRight = if (sunken) light else dark
    drawLine(topLeft, Offset(0f, s / 2), Offset(w, s / 2), s)
    drawLine(topLeft, Offset(s / 2, 0f), Offset(s / 2, h), s)
    drawLine(bottomRight, Offset(0f, h - s / 2), Offset(w, h - s / 2), s)
    drawLine(bottomRight, Offset(w - s / 2, 0f), Offset(w - s / 2, h), s)
    val inner = if (sunken) light else shade
    drawLine(inner, Offset(s, h - s * 1.5f), Offset(w - s, h - s * 1.5f), s)
    drawLine(inner, Offset(w - s * 1.5f, s), Offset(w - s * 1.5f, h - s), s)
}

/** Stepped 8-bit frame: straight edges with notched corners. */
internal fun DrawScope.drawPixelFrame(color: Color) {
    val p = 3.dp.toPx()
    val w = size.width
    val h = size.height
    drawRect(color, topLeft = Offset(p, 0f), size = androidx.compose.ui.geometry.Size(w - 2 * p, p))
    drawRect(color, topLeft = Offset(p, h - p), size = androidx.compose.ui.geometry.Size(w - 2 * p, p))
    drawRect(color, topLeft = Offset(0f, p), size = androidx.compose.ui.geometry.Size(p, h - 2 * p))
    drawRect(color, topLeft = Offset(w - p, p), size = androidx.compose.ui.geometry.Size(p, h - 2 * p))
    for ((x, y) in listOf(p to p, w - 2 * p to p, p to h - 2 * p, w - 2 * p to h - 2 * p)) {
        drawRect(color, topLeft = Offset(x, y), size = androidx.compose.ui.geometry.Size(p, p))
    }
}

@Composable
@NonRestartableComposable
fun Surface(
    modifier: Modifier = Modifier,
    shape: Shape = androidx.compose.ui.graphics.RectangleShape,
    color: Color = MaterialTheme.colorScheme.surface,
    contentColor: Color = contentColorFor(color),
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp,
    border: BorderStroke? = null,
    content: @Composable () -> Unit
) {
    val look = panelLook(shape, color, border, tonalElevation, shadowElevation)
    M3Surface(
        modifier = modifier.then(look.modifier),
        shape = shape,
        color = look.color,
        contentColor = contentColor,
        tonalElevation = look.tonalElevation,
        shadowElevation = look.shadowElevation,
        border = look.border,
        content = content
    )
}

@Composable
@NonRestartableComposable
fun Surface(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = androidx.compose.ui.graphics.RectangleShape,
    color: Color = MaterialTheme.colorScheme.surface,
    contentColor: Color = contentColorFor(color),
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp,
    border: BorderStroke? = null,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable () -> Unit
) {
    val look = panelLook(shape, color, border, tonalElevation, shadowElevation)
    M3Surface(
        onClick = onClick,
        modifier = modifier.then(look.modifier),
        enabled = enabled,
        shape = shape,
        color = look.color,
        contentColor = contentColor,
        tonalElevation = look.tonalElevation,
        shadowElevation = look.shadowElevation,
        border = look.border,
        interactionSource = interactionSource,
        content = content
    )
}

@Composable
@NonRestartableComposable
fun Surface(
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = androidx.compose.ui.graphics.RectangleShape,
    color: Color = MaterialTheme.colorScheme.surface,
    contentColor: Color = contentColorFor(color),
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp,
    border: BorderStroke? = null,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable () -> Unit
) {
    val look = panelLook(shape, color, border, tonalElevation, shadowElevation, pressed = selected)
    M3Surface(
        selected = selected,
        onClick = onClick,
        modifier = modifier.then(look.modifier),
        enabled = enabled,
        shape = shape,
        color = look.color,
        contentColor = contentColor,
        tonalElevation = look.tonalElevation,
        shadowElevation = look.shadowElevation,
        border = look.border,
        interactionSource = interactionSource,
        content = content
    )
}

/** Shared painters for headers/other files (DrawScope extension helpers are internal). */
fun drawBevelPublic(scope: DrawScope, sunken: Boolean) =
    scope.drawBevel(Color.White.copy(alpha = 0.95f), Color(0xFF404040), Color(0xFF808080), sunken)

fun drawPixelFramePublic(scope: DrawScope, color: Color) = scope.drawPixelFrame(color)

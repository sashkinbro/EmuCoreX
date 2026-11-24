package com.sbro.emucorex.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.theme.UiStyle
import com.sbro.emucorex.ui.theme.LocalThemeExtras

/** Header fill for "title bar" UI styles; null means the regular panel header is used. */
@Composable
fun styledHeaderBrush(): Brush? {
    val primary = MaterialTheme.colorScheme.primary
    val secondary = MaterialTheme.colorScheme.secondary
    return when (LocalThemeExtras.current.uiStyle) {
        UiStyle.CLASSIC -> Brush.horizontalGradient(listOf(primary, lerp(primary, Color.White, 0.42f)))
        UiStyle.LUNA -> Brush.verticalGradient(
            0f to lerp(primary, Color.White, 0.45f),
            0.18f to lerp(primary, Color.White, 0.12f),
            0.8f to primary,
            1f to lerp(primary, Color.Black, 0.22f)
        )
        UiStyle.S60 -> Brush.verticalGradient(listOf(lerp(primary, Color.White, 0.28f), lerp(primary, Color.Black, 0.18f)))
        UiStyle.PIXEL -> Brush.verticalGradient(listOf(primary, primary))
        UiStyle.METRO -> Brush.verticalGradient(listOf(primary, primary))
        UiStyle.OSD -> Brush.horizontalGradient(listOf(lerp(primary, Color.Black, 0.55f).copy(alpha = 0.85f), lerp(secondary, Color.Black, 0.6f).copy(alpha = 0.55f)))
        else -> null
    }
}

@Composable
private fun headerFrame(): Modifier {
    val outline = MaterialTheme.colorScheme.outline
    val primary = MaterialTheme.colorScheme.primary
    return when (LocalThemeExtras.current.uiStyle) {
        UiStyle.CLASSIC -> Modifier.drawWithContent {
            drawContent()
            drawBevel(Color.White.copy(alpha = 0.95f), Color(0xFF404040), Color(0xFF808080), sunken = false)
        }
        UiStyle.PIXEL -> Modifier.drawWithContent {
            drawContent()
            drawPixelFrame(lerp(primary, Color.Black, 0.45f))
        }
        UiStyle.OSD -> Modifier.drawWithContent {
            drawContent()
            drawRect(primary.copy(alpha = 0.55f), style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
        }
        UiStyle.LUNA -> Modifier.drawWithContent {
            drawContent()
            drawRect(lerp(outline, Color.Black, 0.3f), style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
        }
        else -> Modifier
    }
}

/**
 * Surface for screen headers. Title-bar styles paint a filled header and remap the content colors
 * (onSurface/onSurfaceVariant) to readable "on header" colors, so any header content adapts.
 */
@Composable
fun HeaderSurface(
    modifier: Modifier = Modifier,
    shape: Shape,
    color: Color = MaterialTheme.colorScheme.surface,
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp,
    border: BorderStroke? = null,
    content: @Composable () -> Unit
) {
    val brush = styledHeaderBrush()
    if (brush == null) {
        Surface(modifier = modifier, shape = shape, color = color, tonalElevation = tonalElevation,
            shadowElevation = shadowElevation, border = border, content = content)
        return
    }
    val scheme = MaterialTheme.colorScheme
    val onHeader = if (LocalThemeExtras.current.uiStyle == UiStyle.OSD) scheme.onSurface else scheme.onPrimary
    val headerScheme = scheme.copy(
        onSurface = onHeader,
        onSurfaceVariant = onHeader.copy(alpha = 0.82f),
        onBackground = onHeader,
        surfaceVariant = lerp(scheme.primary, Color.White, 0.18f),
        primary = onHeader,
        outlineVariant = onHeader.copy(alpha = 0.35f)
    )
    Box(modifier = modifier.clip(shape).background(brush).then(headerFrame())) {
        MaterialTheme(colorScheme = headerScheme, typography = MaterialTheme.typography, shapes = MaterialTheme.shapes) {
            // Menus opened from the header must use the page's colors, not the header remap.
            CompositionLocalProvider(LocalContentColor provides onHeader, LocalOuterColorScheme provides scheme) { content() }
        }
    }
}

/** The page color scheme outside a styled header (null when not inside one). */
val LocalOuterColorScheme = androidx.compose.runtime.compositionLocalOf<androidx.compose.material3.ColorScheme?> { null }

/**
 * Drop-in for material3.DropdownMenu. A menu opened from a title-bar header renders with the page's
 * normal colors instead of inheriting the header's remapped "on header" colors (white on white).
 */
@Composable
fun DropdownMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    offset: androidx.compose.ui.unit.DpOffset = androidx.compose.ui.unit.DpOffset(0.dp, 0.dp),
    scrollState: androidx.compose.foundation.ScrollState = androidx.compose.foundation.rememberScrollState(),
    properties: androidx.compose.ui.window.PopupProperties = androidx.compose.ui.window.PopupProperties(focusable = true),
    shape: Shape? = null,
    containerColor: Color? = null,
    tonalElevation: Dp? = null,
    shadowElevation: Dp? = null,
    border: BorderStroke? = null,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit
) {
    val outer = LocalOuterColorScheme.current
    @Composable
    fun menu() {
        androidx.compose.material3.DropdownMenu(
            expanded = expanded,
            onDismissRequest = onDismissRequest,
            modifier = modifier,
            offset = offset,
            scrollState = scrollState,
            properties = properties,
            shape = shape ?: androidx.compose.material3.MenuDefaults.shape,
            containerColor = containerColor ?: androidx.compose.material3.MenuDefaults.containerColor,
            tonalElevation = tonalElevation ?: androidx.compose.material3.MenuDefaults.TonalElevation,
            shadowElevation = shadowElevation ?: androidx.compose.material3.MenuDefaults.ShadowElevation,
            border = border,
            content = content
        )
    }
    if (outer == null) {
        menu()
    } else {
        MaterialTheme(colorScheme = outer, typography = MaterialTheme.typography, shapes = MaterialTheme.shapes) {
            CompositionLocalProvider(LocalContentColor provides outer.onSurface, LocalOuterColorScheme provides null) { menu() }
        }
    }
}

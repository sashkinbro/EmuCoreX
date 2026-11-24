package com.sbro.emucorex.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.RowScope
import androidx.compose.material3.ButtonColors
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ButtonElevation
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.theme.UiStyle
import com.sbro.emucorex.ui.theme.LocalThemeExtras
import androidx.compose.material3.Button as M3Button
import androidx.compose.material3.FilledTonalButton as M3FilledTonalButton
import androidx.compose.material3.OutlinedButton as M3OutlinedButton
import androidx.compose.material3.TextButton as M3TextButton

/*
 * Drop-in replacements for material3 buttons that apply the theme's UI style. Under STANDARD/NEON
 * they forward unchanged. Containers are painted by the style (bevel, gloss, gradient, glass) and the
 * Material container is made transparent so the painting shows through.
 */

private class ButtonLook(val modifier: Modifier, val colors: ButtonColors, val border: BorderStroke?, val flat: Boolean)

@Composable
private fun buttonLook(
    style: UiStyle,
    base: ButtonColors,
    enabled: Boolean,
    shape: Shape,
    border: BorderStroke?,
    interactionSource: MutableInteractionSource,
    filled: Boolean
): ButtonLook? {
    val pressed by interactionSource.collectIsPressedAsState()
    val container = if (enabled) base.containerColor else base.disabledContainerColor
    // Text/outlined buttons have a transparent container; styles that draw raised buttons give them one.
    val fill = if (container.alpha < 0.05f) androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant else container
    fun painted(brush: Brush, extra: Modifier = Modifier, newBorder: BorderStroke? = border) = ButtonLook(
        modifier = Modifier.background(brush, shape).then(extra),
        colors = base.copy(containerColor = Color.Transparent, disabledContainerColor = Color.Transparent,
            contentColor = if (container.alpha < 0.05f) base.contentColor else base.contentColor),
        border = newBorder,
        flat = true
    )
    return when (style) {
        UiStyle.STANDARD, UiStyle.NEON, UiStyle.METRO, UiStyle.OSD, UiStyle.PAPER -> null
        UiStyle.CLASSIC -> painted(
            Brush.verticalGradient(listOf(fill, fill)),
            Modifier.drawWithContent {
                drawContent()
                drawBevel(Color.White, Color(0xFF404040), Color(0xFF808080), sunken = pressed)
            },
            newBorder = null
        )
        UiStyle.LUNA -> painted(
            Brush.verticalGradient(
                0f to lerp(fill, Color.White, if (pressed) 0.05f else 0.45f),
                0.48f to lerp(fill, Color.White, 0.10f),
                0.52f to fill,
                1f to lerp(fill, Color.Black, 0.12f)
            ),
            newBorder = BorderStroke(1.dp, lerp(fill, Color.Black, 0.35f))
        )
        UiStyle.S60 -> painted(
            Brush.verticalGradient(listOf(lerp(fill, Color.White, if (pressed) 0f else 0.28f), lerp(fill, Color.Black, 0.10f))),
            newBorder = BorderStroke(1.dp, lerp(fill, Color.Black, 0.25f))
        )
        UiStyle.AERO -> painted(
            Brush.verticalGradient(
                0f to fill.copy(alpha = 0.55f).compositeWhite(0.35f),
                0.5f to fill.copy(alpha = 0.55f),
                1f to fill.copy(alpha = 0.75f)
            ),
            newBorder = BorderStroke(1.dp, Color.White.copy(alpha = 0.45f))
        )
        UiStyle.PIXEL -> painted(
            Brush.verticalGradient(listOf(fill, fill)),
            Modifier.drawWithContent {
                drawContent()
                drawPixelFrame(if (pressed) base.contentColor else lerp(fill, Color.Black, 0.5f))
            },
            newBorder = null
        )
        UiStyle.TERMINAL -> if (filled) null else ButtonLook(Modifier, base, BorderStroke(1.dp, base.contentColor.copy(alpha = 0.7f)), false)
    }
}

private fun Color.compositeWhite(amount: Float) = lerp(this, Color.White.copy(alpha = this.alpha), amount)

@Composable
fun Button(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = ButtonDefaults.shape,
    colors: ButtonColors = ButtonDefaults.buttonColors(),
    elevation: ButtonElevation? = ButtonDefaults.buttonElevation(),
    border: BorderStroke? = null,
    contentPadding: PaddingValues = ButtonDefaults.ContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val look = buttonLook(LocalThemeExtras.current.uiStyle, colors, enabled, shape, border, source, filled = true)
    M3Button(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
        if (look?.flat == true) null else elevation, look?.border ?: border, contentPadding, source, content)
}

@Composable
fun FilledTonalButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = ButtonDefaults.filledTonalShape,
    colors: ButtonColors = ButtonDefaults.filledTonalButtonColors(),
    elevation: ButtonElevation? = ButtonDefaults.filledTonalButtonElevation(),
    border: BorderStroke? = null,
    contentPadding: PaddingValues = ButtonDefaults.ContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val look = buttonLook(LocalThemeExtras.current.uiStyle, colors, enabled, shape, border, source, filled = true)
    M3FilledTonalButton(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
        if (look?.flat == true) null else elevation, look?.border ?: border, contentPadding, source, content)
}

@Composable
fun OutlinedButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = ButtonDefaults.outlinedShape,
    colors: ButtonColors = ButtonDefaults.outlinedButtonColors(),
    elevation: ButtonElevation? = null,
    border: BorderStroke? = ButtonDefaults.outlinedButtonBorder(enabled),
    contentPadding: PaddingValues = ButtonDefaults.ContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val look = buttonLook(LocalThemeExtras.current.uiStyle, colors, enabled, shape, border, source, filled = false)
    M3OutlinedButton(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
        elevation, if (look != null) look.border else border, contentPadding, source, content)
}

@Composable
fun TextButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = ButtonDefaults.textShape,
    colors: ButtonColors = ButtonDefaults.textButtonColors(),
    elevation: ButtonElevation? = null,
    border: BorderStroke? = null,
    contentPadding: PaddingValues = ButtonDefaults.TextButtonContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    // Only the "raised button" styles give text buttons a body (classic dialogs had real buttons).
    val style = LocalThemeExtras.current.uiStyle
    val look = if (style == UiStyle.CLASSIC || style == UiStyle.LUNA || style == UiStyle.PIXEL) {
        buttonLook(style, colors, enabled, shape, border, source, filled = false)
    } else null
    M3TextButton(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
        elevation, look?.border ?: border, contentPadding, source, content)
}

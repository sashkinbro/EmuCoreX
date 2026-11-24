package com.sbro.emucorex.ui.theme

import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import com.sbro.emucorex.data.theme.ThemeDecor
import com.sbro.emucorex.data.theme.ThemeShapeStyle
import com.sbro.emucorex.data.theme.ThemeStyleSpec
import com.sbro.emucorex.data.theme.UiStyle
import java.io.File

/**
 * Theme properties that Material's ColorScheme/Shapes/Typography cannot express: the corner style
 * used by every `neonShape(...)` call site, decor (Neon brackets/strips/banners), decor accents,
 * semantic status colors and overlay effects. Built-in themes and downloaded theme packages both
 * resolve to one of these.
 */
@Immutable
data class ThemeExtras(
    val shapeStyle: ThemeShapeStyle = ThemeShapeStyle.ROUNDED,
    val shapeScale: Float = 1f,
    val decor: ThemeDecor = ThemeDecor.NONE,
    val uiStyle: UiStyle = UiStyle.STANDARD,
    val accent1: Color = Color.Unspecified,
    val accent2: Color = Color.Unspecified,
    val accent3: Color = Color.Unspecified,
    val success: Color = DefaultSuccess,
    val warning: Color = DefaultWarning,
    val info: Color = DefaultInfo,
    val gold: Color = DefaultGold,
    val scanlines: Boolean = false,
    val backgroundFile: File? = null,
    val backgroundDim: Float = 0.35f
) {
    val neonDecor: Boolean get() = decor == ThemeDecor.NEON

    fun accent(index: Int): Color = when (index.mod(3)) {
        0 -> accent1
        1 -> accent2
        else -> accent3
    }

    companion object {
        val DefaultSuccess = Color(0xFF4CAF50)
        val DefaultWarning = Color(0xFFFFB300)
        val DefaultInfo = Color(0xFF42A5F5)
        val DefaultGold = Color(0xFFE9B949)
    }
}

val LocalThemeExtras = staticCompositionLocalOf { ThemeExtras() }

internal fun ThemeStyleSpec.toThemeExtras(
    primary: Color,
    secondary: Color,
    tertiary: Color,
    backgroundFile: File?
): ThemeExtras = ThemeExtras(
    shapeStyle = shapeStyle,
    shapeScale = shapeScale,
    decor = decor,
    uiStyle = uiStyle,
    accent1 = accents.getOrNull(0)?.let(::Color) ?: primary,
    accent2 = accents.getOrNull(1)?.let(::Color) ?: tertiary,
    accent3 = accents.getOrNull(2)?.let(::Color) ?: secondary,
    success = success?.let(::Color) ?: ThemeExtras.DefaultSuccess,
    warning = warning?.let(::Color) ?: ThemeExtras.DefaultWarning,
    info = info?.let(::Color) ?: ThemeExtras.DefaultInfo,
    gold = gold?.let(::Color) ?: ThemeExtras.DefaultGold,
    scanlines = scanlines,
    backgroundFile = backgroundFile,
    backgroundDim = backgroundDim
)

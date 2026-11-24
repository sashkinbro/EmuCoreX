package com.sbro.emucorex.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import com.sbro.emucorex.ui.theme.neon.LocalNeonTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.AppFontChoice
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CustomThemeConfig
import com.sbro.emucorex.ui.theme.neon.NeonColorScheme
import com.sbro.emucorex.ui.theme.neon.NeonCrtOverlay
import com.sbro.emucorex.ui.theme.neon.NeonShapes
import com.sbro.emucorex.ui.theme.neon.neonMonospace
import java.io.File
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.material3.Typography
import androidx.compose.foundation.shape.CornerBasedShape
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.font.FontFamily
import com.sbro.emucorex.data.theme.InstalledThemePackage
import com.sbro.emucorex.data.theme.ThemeDecor
import com.sbro.emucorex.data.theme.ThemeFont
import com.sbro.emucorex.data.theme.ThemeShapeStyle
import com.sbro.emucorex.data.theme.toCustomThemeConfig
import com.sbro.emucorex.data.theme.toStyleSpec
import com.sbro.emucorex.ui.theme.neon.NeonBlue
import com.sbro.emucorex.ui.theme.neon.NeonRed
import com.sbro.emucorex.ui.theme.neon.NeonYellow

private val DarkColorScheme = darkColorScheme(
    primary = AccentPrimary,
    onPrimary = OnAccent,
    primaryContainer = AccentPrimaryContainer,
    onPrimaryContainer = AccentPrimaryLight,
    secondary = SecondaryAccent,
    onSecondary = OnAccent,
    secondaryContainer = SecondaryContainer,
    onSecondaryContainer = SecondaryAccent,
    tertiary = TertiaryAccent,
    onTertiary = OnAccent,
    background = DarkBackground,
    onBackground = DarkOnBackground,
    surface = DarkSurface,
    onSurface = DarkOnSurface,
    surfaceVariant = DarkSurfaceVariant,
    onSurfaceVariant = DarkOnSurfaceVariant,
    surfaceTint = Color.Transparent,
    outline = DarkOutline,
    outlineVariant = DarkOutline,
    error = ErrorRed,
    onError = OnAccent,
    errorContainer = ErrorContainer,
    onErrorContainer = ErrorRed,
    scrim = DarkScrim
)

private val ProColorScheme = darkColorScheme(
    primary = ProPrimary,
    onPrimary = OnAccent,
    primaryContainer = ProPrimaryContainer,
    onPrimaryContainer = ProOnPrimaryContainer,
    secondary = ProSecondary,
    onSecondary = Color(0xFF21181C),
    secondaryContainer = ProSecondaryContainer,
    onSecondaryContainer = Color(0xFFE6D8DD),
    tertiary = ProTertiary,
    onTertiary = Color(0xFF1A0D00),
    background = ProBackground,
    onBackground = ProOnBackground,
    surface = ProSurface,
    onSurface = ProOnSurface,
    surfaceVariant = ProSurfaceVariant,
    onSurfaceVariant = ProOnSurfaceVariant,
    outline = ProOutline,
    outlineVariant = ProOutline,
    surfaceTint = Color.Transparent,
    error = ErrorRed,
    onError = OnAccent,
    errorContainer = ErrorContainer,
    onErrorContainer = ErrorRed,
    scrim = ProScrim
)

private val LightColorScheme = lightColorScheme(
    primary = AccentPrimaryDark,
    onPrimary = OnAccent,
    primaryContainer = AccentPrimaryLightContainer,
    onPrimaryContainer = AccentPrimaryDark,
    secondary = SecondaryAccentDark,
    onSecondary = OnAccent,
    secondaryContainer = SecondaryLightContainer,
    onSecondaryContainer = SecondaryAccentDark,
    tertiary = TertiaryDark,
    onTertiary = OnAccent,
    background = LightBackground,
    onBackground = LightOnBackground,
    surface = LightSurface,
    onSurface = LightOnSurface,
    surfaceVariant = LightSurfaceVariant,
    onSurfaceVariant = LightOnSurfaceVariant,
    surfaceTint = Color.Transparent,
    outline = LightOutline,
    outlineVariant = LightOutline,
    error = ErrorRedLight,
    onError = OnAccent,
    errorContainer = ErrorLightContainer,
    onErrorContainer = ErrorRedLight,
)

enum class ThemeMode {
    SYSTEM, LIGHT, DARK, PRO, CUSTOM, NEON, PACKAGE
}

/** Whether [this] mode renders dark; shared with MainActivity's system-bar handling. */
fun ThemeMode.isDark(
    systemDark: Boolean,
    customTheme: CustomThemeConfig,
    themePackage: InstalledThemePackage?
): Boolean = when (this) {
    ThemeMode.SYSTEM -> systemDark
    ThemeMode.LIGHT -> false
    ThemeMode.DARK -> true
    ThemeMode.PRO -> true
    ThemeMode.CUSTOM -> customTheme.dark
    ThemeMode.NEON -> true
    ThemeMode.PACKAGE -> themePackage
        ?.takeIf { it.isSupported }
        ?.let { pkg -> pkg.spec.toCustomThemeConfig("en").dark }
        ?: systemDark
}

private val NeonExtras = ThemeExtras(
    shapeStyle = ThemeShapeStyle.CUT_DIAGONAL,
    decor = ThemeDecor.NEON,
    accent1 = NeonYellow,
    accent2 = NeonBlue,
    accent3 = NeonRed,
    scanlines = true
)

@Composable
fun EmuCoreXTheme(
    themeMode: ThemeMode = ThemeMode.SYSTEM,
    customTheme: CustomThemeConfig = CustomThemeConfig.Default,
    themePackage: InstalledThemePackage? = null,
    fontChoice: AppFontChoice = AppFontChoice.SYSTEM,
    fontScale: Float = 1f,
    customFontFile: File? = null,
    customFontRevision: Int = 0,
    enableCrtOverlay: Boolean = true,
    content: @Composable () -> Unit
) {
    // A package theme that is missing or unsupported by this app version renders as SYSTEM.
    val activePackage = themePackage?.takeIf { themeMode == ThemeMode.PACKAGE && it.isSupported }
    val effectiveMode = if (themeMode == ThemeMode.PACKAGE && activePackage == null) ThemeMode.SYSTEM else themeMode
    val systemDark = isSystemInDarkTheme()
    val localeTag = LocalConfiguration.current.locales[0]?.language ?: "en"

    val packageConfig = remember(activePackage, localeTag) {
        activePackage?.spec?.toCustomThemeConfig(localeTag)
    }
    val packageStyle = remember(activePackage) { activePackage?.spec?.toStyleSpec() }

    val darkTheme = effectiveMode.isDark(systemDark, customTheme, activePackage)

    val safeCustomTheme = remember(customTheme) { customTheme.sanitized() }
    val colorScheme = when (effectiveMode) {
        ThemeMode.PRO -> ProColorScheme
        ThemeMode.CUSTOM -> safeCustomTheme.toColorScheme()
        ThemeMode.NEON -> NeonColorScheme
        ThemeMode.PACKAGE -> packageConfig!!.toColorScheme()
        else -> if (darkTheme) DarkColorScheme else LightColorScheme
    }
    val extras = remember(effectiveMode, colorScheme, packageStyle, activePackage) {
        when (effectiveMode) {
            ThemeMode.NEON -> NeonExtras
            ThemeMode.PACKAGE -> packageStyle!!.toThemeExtras(
                primary = colorScheme.primary,
                secondary = colorScheme.secondary,
                tertiary = colorScheme.tertiary,
                backgroundFile = activePackage?.backgroundFile
            )
            else -> ThemeExtras(
                accent1 = colorScheme.primary,
                accent2 = colorScheme.tertiary,
                accent3 = colorScheme.secondary
            )
        }
    }
    val shapes = when (effectiveMode) {
        ThemeMode.CUSTOM -> safeCustomTheme.toShapes()
        ThemeMode.NEON -> NeonShapes
        ThemeMode.PACKAGE -> packageConfig!!.toShapes(extras.shapeStyle, extras.shapeScale)
        else -> MaterialTheme.shapes
    }

    val packageFont = packageStyle?.font
    val safeFontScale = fontScale.coerceIn(AppPreferences.MIN_APP_FONT_SCALE, AppPreferences.MAX_APP_FONT_SCALE)
    val packageFontFile = activePackage?.fontFile
    val usePackageFontFile = packageFont == ThemeFont.PACKAGE && packageFontFile != null
    val effectiveFontChoice = when {
        usePackageFontFile -> AppFontChoice.CUSTOM
        packageFont == ThemeFont.SYSTEM -> AppFontChoice.SYSTEM
        packageFont == ThemeFont.RUBIK -> AppFontChoice.RUBIK
        packageFont == ThemeFont.EXO2 -> AppFontChoice.EXO_2
        else -> fontChoice
    }
    val effectiveFontFile = if (usePackageFontFile) packageFontFile else customFontFile
    val baseTypography = remember(
        effectiveFontChoice,
        safeFontScale,
        effectiveFontFile?.absolutePath,
        customFontRevision,
        activePackage
    ) {
        typographyFor(
            choice = effectiveFontChoice,
            customFontFile = effectiveFontFile,
            fontScale = safeFontScale
        )
    }
    val typography = when {
        effectiveMode == ThemeMode.NEON -> baseTypography.neonMonospace()
        packageFont == ThemeFont.MONOSPACE -> baseTypography.withFamily(FontFamily.Monospace)
        packageFont == ThemeFont.SERIF -> baseTypography.withFamily(FontFamily.Serif)
        else -> baseTypography
    }
    MaterialTheme(
        colorScheme = colorScheme,
        typography = typography,
        shapes = shapes
    ) {
        CompositionLocalProvider(
            LocalThemeExtras provides extras,
            LocalNeonTheme provides extras.neonDecor
        ) {
            Box(modifier = Modifier.fillMaxSize()) {
                content()
                if (extras.scanlines && enableCrtOverlay) {
                    NeonCrtOverlay()
                }
            }
        }
    }
}

private fun Typography.withFamily(family: FontFamily): Typography = copy(
    displayLarge = displayLarge.copy(fontFamily = family),
    displayMedium = displayMedium.copy(fontFamily = family),
    displaySmall = displaySmall.copy(fontFamily = family),
    headlineLarge = headlineLarge.copy(fontFamily = family),
    headlineMedium = headlineMedium.copy(fontFamily = family),
    headlineSmall = headlineSmall.copy(fontFamily = family),
    titleLarge = titleLarge.copy(fontFamily = family),
    titleMedium = titleMedium.copy(fontFamily = family),
    titleSmall = titleSmall.copy(fontFamily = family),
    bodyLarge = bodyLarge.copy(fontFamily = family),
    bodyMedium = bodyMedium.copy(fontFamily = family),
    bodySmall = bodySmall.copy(fontFamily = family),
    labelLarge = labelLarge.copy(fontFamily = family),
    labelMedium = labelMedium.copy(fontFamily = family),
    labelSmall = labelSmall.copy(fontFamily = family)
)

/**
 * Full Material scheme for a custom/package theme. Container, dim/bright and inverse roles are
 * derived from the theme's own surface/background/text colors; leaving them at Material's
 * baseline would tint cards, menus and dialogs with the default lilac.
 */
fun CustomThemeConfig.toColorScheme(): ColorScheme = baseColorScheme().withDerivedRoles(dark)

private fun ColorScheme.withDerivedRoles(dark: Boolean): ColorScheme {
    val toward = onSurface
    fun step(amount: Float) = lerp(surface, toward, amount)
    return copy(
        surfaceDim = if (dark) lerp(background, Color.Black, 0.25f) else lerp(surface, toward, 0.10f),
        surfaceBright = if (dark) step(0.12f) else lerp(surface, Color.White, 0.6f),
        surfaceContainerLowest = if (dark) lerp(background, Color.Black, 0.35f) else lerp(surface, Color.White, 0.7f),
        surfaceContainerLow = if (dark) lerp(background, surface, 0.6f) else lerp(surface, Color.White, 0.35f),
        surfaceContainer = surface,
        surfaceContainerHigh = step(if (dark) 0.05f else 0.04f),
        surfaceContainerHighest = surfaceVariant,
        inverseSurface = onSurface,
        inverseOnSurface = surface,
        inversePrimary = lerp(primary, if (dark) Color.Black else Color.White, 0.35f)
    )
}

private fun CustomThemeConfig.baseColorScheme() = if (dark) {
    darkColorScheme(
        primary = Color(primary),
        onPrimary = Color(onPrimary),
        primaryContainer = Color(primaryContainer),
        onPrimaryContainer = Color(onPrimaryContainer),
        secondary = Color(secondary),
        onSecondary = Color(onSecondary),
        secondaryContainer = Color(secondaryContainer),
        onSecondaryContainer = Color(onSecondaryContainer),
        tertiary = Color(tertiary),
        onTertiary = Color(onTertiary),
        tertiaryContainer = Color(tertiaryContainer),
        onTertiaryContainer = Color(onTertiaryContainer),
        background = Color(background),
        onBackground = Color(onBackground),
        surface = Color(surface),
        onSurface = Color(onSurface),
        surfaceVariant = Color(surfaceVariant),
        onSurfaceVariant = Color(onSurfaceVariant),
        surfaceTint = Color.Transparent,
        outline = Color(outline),
        outlineVariant = Color(outlineVariant),
        error = Color(error),
        onError = Color(onError),
        errorContainer = Color(errorContainer),
        onErrorContainer = Color(onErrorContainer),
        scrim = Color.Black.copy(alpha = 0.72f)
    )
} else {
    lightColorScheme(
        primary = Color(primary),
        onPrimary = Color(onPrimary),
        primaryContainer = Color(primaryContainer),
        onPrimaryContainer = Color(onPrimaryContainer),
        secondary = Color(secondary),
        onSecondary = Color(onSecondary),
        secondaryContainer = Color(secondaryContainer),
        onSecondaryContainer = Color(onSecondaryContainer),
        tertiary = Color(tertiary),
        onTertiary = Color(onTertiary),
        tertiaryContainer = Color(tertiaryContainer),
        onTertiaryContainer = Color(onTertiaryContainer),
        background = Color(background),
        onBackground = Color(onBackground),
        surface = Color(surface),
        onSurface = Color(onSurface),
        surfaceVariant = Color(surfaceVariant),
        onSurfaceVariant = Color(onSurfaceVariant),
        surfaceTint = Color.Transparent,
        outline = Color(outline),
        outlineVariant = Color(outlineVariant),
        error = Color(error),
        onError = Color(onError),
        errorContainer = Color(errorContainer),
        onErrorContainer = Color(onErrorContainer),
        scrim = Color.Black.copy(alpha = 0.48f)
    )
}

fun CustomThemeConfig.toShapes(): Shapes {
    val safe = sanitized()
    return Shapes(
        extraSmall = RoundedCornerShape((safe.smallCornerDp * 0.65f).dp),
        small = RoundedCornerShape(safe.smallCornerDp.dp),
        medium = RoundedCornerShape(safe.mediumCornerDp.dp),
        large = RoundedCornerShape(safe.largeCornerDp.dp),
        extraLarge = RoundedCornerShape((safe.largeCornerDp * 1.25f).coerceAtMost(60f).dp)
    )
}

/** Material shapes for a package theme: the custom radii, scaled, in the package's corner style. */
fun CustomThemeConfig.toShapes(style: ThemeShapeStyle, scale: Float): Shapes {
    val safe = sanitized()
    fun corner(dp: Float): CornerBasedShape {
        val size = (dp * scale).coerceAtLeast(0f).dp
        return when (style) {
            ThemeShapeStyle.ROUNDED -> RoundedCornerShape(size)
            ThemeShapeStyle.CUT -> CutCornerShape(size)
            ThemeShapeStyle.CUT_DIAGONAL -> CutCornerShape(topEnd = size, bottomStart = size)
            ThemeShapeStyle.SQUARE -> RoundedCornerShape(0.dp)
        }
    }
    return Shapes(
        extraSmall = corner(safe.smallCornerDp * 0.65f),
        small = corner(safe.smallCornerDp),
        medium = corner(safe.mediumCornerDp),
        large = corner(safe.largeCornerDp),
        extraLarge = corner((safe.largeCornerDp * 1.25f).coerceAtMost(60f))
    )
}

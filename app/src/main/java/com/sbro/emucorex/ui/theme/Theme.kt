package com.sbro.emucorex.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
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
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.AppFontChoice
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CustomThemeConfig
import com.sbro.emucorex.ui.theme.neon.NeonColorScheme
import com.sbro.emucorex.ui.theme.neon.NeonCrtOverlay
import com.sbro.emucorex.ui.theme.neon.NeonShapes
import com.sbro.emucorex.ui.theme.neon.neonMonospace
import java.io.File

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
    outline = LightOutline,
    outlineVariant = LightOutline,
    error = ErrorRedLight,
    onError = OnAccent,
    errorContainer = ErrorLightContainer,
    onErrorContainer = ErrorRedLight,
)

enum class ThemeMode {
    SYSTEM, LIGHT, DARK, PRO, CUSTOM, NEON
}

@Composable
fun EmuCoreXTheme(
    themeMode: ThemeMode = ThemeMode.SYSTEM,
    customTheme: CustomThemeConfig = CustomThemeConfig.Default,
    fontChoice: AppFontChoice = AppFontChoice.SYSTEM,
    fontScale: Float = 1f,
    customFontFile: File? = null,
    customFontRevision: Int = 0,
    content: @Composable () -> Unit
) {
    val darkTheme = when (themeMode) {
        ThemeMode.SYSTEM -> isSystemInDarkTheme()
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
        ThemeMode.PRO -> true
        ThemeMode.CUSTOM -> customTheme.dark
        ThemeMode.NEON -> true
    }

    val safeCustomTheme = remember(customTheme) { customTheme.sanitized() }
    val colorScheme = when (themeMode) {
        ThemeMode.PRO -> ProColorScheme
        ThemeMode.CUSTOM -> safeCustomTheme.toColorScheme()
        ThemeMode.NEON -> NeonColorScheme
        else -> if (darkTheme) DarkColorScheme else LightColorScheme
    }
    val shapes = if (themeMode == ThemeMode.CUSTOM) {
        safeCustomTheme.toShapes()
    } else if (themeMode == ThemeMode.NEON) {
        NeonShapes
    } else {
        MaterialTheme.shapes
    }

    val safeFontScale = fontScale.coerceIn(AppPreferences.MIN_APP_FONT_SCALE, AppPreferences.MAX_APP_FONT_SCALE)
    val baseTypography = remember(
        fontChoice,
        safeFontScale,
        customFontFile?.absolutePath,
        customFontRevision
    ) {
        typographyFor(
            choice = fontChoice,
            customFontFile = customFontFile,
            fontScale = safeFontScale
        )
    }
    val typography = if (themeMode == ThemeMode.NEON) {
        baseTypography.neonMonospace()
    } else {
        baseTypography
    }
    MaterialTheme(
        colorScheme = colorScheme,
        typography = typography,
        shapes = shapes
    ) {
        CompositionLocalProvider(
            LocalNeonTheme provides (themeMode == ThemeMode.NEON)
        ) {
            if (themeMode == ThemeMode.NEON) {
                Box(modifier = Modifier.fillMaxSize()) {
                    content()
                    NeonCrtOverlay()
                }
            } else {
                content()
            }
        }
    }
}

fun CustomThemeConfig.toColorScheme() = if (dark) {
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

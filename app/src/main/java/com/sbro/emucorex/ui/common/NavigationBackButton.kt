package com.sbro.emucorex.ui.common

import com.sbro.emucorex.data.theme.UiStyle
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.background
import com.sbro.emucorex.ui.theme.LocalThemeExtras
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import com.sbro.emucorex.ui.theme.style.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.LocalTvUiEnvironment
import com.sbro.emucorex.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorex.ui.theme.neon.NeonTricolorDivider
import com.sbro.emucorex.ui.theme.neon.neonShape
import com.sbro.emucorex.ui.theme.neon.neonShapeRight

@Composable
private fun defaultBackContainer(): Color =
    if (LocalNeonTheme.current) {
        MaterialTheme.colorScheme.surface
    } else {
        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.54f)
    }

@Composable
private fun defaultBackContent(): Color =
    if (LocalNeonTheme.current) {
        LocalThemeExtras.current.accent3
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }

@Composable
private fun defaultTopBarTitle(): Color =
    if (LocalNeonTheme.current) {
        LocalThemeExtras.current.accent1
    } else {
        MaterialTheme.colorScheme.onSurface
    }

@Composable
fun NavigationBackButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    containerColor: Color = defaultBackContainer(),
    contentColor: Color = defaultBackContent(),
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp
) {
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    Surface(
        modifier = modifier.size(if (tvUiEnabled) 52.dp else 44.dp),
        shape = neonShape(14.dp),
        color = containerColor,
        tonalElevation = tonalElevation,
        shadowElevation = shadowElevation,
        border = if (tvUiEnabled && isFocused) {
            BorderStroke(2.dp, MaterialTheme.colorScheme.primary)
        } else {
            BorderStroke(
                1.dp,
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.50f)
            )
        },
        interactionSource = interactionSource,
        onClick = rememberDebouncedClick(onClick = onClick)
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.AutoMirrored.Rounded.ArrowBack,
                contentDescription = stringResource(R.string.back),
                tint = contentColor,
                modifier = Modifier.size(if (tvUiEnabled) 22.dp else 19.dp)
            )
        }
    }
}

@Composable
fun ScreenTopBar(
    title: String,
    onBackClick: () -> Unit,
    modifier: Modifier = Modifier,
    backButtonModifier: Modifier = Modifier,
    subtitle: String? = null,
    titleColor: Color = defaultTopBarTitle(),
    subtitleColor: Color = MaterialTheme.colorScheme.onSurfaceVariant,
    backContentColor: Color = defaultBackContent(),
    titleMaxLines: Int = 1,
    subtitleMaxLines: Int = 1,
    embedded: Boolean = false,
    actions: @Composable RowScope.() -> Unit = {}
) {
    if (embedded) {
        ScreenTopBarContent(
            title = title,
            subtitle = subtitle,
            onBackClick = onBackClick,
            backButtonModifier = backButtonModifier,
            titleColor = titleColor,
            subtitleColor = subtitleColor,
            backContentColor = backContentColor,
            titleMaxLines = titleMaxLines,
            subtitleMaxLines = subtitleMaxLines,
            actions = actions
        )
        return
    }

    val headerBrush = styledHeaderBrush()
    if (headerBrush != null) {
        // Title-bar styles (Classic 9x, Luna, S60, Pixel): a filled header in the theme's primary.
        val onHeader = MaterialTheme.colorScheme.onPrimary
        Box(
            modifier = modifier
                .fillMaxWidth()
                .clip(neonShape(24.dp))
                .background(headerBrush)
                .then(styledHeaderFrame())
        ) {
            ScreenTopBarContent(
                title = styledTitle(title),
                subtitle = subtitle,
                onBackClick = onBackClick,
                backButtonModifier = backButtonModifier,
                titleColor = onHeader,
                subtitleColor = onHeader.copy(alpha = 0.82f),
                backContentColor = onHeader,
                titleMaxLines = titleMaxLines,
                subtitleMaxLines = subtitleMaxLines,
                actions = actions,
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp)
            )
        }
        return
    }

    val barSurface: @Composable (Modifier) -> Unit = { surfaceModifier ->
        Surface(
            modifier = surfaceModifier,
            shape = neonShape(24.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.78f),
            tonalElevation = 1.dp,
            shadowElevation = 0.dp,
            border = BorderStroke(
                1.dp,
                if (LocalNeonTheme.current) {
                    LocalThemeExtras.current.accent1.copy(alpha = 0.35f)
                } else {
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                }
            )
        ) {
            ScreenTopBarContent(
                title = styledTitle(title),
                subtitle = subtitle,
                onBackClick = onBackClick,
                backButtonModifier = backButtonModifier,
                titleColor = titleColor,
                subtitleColor = subtitleColor,
                backContentColor = backContentColor,
                titleMaxLines = titleMaxLines,
                subtitleMaxLines = subtitleMaxLines,
                actions = actions,
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp)
            )
        }
    }
    if (LocalNeonTheme.current) {
        Column(modifier = modifier) {
            barSurface(Modifier.fillMaxWidth())
            Spacer(modifier = Modifier.height(6.dp))
            NeonTricolorDivider(horizontalPadding = 10.dp)
        }
    } else {
        barSurface(modifier.fillMaxWidth())
    }
}

@Composable
private fun ScreenTopBarContent(
    title: String,
    subtitle: String?,
    onBackClick: () -> Unit,
    backButtonModifier: Modifier,
    titleColor: Color,
    subtitleColor: Color,
    backContentColor: Color,
    titleMaxLines: Int,
    subtitleMaxLines: Int,
    actions: @Composable RowScope.() -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        NavigationBackButton(
            onClick = onBackClick,
            modifier = backButtonModifier,
            contentColor = backContentColor
        )
        Spacer(modifier = Modifier.width(14.dp))
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(end = 8.dp)
        ) {
            val neonGlowTitle = LocalNeonTheme.current && titleColor == LocalThemeExtras.current.accent1
            Text(
                text = title,
                style = if (neonGlowTitle) {
                    MaterialTheme.typography.titleLarge.copy(
                        fontWeight = FontWeight.Bold,
                        shadow = Shadow(
                            color = LocalThemeExtras.current.accent1.copy(alpha = 0.85f),
                            blurRadius = 18f
                        )
                    )
                } else {
                    MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
                },
                color = titleColor,
                maxLines = titleMaxLines,
                overflow = TextOverflow.Ellipsis
            )
            if (!subtitle.isNullOrBlank()) {
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodyMedium,
                    color = subtitleColor,
                    maxLines = subtitleMaxLines,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        Row(
            modifier = Modifier.tvFocusGroup(),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
            content = actions
        )
    }
}

/** Header fill for title-bar UI styles; null keeps the regular panel header. */
@Composable
private fun styledHeaderBrush(): Brush? {
    val primary = MaterialTheme.colorScheme.primary
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
        else -> null
    }
}

@Composable
private fun styledHeaderFrame(): Modifier {
    val style = LocalThemeExtras.current.uiStyle
    val outline = MaterialTheme.colorScheme.outline
    return when (style) {
        UiStyle.CLASSIC -> Modifier.drawWithContent {
            drawContent()
            com.sbro.emucorex.ui.theme.style.drawBevelPublic(this, sunken = false)
        }
        UiStyle.PIXEL -> Modifier.drawWithContent {
            drawContent()
            com.sbro.emucorex.ui.theme.style.drawPixelFramePublic(this, outline)
        }
        else -> Modifier
    }
}

/** Terminal style shows headers as a prompt line. */
@Composable
private fun styledTitle(title: String): String =
    if (LocalThemeExtras.current.uiStyle == UiStyle.TERMINAL) "> ${title}_" else title

package com.sbro.emucorex.ui.common

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
import androidx.compose.material3.Surface
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
import com.sbro.emucorex.ui.theme.neon.NeonDark
import com.sbro.emucorex.ui.theme.neon.NeonRed
import com.sbro.emucorex.ui.theme.neon.NeonTricolorDivider
import com.sbro.emucorex.ui.theme.neon.NeonYellow
import com.sbro.emucorex.ui.theme.neon.neonShape
import com.sbro.emucorex.ui.theme.neon.neonShapeRight

@Composable
private fun defaultBackContainer(): Color =
    if (LocalNeonTheme.current) {
        NeonDark
    } else {
        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.54f)
    }

@Composable
private fun defaultBackContent(): Color =
    if (LocalNeonTheme.current) {
        NeonRed
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }

@Composable
private fun defaultTopBarTitle(): Color =
    if (LocalNeonTheme.current) {
        NeonYellow
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

    val barSurface: @Composable () -> Unit = {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = neonShape(24.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.78f),
            tonalElevation = 1.dp,
            shadowElevation = 0.dp,
            border = BorderStroke(
                1.dp,
                if (LocalNeonTheme.current) {
                    NeonYellow.copy(alpha = 0.35f)
                } else {
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                }
            )
        ) {
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
                actions = actions,
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp)
            )
        }
    }
    if (LocalNeonTheme.current) {
        Column(modifier = modifier) {
            barSurface()
            Spacer(modifier = Modifier.height(6.dp))
            NeonTricolorDivider(horizontalPadding = 10.dp)
        }
    } else {
        barSurface()
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
            val neonGlowTitle = LocalNeonTheme.current && titleColor == NeonYellow
            Text(
                text = title,
                style = if (neonGlowTitle) {
                    MaterialTheme.typography.titleLarge.copy(
                        fontWeight = FontWeight.Bold,
                        shadow = Shadow(
                            color = NeonYellow.copy(alpha = 0.85f),
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

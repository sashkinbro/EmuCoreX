package com.sbro.emucorex.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.ui.theme.neon.neonShape

/**
 * Modern, search-enabled game selector for Game Manager, Cheat Manager, and Texture Manager.
 * Replaces cumbersome horizontal scrolling with a compact active game card and a full-featured
 * search & fast-alphabetical picker dialog.
 */
@Composable
fun ManagerGameSelector(
    games: List<GameItem>,
    selectedPath: String?,
    onSelected: (GameItem) -> Unit,
    modifier: Modifier = Modifier,
    horizontalContentPadding: Dp = 0.dp,
    fullBleedPadding: Dp = 0.dp,
    extraBadge: ((GameItem) -> String?)? = null
) {
    val selectedGame = games.firstOrNull { it.path == selectedPath }
    var showPicker by rememberSaveable { mutableStateOf(false) }

    Column(
        modifier = modifier
            .managerGameSelectorFullBleed(fullBleedPadding)
            .fillMaxWidth()
    ) {
        if (games.isEmpty()) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = horizontalContentPadding),
                shape = neonShape(18.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
            ) {
                Text(
                    text = stringResource(R.string.content_library_empty),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(16.dp)
                )
            }
            return
        }

        val interactionSource = remember { MutableInteractionSource() }
        val shape = neonShape(18.dp)
        val extraBadgeText = selectedGame?.let { extraBadge?.invoke(it) }

        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalContentPadding)
                .tvGamepadFocusableCard(
                    shape = shape,
                    interactionSource = interactionSource,
                    addFocusTarget = false
                )
                .clickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = { showPicker = true }
                ),
            shape = shape,
            color = if (selectedGame != null) {
                MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.55f)
            } else {
                MaterialTheme.colorScheme.surface
            },
            border = BorderStroke(
                width = if (selectedGame != null) 2.dp else 1.dp,
                color = if (selectedGame != null) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.outlineVariant
                }
            )
        ) {
            Row(
                modifier = Modifier.padding(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                if (selectedGame != null) {
                    GameCoverArt(
                        coverPath = selectedGame.coverArtPath,
                        fallbackTitle = selectedGame.title,
                        contentScale = gameCoverContentScale(selectedGame.coverArtPath, ContentScale.Crop),
                        modifier = Modifier
                            .size(width = 52.dp, height = 74.dp)
                            .clip(gameCoverShape(selectedGame.coverArtPath, neonShape(10.dp)))
                    )
                    Spacer(Modifier.width(12.dp))
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = selectedGame.title,
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.Bold,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = selectedGame.serial
                                    ?: stringResource(R.string.content_serial_unknown),
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.75f),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            if (extraBadgeText != null) {
                                Surface(
                                    shape = neonShape(6.dp),
                                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.2f)
                                ) {
                                    Text(
                                        text = extraBadgeText,
                                        style = MaterialTheme.typography.labelSmall,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.primary,
                                        modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
                                    )
                                }
                            }
                        }
                    }
                } else {
                    Icon(
                        imageVector = Icons.Rounded.SportsEsports,
                        contentDescription = null,
                        modifier = Modifier.size(28.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(Modifier.width(12.dp))
                    Text(
                        text = stringResource(R.string.game_picker_tap_to_select),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f)
                    )
                }
                Spacer(Modifier.width(8.dp))
                Surface(
                    shape = neonShape(12.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f)
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Search,
                            contentDescription = stringResource(R.string.game_picker_change_game),
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(16.dp)
                        )
                        Text(
                            text = stringResource(R.string.game_picker_change_game),
                            style = MaterialTheme.typography.labelSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
            }
        }
    }

    // Full-screen search & filter dialog
    GamePickerSheet(
        visible = showPicker,
        games = games,
        selectedPath = selectedPath,
        onSelected = onSelected,
        onDismiss = { showPicker = false },
        extraBadge = extraBadge
    )
}

private fun Modifier.managerGameSelectorFullBleed(horizontalPadding: Dp): Modifier {
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

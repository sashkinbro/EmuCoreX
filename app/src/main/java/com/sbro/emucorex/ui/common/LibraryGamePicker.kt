package com.sbro.emucorex.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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

@Composable
fun LibraryGamePicker(
    games: List<GameItem>,
    selectedPath: String?,
    onSelected: (GameItem) -> Unit,
    modifier: Modifier = Modifier,
    horizontalContentPadding: Dp = 0.dp,
    fullBleedPadding: Dp = 0.dp
) {
    val selectedGame = games.firstOrNull { it.path == selectedPath }
    Column(
        modifier = modifier
            .libraryPickerFullBleed(fullBleedPadding)
            .fillMaxWidth()
    ) {
        Text(
            text = selectedGame?.let { game ->
                stringResource(R.string.content_select_game_named, game.title)
            } ?: stringResource(R.string.content_select_game),
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            minLines = 2,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = horizontalContentPadding)
        )
        Spacer(Modifier.height(10.dp))
        if (games.isEmpty()) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = horizontalContentPadding),
                shape = RoundedCornerShape(18.dp),
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
        LazyRow(
            contentPadding = PaddingValues(horizontal = horizontalContentPadding),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            items(games, key = GameItem::path) { game ->
                val selected = game.path == selectedPath
                Surface(
                    modifier = Modifier
                        .width(236.dp)
                        .height(106.dp)
                        .clip(RoundedCornerShape(18.dp))
                        .clickable { onSelected(game) },
                    shape = RoundedCornerShape(18.dp),
                    color = if (selected) {
                        MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f)
                    } else {
                        MaterialTheme.colorScheme.surface
                    },
                    border = BorderStroke(
                        width = if (selected) 2.dp else 1.dp,
                        color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant
                    )
                ) {
                    Row(modifier = Modifier.padding(10.dp)) {
                        GameCoverArt(
                            coverPath = game.coverArtPath,
                            fallbackTitle = game.title,
                            contentScale = ContentScale.Crop,
                            modifier = Modifier
                                .size(width = 58.dp, height = 82.dp)
                                .clip(RoundedCornerShape(12.dp))
                        )
                        Spacer(Modifier.width(12.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = game.title,
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold,
                                minLines = 2,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                            Spacer(Modifier.height(6.dp))
                            Text(
                                text = game.serial ?: stringResource(R.string.content_serial_unknown),
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }
            }
        }
    }
}

private fun Modifier.libraryPickerFullBleed(horizontalPadding: Dp): Modifier {
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

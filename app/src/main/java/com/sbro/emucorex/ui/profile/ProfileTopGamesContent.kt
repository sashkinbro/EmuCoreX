package com.sbro.emucorex.ui.profile

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Devices
import androidx.compose.material.icons.rounded.LocalFireDepartment
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Smartphone
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Update
import com.sbro.emucorex.ui.theme.style.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import com.sbro.emucorex.ui.theme.style.Surface
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameDeviceStat
import com.sbro.emucorex.data.GameTopPeriod
import com.sbro.emucorex.data.PlayerGamePlayStat
import com.sbro.emucorex.data.ProfileDeviceInfoProvider
import com.sbro.emucorex.data.TopGameStat
import com.sbro.emucorex.data.normalizeGameDeviceKey
import com.sbro.emucorex.ui.common.AppAlertDialog
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.skipGamepadTextFieldFocus
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import java.text.NumberFormat
import java.util.Locale

internal fun sortedTopGames(
    entries: List<TopGameStat>,
    sort: TopGamesSort,
    query: String
): List<TopGameStat> {
    val trimmed = query.trim()
    val filtered = if (trimmed.isBlank()) {
        entries
    } else {
        entries.filter { game ->
            game.title.contains(trimmed, ignoreCase = true) ||
                game.serial?.contains(trimmed, ignoreCase = true) == true
        }
    }
    return when (sort) {
        TopGamesSort.PlayTime -> filtered.sortedByDescending { it.totalPlayTimeMs }
        TopGamesSort.Players -> filtered.sortedWith(
            compareByDescending<TopGameStat> { it.players }.thenByDescending { it.totalPlayTimeMs }
        )
        TopGamesSort.Sessions -> filtered.sortedWith(
            compareByDescending<TopGameStat> { it.sessions }.thenByDescending { it.totalPlayTimeMs }
        )
        TopGamesSort.Recent -> filtered.sortedWith(
            compareByDescending<TopGameStat> { it.lastPlayedAtMs ?: 0L }.thenByDescending { it.totalPlayTimeMs }
        )
    }
}

@Composable
internal fun ProfileTopGamesHeader(
    period: GameTopPeriod,
    totalGames: Int,
    totalPlayTimeMs: Long,
    totalPlayers: Int,
    updatedAtMs: Long,
    isLoading: Boolean,
    query: String,
    onPeriodChange: (GameTopPeriod) -> Unit,
    onQueryChange: (String) -> Unit,
    onRefresh: () -> Unit
) {
    var searchVisible by rememberSaveable { mutableStateOf(query.isNotBlank()) }
    val locale = LocalConfiguration.current.locales[0]
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Icon(
                    imageVector = Icons.Rounded.LocalFireDepartment,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(28.dp)
                )
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    Text(
                        text = stringResource(R.string.profile_top_games_title),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = topGamesUpdatedAtLabel(updatedAtMs),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                IconButton(
                    onClick = {
                        if (searchVisible) onQueryChange("")
                        searchVisible = !searchVisible
                    }
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Search,
                        contentDescription = stringResource(R.string.profile_top_games_search_hint),
                        tint = if (searchVisible) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        }
                    )
                }
                IconButton(onClick = onRefresh, enabled = !isLoading) {
                    if (isLoading) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            strokeWidth = 2.dp
                        )
                    } else {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = stringResource(R.string.profile_top_games_refresh),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            AnimatedVisibility(
                visible = searchVisible,
                enter = fadeIn(animationSpec = tween(180)) +
                    expandVertically(animationSpec = tween(220), expandFrom = Alignment.Top),
                exit = fadeOut(animationSpec = tween(140)) +
                    shrinkVertically(animationSpec = tween(180), shrinkTowards = Alignment.Top)
            ) {
                OutlinedTextField(
                    value = query,
                    onValueChange = onQueryChange,
                    modifier = Modifier
                        .fillMaxWidth()
                        .skipGamepadTextFieldFocus(),
                    placeholder = { Text(text = stringResource(R.string.profile_top_games_search_hint)) },
                    leadingIcon = {
                        Icon(imageVector = Icons.Rounded.Search, contentDescription = null)
                    },
                    trailingIcon = {
                        if (query.isNotBlank()) {
                            IconButton(onClick = { onQueryChange("") }) {
                                Icon(
                                    imageVector = Icons.Rounded.Close,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp)
                                )
                            }
                        }
                    },
                    singleLine = true,
                    shape = neonShape(20.dp),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f),
                        unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f),
                        focusedContainerColor = MaterialTheme.colorScheme.surface,
                        unfocusedContainerColor = MaterialTheme.colorScheme.surface
                    )
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GameTopPeriod.entries.forEach { option ->
                    PeriodChip(
                        label = stringResource(periodLabelRes(option)),
                        selected = option == period,
                        modifier = Modifier.weight(1f),
                        onClick = { onPeriodChange(option) }
                    )
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TopGamesTotal(
                    value = formatCompactCount(totalGames.toLong(), locale),
                    label = stringResource(R.string.profile_top_games_label_games),
                    modifier = Modifier.weight(1f)
                )
                TopGamesTotal(
                    value = formatTopDuration(totalPlayTimeMs),
                    label = stringResource(R.string.profile_top_games_label_hours),
                    modifier = Modifier.weight(1f)
                )
                TopGamesTotal(
                    value = formatCompactCount(totalPlayers.toLong(), locale),
                    label = stringResource(R.string.profile_top_games_label_players),
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }
}

@Composable
internal fun TopGamesSortRow(
    sort: TopGamesSort,
    onSortChange: (TopGamesSort) -> Unit
) {
    LazyRow(
        modifier = Modifier
            .fillMaxWidth()
            .fullBleedHorizontal(ScreenHorizontalPadding),
        contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(items = TopGamesSort.entries, key = { it.name }) { option ->
            SortChip(
                label = stringResource(sortLabelRes(option)),
                icon = sortIcon(option),
                selected = option == sort,
                onClick = { onSortChange(option) }
            )
        }
    }
}

/**
 * Measures the content at the full screen width but reports the original slot size,
 * so a row inside a horizontally padded parent can scroll edge-to-edge without
 * clipping at the parent content padding. First/last insets stay on the content.
 */
private fun Modifier.fullBleedHorizontal(inset: Dp): Modifier = layout { measurable, constraints ->
    val insetPx = inset.roundToPx()
    val fullWidth = constraints.maxWidth + insetPx * 2
    val placeable = measurable.measure(
        constraints.copy(minWidth = fullWidth, maxWidth = fullWidth)
    )
    layout(constraints.maxWidth, placeable.height) {
        placeable.place(-insetPx, 0)
    }
}

@Composable
internal fun TopGameStatRow(
    game: TopGameStat,
    rank: Int,
    ownStat: PlayerGamePlayStat?,
    onDevicesClick: () -> Unit,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = profileCardBorder(alpha = 0.48f)
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            GameCoverArt(
                coverPath = game.coverUrl,
                fallbackCoverPath = game.fallbackCoverUrl,
                fallbackTitle = game.title,
                showTitleWhileLoading = false,
                shimmerWhileLoading = false,
                decodeWidth = 300,
                decodeHeight = 450,
                modifier = Modifier
                    .size(width = 84.dp, height = 128.dp)
                    .clip(neonShape(6.dp)),
                contentScale = ContentScale.Crop
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .height(128.dp),
                verticalArrangement = Arrangement.SpaceBetween
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Row(
                        verticalAlignment = Alignment.Top,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(
                            text = "#$rank",
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = topGameRankColor(rank)
                        )
                        Text(
                            text = game.title,
                            modifier = Modifier.weight(1f),
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        if (game.players > 0) {
                            TopGameMetaPill(
                                icon = Icons.Rounded.Person,
                                text = stringResource(
                                    R.string.profile_top_games_players_format,
                                    formatCompactCount(game.players.toLong(), LocalConfiguration.current.locales[0])
                                )
                            )
                        }
                        if (game.sessions > 0) {
                            TopGameMetaPill(
                                icon = Icons.Rounded.SportsEsports,
                                text = stringResource(R.string.profile_game_sessions_format, game.sessions)
                            )
                        }
                    }
                    ownStat?.let { own ->
                        Text(
                            text = stringResource(
                                R.string.profile_top_games_your_time_format,
                                formatTopDuration(own.totalPlayTimeMs)
                            ),
                            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TopGameDevicesBadge(onClick = onDevicesClick)
                    TopGameTimeBadge(text = formatTopDuration(game.totalPlayTimeMs))
                }
            }
        }
    }
}

@Composable
internal fun TopGameStatSkeletonRow() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = profileCardBorder(alpha = 0.48f)
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            SkeletonBlock(
                modifier = Modifier
                    .size(width = 84.dp, height = 128.dp)
                    .clip(neonShape(6.dp))
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .height(128.dp),
                verticalArrangement = Arrangement.SpaceBetween
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.94f)
                            .height(20.dp)
                            .clip(neonShape(8.dp))
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.7f)
                            .height(20.dp)
                            .clip(neonShape(8.dp))
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        SkeletonBlock(
                            modifier = Modifier
                                .width(72.dp)
                                .height(22.dp)
                                .clip(neonShape(999.dp))
                        )
                        SkeletonBlock(
                            modifier = Modifier
                                .width(64.dp)
                                .height(22.dp)
                                .clip(neonShape(999.dp))
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    SkeletonBlock(
                        modifier = Modifier
                            .width(86.dp)
                            .height(26.dp)
                            .clip(neonShape(999.dp))
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .width(66.dp)
                            .height(28.dp)
                            .clip(neonShape(999.dp))
                    )
                }
            }
        }
    }
}

@Composable
private fun PeriodChip(
    label: String,
    selected: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        modifier = modifier,
        colors = hubChipColors(),
        label = {
            Text(
                text = label,
                modifier = Modifier.fillMaxWidth(),
                textAlign = TextAlign.Center,
                style = MaterialTheme.typography.labelLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    )
}

@Composable
private fun SortChip(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    onClick: () -> Unit
) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        colors = hubChipColors(),
        label = {
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                maxLines = 1
            )
        },
        leadingIcon = {
            Icon(
                imageVector = icon,
                contentDescription = null,
                modifier = Modifier.size(16.dp)
            )
        }
    )
}

@Composable
private fun hubChipColors() = FilterChipDefaults.filterChipColors(
    containerColor = Color.Transparent,
    labelColor = MaterialTheme.colorScheme.onSurfaceVariant,
    iconColor = MaterialTheme.colorScheme.primary,
    selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
    selectedLabelColor = MaterialTheme.colorScheme.onPrimaryContainer,
    selectedLeadingIconColor = MaterialTheme.colorScheme.onPrimaryContainer
)

@Composable
private fun TopGamesTotal(
    value: String,
    label: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.36f),
        border = profileCardBorder(alpha = 0.34f)
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = value,
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun topGameRankColor(rank: Int): Color = when (rank) {
    1 -> Color(0xFFFFC857)
    2 -> Color(0xFFB6C2D9)
    3 -> Color(0xFFD89C64)
    else -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
}

@Composable
internal fun TopGameMetaPill(icon: ImageVector, text: String) {
    Surface(
        shape = neonShape(999.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(15.dp)
            )
            Text(
                text = text,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
internal fun TopGameTimeBadge(text: String, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = neonShape(999.dp),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.35f))
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
private fun TopGameDevicesBadge(onClick: () -> Unit, modifier: Modifier = Modifier) {
    Surface(
        onClick = onClick,
        modifier = modifier,
        shape = neonShape(999.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.Devices,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(14.dp)
            )
            Text(
                text = stringResource(R.string.profile_game_devices_button),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
internal fun TopGameDevicesDialog(
    game: TopGameStat,
    stats: List<GameDeviceStat>,
    isLoading: Boolean,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val currentDeviceKey = remember {
        normalizeGameDeviceKey(ProfileDeviceInfoProvider.current(context).soc)
    }
    AppAlertDialog(
        onDismissRequest = onDismiss,
        showEyebrow = false,
        showIconContainer = false,
        icon = {
            Icon(
                imageVector = Icons.Rounded.Devices,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(40.dp)
            )
        },
        title = {
            Text(
                text = stringResource(R.string.profile_game_devices_title),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    GameCoverArt(
                        coverPath = game.coverUrl,
                        fallbackCoverPath = game.fallbackCoverUrl,
                        fallbackTitle = game.title,
                        showTitleWhileLoading = false,
                        shimmerWhileLoading = false,
                        decodeWidth = 200,
                        decodeHeight = 300,
                        modifier = Modifier
                            .size(width = 54.dp, height = 76.dp)
                            .clip(neonShape(6.dp)),
                        contentScale = ContentScale.Crop
                    )
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = game.title,
                            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = listOf(
                                formatTopDuration(game.totalPlayTimeMs),
                                stringResource(
                                    R.string.profile_top_games_players_format,
                                    formatCompactCount(game.players.toLong(), LocalConfiguration.current.locales[0])
                                ),
                                stringResource(R.string.profile_game_sessions_format, game.sessions)
                            ).joinToString(" · "),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                when {
                    isLoading -> Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        repeat(3) {
                            SkeletonBlock(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(112.dp)
                                    .clip(neonShape(16.dp))
                            )
                        }
                    }
                    stats.isEmpty() -> Text(
                        text = stringResource(R.string.profile_game_devices_empty),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    else -> Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        val maxPlayTimeMs = stats.maxOf { it.totalPlayTimeMs }.coerceAtLeast(1L)
                        stats.take(5).forEach { stat ->
                            DeviceStatRow(
                                stat = stat,
                                maxPlayTimeMs = maxPlayTimeMs,
                                isCurrent = stat.deviceKey == currentDeviceKey
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            Button(shape = neonButtonShape(), onClick = onDismiss) {
                Text(text = stringResource(R.string.profile_done))
            }
        }
    )
}

@Composable
private fun DeviceStatRow(
    stat: GameDeviceStat,
    maxPlayTimeMs: Long,
    isCurrent: Boolean
) {
    val share = (stat.totalPlayTimeMs.toFloat() / maxPlayTimeMs.toFloat()).coerceIn(0.04f, 1f)
    Surface(
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        border = profileCardBorder(alpha = 0.3f)
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(9.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Surface(
                    shape = neonShape(10.dp),
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                ) {
                    Box(
                        modifier = Modifier.size(38.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Smartphone,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(20.dp)
                        )
                    }
                }
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(3.dp)
                ) {
                    Text(
                        text = stat.soc.ifBlank { stat.deviceKey },
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    val details = listOfNotNull(
                        stat.gpu?.takeIf { it.isNotBlank() },
                        if (stat.ramMb > 0) {
                            stringResource(R.string.profile_game_devices_ram_format, stat.ramMb / 1024)
                        } else {
                            null
                        }
                    ).joinToString(" · ")
                    if (details.isNotBlank()) {
                        Text(
                            text = details,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (isCurrent) {
                    Surface(
                        shape = neonShape(999.dp),
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
                    ) {
                        Text(
                            text = stringResource(R.string.profile_game_devices_your_device),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
                            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.primary,
                            maxLines = 1
                        )
                    }
                }
                Row(
                    modifier = Modifier.weight(1f),
                    horizontalArrangement = Arrangement.End,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = formatTopDuration(stat.totalPlayTimeMs),
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.primary,
                        maxLines = 1
                    )
                    Text(
                        text = stringResource(R.string.profile_game_sessions_format, stat.sessions),
                        modifier = Modifier
                            .padding(start = 8.dp)
                            .weight(1f, fill = false),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(6.dp)
                    .clip(neonShape(3.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth(share)
                        .fillMaxHeight()
                        .clip(neonShape(3.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.65f))
                )
            }
        }
    }
}

@Composable
private fun periodLabelRes(period: GameTopPeriod): Int = when (period) {
    GameTopPeriod.AllTime -> R.string.profile_top_games_period_all
    GameTopPeriod.Week -> R.string.profile_top_games_period_week
    GameTopPeriod.Month -> R.string.profile_top_games_period_month
}

private fun sortLabelRes(sort: TopGamesSort): Int = when (sort) {
    TopGamesSort.PlayTime -> R.string.profile_top_games_sort_time
    TopGamesSort.Players -> R.string.profile_top_games_sort_players
    TopGamesSort.Sessions -> R.string.profile_top_games_sort_sessions
    TopGamesSort.Recent -> R.string.profile_top_games_sort_recent
}

private fun sortIcon(sort: TopGamesSort): ImageVector = when (sort) {
    TopGamesSort.PlayTime -> Icons.Rounded.Schedule
    TopGamesSort.Players -> Icons.Rounded.Person
    TopGamesSort.Sessions -> Icons.Rounded.SportsEsports
    TopGamesSort.Recent -> Icons.Rounded.Update
}

@Composable
private fun topGamesUpdatedAtLabel(updatedAtMs: Long): String {
    if (updatedAtMs <= 0L) return stringResource(R.string.profile_top_games_updated_just_now)
    val elapsedMs = (System.currentTimeMillis() - updatedAtMs).coerceAtLeast(0L)
    val minutes = elapsedMs / 60_000L
    val hours = elapsedMs / 3_600_000L
    val days = elapsedMs / 86_400_000L
    return when {
        minutes < 1L -> stringResource(R.string.profile_top_games_updated_just_now)
        minutes < 60L -> stringResource(R.string.profile_top_games_updated_minutes_format, minutes.toInt())
        hours < 24L -> stringResource(R.string.profile_top_games_updated_hours_format, hours.toInt())
        else -> stringResource(R.string.profile_top_games_updated_days_format, days.toInt())
    }
}

@Composable
internal fun formatTopDuration(durationMs: Long): String {
    val totalMinutes = (durationMs / 60_000L).coerceAtLeast(0L)
    val hours = totalMinutes / 60L
    val minutes = totalMinutes % 60L
    val locale = LocalConfiguration.current.locales[0]
    return when {
        hours >= COMPACT_NUMBER_THRESHOLD ->
            stringResource(R.string.profile_duration_hours_compact_format, formatCompactCount(hours, locale))
        hours >= 100L -> stringResource(R.string.profile_duration_hours_format, hours)
        hours > 0L -> stringResource(R.string.profile_duration_hours_minutes_format, hours, minutes)
        else -> stringResource(R.string.profile_duration_minutes_format, minutes)
    }
}

private const val COMPACT_NUMBER_THRESHOLD = 10_000L

private val integerFormats = HashMap<Locale, NumberFormat>()

private fun integerFormat(locale: Locale): NumberFormat = synchronized(integerFormats) {
    integerFormats.getOrPut(locale) { NumberFormat.getIntegerInstance(locale) }
}

private fun formatCompactCount(value: Long, locale: Locale): String {
    if (value < COMPACT_NUMBER_THRESHOLD) {
        return integerFormat(locale).format(value)
    }
    return if (value >= 1_000_000L) {
        "${formatCompactDecimal(value / 1_000_000.0, locale)}M"
    } else {
        "${formatCompactDecimal(value / 1_000.0, locale)}K"
    }
}

private fun formatCompactDecimal(value: Double, locale: Locale): String {
    return String.format(locale, "%.1f", value)
        .trimEnd('0')
        .trimEnd('.', ',')
}

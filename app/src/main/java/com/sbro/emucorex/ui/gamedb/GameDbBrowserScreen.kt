package com.sbro.emucorex.ui.gamedb

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.skipGamepadTextFieldFocus
import com.sbro.emucorex.ui.common.tvFocusGroup
import com.sbro.emucorex.ui.common.tvGamepadFocusableCard
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import androidx.compose.runtime.snapshotFlow

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun GameDbBrowserScreen(
    initialQuery: String? = null,
    onBackClick: () -> Unit,
    viewModel: GameDbBrowserViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val queryText by viewModel.queryText.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 14.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val guardedBackClick = rememberDebouncedClick(onClick = onBackClick)
    val gridState = rememberLazyGridState()
    var visibleGameSerials by remember { mutableStateOf(emptySet<String>()) }

    LaunchedEffect(initialQuery) {
        initialQuery?.takeIf { it.isNotBlank() }?.let(viewModel::openQuery)
    }

    LaunchedEffect(gridState) {
        snapshotFlow { gridState.layoutInfo.visibleItemsInfo }
            .map { items -> items.mapNotNull { it.key as? String }.toSet() }
            .distinctUntilChanged()
            .collect { visibleGameSerials = it }
    }

    LazyVerticalGrid(
        columns = GridCells.Adaptive(minSize = 320.dp),
        state = gridState,
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            top = topInset,
            end = ScreenHorizontalPadding,
            bottom = 32.dp + bottomInset
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp),
        horizontalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item(span = { GridItemSpan(maxLineSpan) }) {
            ScreenTopBar(
                title = stringResource(R.string.gamedb_browser_title),
                subtitle = if (uiState.loading) {
                    stringResource(R.string.gamedb_browser_loading)
                } else {
                    stringResource(R.string.gamedb_browser_subtitle, uiState.totalCount)
                },
                onBackClick = guardedBackClick
            )
        }

        item(span = { GridItemSpan(maxLineSpan) }) {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(
                    value = queryText,
                    onValueChange = viewModel::setQuery,
                    modifier = Modifier
                        .fillMaxWidth()
                        .skipGamepadTextFieldFocus(),
                    singleLine = true,
                    leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                    trailingIcon = {
                        if (queryText.isNotBlank()) {
                            IconButton(onClick = { viewModel.setQuery("") }) {
                                Icon(
                                    Icons.Rounded.Close,
                                    contentDescription = stringResource(R.string.gamedb_browser_clear_search)
                                )
                            }
                        }
                    },
                    placeholder = { Text(stringResource(R.string.gamedb_browser_search_hint)) },
                    shape = RoundedCornerShape(18.dp)
                )
                FlowRow(
                    modifier = Modifier.tvFocusGroup(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    GameDbFilter.entries.forEach { filter ->
                        val interactionSource = remember { MutableInteractionSource() }
                        FilterChip(
                            modifier = Modifier.tvGamepadFocusableCard(
                                shape = RoundedCornerShape(16.dp),
                                interactionSource = interactionSource,
                                addFocusTarget = false
                            ),
                            selected = uiState.filter == filter,
                            onClick = { viewModel.setFilter(filter) },
                            interactionSource = interactionSource,
                            label = { Text(filter.label()) },
                            leadingIcon = if (uiState.filter == filter) {
                                { Icon(Icons.Rounded.Tune, contentDescription = null, modifier = Modifier.size(16.dp)) }
                            } else null
                        )
                    }
                }
            }
        }

        when {
            uiState.loading -> item(span = { GridItemSpan(maxLineSpan) }) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(180.dp),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            }

            uiState.loadFailed -> item(span = { GridItemSpan(maxLineSpan) }) {
                GameDbMessageCard(
                    title = stringResource(R.string.gamedb_browser_load_failed),
                    body = stringResource(R.string.gamedb_browser_load_failed_desc)
                )
            }

            uiState.entries.isEmpty() -> item(span = { GridItemSpan(maxLineSpan) }) {
                GameDbMessageCard(
                    title = stringResource(R.string.gamedb_browser_no_results),
                    body = stringResource(R.string.gamedb_browser_no_results_desc)
                )
            }

            else -> items(uiState.entries, key = { it.serial }) { entry ->
                GameDbEntryCard(
                    entry = entry,
                    loadCover = entry.serial in visibleGameSerials
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun GameDbEntryCard(entry: GameDbCatalogEntry, loadCover: Boolean) {
    var expanded by rememberSaveable(entry.serial) { mutableStateOf(false) }
    val cardInteractionSource = remember { MutableInteractionSource() }
    val cardShape = RoundedCornerShape(24.dp)
    val chevronRotation by animateFloatAsState(
        targetValue = if (expanded) 180f else 0f,
        animationSpec = spring(stiffness = 500f),
        label = "gameDbChevron"
    )
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(
                shape = cardShape,
                interactionSource = cardInteractionSource,
                addFocusTarget = false
            )
            .clickable(
                interactionSource = cardInteractionSource,
                indication = null,
                onClick = { expanded = !expanded }
            )
            .animateContentSize(animationSpec = spring(stiffness = 420f)),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        shadowElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.46f))
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                GameCoverArt(
                    coverPath = entry.coverUrl,
                    fallbackTitle = entry.title,
                    loadEnabled = loadCover,
                    contentScale = ContentScale.Crop,
                    modifier = Modifier
                        .width(88.dp)
                        .height(126.dp)
                        .clip(RoundedCornerShape(16.dp))
                )
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(7.dp)
                ) {
                    Text(
                        text = entry.title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = listOfNotNull(entry.serial, entry.region).joinToString(" • "),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        if (entry.coreSettingCount > 0) {
                            GameDbCountBadge(
                                text = stringResource(R.string.gamedb_browser_core_count, entry.coreSettingCount),
                                accent = true
                            )
                        }
                        if (entry.graphicsSettingCount > 0) {
                            GameDbCountBadge(
                                text = stringResource(R.string.gamedb_browser_graphics_count, entry.graphicsSettingCount),
                                accent = false
                            )
                        }
                    }
                    Spacer(modifier = Modifier.weight(1f))
                    Row(
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = stringResource(
                                if (expanded) R.string.gamedb_browser_show_less else R.string.gamedb_browser_show_details
                            ),
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.weight(1f)
                        )
                        Icon(
                            imageVector = Icons.Rounded.ExpandMore,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.rotate(chevronRotation)
                        )
                    }
                }
            }

            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(animationSpec = spring(stiffness = 420f)) + fadeIn(),
                exit = shrinkVertically(animationSpec = spring(stiffness = 520f)) + fadeOut()
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    entry.settings.groupBy { it.category }.forEach { (category, settings) ->
                        Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
                            Text(
                                text = category.label(),
                                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                color = if (category == GameDbSettingCategory.GRAPHICS_FIX) {
                                    MaterialTheme.colorScheme.tertiary
                                } else {
                                    MaterialTheme.colorScheme.primary
                                }
                            )
                            settings.forEach { setting -> GameDbSettingRow(setting) }
                        }
                    }
                }
            }
            if (!expanded) {
                entry.settings.take(2).forEach { setting -> GameDbSettingRow(setting, compact = true) }
            }
        }
    }
}

@Composable
private fun GameDbSettingRow(setting: GameDbSettingItem, compact: Boolean = false) {
    val presentation = GameDbSettingPresentationMapper.map(setting)
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.62f),
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
        ),
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(5.dp)
        ) {
            Text(
                text = presentation.title.resolve(),
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                modifier = Modifier.fillMaxWidth(),
                maxLines = if (compact) 1 else 2,
                overflow = TextOverflow.Ellipsis
            )
            if (!compact) {
                Text(
                    text = stringResource(
                        if (setting.controlledByGameFixesToggle) {
                            R.string.gamedb_browser_controlled_by_toggle
                        } else {
                            R.string.gamedb_browser_graphics_always_applied
                        }
                    ),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.78f)
                )
            }
            GameDbValueBadge(
                text = presentation.value.resolve(),
                graphics = !setting.controlledByGameFixesToggle,
                modifier = Modifier.align(Alignment.End)
            )
        }
    }
}

@Composable
private fun GameDbValueBadge(
    text: String,
    graphics: Boolean,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.widthIn(max = 260.dp),
        shape = RoundedCornerShape(999.dp),
        color = if (graphics) {
            MaterialTheme.colorScheme.tertiaryContainer
        } else {
            MaterialTheme.colorScheme.primaryContainer
        },
        border = BorderStroke(
            1.dp,
            if (graphics) {
                MaterialTheme.colorScheme.tertiary.copy(alpha = 0.18f)
            } else {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
            }
        )
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 11.dp, vertical = 6.dp),
            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
            color = if (graphics) {
                MaterialTheme.colorScheme.onTertiaryContainer
            } else {
                MaterialTheme.colorScheme.onPrimaryContainer
            },
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun GameDbCountBadge(text: String, accent: Boolean) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = if (accent) {
            MaterialTheme.colorScheme.primaryContainer
        } else {
            MaterialTheme.colorScheme.tertiaryContainer
        }
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 5.dp),
            style = MaterialTheme.typography.labelSmall,
            color = if (accent) {
                MaterialTheme.colorScheme.onPrimaryContainer
            } else {
                MaterialTheme.colorScheme.onTertiaryContainer
            }
        )
    }
}

@Composable
private fun GameDbMessageCard(title: String, body: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.42f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(48.dp)
                    .clip(RoundedCornerShape(15.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    Icons.Rounded.Image,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(24.dp)
                )
            }
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            Text(
                text = body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.widthIn(max = 360.dp)
            )
        }
    }
}

@Composable
private fun GameDbFilter.label(): String = stringResource(
    when (this) {
        GameDbFilter.ALL -> R.string.gamedb_browser_filter_all
        GameDbFilter.CORE -> R.string.gamedb_browser_filter_core
        GameDbFilter.GRAPHICS -> R.string.gamedb_browser_filter_graphics
    }
)

@Composable
private fun GameDbSettingCategory.label(): String = stringResource(
    when (this) {
        GameDbSettingCategory.CORE_FIX -> R.string.gamedb_browser_category_core
        GameDbSettingCategory.ROUND_MODE -> R.string.gamedb_browser_category_round
        GameDbSettingCategory.CLAMP_MODE -> R.string.gamedb_browser_category_clamp
        GameDbSettingCategory.SPEED_HACK -> R.string.gamedb_browser_category_speedhack
        GameDbSettingCategory.GRAPHICS_FIX -> R.string.gamedb_browser_category_graphics
    }
)

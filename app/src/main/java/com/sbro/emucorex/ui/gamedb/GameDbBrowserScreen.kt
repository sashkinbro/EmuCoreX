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
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import android.widget.Toast
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.data.gamedb.GameDbFixId
import com.sbro.emucorex.data.gamedb.GameDbFixSection
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.category
import com.sbro.emucorex.data.gamedb.section
import com.sbro.emucorex.ui.common.AppAlertDialog
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.SettingsStyledDialog
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.common.gameCoverShape
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.premiumFilterChipColors
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.skipGamepadTextFieldFocus
import com.sbro.emucorex.ui.common.tvFocusGroup
import com.sbro.emucorex.ui.common.tvGamepadFocusableCard
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.neon.neonShape
import java.util.Locale
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map

private data class GameDbAddFixTarget(
    val serial: String,
    val title: String,
    val section: GameDbFixSection,
    val existing: Set<String>
)

private data class GameDbValueTarget(
    val serial: String,
    val title: String,
    val setting: GameDbSettingUi
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun GameDbBrowserScreen(
    initialQuery: String? = null,
    onBackClick: () -> Unit,
    viewModel: GameDbBrowserViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val queryText by viewModel.queryText.collectAsState()
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val guardedBackClick = rememberDebouncedClick(onClick = onBackClick)
    val gridState = rememberLazyGridState()
    val context = LocalContext.current
    val resetToast = stringResource(R.string.gamedb_browser_reset_game_toast)
    var visibleGameSerials by remember { mutableStateOf(emptySet<String>()) }
    var expandedSerial by rememberSaveable { mutableStateOf<String?>(null) }
    var addFixTarget by remember { mutableStateOf<GameDbAddFixTarget?>(null) }
    var valueTarget by remember { mutableStateOf<GameDbValueTarget?>(null) }
    var showResetAllDialog by remember { mutableStateOf(false) }

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
        // A lone result must not sit in half of a wide screen, so it takes the full row.
        columns = if (uiState.entries.size == 1) {
            GridCells.Fixed(1)
        } else {
            GridCells.Adaptive(minSize = 320.dp)
        },
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
                    shape = neonShape(18.dp)
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
                                shape = neonShape(16.dp),
                                interactionSource = interactionSource,
                                addFocusTarget = false
                            ),
                            selected = uiState.filter == filter,
                            onClick = { viewModel.setFilter(filter) },
                            interactionSource = interactionSource,
                            colors = premiumFilterChipColors(),
                            label = { Text(filter.label()) },
                            leadingIcon = if (uiState.filter == filter) {
                                { Icon(Icons.Rounded.Tune, contentDescription = null, modifier = Modifier.size(16.dp)) }
                            } else null
                        )
                    }
                }
                if (uiState.customizedGames > 0) {
                    GameDbCustomizedSummaryRow(
                        customizedGames = uiState.customizedGames,
                        onResetAll = { showResetAllDialog = true }
                    )
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

            else -> items(
                items = uiState.entries,
                key = { it.entry.serial },
                // The expanded card owns the whole row so the detail view never looks squeezed into
                // one column next to an empty neighbour on landscape and tablet layouts.
                span = { entryState ->
                    if (entryState.entry.serial == expandedSerial) {
                        GridItemSpan(maxLineSpan)
                    } else {
                        GridItemSpan(1)
                    }
                }
            ) { entryState ->
                GameDbEntryCard(
                    state = entryState,
                    loadCover = entryState.entry.serial in visibleGameSerials,
                    expanded = entryState.entry.serial == expandedSerial,
                    onExpandedChange = { expanded ->
                        expandedSerial = if (expanded) entryState.entry.serial else null
                    },
                    coreFixesEnabled = uiState.coreFixesEnabled,
                    graphicsFixesEnabled = uiState.graphicsFixesEnabled,
                    onToggle = { setting, enabled ->
                        viewModel.toggleFix(entryState.entry.serial, setting.id, enabled)
                    },
                    onResetFix = { setting ->
                        viewModel.resetFix(entryState.entry.serial, setting.id)
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    },
                    onEditValue = { setting ->
                        valueTarget = GameDbValueTarget(entryState.entry.serial, entryState.entry.title, setting)
                    },
                    onAddFix = { category ->
                        addFixTarget = GameDbAddFixTarget(
                            serial = entryState.entry.serial,
                            title = entryState.entry.title,
                            section = category.section,
                            existing = entryState.settings.map { it.id }.toSet()
                        )
                    },
                    onResetGame = {
                        viewModel.resetGame(entryState.entry.serial)
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    }
                )
            }
        }
    }

    addFixTarget?.let { target ->
        GameDbAddFixDialog(
            target = target,
            onDismiss = { addFixTarget = null },
            onAdd = { fix ->
                viewModel.addFix(target.serial, GameDbFixId.of(target.section, fix.key), fix.defaultValue)
            }
        )
    }

    valueTarget?.let { target ->
        GameDbValuePickerDialog(
            target = target,
            onDismiss = { valueTarget = null },
            onSelect = { value ->
                viewModel.setFixValue(target.serial, target.setting.id, value)
            }
        )
    }

    if (showResetAllDialog) {
        AppAlertDialog(
            onDismissRequest = { showResetAllDialog = false },
            icon = {
                Icon(
                    imageVector = Icons.Rounded.RestartAlt,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
            },
            title = { Text(text = stringResource(R.string.gamedb_browser_reset_all)) },
            text = { Text(text = stringResource(R.string.gamedb_browser_reset_all_desc)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        showResetAllDialog = false
                        viewModel.resetAll()
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    }
                ) {
                    Text(text = stringResource(R.string.gamedb_browser_reset_game))
                }
            },
            dismissButton = {
                TextButton(onClick = { showResetAllDialog = false }) {
                    Text(text = stringResource(R.string.common_close))
                }
            }
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun GameDbEntryCard(
    state: GameDbEntryUi,
    loadCover: Boolean,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    coreFixesEnabled: Boolean,
    graphicsFixesEnabled: Boolean,
    onToggle: (GameDbSettingUi, Boolean) -> Unit,
    onResetFix: (GameDbSettingUi) -> Unit,
    onEditValue: (GameDbSettingUi) -> Unit,
    onAddFix: (GameDbSettingCategory) -> Unit,
    onResetGame: () -> Unit
) {
    val entry = state.entry
    val cardInteractionSource = remember { MutableInteractionSource() }
    val cardShape = neonShape(24.dp)
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
                onClick = { onExpandedChange(!expanded) }
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
                    // Pure layout: Fit draws the decoded bitmap 1:1 into the 2:3 frame, so square
                    // and 3D covers keep their corners instead of being cropped by the rounded clip.
                    contentScale = ContentScale.Fit,
                    modifier = Modifier
                        .width(88.dp)
                        .aspectRatio(2f / 3f)
                        .clip(gameCoverShape(entry.coverUrl, neonShape(16.dp)))
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
                        if (state.coreSettingCount > 0) {
                            GameDbCountBadge(
                                text = stringResource(R.string.gamedb_browser_core_count, state.coreSettingCount),
                                accent = true
                            )
                        }
                        if (state.graphicsSettingCount > 0) {
                            GameDbCountBadge(
                                text = stringResource(R.string.gamedb_browser_graphics_count, state.graphicsSettingCount),
                                accent = false
                            )
                        }
                        if (state.customized) {
                            GameDbTagBadge(
                                text = stringResource(R.string.gamedb_browser_customized),
                                container = MaterialTheme.colorScheme.primaryContainer,
                                content = MaterialTheme.colorScheme.onPrimaryContainer
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
                    if (state.customized) {
                        GameDbOverrideSummaryRow(
                            enabledCount = state.enabledCount,
                            totalCount = state.totalCount,
                            onReset = onResetGame
                        )
                    }
                    if (!coreFixesEnabled) {
                        GameDbInlineNote(stringResource(R.string.gamedb_browser_core_globally_off))
                    }
                    if (!graphicsFixesEnabled) {
                        GameDbInlineNote(stringResource(R.string.gamedb_browser_graphics_globally_off))
                    }
                    state.sections.forEach { section ->
                        Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
                            GameDbCategoryHeader(
                                category = section.category,
                                enabledCount = section.settings.count { it.enabled },
                                totalCount = section.settings.size,
                                canAdd = GameDbFixCatalog.supportsAdding(section.category.section),
                                onAdd = { onAddFix(section.category) }
                            )
                            section.settings.forEach { setting ->
                                GameDbSettingRow(
                                    setting = setting,
                                    compact = false,
                                    onToggle = { onToggle(setting, !setting.enabled) },
                                    onReset = { onResetFix(setting) },
                                    onEditValue = { onEditValue(setting) }
                                )
                            }
                        }
                    }
                    Text(
                        text = stringResource(R.string.gamedb_browser_apply_next_launch),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.78f)
                    )
                }
            }

            if (!expanded) {
                state.settings.take(2).forEach { setting ->
                    GameDbSettingRow(
                        setting = setting,
                        compact = true,
                        onToggle = null,
                        onReset = null,
                        onEditValue = null
                    )
                }
            }
        }
    }
}

@Composable
private fun GameDbCategoryHeader(
    category: GameDbSettingCategory,
    enabledCount: Int,
    totalCount: Int,
    canAdd: Boolean,
    onAdd: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(
            text = category.label(),
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = if (category == GameDbSettingCategory.GRAPHICS_FIX) {
                MaterialTheme.colorScheme.tertiary
            } else {
                MaterialTheme.colorScheme.primary
            },
            modifier = Modifier.weight(1f)
        )
        Text(
            text = "$enabledCount/$totalCount",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.78f)
        )
        if (canAdd) {
            GameDbAddButton(onClick = onAdd)
        }
    }
}

@Composable
private fun GameDbAddButton(onClick: () -> Unit) {
    Surface(
        modifier = Modifier
            .size(30.dp)
            .clip(CircleShape)
            .clickable(onClick = onClick),
        shape = CircleShape,
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
        contentColor = MaterialTheme.colorScheme.primary
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                imageVector = Icons.Rounded.Add,
                contentDescription = stringResource(R.string.gamedb_browser_add_fix),
                modifier = Modifier.size(18.dp)
            )
        }
    }
}

@Composable
private fun GameDbOverrideSummaryRow(
    enabledCount: Int,
    totalCount: Int,
    onReset: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(
            text = stringResource(R.string.gamedb_browser_fix_count, enabledCount, totalCount),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f)
        )
        TextButton(onClick = onReset) {
            Icon(
                imageVector = Icons.Rounded.RestartAlt,
                contentDescription = null,
                modifier = Modifier.size(16.dp)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(text = stringResource(R.string.gamedb_browser_reset_game))
        }
    }
}

@Composable
private fun GameDbCustomizedSummaryRow(
    customizedGames: Int,
    onResetAll: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(14.dp),
        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.24f))
    ) {
        Row(
            modifier = Modifier.padding(start = 14.dp, end = 6.dp, top = 3.dp, bottom = 3.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(R.string.gamedb_browser_customized_games, customizedGames),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onPrimaryContainer,
                modifier = Modifier.weight(1f)
            )
            TextButton(onClick = onResetAll) {
                Icon(
                    imageVector = Icons.Rounded.RestartAlt,
                    contentDescription = null,
                    modifier = Modifier.size(16.dp)
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(text = stringResource(R.string.gamedb_browser_reset_all))
            }
        }
    }
}

@Composable
private fun GameDbInlineNote(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(12.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 9.dp),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun GameDbSettingRow(
    setting: GameDbSettingUi,
    compact: Boolean,
    onToggle: (() -> Unit)?,
    onReset: (() -> Unit)?,
    onEditValue: (() -> Unit)?
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(14.dp)
    val enabled = setting.enabled
    val titleColor = if (enabled) {
        MaterialTheme.colorScheme.onSurface
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.62f)
    }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .then(
                if (compact || onToggle == null) {
                    Modifier
                } else {
                    Modifier
                        .gamepadFocusableCard(
                            shape = shape,
                            interactionSource = interactionSource,
                            addFocusTarget = true,
                            showIdleBorder = false
                        )
                        .combinedClickable(
                            interactionSource = interactionSource,
                            indication = null,
                            onClick = onToggle,
                            onLongClick = onReset
                        )
                }
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = if (enabled) 0.62f else 0.26f),
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = if (enabled) 0.72f else 0.34f)
        ),
        tonalElevation = 1.dp
    ) {
        if (compact) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = setting.presentation.title.resolve(),
                    style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                    color = titleColor,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f)
                )
                GameDbValueBadge(setting = setting, compact = true, onEditValue = null)
            }
        } else {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 12.dp, end = 6.dp, top = 9.dp, bottom = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = setting.presentation.title.resolve(),
                        style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = titleColor,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        GameDbValueBadge(
                            setting = setting,
                            compact = false,
                            onEditValue = onEditValue.takeIf { setting.valueOptions != null }
                        )
                        if (setting.added) {
                            GameDbTagBadge(
                                text = stringResource(R.string.gamedb_browser_fix_added),
                                container = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.85f),
                                content = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                    }
                }
                Switch(
                    checked = enabled,
                    onCheckedChange = null,
                    modifier = Modifier.padding(start = 2.dp)
                )
            }
        }
    }
}

@Composable
private fun GameDbValueBadge(
    setting: GameDbSettingUi,
    compact: Boolean,
    onEditValue: (() -> Unit)?
) {
    val graphics = setting.category == GameDbSettingCategory.GRAPHICS_FIX
    val enabled = setting.enabled
    val text = if (enabled) {
        setting.presentation.value.resolve()
    } else {
        stringResource(R.string.gamedb_browser_fix_off)
    }
    val container = when {
        !enabled -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
        graphics -> MaterialTheme.colorScheme.tertiaryContainer
        else -> MaterialTheme.colorScheme.primaryContainer
    }
    val content = when {
        !enabled -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
        graphics -> MaterialTheme.colorScheme.onTertiaryContainer
        else -> MaterialTheme.colorScheme.onPrimaryContainer
    }
    val modifier = if (onEditValue != null) {
        Modifier
            .clip(RoundedCornerShape(999.dp))
            .clickable(onClick = onEditValue)
    } else {
        Modifier
    }
    Surface(
        modifier = modifier.widthIn(max = if (compact) 128.dp else 210.dp),
        shape = RoundedCornerShape(999.dp),
        color = container,
        border = if (enabled) null else BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.8f)
        )
    ) {
        Row(
            modifier = Modifier.padding(
                start = 10.dp,
                end = if (onEditValue != null) 6.dp else 10.dp,
                top = 4.dp,
                bottom = 4.dp
            ),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(3.dp)
        ) {
            Text(
                text = text,
                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                color = content,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f, fill = false)
            )
            if (onEditValue != null) {
                Icon(
                    imageVector = Icons.Rounded.ExpandMore,
                    contentDescription = null,
                    tint = content.copy(alpha = 0.8f),
                    modifier = Modifier.size(14.dp)
                )
            }
        }
    }
}

@Composable
private fun GameDbTagBadge(
    text: String,
    container: androidx.compose.ui.graphics.Color,
    content: androidx.compose.ui.graphics.Color
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = container
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 4.dp),
            style = MaterialTheme.typography.labelSmall,
            color = content,
            maxLines = 1
        )
    }
}

@Composable
private fun GameDbAddFixDialog(
    target: GameDbAddFixTarget,
    onDismiss: () -> Unit,
    onAdd: (GameDbAddableFix) -> Unit
) {
    var search by rememberSaveable { mutableStateOf("") }
    val candidates = remember(target.section, target.existing) {
        GameDbFixCatalog.addable(target.section).filter {
            GameDbFixId.of(target.section, it.key) !in target.existing
        }
    }
    val labeled = candidates.map { fix ->
        fix to GameDbSettingPresentationMapper.map(
            com.sbro.emucorex.data.gamedb.GameDbSettingItem(
                category = target.section.category,
                key = fix.key,
                value = fix.defaultValue,
                note = null,
                controlledByGameFixesToggle = target.section != GameDbFixSection.GS_HW_FIXES
            )
        ).title.resolve()
    }
    val query = search.trim().lowercase(Locale.ROOT)
    val filtered = if (query.isBlank()) {
        labeled
    } else {
        labeled.filter { (fix, label) ->
            label.lowercase(Locale.ROOT).contains(query) || fix.key.lowercase(Locale.ROOT).contains(query)
        }
    }

    SettingsStyledDialog(
        title = stringResource(R.string.gamedb_browser_add_fix_title),
        eyebrow = target.title,
        icon = Icons.Rounded.Add,
        onDismissRequest = onDismiss
    ) {
        OutlinedTextField(
            value = search,
            onValueChange = { search = it },
            modifier = Modifier
                .fillMaxWidth()
                .skipGamepadTextFieldFocus(),
            singleLine = true,
            leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
            placeholder = { Text(stringResource(R.string.gamedb_browser_add_fix_search)) },
            shape = neonShape(16.dp)
        )
        if (filtered.isEmpty()) {
            Text(
                text = stringResource(R.string.gamedb_browser_add_fix_empty),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            filtered.forEach { (fix, label) ->
                GameDbChoiceRow(
                    label = label,
                    selected = false,
                    trailingIcon = Icons.Rounded.Add,
                    onClick = {
                        onAdd(fix)
                        onDismiss()
                    }
                )
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End
        ) {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(R.string.common_close))
            }
        }
    }
}

@Composable
private fun GameDbValuePickerDialog(
    target: GameDbValueTarget,
    onDismiss: () -> Unit,
    onSelect: (String?) -> Unit
) {
    val options = target.setting.valueOptions.orEmpty()
    SettingsStyledDialog(
        title = stringResource(R.string.gamedb_browser_value_picker_title),
        eyebrow = target.setting.presentation.title.resolve(),
        icon = Icons.Rounded.Tune,
        onDismissRequest = onDismiss
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEach { option ->
                GameDbChoiceRow(
                    label = stringResource(option.label),
                    selected = option.value.toString() == target.setting.value,
                    trailingIcon = null,
                    onClick = {
                        onSelect(option.value.toString())
                        onDismiss()
                    }
                )
            }
            // An added line has no bundled value to fall back to; long-press removes it instead.
            if (target.setting.valueEdited && !target.setting.added) {
                GameDbChoiceRow(
                    label = stringResource(R.string.gamedb_browser_value_use_gamedb),
                    selected = false,
                    trailingIcon = Icons.Rounded.RestartAlt,
                    onClick = {
                        onSelect(null)
                        onDismiss()
                    }
                )
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End
        ) {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(R.string.common_close))
            }
        }
    }
}

@Composable
private fun GameDbChoiceRow(
    label: String,
    selected: Boolean,
    trailingIcon: androidx.compose.ui.graphics.vector.ImageVector?,
    onClick: () -> Unit
) {
    val shape = neonShape(14.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clip(shape)
            .clickable(onClick = onClick),
        shape = shape,
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.9f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.45f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            }
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
                color = if (selected) {
                    MaterialTheme.colorScheme.onPrimaryContainer
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f)
            )
            when {
                selected -> Icon(
                    imageVector = Icons.Rounded.Check,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(18.dp)
                )
                trailingIcon != null -> Icon(
                    imageVector = trailingIcon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(18.dp)
                )
            }
        }
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
        shape = neonShape(22.dp),
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
                    .clip(neonShape(15.dp))
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

package com.sbro.emucorex.ui.hub

import android.app.Activity
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import com.sbro.emucorex.ui.theme.style.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import com.sbro.emucorex.ui.theme.style.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import com.sbro.emucorex.ui.theme.style.OutlinedButton
import com.sbro.emucorex.ui.theme.style.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.BuildConfig
import com.sbro.emucorex.R
import com.sbro.emucorex.data.hub.HubThemeCatalog
import com.sbro.emucorex.data.hub.HubThemeEntry
import com.sbro.emucorex.data.hub.HubThemeSnapshot
import com.sbro.emucorex.data.theme.InstalledThemePackage
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.theme.LocalThemeExtras
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonChipShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import java.text.Normalizer
import java.util.Locale

internal enum class HubThemeStatus { NONE, INSTALLED, APPLIED, UPDATE, NEEDS_NEWER_APP }

internal fun HubThemeFilters.isActive(): Boolean =
    access != HubThemeAccessFilter.ALL || tone != HubThemeToneFilter.ALL ||
        categoryId != null || installedOnly

internal fun HubThemeEntry.status(installed: InstalledThemePackage?, appliedId: String?): HubThemeStatus = when {
    minAppVersionCode > BuildConfig.VERSION_CODE -> HubThemeStatus.NEEDS_NEWER_APP
    installed == null -> HubThemeStatus.NONE
    installed.spec.version < version -> HubThemeStatus.UPDATE
    appliedId == id -> HubThemeStatus.APPLIED
    else -> HubThemeStatus.INSTALLED
}

private fun String.normalizedThemeText(): String = Normalizer.normalize(this, Normalizer.Form.NFKC)
    .lowercase(Locale.ROOT)
    .replace(Regex("[\\p{Punct}\\s]+"), " ")
    .trim()

/** Applies filters and search, featured first, then newest. */
internal fun filterThemes(
    snapshot: HubThemeSnapshot?,
    filters: HubThemeFilters,
    query: String,
    installedIds: Set<String>,
    locale: String
): List<HubThemeEntry> {
    val themes = snapshot?.catalog?.themes ?: return emptyList()
    val normalizedQuery = query.normalizedThemeText()
    return themes.asSequence()
        .filter {
            when (filters.access) {
                HubThemeAccessFilter.ALL -> true
                HubThemeAccessFilter.FREE -> !it.requiresPro
                HubThemeAccessFilter.PRO -> it.requiresPro
            }
        }
        .filter {
            when (filters.tone) {
                HubThemeToneFilter.ALL -> true
                HubThemeToneFilter.DARK -> it.dark
                HubThemeToneFilter.LIGHT -> !it.dark
            }
        }
        .filter { filters.categoryId == null || filters.categoryId in it.categories }
        .filter { !filters.installedOnly || it.id in installedIds }
        .filter {
            normalizedQuery.isBlank() || buildString {
                append(it.displayName(locale)); append(' ')
                append(it.displayDescription(locale)); append(' ')
                append(it.tags.joinToString(" "))
            }.normalizedThemeText().contains(normalizedQuery)
        }
        .sortedWith(
            compareByDescending<HubThemeEntry> { it.featured }
                .thenByDescending { it.publishedAt }
                .thenBy { it.id }
        )
        .toList()
}

private fun parseSwatch(values: List<String>): List<Color> = values.mapNotNull { value ->
    runCatching { Color(android.graphics.Color.parseColor(value)) }.getOrNull()
}

private fun swatchBrush(colors: List<Color>): Brush = when {
    colors.isEmpty() -> Brush.linearGradient(listOf(Color(0xFF2A2A33), Color(0xFF15151A)))
    colors.size == 1 -> Brush.linearGradient(listOf(colors[0], colors[0].copy(alpha = 0.6f)))
    else -> Brush.linearGradient(colors)
}

internal fun LazyGridScope.hubThemesItems(
    snapshot: HubThemeSnapshot?,
    themes: List<HubThemeEntry>,
    filters: HubThemeFilters,
    installed: Map<String, InstalledThemePackage>,
    appliedId: String?,
    columns: Int,
    searchActive: Boolean,
    locale: String,
    onFiltersChange: ((HubThemeFilters) -> HubThemeFilters) -> Unit,
    onClearSearch: () -> Unit,
    onOpen: (HubThemeEntry) -> Unit
) {
    val catalog = snapshot?.catalog
    if (catalog == null || catalog.themes.isEmpty()) {
        item(key = "themes-empty", span = { GridItemSpan(maxLineSpan) }) {
            HubEmptyState(
                tab = HubTab.THEMES,
                searchActive = false,
                filterActive = false,
                onClearSearch = {},
                onClearFilters = {}
            )
        }
        return
    }
    item(key = "themes-filters", span = { GridItemSpan(maxLineSpan) }) {
        HubThemeFilterRow(
            catalog = catalog,
            filters = filters,
            locale = locale,
            onFiltersChange = onFiltersChange
        )
    }
    if (themes.isEmpty()) {
        item(key = "themes-no-match", span = { GridItemSpan(maxLineSpan) }) {
            HubEmptyState(
                tab = HubTab.THEMES,
                searchActive = searchActive,
                filterActive = filters.isActive(),
                onClearSearch = onClearSearch,
                onClearFilters = { onFiltersChange { HubThemeFilters() } }
            )
        }
        return
    }
    val rows = themes.chunked(columns)
    items(rows.size, key = { "themes-row-${rows[it].first().id}" }, span = { GridItemSpan(maxLineSpan) }) { index ->
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            val row = rows[index]
            row.forEach { entry ->
                Box(Modifier.weight(1f)) {
                    HubThemeCard(
                        entry = entry,
                        snapshot = snapshot,
                        status = entry.status(installed[entry.id], appliedId),
                        locale = locale,
                        onClick = { onOpen(entry) }
                    )
                }
            }
            repeat(columns - row.size) { Spacer(Modifier.weight(1f)) }
        }
    }
}

@Composable
private fun HubThemeFilterRow(
    catalog: HubThemeCatalog,
    filters: HubThemeFilters,
    locale: String,
    onFiltersChange: ((HubThemeFilters) -> HubThemeFilters) -> Unit
) {
    val usedCategories = remember(catalog) {
        val used = catalog.themes.flatMapTo(mutableSetOf()) { it.categories }
        catalog.categories.filter { it.id in used }
    }
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        item { ThemeFilterChip(stringResource(R.string.hub_theme_filter_all), filters.access == HubThemeAccessFilter.ALL) { onFiltersChange { it.copy(access = HubThemeAccessFilter.ALL) } } }
        item { ThemeFilterChip(stringResource(R.string.hub_theme_filter_free), filters.access == HubThemeAccessFilter.FREE) { onFiltersChange { it.copy(access = HubThemeAccessFilter.FREE) } } }
        item { ThemeFilterChip(stringResource(R.string.hub_theme_filter_pro), filters.access == HubThemeAccessFilter.PRO) { onFiltersChange { it.copy(access = HubThemeAccessFilter.PRO) } } }
        item { Spacer(Modifier.size(4.dp)) }
        item { ThemeFilterChip(stringResource(R.string.hub_theme_filter_all), filters.tone == HubThemeToneFilter.ALL) { onFiltersChange { it.copy(tone = HubThemeToneFilter.ALL) } } }
        item { ThemeFilterChip(stringResource(R.string.hub_theme_filter_dark), filters.tone == HubThemeToneFilter.DARK) { onFiltersChange { it.copy(tone = HubThemeToneFilter.DARK) } } }
        item { ThemeFilterChip(stringResource(R.string.hub_theme_filter_light), filters.tone == HubThemeToneFilter.LIGHT) { onFiltersChange { it.copy(tone = HubThemeToneFilter.LIGHT) } } }
        item { Spacer(Modifier.size(4.dp)) }
        items(usedCategories, key = { it.id }) { category ->
            val selected = filters.categoryId == category.id
            ThemeFilterChip(category.label(locale), selected) {
                onFiltersChange { it.copy(categoryId = if (selected) null else category.id) }
            }
        }
        item {
            ThemeFilterChip(stringResource(R.string.hub_theme_installed), filters.installedOnly) {
                onFiltersChange { it.copy(installedOnly = !it.installedOnly) }
            }
        }
    }
}

@Composable
private fun ThemeFilterChip(label: String, selected: Boolean, onClick: () -> Unit) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        label = { Text(label, maxLines = 1) },
        shape = neonChipShape()
    )
}

@Composable
private fun ProBadge() {
    val gold = LocalThemeExtras.current.gold
    Surface(
        shape = neonShape(8.dp),
        color = Color.Black.copy(alpha = 0.62f),
        contentColor = gold
    ) {
        Text(
            "PRO",
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
private fun ThemePreview(
    entry: HubThemeEntry,
    snapshot: HubThemeSnapshot?,
    modifier: Modifier = Modifier
) {
    val colors = remember(entry.swatch) { parseSwatch(entry.swatch) }
    val ratio = if (entry.preview.width > 0 && entry.preview.height > 0) {
        entry.preview.width.toFloat() / entry.preview.height
    } else 9f / 16f
    Box(
        modifier = modifier
            .fillMaxWidth()
            .aspectRatio(ratio)
            .background(swatchBrush(colors))
    ) {
        if (snapshot != null) {
            HubImage(
                spec = HubImageSpec(
                    url = snapshot.url(entry.preview),
                    sha256 = entry.preview.sha256,
                    bytes = entry.preview.bytes
                ),
                modifier = Modifier.matchParentSize()
            )
        }
    }
}

@Composable
private fun SwatchDots(entry: HubThemeEntry) {
    val colors = remember(entry.swatch) { parseSwatch(entry.swatch).take(4) }
    Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
        colors.forEach { color ->
            Box(
                Modifier
                    .size(12.dp)
                    .clip(CircleShape)
                    .background(color)
            )
        }
    }
}

@Composable
private fun HubThemeStatus.label(): String? = when (this) {
    HubThemeStatus.NONE -> null
    HubThemeStatus.INSTALLED -> stringResource(R.string.hub_theme_installed)
    HubThemeStatus.APPLIED -> stringResource(R.string.hub_theme_applied)
    HubThemeStatus.UPDATE -> stringResource(R.string.hub_theme_update_available)
    HubThemeStatus.NEEDS_NEWER_APP -> stringResource(R.string.hub_theme_requires_newer_app)
}

@Composable
private fun HubThemeCard(
    entry: HubThemeEntry,
    snapshot: HubThemeSnapshot?,
    status: HubThemeStatus,
    locale: String,
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    val open = rememberDebouncedClick(onClick = onClick)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Column {
            Box {
                ThemePreview(entry, snapshot)
                if (entry.requiresPro) {
                    Box(Modifier.align(Alignment.TopStart).padding(8.dp)) { ProBadge() }
                }
            }
            Column(
                modifier = Modifier.padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    entry.displayName(locale),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                SwatchDots(entry)
                status.label()?.let {
                    Text(
                        it,
                        style = MaterialTheme.typography.labelMedium,
                        color = if (status == HubThemeStatus.NEEDS_NEWER_APP) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.primary,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
    }
}

/** Detail sheet and one-shot message toast for the Themes tab. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun HubThemesOverlay(
    viewModel: HubViewModel,
    openThemeId: String?,
    locale: String,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val actions by viewModel.themeActions.collectAsState()
    val snapshot by viewModel.themes.collectAsState()
    val installed by viewModel.installedThemes.collectAsState()
    val appliedId by viewModel.appliedThemeId.collectAsState()
    val isPro by viewModel.isProUnlocked.collectAsState()

    LaunchedEffect(actions.message) {
        actions.message?.let {
            Toast.makeText(context, it, Toast.LENGTH_LONG).show()
            viewModel.consumeThemeMessage()
        }
    }

    val entry = openThemeId?.let { id -> snapshot?.catalog?.themes?.firstOrNull { it.id == id } } ?: return
    val pkg = installed.firstOrNull { it.id == entry.id }
    val status = entry.status(pkg, appliedId)
    val busy = entry.id in actions.busyIds
    val locked = entry.requiresPro && !isPro
    val categories = snapshot?.catalog?.categories.orEmpty()
        .filter { it.id in entry.categories }
        .joinToString(" · ") { it.label(locale) }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        shape = neonShape(28.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .navigationBarsPadding()
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(Modifier.align(Alignment.CenterHorizontally).heightIn(max = 380.dp)) {
                ThemePreview(
                    entry, snapshot,
                    modifier = Modifier
                        .heightIn(max = 380.dp)
                        .clip(neonShape(18.dp))
                )
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    entry.displayName(locale),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f, fill = false)
                )
                if (entry.requiresPro) ProBadge()
            }
            SwatchDots(entry)
            status.label()?.let {
                Text(
                    it,
                    style = MaterialTheme.typography.labelLarge,
                    color = if (status == HubThemeStatus.NEEDS_NEWER_APP) MaterialTheme.colorScheme.error
                    else MaterialTheme.colorScheme.primary
                )
            }
            if (categories.isNotEmpty()) {
                Text(categories, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            entry.displayDescription(locale).takeIf { it.isNotBlank() }?.let {
                Text(it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }

            val purchase: () -> Unit = { (context as? Activity)?.let(viewModel::purchasePro) }
            val needsNewer = status == HubThemeStatus.NEEDS_NEWER_APP
            val primaryEnabled = !busy && !needsNewer
            Column(verticalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                if (pkg == null) {
                    if (locked) {
                        ThemeSheetButton(stringResource(R.string.hub_theme_action_install), primaryEnabled, busy, primary = false) {
                            viewModel.installTheme(entry, apply = false)
                        }
                        ThemeSheetButton(stringResource(R.string.hub_theme_action_apply), primaryEnabled, false, primary = true) { purchase() }
                    } else {
                        ThemeSheetButton(stringResource(R.string.hub_theme_action_install_apply), primaryEnabled, busy, primary = true) {
                            viewModel.installTheme(entry, apply = true)
                        }
                        ThemeSheetButton(stringResource(R.string.hub_theme_action_install), primaryEnabled, false, primary = false) {
                            viewModel.installTheme(entry, apply = false)
                        }
                    }
                } else {
                    if (status == HubThemeStatus.UPDATE) {
                        ThemeSheetButton(stringResource(R.string.hub_theme_action_update), primaryEnabled, busy, primary = true) {
                            viewModel.installTheme(entry, apply = appliedId == entry.id)
                        }
                    }
                    val isApplied = appliedId == entry.id
                    ThemeSheetButton(
                        stringResource(if (isApplied) R.string.hub_theme_applied else R.string.hub_theme_action_apply),
                        !busy && !needsNewer && !isApplied,
                        false,
                        primary = status != HubThemeStatus.UPDATE
                    ) {
                        if (locked) purchase() else viewModel.applyTheme(entry)
                    }
                    ThemeSheetButton(stringResource(R.string.hub_theme_action_remove), !busy, false, primary = false) {
                        viewModel.removeTheme(entry)
                    }
                }
            }
            Spacer(Modifier.size(8.dp))
        }
    }
}

@Composable
private fun ThemeSheetButton(
    label: String,
    enabled: Boolean,
    loading: Boolean,
    primary: Boolean,
    onClick: () -> Unit
) {
    val content: @Composable () -> Unit = {
        if (loading) {
            CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
            Spacer(Modifier.size(8.dp))
        }
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
    if (primary) {
        Button(onClick = onClick, enabled = enabled, shape = neonButtonShape(), modifier = Modifier.fillMaxWidth()) { content() }
    } else {
        OutlinedButton(onClick = onClick, enabled = enabled, shape = neonButtonShape(), modifier = Modifier.fillMaxWidth()) { content() }
    }
}

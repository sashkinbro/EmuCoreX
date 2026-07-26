package com.sbro.emucorex.ui.theme

import android.graphics.Color as AndroidColor
import androidx.annotation.StringRes
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.DarkMode
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.LightMode
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.layout
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.CustomThemeConfig
import com.sbro.emucorex.data.CustomThemeLibrary
import com.sbro.emucorex.data.SavedCustomTheme
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import java.util.UUID
import kotlin.math.roundToInt

private val ManagerCardShape = RoundedCornerShape(28.dp)
private val ManagerControlShape = RoundedCornerShape(18.dp)
private val ManagerSmallShape = RoundedCornerShape(14.dp)

@Composable
fun ThemeManagerScreen(
    initialLibrary: CustomThemeLibrary,
    isProUnlocked: Boolean,
    onPurchasePro: () -> Unit,
    onSave: (CustomThemeLibrary) -> Unit,
    onApply: (CustomThemeLibrary) -> Unit,
    onBackClick: () -> Unit
) {
    val localizedDefaultName = stringResource(R.string.theme_manager_default_name)
    val localizedDefaultTheme = remember(localizedDefaultName) {
        CustomThemeConfig.Default.copy(name = localizedDefaultName)
    }
    val seedLibrary = remember(initialLibrary, localizedDefaultName) {
        val safeInitial = initialLibrary.sanitized()
        safeInitial
            .copy(
                themes = safeInitial.themes.map { saved ->
                    if (saved.config.name == CustomThemeConfig.DEFAULT_NAME) {
                        saved.copy(config = saved.config.copy(name = localizedDefaultName))
                    } else {
                        saved
                    }
                }
            )
            .takeIf { it.themes.isNotEmpty() } ?: run {
                val now = System.currentTimeMillis()
                val first = SavedCustomTheme(
                    id = UUID.randomUUID().toString(),
                    config = localizedDefaultTheme,
                    createdAtMillis = now,
                    updatedAtMillis = now
                )
                CustomThemeLibrary(activeThemeId = null, themes = listOf(first))
            }
    }
    var library by remember { mutableStateOf(seedLibrary) }
    var selectedThemeId by remember {
        mutableStateOf(seedLibrary.activeThemeId ?: seedLibrary.themes.first().id)
    }
    var draft by remember {
        mutableStateOf(
            seedLibrary.themes.first { it.id == selectedThemeId }.config.sanitized()
        )
    }
    var expandedRole by remember { mutableStateOf<ColorRole?>(null) }
    var selectedEditorCategory by remember {
        mutableStateOf(ThemeEditorCategory.ACTIONS)
    }
    var deleteCandidateId by remember { mutableStateOf<String?>(null) }
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()

    fun selectTheme(id: String) {
        val selected = library.themes.firstOrNull { it.id == id } ?: return
        selectedThemeId = id
        draft = selected.config.sanitized()
        expandedRole = null
    }

    fun libraryWithDraft(): CustomThemeLibrary {
        val now = System.currentTimeMillis()
        return library.copy(
            themes = library.themes.map { saved ->
                if (saved.id == selectedThemeId) {
                    saved.copy(config = draft.sanitized(), updatedAtMillis = now)
                } else {
                    saved
                }
            }
        ).sanitized()
    }

    fun createTheme(source: CustomThemeConfig? = null) {
        if (!isProUnlocked) {
            onPurchasePro()
            return
        }
        if (library.themes.size >= CustomThemeLibrary.MAX_THEMES) return
        library = libraryWithDraft()
        val now = System.currentTimeMillis()
        val sourceTheme = source ?: localizedDefaultTheme
        val newTheme = SavedCustomTheme(
            id = UUID.randomUUID().toString(),
            config = sourceTheme.copy(
                name = uniqueThemeName(
                    base = sourceTheme.name,
                    existing = library.themes.map { it.config.name },
                    fallback = localizedDefaultName
                )
            ).sanitized(),
            createdAtMillis = now,
            updatedAtMillis = now
        )
        library = library.copy(themes = library.themes + newTheme).sanitized()
        selectTheme(newTheme.id)
    }

    fun deleteTheme(id: String) {
        val remaining = libraryWithDraft().themes.filterNot { it.id == id }
        if (remaining.isEmpty()) {
            val now = System.currentTimeMillis()
            val replacement = SavedCustomTheme(
                id = UUID.randomUUID().toString(),
                config = localizedDefaultTheme,
                createdAtMillis = now,
                updatedAtMillis = now
            )
            library = CustomThemeLibrary(themes = listOf(replacement))
            selectTheme(replacement.id)
            return
        }
        library = library.copy(
            activeThemeId = library.activeThemeId?.takeUnless { it == id },
            themes = remaining
        ).sanitized()
        if (selectedThemeId == id) {
            selectTheme(remaining.first().id)
        }
    }

    deleteCandidateId?.let { id ->
        val candidate = library.themes.firstOrNull { it.id == id }
        if (candidate != null) {
            AlertDialog(
                onDismissRequest = { deleteCandidateId = null },
                shape = ManagerCardShape,
                title = { Text(stringResource(R.string.theme_manager_delete_title)) },
                text = {
                    Text(
                        stringResource(
                            R.string.theme_manager_delete_message,
                            candidate.config.name
                        )
                    )
                },
                confirmButton = {
                    TextButton(
                        onClick = {
                            deleteTheme(id)
                            deleteCandidateId = null
                        }
                    ) {
                        Text(stringResource(R.string.theme_manager_delete))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { deleteCandidateId = null }) {
                        Text(stringResource(R.string.theme_manager_cancel))
                    }
                }
            )
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .testTag("theme_manager_list")
            .background(MaterialTheme.colorScheme.background),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = appScreenTopPadding(),
            bottom = bottomInset + 28.dp
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.theme_manager_title),
                onBackClick = onBackClick,
                modifier = Modifier.testTag("theme_manager_top_bar")
            )
        }
        if (!isProUnlocked) {
            item {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag("theme_manager_preview_banner"),
                    shape = ManagerControlShape,
                    color = MaterialTheme.colorScheme.primaryContainer,
                    contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(Icons.Rounded.Palette, contentDescription = null)
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                stringResource(R.string.theme_manager_preview_mode),
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                stringResource(R.string.theme_manager_preview_mode_desc),
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                        Button(onClick = onPurchasePro, shape = ManagerSmallShape) {
                            Text(stringResource(R.string.theme_manager_unlock))
                        }
                    }
                }
            }
        }
        item {
            ThemeSection(
                title = stringResource(R.string.theme_manager_library),
                trailing = {
                    IconButton(
                        onClick = { createTheme() },
                        enabled = isProUnlocked &&
                            library.themes.size < CustomThemeLibrary.MAX_THEMES
                    ) {
                        Icon(
                            Icons.Rounded.Add,
                            contentDescription = stringResource(R.string.theme_manager_create),
                            tint = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
            ) {
                Text(
                    text = stringResource(
                        R.string.theme_manager_library_count,
                        library.themes.size,
                        CustomThemeLibrary.MAX_THEMES
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                LazyRow(
                    modifier = Modifier.themeSectionHorizontalViewport(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    contentPadding = PaddingValues(horizontal = 18.dp, vertical = 2.dp)
                ) {
                    items(library.themes, key = { it.id }) { saved ->
                        SavedThemeCard(
                            saved = if (saved.id == selectedThemeId) {
                                saved.copy(config = draft)
                            } else {
                                saved
                            },
                            selected = saved.id == selectedThemeId,
                            active = saved.id == library.activeThemeId,
                            onClick = {
                                library = libraryWithDraft()
                                selectTheme(saved.id)
                            }
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    OutlinedButton(
                        onClick = { createTheme(draft.copy(name = "${draft.name} Copy")) },
                        enabled = isProUnlocked &&
                            library.themes.size < CustomThemeLibrary.MAX_THEMES,
                        shape = ManagerControlShape,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.ContentCopy, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.theme_manager_duplicate))
                    }
                    OutlinedButton(
                        onClick = { deleteCandidateId = selectedThemeId },
                        enabled = isProUnlocked,
                        shape = ManagerControlShape,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.Delete, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.theme_manager_delete))
                    }
                }
            }
        }
        item {
            ThemeSection(title = stringResource(R.string.theme_manager_identity)) {
                OutlinedTextField(
                    value = draft.name,
                    onValueChange = {
                        draft = draft.copy(name = it.take(CustomThemeConfig.MAX_NAME_LENGTH))
                    },
                    enabled = isProUnlocked,
                    label = { Text(stringResource(R.string.theme_manager_name)) },
                    singleLine = true,
                    shape = ManagerControlShape,
                    modifier = Modifier.fillMaxWidth()
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    FilterChip(
                        selected = !draft.dark,
                        onClick = { draft = draft.withCanvas(dark = false) },
                        enabled = isProUnlocked,
                        label = { Text(stringResource(R.string.theme_manager_light_canvas)) },
                        leadingIcon = { Icon(Icons.Rounded.LightMode, contentDescription = null) },
                        shape = ManagerControlShape,
                        modifier = Modifier.weight(1f)
                    )
                    FilterChip(
                        selected = draft.dark,
                        onClick = { draft = draft.withCanvas(dark = true) },
                        enabled = isProUnlocked,
                        label = { Text(stringResource(R.string.theme_manager_dark_canvas)) },
                        leadingIcon = { Icon(Icons.Rounded.DarkMode, contentDescription = null) },
                        shape = ManagerControlShape,
                        modifier = Modifier.weight(1f)
                    )
                }
                ConfiguredThemePreview(draft)
            }
        }
        item {
            ThemeSection(title = stringResource(R.string.theme_manager_presets)) {
                LazyRow(
                    modifier = Modifier.themeSectionHorizontalViewport(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    contentPadding = PaddingValues(horizontal = 18.dp, vertical = 2.dp)
                ) {
                    items(ThemePreset.entries, key = { it.name }) { preset ->
                        val config = preset.config().sanitized()
                        Surface(
                            onClick = {
                                    draft = config.copy(name = draft.name).sanitized()
                                    expandedRole = null
                            },
                            enabled = isProUnlocked,
                            modifier = Modifier.width(154.dp),
                            shape = ManagerControlShape,
                            color = config.toColorScheme().surface,
                            contentColor = config.toColorScheme().onSurface,
                            border = BorderStroke(
                                1.dp,
                                config.toColorScheme().outlineVariant
                            )
                        ) {
                            Column(
                                modifier = Modifier.padding(14.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                PresetSwatches(config)
                                Text(
                                    text = stringResource(preset.titleRes),
                                    style = MaterialTheme.typography.titleSmall,
                                    fontWeight = FontWeight.SemiBold
                                )
                                Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                                    Box(
                                        Modifier
                                            .weight(1f)
                                            .height(7.dp)
                                            .clip(CircleShape)
                                            .background(Color(config.primary))
                                    )
                                    Box(
                                        Modifier
                                            .width(24.dp)
                                            .height(7.dp)
                                            .clip(CircleShape)
                                            .background(Color(config.surfaceVariant))
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
        item {
            ThemeSection(title = stringResource(R.string.theme_manager_studio)) {
                LazyRow(
                    modifier = Modifier
                        .themeSectionHorizontalViewport()
                        .testTag("theme_manager_categories"),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    contentPadding = PaddingValues(horizontal = 18.dp, vertical = 2.dp)
                ) {
                    items(ThemeEditorCategory.entries, key = { it.name }) { category ->
                        FilterChip(
                            selected = selectedEditorCategory == category,
                            onClick = {
                                selectedEditorCategory = category
                                expandedRole = null
                            },
                            label = { Text(stringResource(category.titleRes)) },
                            leadingIcon = if (selectedEditorCategory == category) {
                                {
                                    Icon(
                                        Icons.Rounded.Check,
                                        contentDescription = null,
                                        modifier = Modifier.size(16.dp)
                                    )
                                }
                            } else {
                                null
                            },
                            shape = ManagerSmallShape
                        )
                    }
                }
                val group = selectedEditorCategory.group
                if (group != null) {
                    ConfiguredThemeGroupPreview(draft, group)
                    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        group.roles.forEach { role ->
                            ColorEditor(
                                role = role,
                                group = group,
                                value = role.read(draft),
                                config = draft,
                                enabled = isProUnlocked,
                                expanded = expandedRole == role,
                                onExpandedChange = {
                                    expandedRole = if (expandedRole == role) null else role
                                },
                                onValueChange = {
                                    // Keep the raw draft while dragging. Persisted themes are sanitized
                                    // on save, but doing that on every tick makes low-contrast foreground
                                    // sliders snap back to the same repaired value.
                                    draft = role.write(draft, it)
                                }
                            )
                        }
                    }
                } else {
                    ShapePreview(draft)
                    CornerSlider(
                        title = stringResource(R.string.theme_manager_rounding_small),
                        value = draft.smallCornerDp,
                        max = CustomThemeConfig.MAX_SMALL_CORNER_DP,
                        enabled = isProUnlocked,
                        onValueChange = { draft = draft.copy(smallCornerDp = it) }
                    )
                    CornerSlider(
                        title = stringResource(R.string.theme_manager_rounding_medium),
                        value = draft.mediumCornerDp,
                        max = CustomThemeConfig.MAX_MEDIUM_CORNER_DP,
                        enabled = isProUnlocked,
                        onValueChange = { draft = draft.copy(mediumCornerDp = it) }
                    )
                    CornerSlider(
                        title = stringResource(R.string.theme_manager_rounding_large),
                        value = draft.largeCornerDp,
                        max = CustomThemeConfig.MAX_LARGE_CORNER_DP,
                        enabled = isProUnlocked,
                        onValueChange = { draft = draft.copy(largeCornerDp = it) }
                    )
                }
            }
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = {
                        draft = CustomThemeConfig.Default
                        expandedRole = null
                    },
                    enabled = isProUnlocked,
                    shape = ManagerControlShape,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.theme_manager_reset))
                }
                OutlinedButton(
                    onClick = {
                        library = libraryWithDraft()
                        onSave(library)
                    },
                    enabled = isProUnlocked,
                    shape = ManagerControlShape,
                    modifier = Modifier.weight(1f)
                ) {
                    Text(stringResource(R.string.theme_manager_save))
                }
            }
        }
        item {
            Button(
                onClick = {
                    library = libraryWithDraft().copy(activeThemeId = selectedThemeId).sanitized()
                    onApply(library)
                },
                enabled = isProUnlocked,
                shape = ManagerControlShape,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(54.dp)
                    .testTag("theme_manager_apply")
            ) {
                Icon(Icons.Rounded.Check, contentDescription = null)
                Spacer(Modifier.size(9.dp))
                Text(
                    text = stringResource(R.string.theme_manager_apply),
                    fontWeight = FontWeight.SemiBold
                )
            }
        }
    }
}

@Composable
private fun ThemeSection(
    title: String,
    trailing: @Composable (() -> Unit)? = null,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = ManagerCardShape,
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 2.dp,
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
        )
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.weight(1f)
                )
                trailing?.invoke()
            }
            content()
        }
    }
}

/**
 * Lets horizontal content scroll all the way to a section card's clipped edge while
 * keeping its resting first and last items aligned with the section's 18 dp content.
 */
private fun Modifier.themeSectionHorizontalViewport(): Modifier = layout { measurable, constraints ->
    val edgePadding = 18.dp.roundToPx()
    val horizontalBleed = edgePadding * 2
    val expandedConstraints = constraints.copy(
        minWidth = (constraints.minWidth + horizontalBleed).coerceAtMost(constraints.maxWidth + horizontalBleed),
        maxWidth = constraints.maxWidth + horizontalBleed
    )
    val placeable = measurable.measure(expandedConstraints)
    layout(constraints.maxWidth, placeable.height) {
        placeable.placeRelative(-edgePadding, 0)
    }
}

@Composable
private fun SavedThemeCard(
    saved: SavedCustomTheme,
    selected: Boolean,
    active: Boolean,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        modifier = Modifier.width(186.dp),
        shape = ManagerControlShape,
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
        },
        contentColor = if (selected) {
            MaterialTheme.colorScheme.onPrimaryContainer
        } else {
            MaterialTheme.colorScheme.onSurface
        },
        border = if (selected) {
            BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.7f))
        } else {
            BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.25f))
        }
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            PresetSwatches(saved.config)
            Text(
                text = saved.config.name,
                maxLines = 1,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold
            )
            Text(
                text = when {
                    active -> stringResource(R.string.theme_manager_active)
                    selected -> stringResource(R.string.theme_manager_editing)
                    else -> if (saved.config.dark) {
                        stringResource(R.string.theme_manager_dark_canvas)
                    } else {
                        stringResource(R.string.theme_manager_light_canvas)
                    }
                },
                style = MaterialTheme.typography.labelSmall,
                color = if (active) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                }
            )
        }
    }
}

@Composable
private fun ThemePreview() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.background,
        contentColor = MaterialTheme.colorScheme.onBackground,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Surface(
                    shape = MaterialTheme.shapes.small,
                    color = MaterialTheme.colorScheme.primaryContainer,
                    modifier = Modifier.size(44.dp)
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            Icons.Rounded.Palette,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                    }
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        stringResource(R.string.app_name),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        stringResource(R.string.theme_manager_preview_library),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                repeat(2) { index ->
                    Surface(
                        modifier = Modifier.size(34.dp),
                        shape = CircleShape,
                        color = MaterialTheme.colorScheme.surfaceVariant
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Box(
                                modifier = Modifier
                                    .size(if (index == 0) 12.dp else 16.dp)
                                    .clip(if (index == 0) CircleShape else MaterialTheme.shapes.extraSmall)
                                    .background(MaterialTheme.colorScheme.primary)
                            )
                        }
                    }
                }
            }
            Text(
                stringResource(R.string.theme_manager_preview_continue),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Surface(
                    modifier = Modifier
                        .weight(1f)
                        .height(86.dp),
                    shape = MaterialTheme.shapes.medium,
                    color = MaterialTheme.colorScheme.primaryContainer
                ) {
                    Column(
                        modifier = Modifier.padding(12.dp),
                        verticalArrangement = Arrangement.SpaceBetween
                    ) {
                        Box(
                            Modifier
                                .size(28.dp)
                                .clip(MaterialTheme.shapes.extraSmall)
                                .background(MaterialTheme.colorScheme.primary)
                        )
                        Text(
                            stringResource(R.string.theme_manager_preview_game_one),
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                            style = MaterialTheme.typography.labelLarge,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
                Surface(
                    modifier = Modifier
                        .weight(1f)
                        .height(86.dp),
                    shape = MaterialTheme.shapes.medium,
                    color = MaterialTheme.colorScheme.secondaryContainer
                ) {
                    Column(
                        modifier = Modifier.padding(12.dp),
                        verticalArrangement = Arrangement.SpaceBetween
                    ) {
                        Box(
                            Modifier
                                .size(28.dp)
                                .clip(MaterialTheme.shapes.extraSmall)
                                .background(MaterialTheme.colorScheme.secondary)
                        )
                        Text(
                            stringResource(R.string.theme_manager_preview_game_two),
                            color = MaterialTheme.colorScheme.onSecondaryContainer,
                            style = MaterialTheme.typography.labelLarge,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
            }
            Surface(
                shape = MaterialTheme.shapes.medium,
                color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
            ) {
                Row(
                    modifier = Modifier.padding(13.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(11.dp)
                ) {
                    Surface(
                        modifier = Modifier.size(38.dp),
                        shape = MaterialTheme.shapes.small,
                        color = MaterialTheme.colorScheme.tertiaryContainer
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(
                                Icons.Rounded.Palette,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.tertiary
                            )
                        }
                    }
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            stringResource(R.string.theme_manager_preview_appearance),
                            style = MaterialTheme.typography.titleSmall
                        )
                        Text(
                            stringResource(R.string.theme_manager_preview_appearance_desc),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(checked = true, onCheckedChange = null)
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(onClick = {}, modifier = Modifier.weight(1f)) {
                    Text(stringResource(R.string.theme_manager_preview_button))
                }
                OutlinedButton(onClick = {}, modifier = Modifier.weight(1f)) {
                    Text(stringResource(R.string.theme_manager_preview_secondary_button))
                }
            }
        }
    }
}

@Composable
private fun ColorEditor(
    role: ColorRole,
    group: ThemeColorGroup,
    value: Int,
    config: CustomThemeConfig,
    enabled: Boolean,
    expanded: Boolean,
    onExpandedChange: () -> Unit,
    onValueChange: (Int) -> Unit
) {
    val color = Color(value)
    val rgb = remember(value) {
        intArrayOf(
            (color.red * 255).roundToInt(),
            (color.green * 255).roundToInt(),
            (color.blue * 255).roundToInt()
        )
    }
    var hexDraft by remember(role) { mutableStateOf(value.toRgbHex()) }
    LaunchedEffect(value) { hexDraft = value.toRgbHex() }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("theme_manager_color_${role.name.lowercase()}"),
        shape = ManagerControlShape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f),
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = if (expanded) 2.dp else 0.dp,
        border = if (expanded) {
            BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.55f))
        } else {
            BorderStroke(
                1.dp,
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.34f)
            )
        }
    ) {
        Column {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(ManagerControlShape)
                    .clickable(onClick = onExpandedChange)
                    .padding(12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .clip(CircleShape)
                        .background(color)
                )
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(role.titleRes),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = value.toRgbHex(),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(role.usageRes),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Icon(
                    imageVector = if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
            }
            AnimatedVisibility(visible = expanded) {
                Column(
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = hexDraft,
                        onValueChange = { input ->
                            hexDraft = input.take(7).uppercase()
                            input.parseRgbHex()?.let(onValueChange)
                        },
                        label = { Text(stringResource(R.string.theme_manager_hex_color)) },
                        supportingText = { Text(stringResource(R.string.theme_manager_hex_hint)) },
                        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                        enabled = enabled,
                        singleLine = true,
                        shape = ManagerControlShape,
                        modifier = Modifier
                            .fillMaxWidth()
                            .testTag("theme_manager_hex_${role.name.lowercase()}")
                    )
                    RgbSlider("R", rgb[0], enabled) { channel ->
                        onValueChange(AndroidColor.rgb(channel, rgb[1], rgb[2]))
                    }
                    RgbSlider("G", rgb[1], enabled) { channel ->
                        onValueChange(AndroidColor.rgb(rgb[0], channel, rgb[2]))
                    }
                    RgbSlider("B", rgb[2], enabled) { channel ->
                        onValueChange(AndroidColor.rgb(rgb[0], rgb[1], channel))
                    }
                    ConfiguredThemeGroupPreview(config, group, compact = true)
                }
            }
        }
    }
}

@Composable
private fun RgbSlider(
    label: String,
    value: Int,
    enabled: Boolean,
    onValueChange: (Int) -> Unit
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label  $value", modifier = Modifier.size(width = 58.dp, height = 24.dp))
        Slider(
            value = value.toFloat(),
            onValueChange = { onValueChange(it.roundToInt().coerceIn(0, 255)) },
            enabled = enabled,
            valueRange = 0f..255f,
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
private fun CornerSlider(
    title: String,
    value: Float,
    max: Float,
    enabled: Boolean,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(modifier = Modifier.fillMaxWidth()) {
            Text(title, modifier = Modifier.weight(1f))
            Text(
                text = "${value.roundToInt()} dp",
                color = MaterialTheme.colorScheme.primary,
                fontWeight = FontWeight.SemiBold
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            enabled = enabled,
            valueRange = 0f..max,
            steps = max.roundToInt() - 1
        )
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .height(48.dp),
            shape = RoundedCornerShape(value.dp),
            color = MaterialTheme.colorScheme.primaryContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)
        ) {
            Box(contentAlignment = Alignment.Center) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
        }
    }
}

@Composable
private fun ConfiguredThemePreview(config: CustomThemeConfig) {
    val typography = MaterialTheme.typography
    MaterialTheme(
        colorScheme = config.toColorScheme(),
        shapes = config.toShapes(),
        typography = typography
    ) {
        ThemePreview()
    }
}

@Composable
private fun ConfiguredThemeGroupPreview(
    config: CustomThemeConfig,
    group: ThemeColorGroup,
    compact: Boolean = false
) {
    val typography = MaterialTheme.typography
    MaterialTheme(
        colorScheme = config.toColorScheme(),
        shapes = config.toShapes(),
        typography = typography
    ) {
        ThemeGroupPreview(group = group, compact = compact)
    }
}

@Composable
private fun ThemeGroupPreview(
    group: ThemeColorGroup,
    compact: Boolean
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.medium,
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 1.dp,
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)
        )
    ) {
        Column(
            modifier = Modifier.padding(if (compact) 12.dp else 16.dp),
            verticalArrangement = Arrangement.spacedBy(if (compact) 8.dp else 11.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                Box(
                    Modifier
                        .size(7.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.tertiary)
                )
                Text(
                    text = stringResource(R.string.theme_manager_live_preview),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            when (group) {
                ThemeColorGroup.ACTIONS -> {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(onClick = {}, modifier = Modifier.weight(1f)) {
                            Text(stringResource(R.string.theme_manager_preview_button))
                        }
                        PreviewContainer(
                            label = stringResource(R.string.theme_manager_preview_selected),
                            color = MaterialTheme.colorScheme.primaryContainer,
                            contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                            modifier = Modifier.weight(1f)
                        )
                    }
                }

                ThemeColorGroup.ACCENTS -> {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        PreviewContainer(
                            label = stringResource(R.string.theme_manager_preview_secondary),
                            color = MaterialTheme.colorScheme.secondary,
                            contentColor = MaterialTheme.colorScheme.onSecondary,
                            modifier = Modifier.weight(1f)
                        )
                        PreviewContainer(
                            label = stringResource(R.string.theme_manager_preview_highlight),
                            color = MaterialTheme.colorScheme.tertiary,
                            contentColor = MaterialTheme.colorScheme.onTertiary,
                            modifier = Modifier.weight(1f)
                        )
                    }
                    if (!compact) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            PreviewContainer(
                                label = stringResource(R.string.theme_manager_preview_secondary),
                                color = MaterialTheme.colorScheme.secondaryContainer,
                                contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                                modifier = Modifier.weight(1f)
                            )
                            PreviewContainer(
                                label = stringResource(R.string.theme_manager_preview_highlight),
                                color = MaterialTheme.colorScheme.tertiaryContainer,
                                contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }
                }

                ThemeColorGroup.SURFACES -> {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = MaterialTheme.shapes.medium,
                        color = MaterialTheme.colorScheme.surface,
                        tonalElevation = 2.dp
                    ) {
                        Column(
                            modifier = Modifier.padding(12.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                stringResource(R.string.theme_manager_preview_surface),
                                color = MaterialTheme.colorScheme.onSurface,
                                style = MaterialTheme.typography.titleSmall
                            )
                            Surface(
                                modifier = Modifier.fillMaxWidth(),
                                shape = MaterialTheme.shapes.small,
                                color = MaterialTheme.colorScheme.surfaceVariant
                            ) {
                                Text(
                                    stringResource(R.string.theme_manager_preview_surface_variant),
                                    modifier = Modifier.padding(12.dp),
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    style = MaterialTheme.typography.bodySmall
                                )
                            }
                        }
                    }
                }

                ThemeColorGroup.TEXT -> {
                    Text(
                        stringResource(R.string.theme_manager_preview_heading),
                        color = MaterialTheme.colorScheme.onBackground,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = MaterialTheme.shapes.medium,
                        color = MaterialTheme.colorScheme.surface
                    ) {
                        Column(modifier = Modifier.padding(12.dp)) {
                            Text(
                                stringResource(R.string.theme_manager_preview_body_text),
                                color = MaterialTheme.colorScheme.onSurface,
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Text(
                                stringResource(R.string.theme_manager_preview_caption),
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                    }
                }

                ThemeColorGroup.OUTLINES -> {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = MaterialTheme.shapes.medium,
                        color = MaterialTheme.colorScheme.surface,
                        border = BorderStroke(2.dp, MaterialTheme.colorScheme.outline)
                    ) {
                        Column(
                            modifier = Modifier.padding(12.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                stringResource(R.string.theme_manager_preview_outlined_card),
                                color = MaterialTheme.colorScheme.onSurface,
                                style = MaterialTheme.typography.titleSmall
                            )
                            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                            Text(
                                stringResource(R.string.theme_manager_preview_divider),
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                    }
                }

                ThemeColorGroup.ERRORS -> {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = MaterialTheme.shapes.medium,
                        color = MaterialTheme.colorScheme.errorContainer,
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.error)
                    ) {
                        Row(
                            modifier = Modifier.padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Surface(
                                shape = CircleShape,
                                color = MaterialTheme.colorScheme.error,
                                modifier = Modifier.size(34.dp)
                            ) {
                                Box(contentAlignment = Alignment.Center) {
                                    Text(
                                        "!",
                                        color = MaterialTheme.colorScheme.onError,
                                        fontWeight = FontWeight.Bold
                                    )
                                }
                            }
                            Text(
                                stringResource(R.string.theme_manager_preview_warning),
                                modifier = Modifier.weight(1f),
                                color = MaterialTheme.colorScheme.onErrorContainer,
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun PreviewContainer(
    label: String,
    color: Color,
    contentColor: Color,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.height(44.dp),
        shape = MaterialTheme.shapes.small,
        color = color
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(label, color = contentColor, style = MaterialTheme.typography.labelMedium)
        }
    }
}

@Composable
private fun ShapePreview(config: CustomThemeConfig) {
    val typography = MaterialTheme.typography
    MaterialTheme(
        colorScheme = MaterialTheme.colorScheme,
        shapes = config.toShapes(),
        typography = typography
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            listOf(
                stringResource(R.string.theme_manager_rounding_small) to MaterialTheme.shapes.small,
                stringResource(R.string.theme_manager_rounding_medium) to MaterialTheme.shapes.medium,
                stringResource(R.string.theme_manager_rounding_large) to MaterialTheme.shapes.large
            ).forEach { (label, shape) ->
                Surface(
                    modifier = Modifier
                        .weight(1f)
                        .height(64.dp),
                    shape = shape,
                    color = MaterialTheme.colorScheme.primaryContainer
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Text(
                            text = label,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun PresetSwatches(config: CustomThemeConfig) {
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        listOf(config.primary, config.secondary, config.tertiary, config.surfaceVariant).forEach { argb ->
            Box(
                modifier = Modifier
                    .size(18.dp)
                    .clip(CircleShape)
                    .background(Color(argb))
            )
        }
    }
}

private enum class ThemeEditorCategory(
    @StringRes val titleRes: Int,
    val group: ThemeColorGroup?
) {
    ACTIONS(R.string.theme_manager_category_actions, ThemeColorGroup.ACTIONS),
    ACCENTS(R.string.theme_manager_category_accents, ThemeColorGroup.ACCENTS),
    SURFACES(R.string.theme_manager_category_surfaces, ThemeColorGroup.SURFACES),
    TEXT(R.string.theme_manager_category_text, ThemeColorGroup.TEXT),
    OUTLINES(R.string.theme_manager_category_outlines, ThemeColorGroup.OUTLINES),
    ERRORS(R.string.theme_manager_category_errors, ThemeColorGroup.ERRORS),
    SHAPES(R.string.theme_manager_category_shapes, null)
}

private enum class ThemeColorGroup(
    @StringRes val titleRes: Int,
    val roles: List<ColorRole>
) {
    ACTIONS(
        R.string.theme_manager_group_actions,
        listOf(
            ColorRole.PRIMARY,
            ColorRole.ON_PRIMARY,
            ColorRole.PRIMARY_CONTAINER,
            ColorRole.ON_PRIMARY_CONTAINER
        )
    ),
    ACCENTS(
        R.string.theme_manager_group_accents,
        listOf(
            ColorRole.SECONDARY,
            ColorRole.ON_SECONDARY,
            ColorRole.SECONDARY_CONTAINER,
            ColorRole.ON_SECONDARY_CONTAINER,
            ColorRole.TERTIARY,
            ColorRole.ON_TERTIARY,
            ColorRole.TERTIARY_CONTAINER,
            ColorRole.ON_TERTIARY_CONTAINER
        )
    ),
    SURFACES(
        R.string.theme_manager_group_surfaces,
        listOf(
            ColorRole.BACKGROUND,
            ColorRole.SURFACE,
            ColorRole.SURFACE_VARIANT
        )
    ),
    TEXT(
        R.string.theme_manager_group_text,
        listOf(
            ColorRole.ON_BACKGROUND,
            ColorRole.ON_SURFACE,
            ColorRole.ON_SURFACE_VARIANT
        )
    ),
    OUTLINES(
        R.string.theme_manager_group_outlines,
        listOf(ColorRole.OUTLINE, ColorRole.OUTLINE_VARIANT)
    ),
    ERRORS(
        R.string.theme_manager_group_errors,
        listOf(
            ColorRole.ERROR,
            ColorRole.ON_ERROR,
            ColorRole.ERROR_CONTAINER,
            ColorRole.ON_ERROR_CONTAINER
        )
    )
}

private enum class ColorRole(
    @StringRes val titleRes: Int,
    @StringRes val usageRes: Int
) {
    PRIMARY(R.string.theme_manager_color_primary, R.string.theme_manager_usage_primary),
    ON_PRIMARY(R.string.theme_manager_color_on_primary, R.string.theme_manager_usage_on_primary),
    PRIMARY_CONTAINER(R.string.theme_manager_color_primary_container, R.string.theme_manager_usage_primary_container),
    ON_PRIMARY_CONTAINER(R.string.theme_manager_color_on_primary_container, R.string.theme_manager_usage_on_primary_container),
    SECONDARY(R.string.theme_manager_color_secondary, R.string.theme_manager_usage_secondary),
    ON_SECONDARY(R.string.theme_manager_color_on_secondary, R.string.theme_manager_usage_on_secondary),
    SECONDARY_CONTAINER(R.string.theme_manager_color_secondary_container, R.string.theme_manager_usage_secondary_container),
    ON_SECONDARY_CONTAINER(R.string.theme_manager_color_on_secondary_container, R.string.theme_manager_usage_on_secondary_container),
    TERTIARY(R.string.theme_manager_color_tertiary, R.string.theme_manager_usage_tertiary),
    ON_TERTIARY(R.string.theme_manager_color_on_tertiary, R.string.theme_manager_usage_on_tertiary),
    TERTIARY_CONTAINER(R.string.theme_manager_color_tertiary_container, R.string.theme_manager_usage_tertiary_container),
    ON_TERTIARY_CONTAINER(R.string.theme_manager_color_on_tertiary_container, R.string.theme_manager_usage_on_tertiary_container),
    BACKGROUND(R.string.theme_manager_color_background, R.string.theme_manager_usage_background),
    SURFACE(R.string.theme_manager_color_surface, R.string.theme_manager_usage_surface),
    SURFACE_VARIANT(R.string.theme_manager_color_surface_variant, R.string.theme_manager_usage_surface_variant),
    ON_BACKGROUND(R.string.theme_manager_color_on_background, R.string.theme_manager_usage_on_background),
    ON_SURFACE(R.string.theme_manager_color_on_surface, R.string.theme_manager_usage_on_surface),
    ON_SURFACE_VARIANT(R.string.theme_manager_color_on_surface_variant, R.string.theme_manager_usage_on_surface_variant),
    OUTLINE(R.string.theme_manager_color_outline, R.string.theme_manager_usage_outline),
    OUTLINE_VARIANT(R.string.theme_manager_color_outline_variant, R.string.theme_manager_usage_outline_variant),
    ERROR(R.string.theme_manager_color_error, R.string.theme_manager_usage_error),
    ON_ERROR(R.string.theme_manager_color_on_error, R.string.theme_manager_usage_on_error),
    ERROR_CONTAINER(R.string.theme_manager_color_error_container, R.string.theme_manager_usage_error_container),
    ON_ERROR_CONTAINER(R.string.theme_manager_color_on_error_container, R.string.theme_manager_usage_on_error_container);

    fun read(config: CustomThemeConfig): Int = when (this) {
        PRIMARY -> config.primary
        ON_PRIMARY -> config.onPrimary
        PRIMARY_CONTAINER -> config.primaryContainer
        ON_PRIMARY_CONTAINER -> config.onPrimaryContainer
        SECONDARY -> config.secondary
        ON_SECONDARY -> config.onSecondary
        SECONDARY_CONTAINER -> config.secondaryContainer
        ON_SECONDARY_CONTAINER -> config.onSecondaryContainer
        TERTIARY -> config.tertiary
        ON_TERTIARY -> config.onTertiary
        TERTIARY_CONTAINER -> config.tertiaryContainer
        ON_TERTIARY_CONTAINER -> config.onTertiaryContainer
        BACKGROUND -> config.background
        SURFACE -> config.surface
        SURFACE_VARIANT -> config.surfaceVariant
        ON_BACKGROUND -> config.onBackground
        ON_SURFACE -> config.onSurface
        ON_SURFACE_VARIANT -> config.onSurfaceVariant
        OUTLINE -> config.outline
        OUTLINE_VARIANT -> config.outlineVariant
        ERROR -> config.error
        ON_ERROR -> config.onError
        ERROR_CONTAINER -> config.errorContainer
        ON_ERROR_CONTAINER -> config.onErrorContainer
    }

    fun write(config: CustomThemeConfig, value: Int): CustomThemeConfig = when (this) {
        PRIMARY -> config.copy(primary = value)
        ON_PRIMARY -> config.copy(onPrimary = value)
        PRIMARY_CONTAINER -> config.copy(primaryContainer = value)
        ON_PRIMARY_CONTAINER -> config.copy(onPrimaryContainer = value)
        SECONDARY -> config.copy(secondary = value)
        ON_SECONDARY -> config.copy(onSecondary = value)
        SECONDARY_CONTAINER -> config.copy(secondaryContainer = value)
        ON_SECONDARY_CONTAINER -> config.copy(onSecondaryContainer = value)
        TERTIARY -> config.copy(tertiary = value)
        ON_TERTIARY -> config.copy(onTertiary = value)
        TERTIARY_CONTAINER -> config.copy(tertiaryContainer = value)
        ON_TERTIARY_CONTAINER -> config.copy(onTertiaryContainer = value)
        BACKGROUND -> config.copy(background = value)
        SURFACE -> config.copy(surface = value)
        SURFACE_VARIANT -> config.copy(surfaceVariant = value)
        ON_BACKGROUND -> config.copy(onBackground = value)
        ON_SURFACE -> config.copy(onSurface = value)
        ON_SURFACE_VARIANT -> config.copy(onSurfaceVariant = value)
        OUTLINE -> config.copy(outline = value)
        OUTLINE_VARIANT -> config.copy(outlineVariant = value)
        ERROR -> config.copy(error = value)
        ON_ERROR -> config.copy(onError = value)
        ERROR_CONTAINER -> config.copy(errorContainer = value)
        ON_ERROR_CONTAINER -> config.copy(onErrorContainer = value)
    }
}

private enum class ThemePreset(@StringRes val titleRes: Int) {
    MIDNIGHT(R.string.theme_manager_preset_midnight),
    OCEAN(R.string.theme_manager_preset_ocean),
    FOREST(R.string.theme_manager_preset_forest),
    SUNSET(R.string.theme_manager_preset_sunset),
    MONOCHROME(R.string.theme_manager_preset_monochrome);

    fun config(): CustomThemeConfig = when (this) {
        MIDNIGHT -> CustomThemeConfig.Default
        OCEAN -> CustomThemeConfig(
            name = "Ocean",
            primary = 0xFF36A9FF.toInt(),
            secondary = 0xFF4DD8DA.toInt(),
            tertiary = 0xFF8BE28B.toInt(),
            background = 0xFF061018.toInt(),
            surface = 0xFF0D1B25.toInt(),
            surfaceVariant = 0xFF183242.toInt(),
            outline = 0xFF3B6479.toInt()
        )
        FOREST -> CustomThemeConfig(
            name = "Forest",
            primary = 0xFF73D998.toInt(),
            secondary = 0xFFA8C96F.toInt(),
            tertiary = 0xFFE0BC66.toInt(),
            background = 0xFF07110C.toInt(),
            surface = 0xFF101D15.toInt(),
            surfaceVariant = 0xFF203629.toInt(),
            outline = 0xFF466854.toInt()
        )
        SUNSET -> CustomThemeConfig(
            name = "Sunset",
            primary = 0xFFFF7B72.toInt(),
            secondary = 0xFFFFB86B.toInt(),
            tertiary = 0xFFE987D8.toInt(),
            background = 0xFF160B11.toInt(),
            surface = 0xFF24131B.toInt(),
            surfaceVariant = 0xFF3A202C.toInt(),
            outline = 0xFF7A4C5E.toInt()
        )
        MONOCHROME -> CustomThemeConfig(
            name = "Monochrome",
            primary = 0xFFF1F1F1.toInt(),
            onPrimary = 0xFF111111.toInt(),
            secondary = 0xFFBDBDBD.toInt(),
            tertiary = 0xFF909090.toInt(),
            background = 0xFF090909.toInt(),
            surface = 0xFF151515.toInt(),
            surfaceVariant = 0xFF292929.toInt(),
            onBackground = 0xFFF5F5F5.toInt(),
            onSurface = 0xFFECECEC.toInt(),
            onSurfaceVariant = 0xFFBDBDBD.toInt(),
            outline = 0xFF525252.toInt()
        )
    }
}

private fun Int.toRgbHex(): String = "#%06X".format(this and 0x00FFFFFF)

private fun String.parseRgbHex(): Int? {
    val normalized = trim().removePrefix("#")
    if (normalized.length != 6) return null
    return normalized.toLongOrNull(16)?.let { (it or 0xFF000000L).toInt() }
}

private fun uniqueThemeName(base: String, existing: List<String>, fallback: String): String {
    val safeBase = base.trim().ifBlank { fallback }
        .take(CustomThemeConfig.MAX_NAME_LENGTH)
    val used = existing.map { it.trim().lowercase() }.toSet()
    if (safeBase.lowercase() !in used) return safeBase

    var index = 2
    while (index < 10_000) {
        val suffix = " $index"
        val candidate = safeBase
            .take((CustomThemeConfig.MAX_NAME_LENGTH - suffix.length).coerceAtLeast(1)) + suffix
        if (candidate.lowercase() !in used) return candidate
        index++
    }
    return UUID.randomUUID().toString().take(CustomThemeConfig.MAX_NAME_LENGTH)
}

private fun CustomThemeConfig.withCanvas(dark: Boolean): CustomThemeConfig {
    if (this.dark == dark) return this
    val canvas = if (dark) {
        copy(
            dark = true,
            background = 0xFF080A10.toInt(),
            surface = 0xFF12151D.toInt(),
            surfaceVariant = 0xFF202532.toInt(),
            onBackground = 0xFFF3F5FF.toInt(),
            onSurface = 0xFFE9ECF8.toInt(),
            onSurfaceVariant = 0xFFB6BED3.toInt(),
            outline = 0xFF3B4356.toInt(),
            error = 0xFFFF6B7A.toInt()
        )
    } else {
        copy(
            dark = false,
            background = 0xFFF7F8FC.toInt(),
            surface = 0xFFFFFFFF.toInt(),
            surfaceVariant = 0xFFE9EDF5.toInt(),
            onBackground = 0xFF161922.toInt(),
            onSurface = 0xFF1B1E27.toInt(),
            onSurfaceVariant = 0xFF555D6E.toInt(),
            outline = 0xFF8791A5.toInt(),
            error = 0xFFBA1A1A.toInt()
        )
    }
    return canvas.copy(
        primaryContainer = blendThemeColor(
            canvas.primary,
            canvas.background,
            if (dark) 0.24f else 0.20f
        ),
        onPrimaryContainer = canvas.onBackground,
        onSecondary = canvas.onPrimary,
        secondaryContainer = blendThemeColor(
            canvas.secondary,
            canvas.background,
            if (dark) 0.20f else 0.16f
        ),
        onSecondaryContainer = canvas.onBackground,
        onTertiary = canvas.onPrimary,
        tertiaryContainer = blendThemeColor(
            canvas.tertiary,
            canvas.background,
            if (dark) 0.20f else 0.16f
        ),
        onTertiaryContainer = canvas.onBackground,
        outlineVariant = blendThemeColor(canvas.outline, canvas.surface, 0.68f),
        onError = canvas.onPrimary,
        errorContainer = blendThemeColor(
            canvas.error,
            canvas.background,
            if (dark) 0.22f else 0.18f
        ),
        onErrorContainer = canvas.onBackground
    )
}

private fun blendThemeColor(foreground: Int, background: Int, foregroundAmount: Float): Int {
    val amount = foregroundAmount.coerceIn(0f, 1f)
    fun channel(shift: Int): Int {
        val foregroundChannel = (foreground ushr shift) and 0xFF
        val backgroundChannel = (background ushr shift) and 0xFF
        return (backgroundChannel + (foregroundChannel - backgroundChannel) * amount)
            .roundToInt()
            .coerceIn(0, 255)
    }
    return (0xFF shl 24) or
        (channel(16) shl 16) or
        (channel(8) shl 8) or
        channel(0)
}

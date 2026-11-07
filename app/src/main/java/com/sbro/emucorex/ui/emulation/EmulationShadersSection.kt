package com.sbro.emucorex.ui.emulation

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.ChevronRight
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Gradient
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.draw.scale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorex.R
import com.sbro.emucorex.data.RetroArchShaderPreset
import com.sbro.emucorex.data.ShaderPackInstallProgress
import com.sbro.emucorex.data.ShaderPackInstallStage
import com.sbro.emucorex.data.formatDownloadBytes
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.tvFocusGroup
import com.sbro.emucorex.ui.common.tvGamepadFocusableCard
import com.sbro.emucorex.ui.settings.ShaderPresetDialogGroup
import com.sbro.emucorex.ui.settings.buildShaderPresetDialogGroups
import com.sbro.emucorex.ui.settings.shaderPresetCategoryTitle
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonChipShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import java.io.File

private const val SHADER_PRESET_USE_GLOBAL = "__emucorex_use_global_shader__"

/**
 * In-game shader tab. Selection is scoped to the running game: the effective
 * value comes from the game profile when an override exists, otherwise from the
 * app-wide setting. Downloading and importing a pack are delegated to the view
 * model so they survive the menu being closed.
 */
@Composable
internal fun EmulationShadersSection(
    state: ShaderBrowserState,
    onDownloadPack: () -> Unit,
    onImportPack: (Uri) -> Unit,
    onSelectPreset: (String) -> Unit,
    onUseGlobal: () -> Unit,
    onSetEnabled: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (state.loading && state.presets.isEmpty() && !state.busy) {
            ShaderBrowserLoadingCard()
        } else {
            ShaderSelectionCard(
                state = state,
                onSelectPreset = onSelectPreset,
                onUseGlobal = onUseGlobal,
                onSetEnabled = onSetEnabled
            )
            ShaderPackCard(
                state = state,
                onDownloadPack = onDownloadPack,
                onImportPack = onImportPack
            )
        }
    }
}

@Composable
private fun ShaderBrowserLoadingCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 18.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            CircularProgressIndicator(
                modifier = Modifier.size(18.dp),
                strokeWidth = 2.dp,
                color = MaterialTheme.colorScheme.primary
            )
            Text(
                text = stringResource(R.string.loading),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ShaderSelectionCard(
    state: ShaderBrowserState,
    onSelectPreset: (String) -> Unit,
    onUseGlobal: () -> Unit,
    onSetEnabled: (Boolean) -> Unit
) {
    // The picker is transient UI: do not restore it from saved state when the
    // in-game menu is reopened on its last tab.
    var showPicker by remember { mutableStateOf(false) }
    val selection = state.selection
    val effectiveLabel = shaderPresetLabel(state.presets, selection.effectivePreset)
        ?: stringResource(R.string.settings_shader_preset_none)
    val globalLabel = shaderPresetLabel(state.presets, selection.globalPreset)
        ?: stringResource(R.string.settings_shader_preset_none)
    val canEnable = selection.gamePreset.isNotEmpty() || selection.globalPreset.isNotEmpty()
    val helpText = stringResource(R.string.emulation_shaders_help_enable)
    val scopeHelp = stringResource(R.string.emulation_shaders_help_scope)

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = stringResource(R.string.settings_shader_preset),
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                SettingHelpButton(
                    title = stringResource(R.string.emulation_shaders_title),
                    description = scopeHelp
                )
            }
            Surface(
                onClick = { showPicker = true },
                modifier = Modifier.fillMaxWidth(),
                shape = neonShape(14.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.72f),
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                )
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 11.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Gradient,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(22.dp)
                    )
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = effectiveLabel,
                            style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            ShaderSourceBadge(usesGameProfile = selection.usesGameProfile)
                            if (selection.usesGameProfile) {
                                Text(
                                    text = stringResource(R.string.emulation_shaders_global_value, globalLabel),
                                    modifier = Modifier.weight(1f, fill = false),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                    }
                    Icon(
                        imageVector = Icons.Rounded.ChevronRight,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.size(20.dp)
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = stringResource(R.string.emulation_shaders_enable),
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                SettingHelpButton(
                    title = stringResource(R.string.emulation_shaders_enable),
                    description = helpText
                )
                Switch(
                    checked = selection.effectiveEnabled,
                    onCheckedChange = onSetEnabled,
                    enabled = canEnable || selection.effectiveEnabled,
                    modifier = Modifier.scale(0.85f),
                    colors = androidx.compose.material3.SwitchDefaults.colors(
                        checkedThumbColor = MaterialTheme.colorScheme.primary,
                        checkedTrackColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                    )
                )
            }
            Text(
                text = scopeHelp,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }

    if (showPicker) {
        ShaderPresetPickerDialog(
            state = state,
            onDismiss = { showPicker = false },
            onSelect = { path ->
                if (path == SHADER_PRESET_USE_GLOBAL) {
                    onUseGlobal()
                } else {
                    onSelectPreset(path)
                }
                showPicker = false
            }
        )
    }
}

@Composable
private fun ShaderSourceBadge(usesGameProfile: Boolean) {
    Surface(
        shape = neonChipShape(),
        color = if (usesGameProfile) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
        } else {
            MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
        },
        border = BorderStroke(
            1.dp,
            if (usesGameProfile) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.32f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            }
        )
    ) {
        Text(
            text = stringResource(
                if (usesGameProfile) {
                    R.string.emulation_shaders_source_game
                } else {
                    R.string.emulation_shaders_source_global
                }
            ),
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
            color = if (usesGameProfile) {
                MaterialTheme.colorScheme.primary
            } else {
                MaterialTheme.colorScheme.onSurfaceVariant
            }
        )
    }
}

@Composable
private fun ShaderPackCard(
    state: ShaderBrowserState,
    onDownloadPack: () -> Unit,
    onImportPack: (Uri) -> Unit
) {
    val context = LocalContext.current
    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        // Persist the grant so the pack still imports if the activity is recreated.
        runCatching {
            context.contentResolver.takePersistableUriPermission(
                uri,
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        }
        onImportPack(uri)
    }
    val progress = state.progress
    val hasPresets = state.presets.isNotEmpty()
    val downloadProgress = progress
        ?.takeIf { it.stage == ShaderPackInstallStage.DOWNLOADING }
        ?.fraction

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = tween(220))
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Surface(
                    shape = neonShape(12.dp),
                    color = MaterialTheme.colorScheme.primaryContainer,
                    contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                ) {
                    Icon(
                        imageVector = Icons.Rounded.CloudDownload,
                        contentDescription = null,
                        modifier = Modifier.padding(8.dp).size(19.dp)
                    )
                }
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    Text(
                        text = stringResource(R.string.emulation_shaders_pack_title),
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = if (hasPresets) {
                            stringResource(R.string.emulation_shaders_pack_ready, state.presetCount)
                        } else {
                            stringResource(R.string.emulation_shaders_pack_hint)
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                if (hasPresets) {
                    Icon(
                        imageVector = Icons.Rounded.Check,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(20.dp)
                    )
                }
            }

            AnimatedVisibility(visible = state.busy) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    if (downloadProgress != null) {
                        LinearProgressIndicator(
                            progress = { downloadProgress },
                            modifier = Modifier.fillMaxWidth()
                        )
                    } else {
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    }
                    Text(
                        text = shaderPackProgressLabel(progress),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Button(
                onClick = onDownloadPack,
                enabled = !state.busy,
                shape = neonButtonShape(),
                modifier = Modifier.fillMaxWidth(),
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp)
            ) {
                if (state.busy && progress?.stage == ShaderPackInstallStage.DOWNLOADING) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(16.dp),
                        strokeWidth = 2.dp,
                        color = LocalContentColor.current
                    )
                } else {
                    Icon(
                        imageVector = if (state.installed) Icons.Rounded.Refresh else Icons.Rounded.CloudDownload,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                }
                Spacer(Modifier.width(8.dp))
                Text(
                    text = stringResource(
                        if (state.installed) R.string.content_reinstall else R.string.content_download_install
                    ),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            OutlinedButton(
                onClick = { importLauncher.launch(arrayOf("application/zip", "application/octet-stream", "*/*")) },
                enabled = !state.busy,
                shape = neonButtonShape(),
                modifier = Modifier.fillMaxWidth(),
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp)
            ) {
                Icon(
                    imageVector = Icons.Rounded.FolderOpen,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp)
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    text = stringResource(R.string.settings_shader_pack_import),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }

            AnimatedContent(
                targetState = state.message,
                transitionSpec = {
                    fadeIn(animationSpec = tween(180)) togetherWith fadeOut(animationSpec = tween(120))
                },
                label = "shaderPackMessage"
            ) { message ->
                if (message != null) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            imageVector = if (message == ShaderPackMessage.Installed) {
                                Icons.Rounded.Check
                            } else {
                                Icons.Rounded.Close
                            },
                            contentDescription = null,
                            tint = if (message == ShaderPackMessage.Installed) {
                                MaterialTheme.colorScheme.primary
                            } else {
                                MaterialTheme.colorScheme.error
                            },
                            modifier = Modifier.size(18.dp)
                        )
                        Text(
                            text = stringResource(
                                if (message == ShaderPackMessage.Installed) {
                                    R.string.settings_shader_pack_installed
                                } else {
                                    R.string.settings_shader_pack_failed
                                }
                            ),
                            style = MaterialTheme.typography.bodySmall,
                            color = if (message == ShaderPackMessage.Installed) {
                                MaterialTheme.colorScheme.onSurfaceVariant
                            } else {
                                MaterialTheme.colorScheme.error
                            }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun shaderPackProgressLabel(progress: ShaderPackInstallProgress?): String {
    if (progress == null) return ""
    return when (progress.stage) {
        ShaderPackInstallStage.INSTALLING -> stringResource(R.string.settings_shader_pack_working)
        ShaderPackInstallStage.DOWNLOADING -> {
            val percent = ((progress.fraction ?: 0f) * 100f).toInt()
            val status = stringResource(R.string.content_downloading_percent, percent)
            if (progress.downloadedBytes > 0L && progress.totalBytes > 0L) {
                "$status · ${formatDownloadBytes(progress.downloadedBytes)} / " +
                    formatDownloadBytes(progress.totalBytes)
            } else {
                status
            }
        }
    }
}

@Composable
private fun ShaderPresetPickerDialog(
    state: ShaderBrowserState,
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit
) {
    var query by remember { mutableStateOf("") }
    var expandedCategoryKey by remember { mutableStateOf<String?>(null) }
    // Keep the category list where the player left it when they open a
    // category and then go back: only the preset list itself should scroll.
    var savedCategoryIndex by remember { mutableStateOf(0) }
    var savedCategoryOffset by remember { mutableStateOf(0) }
    var restoreCategoryScroll by remember { mutableStateOf(false) }
    val selection = state.selection
    val selectedPath = when (selection.overrideEnabled) {
        null -> SHADER_PRESET_USE_GLOBAL
        false -> ""
        true -> selection.profilePreset
    }
    val noneLabel = stringResource(R.string.settings_shader_preset_none)
    val generalLabel = stringResource(R.string.settings_general_tab)
    val globalPresetLabel = shaderPresetLabel(state.presets, selection.globalPreset)
        ?: stringResource(R.string.settings_shader_preset_none)
    val globalScopeLabel = stringResource(R.string.controls_editor_global_scope)
    val globalValueLabel = stringResource(
        R.string.emulation_shaders_global_value,
        globalPresetLabel
    )
    val useGlobalLabel = "$globalScopeLabel · $globalValueLabel"
    val listState = rememberLazyListState()
    val groups = remember(state.presets, noneLabel, useGlobalLabel, generalLabel) {
        buildShaderPresetDialogGroups(
            presets = state.presets,
            leadingOptions = listOf(SHADER_PRESET_USE_GLOBAL to useGlobalLabel),
            noneLabel = noneLabel,
            generalLabel = generalLabel,
            query = ""
        )
    }
    val trimmedQuery = query.trim()
    val results = remember(state.presets, trimmedQuery, noneLabel, useGlobalLabel) {
        buildList {
            if (useGlobalLabel.contains(trimmedQuery, ignoreCase = true)) {
                add(SHADER_PRESET_USE_GLOBAL to useGlobalLabel)
            }
            if (noneLabel.contains(trimmedQuery, ignoreCase = true)) {
                add("" to noneLabel)
            }
            addAll(
                state.presets
                    .filter { it.label.contains(trimmedQuery, ignoreCase = true) }
                    .sortedBy { it.label.lowercase() }
                    .map { it.absolutePath to it.label }
            )
        }
    }
    val expandedGroup = remember(groups, expandedCategoryKey) {
        groups.firstOrNull { it.key == expandedCategoryKey }
    }

    LaunchedEffect(query) {
        expandedCategoryKey = null
        restoreCategoryScroll = false
        listState.scrollToItem(0)
    }
    LaunchedEffect(expandedCategoryKey, groups) {
        when {
            expandedCategoryKey != null && expandedGroup == null -> {
                expandedCategoryKey = null
            }
            expandedCategoryKey != null -> {
                // The header and the search field are the first two items.
                // Scroll them out of the way so presets use the whole dialog.
                listState.scrollToItem(2)
            }
            restoreCategoryScroll -> {
                restoreCategoryScroll = false
                listState.scrollToItem(savedCategoryIndex, savedCategoryOffset)
            }
            query.isBlank() -> listState.scrollToItem(0)
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .padding(horizontal = 12.dp, vertical = 8.dp),
            contentAlignment = Alignment.Center
        ) {
            val isLandscape = maxWidth > maxHeight
            Surface(
                modifier = Modifier
                    .fillMaxWidth(if (isLandscape) 0.78f else 0.94f)
                    .widthIn(max = if (isLandscape) 820.dp else 620.dp)
                    .fillMaxHeight(0.94f),
                shape = neonShape(26.dp),
                color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
                )
            ) {
                Box(modifier = Modifier.fillMaxSize()) {
                    LazyColumn(
                        state = listState,
                        modifier = Modifier
                            .fillMaxSize()
                            .tvFocusGroup(),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 16.dp)
                    ) {
                        item(key = "shader-picker-header") {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(top = 6.dp, end = 44.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Surface(
                                    modifier = Modifier.size(42.dp),
                                    shape = neonShape(16.dp),
                                    color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.78f)
                                ) {
                                    Box(contentAlignment = Alignment.Center) {
                                        Icon(
                                            imageVector = Icons.Rounded.Gradient,
                                            contentDescription = null,
                                            tint = MaterialTheme.colorScheme.primary,
                                            modifier = Modifier.size(24.dp)
                                        )
                                    }
                                }
                                Column(
                                    modifier = Modifier.weight(1f),
                                    verticalArrangement = Arrangement.spacedBy(2.dp)
                                ) {
                                    Text(
                                        text = stringResource(R.string.emulation_shaders_picker_title),
                                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                                        color = MaterialTheme.colorScheme.onSurface
                                    )
                                    Text(
                                        text = stringResource(R.string.emulation_shaders_help_scope),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        maxLines = 2,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                        }
                        item(key = "shader-picker-search") {
                            OutlinedTextField(
                                value = query,
                                onValueChange = { query = it },
                                modifier = Modifier.fillMaxWidth(),
                                placeholder = { Text(stringResource(R.string.emulation_shaders_search)) },
                                singleLine = true,
                                leadingIcon = {
                                    Icon(imageVector = Icons.Rounded.Search, contentDescription = null)
                                },
                                trailingIcon = {
                                    if (query.isNotEmpty()) {
                                        IconButton(onClick = { query = "" }) {
                                            Icon(
                                                imageVector = Icons.Rounded.Close,
                                                contentDescription = stringResource(R.string.home_search_clear)
                                            )
                                        }
                                    }
                                },
                                shape = neonShape(16.dp)
                            )
                        }
                        if (trimmedQuery.isNotEmpty()) {
                            if (results.isEmpty()) {
                                item(key = "shader-picker-empty") {
                                    ShaderPickerEmptyState()
                                }
                            } else {
                                items(
                                    items = results,
                                    key = { (path, _) -> "shader-option:" + path.ifEmpty { "none" } }
                                ) { (path, label) ->
                                    ShaderOptionRow(
                                        path = path,
                                        label = label,
                                        selected = path == selectedPath,
                                        onSelect = onSelect
                                    )
                                }
                            }
                        } else if (expandedGroup == null) {
                            if (state.presets.isEmpty()) {
                                item(key = "shader-picker-empty") {
                                    ShaderPickerEmptyState()
                                }
                            }
                            items(items = groups, key = { "shader-group:" + it.key }) { group ->
                                ShaderCategoryCard(
                                    group = group,
                                    selected = group.options.any { (path, _) -> path == selectedPath },
                                    onClick = {
                                        savedCategoryIndex = listState.firstVisibleItemIndex
                                        savedCategoryOffset = listState.firstVisibleItemScrollOffset
                                        restoreCategoryScroll = false
                                        expandedCategoryKey = group.key
                                    }
                                )
                            }
                        } else {
                            item(key = "shader-category-header") {
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    TextButton(
                                        onClick = {
                                            restoreCategoryScroll = true
                                            expandedCategoryKey = null
                                        }
                                    ) {
                                        Text(stringResource(R.string.back))
                                    }
                                    Text(
                                        text = expandedGroup.title,
                                        modifier = Modifier.weight(1f),
                                        style = MaterialTheme.typography.titleSmall.copy(
                                            fontWeight = FontWeight.SemiBold
                                        ),
                                        color = MaterialTheme.colorScheme.onSurface,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                            items(
                                items = expandedGroup.options,
                                key = { (path, _) -> "shader-option:" + path.ifEmpty { "none" } }
                            ) { (path, label) ->
                                ShaderOptionRow(
                                    path = path,
                                    label = if (expandedGroup.stripsCategoryPrefix) {
                                        label.substringAfter('/')
                                    } else {
                                        label
                                    },
                                    selected = path == selectedPath,
                                    onSelect = onSelect
                                )
                            }
                        }
                    }
                    IconButton(
                        onClick = onDismiss,
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .padding(top = 4.dp, end = 4.dp)
                            .background(
                                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f),
                                shape = CircleShape
                            )
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Close,
                            contentDescription = stringResource(R.string.cancel)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ShaderCategoryCard(
    group: ShaderPresetDialogGroup,
    selected: Boolean,
    onClick: () -> Unit
) {
    val shape = neonShape(16.dp)
    Surface(
        onClick = onClick,
        modifier = Modifier
            .fillMaxWidth()
            .tvGamepadFocusableCard(shape = shape, addFocusTarget = false),
        shape = shape,
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.34f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.58f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
            }
        )
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.FolderOpen,
                contentDescription = null,
                tint = if (selected) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                }
            )
            Text(
                text = group.title,
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Surface(
                shape = RoundedCornerShape(50),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.72f),
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                )
            ) {
                Text(
                    text = group.options.size.toString(),
                    modifier = Modifier.padding(horizontal = 9.dp, vertical = 3.dp),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Icon(
                imageVector = Icons.Rounded.ChevronRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

@Composable
private fun ShaderOptionRow(
    path: String,
    label: String,
    selected: Boolean,
    onSelect: (String) -> Unit
) {
    val shape = neonShape(14.dp)
    Surface(
        onClick = { onSelect(path) },
        modifier = Modifier
            .fillMaxWidth()
            .tvGamepadFocusableCard(shape = shape, addFocusTarget = false),
        shape = shape,
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.5f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f)
            }
        )
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = if (path == SHADER_PRESET_USE_GLOBAL || path.isEmpty()) {
                    label
                } else {
                    shaderPresetCategoryTitle(label.substringAfter('/'))
                },
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
            if (selected) {
                Icon(
                    imageVector = Icons.Rounded.Check,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
        }
    }
}

@Composable
private fun ShaderPickerEmptyState() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 28.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = stringResource(R.string.emulation_shaders_empty),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center
        )
    }
}

private fun shaderPresetLabel(presets: List<RetroArchShaderPreset>, path: String): String? {
    val selected = path.trim()
    if (selected.isEmpty()) return null
    val preset = findShaderPreset(presets, selected)
    if (preset != null) return shaderPresetShortLabel(preset)
    return File(selected).nameWithoutExtension.takeIf(String::isNotBlank)
}

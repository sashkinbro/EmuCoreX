package com.sbro.emucorex.ui.saves

import android.annotation.SuppressLint
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
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
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.PhoneAndroid
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Tune
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.layout.ContentScale
import com.sbro.emucorex.R
import com.sbro.emucorex.data.SaveStateEntryInfo
import com.sbro.emucorex.data.SaveStateImportFormat
import com.sbro.emucorex.data.SaveStateImportPreview
import com.sbro.emucorex.data.SaveStateImportSource
import com.sbro.emucorex.data.SaveStateRepository
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SaveManagerScreen(
    gamePath: String? = null,
    gameTitle: String? = null,
    gameSerial: String? = null,
    onLoadClick: (String, Int) -> Unit,
    onBackClick: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember(context) { SaveStateRepository(context) }
    val scope = rememberCoroutineScope()
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val backupSuccessMessage = stringResource(R.string.save_manager_backup_success)
    val backupFailureMessage = stringResource(R.string.save_manager_backup_failed)
    val restoreFailureMessage = stringResource(R.string.save_manager_restore_failed)
    val importSuccessTemplate = stringResource(R.string.save_manager_import_success)
    val importPartialTemplate = stringResource(R.string.save_manager_import_partial)
    val deleteSuccessMessage = stringResource(R.string.save_manager_delete_success)
    val deleteFailureMessage = stringResource(R.string.save_manager_delete_failed)

    var entries by remember(gamePath, gameTitle, gameSerial) { mutableStateOf<List<SaveStateEntryInfo>>(emptyList()) }
    var previewPaths by remember(gamePath, gameTitle, gameSerial) { mutableStateOf<Map<String, String>>(emptyMap()) }
    var isWorking by remember { mutableStateOf(false) }
    var isPreparingEntries by remember(gamePath, gameTitle, gameSerial) { mutableStateOf(true) }
    var isResolvingEntries by remember(gamePath, gameTitle, gameSerial) { mutableStateOf(false) }
    val pendingDelete = remember { mutableStateOf<SaveStateEntryInfo?>(null) }
    var showImportSourceDialog by remember { mutableStateOf(false) }
    var selectedImportSource by remember { mutableStateOf(SaveStateImportSource.AUTO) }
    var importPreview by remember { mutableStateOf<SaveStateImportPreview?>(null) }
    var refreshGeneration by remember(gamePath, gameTitle, gameSerial) { mutableIntStateOf(0) }

    val isFiltered = !gamePath.isNullOrBlank()
    val screenSubtitle = remember(isFiltered, gameTitle, entries) {
        when {
            isFiltered && entries.isNotEmpty() -> entries.first().gameTitle
            isFiltered && gameTitle.isUsableDisplayTitle() -> gameTitle
            isFiltered -> ""
            else -> null
        }
    }

    fun refresh() {
        val generation = refreshGeneration + 1
        refreshGeneration = generation
        scope.launch {
            isWorking = true
            isPreparingEntries = true
            entries = emptyList()
            previewPaths = emptyMap()
            val initialEntries = withContext(Dispatchers.IO) {
                repository.listEntries(
                    filterGamePath = gamePath,
                    filterGameTitle = gameTitle,
                    filterGameSerial = gameSerial
                )
            }
            if (refreshGeneration != generation) return@launch

            entries = initialEntries
            isPreparingEntries = false
            isWorking = false

            val resolvedEntries = if (gamePath.isNullOrBlank() && initialEntries.isNotEmpty()) {
                isResolvingEntries = true
                withContext(Dispatchers.IO) {
                    repository.enrichGlobalEntries(initialEntries)
                }
            } else {
                initialEntries
            }
            if (refreshGeneration != generation) return@launch
            if (resolvedEntries !== initialEntries) {
                entries = resolvedEntries
            }
            isResolvingEntries = false

            scope.launch(Dispatchers.IO) {
                resolvedEntries.forEach { entry ->
                    val previewPath = repository.getPreviewImagePath(entry) ?: return@forEach
                    withContext(Dispatchers.Main) {
                        if (refreshGeneration != generation) return@withContext
                        if (previewPaths[entry.absolutePath] == previewPath) return@withContext
                        previewPaths = previewPaths + (entry.absolutePath to previewPath)
                    }
                }
            }
        }
    }

    LaunchedEffect(gamePath, gameTitle, gameSerial) {
        refresh()
    }

    val backupLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/zip")
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            isWorking = true
            val success = withContext(Dispatchers.IO) {
                repository.backupEntries(entries, uri)
            }
            isWorking = false
            Toast.makeText(
                context,
                if (success) backupSuccessMessage else backupFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
            refresh()
        }
    }

    val restoreLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            isWorking = true
            val preview = runCatching {
                withContext(Dispatchers.IO) {
                    repository.analyzeImport(
                        source = uri,
                        requestedSource = selectedImportSource,
                        gamePath = gamePath,
                        gameSerial = gameSerial
                    )
                }
            }.getOrNull()
            isWorking = false
            if (preview == null) {
                Toast.makeText(context, restoreFailureMessage, Toast.LENGTH_SHORT).show()
            } else {
                importPreview = preview
            }
        }
    }

    if (showImportSourceDialog) {
        SaveImportSourceDialog(
            onDismiss = { showImportSourceDialog = false },
            onSourceSelected = { source ->
                selectedImportSource = source
                showImportSourceDialog = false
                restoreLauncher.launch(arrayOf("application/zip", "application/octet-stream", "*/*"))
            }
        )
    }

    importPreview?.let { preview ->
        SaveImportPreviewDialog(
            preview = preview,
            isFiltered = isFiltered,
            onDismiss = {
                repository.discardImport(preview)
                importPreview = null
            },
            onConfirm = {
                importPreview = null
                scope.launch {
                    isWorking = true
                    val result = withContext(Dispatchers.IO) { repository.importStates(preview) }
                    isWorking = false
                    val message = when {
                        result.importedCount == 0 -> restoreFailureMessage
                        result.failedCount > 0 -> String.format(
                            Locale.getDefault(),
                            importPartialTemplate,
                            result.importedCount,
                            result.failedCount
                        )
                        else -> String.format(Locale.getDefault(), importSuccessTemplate, result.importedCount)
                    }
                    Toast.makeText(context, message, Toast.LENGTH_LONG).show()
                    refresh()
                }
            }
        )
    }

    pendingDelete.value?.let { entry ->
        AlertDialog(
            onDismissRequest = { pendingDelete.value = null },
            title = { Text(stringResource(R.string.save_manager_delete_confirm_title)) },
            text = {
                Text(
                    if (entry.isAutoSave) {
                        stringResource(
                            R.string.save_manager_delete_confirm_body_named,
                            entry.gameTitle,
                            stringResource(R.string.save_manager_auto_save_label)
                        )
                    } else {
                        stringResource(
                            R.string.save_manager_delete_confirm_body,
                            entry.gameTitle,
                            entry.slot
                        )
                    }
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            isWorking = true
                            val success = withContext(Dispatchers.IO) {
                                repository.deleteEntry(entry)
                            }
                            isWorking = false
                            pendingDelete.value = null
                            Toast.makeText(
                                context,
                                if (success) deleteSuccessMessage else deleteFailureMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                            refresh()
                        }
                    }
                ) {
                    Text(stringResource(R.string.save_manager_delete_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete.value = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = 0.dp,
                bottom = 24.dp + bottomInset
            ),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (isPreparingEntries) {
                item {
                    SaveManagerHeaderSkeleton(topInset = topInset)
                }
                item {
                    SaveManagerActionsSkeleton()
                }
                items(3) {
                    SaveEntrySkeletonCard()
                }
            } else {
                item {
                    SaveManagerHeader(
                        topInset = topInset,
                        subtitle = screenSubtitle,
                        isWorking = isWorking,
                        onBackClick = onBackClick
                    )
                }

                item {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        FilledTonalButton(
                            onClick = {
                                val fileName = if (isFiltered) {
                                    "${(screenSubtitle ?: "game").replace(' ', '_')}_saves.zip"
                                } else {
                                    "EmuCoreX_saves.zip"
                                }
                                backupLauncher.launch(fileName)
                            },
                            enabled = entries.isNotEmpty() && !isWorking,
                            modifier = Modifier.weight(0.86f),
                            colors = ButtonDefaults.filledTonalButtonColors(
                                containerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.22f),
                                contentColor = MaterialTheme.colorScheme.primary
                            )
                        ) {
                            Icon(Icons.Rounded.Save, contentDescription = null)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = stringResource(R.string.save_manager_backup_action),
                                style = MaterialTheme.typography.labelLarge,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        OutlinedButton(
                            onClick = { showImportSourceDialog = true },
                            enabled = !isWorking,
                            modifier = Modifier.weight(1.14f),
                            colors = ButtonDefaults.outlinedButtonColors(
                                contentColor = MaterialTheme.colorScheme.onSurface
                            )
                        ) {
                            Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = stringResource(R.string.save_manager_restore_action),
                                style = MaterialTheme.typography.labelLarge,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }

                if (entries.isEmpty()) {
                    item {
                        EmptyStateCard(isFiltered = isFiltered)
                    }
                } else {
                    items(entries, key = { it.absolutePath }) { entry ->
                        SaveEntryCard(
                            entry = entry,
                            previewPath = previewPaths[entry.absolutePath],
                            showLoadUnavailable = !isResolvingEntries,
                            onLoadClick = {
                                val path = entry.gamePath ?: return@SaveEntryCard
                                onLoadClick(path, entry.slot)
                            },
                            onDeleteClick = { pendingDelete.value = entry }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SaveImportSourceDialog(
    onDismiss: () -> Unit,
    onSourceSelected: (SaveStateImportSource) -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.save_manager_import_source_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    text = stringResource(R.string.save_manager_import_source_body),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                SaveImportSourceOption(
                    icon = Icons.Rounded.PhoneAndroid,
                    title = stringResource(R.string.save_manager_import_nethersx2),
                    description = stringResource(R.string.save_manager_import_nethersx2_desc),
                    onClick = { onSourceSelected(SaveStateImportSource.NETHERSX2) }
                )
                SaveImportSourceOption(
                    icon = Icons.Rounded.Gamepad,
                    title = stringResource(R.string.save_manager_import_armsx2),
                    description = stringResource(R.string.save_manager_import_armsx2_desc),
                    onClick = { onSourceSelected(SaveStateImportSource.ARMSX2) }
                )
                SaveImportSourceOption(
                    icon = Icons.Rounded.FolderZip,
                    title = stringResource(R.string.save_manager_import_emucorex),
                    description = stringResource(R.string.save_manager_import_emucorex_desc),
                    onClick = { onSourceSelected(SaveStateImportSource.EMUCOREX) }
                )
                SaveImportSourceOption(
                    icon = Icons.Rounded.Tune,
                    title = stringResource(R.string.save_manager_import_auto),
                    description = stringResource(R.string.save_manager_import_auto_desc),
                    onClick = { onSourceSelected(SaveStateImportSource.AUTO) }
                )
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(android.R.string.cancel))
            }
        }
    )
}

@Composable
private fun SaveImportSourceOption(
    icon: ImageVector,
    title: String,
    description: String,
    onClick: () -> Unit
) {
    val shape = RoundedCornerShape(18.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clip(shape)
            .clickable(onClick = onClick),
        shape = shape,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 13.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(44.dp),
                shape = RoundedCornerShape(14.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                }
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 12.dp),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun SaveImportPreviewDialog(
    preview: SaveStateImportPreview,
    isFiltered: Boolean,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit
) {
    val sourceName = when (preview.detectedSource) {
        SaveStateImportSource.NETHERSX2 -> stringResource(R.string.save_manager_import_nethersx2)
        SaveStateImportSource.ARMSX2 -> stringResource(R.string.save_manager_import_armsx2)
        SaveStateImportSource.EMUCOREX -> stringResource(R.string.save_manager_import_emucorex)
        SaveStateImportSource.AUTO -> stringResource(R.string.save_manager_import_auto_detected)
    }
    val formats = preview.candidates.map { candidate ->
        when (candidate.format) {
            SaveStateImportFormat.CURRENT -> stringResource(R.string.save_manager_import_format_current)
            SaveStateImportFormat.AETHERSX2 -> "AetherSX2"
            SaveStateImportFormat.NETHERSX2 -> "NetherSX2"
            SaveStateImportFormat.UNKNOWN -> stringResource(R.string.save_manager_import_format_unknown)
        }
    }.distinct().joinToString()

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.save_manager_import_preview_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(18.dp),
                    color = MaterialTheme.colorScheme.surface,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
                ) {
                    Column(
                        modifier = Modifier.padding(14.dp),
                        verticalArrangement = Arrangement.spacedBy(5.dp)
                    ) {
                        Text(
                            text = preview.displayName,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = stringResource(R.string.save_manager_import_detected_source, sourceName),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        if (formats.isNotBlank()) {
                            Text(
                                text = stringResource(R.string.save_manager_import_detected_format, formats),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
                Text(
                    text = stringResource(
                        if (isFiltered) R.string.save_manager_import_target_game else R.string.save_manager_import_target_library
                    ),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = stringResource(R.string.save_manager_import_ready_count, preview.importableCount),
                    style = MaterialTheme.typography.titleMedium,
                    color = if (preview.importableCount > 0) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                    fontWeight = FontWeight.SemiBold
                )
                if (preview.skippedCount > 0) {
                    Text(
                        text = stringResource(R.string.save_manager_import_skipped_count, preview.skippedCount),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                if (preview.incompatibleCount > 0) {
                    Text(
                        text = stringResource(R.string.save_manager_import_incompatible_count, preview.incompatibleCount),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error
                    )
                }
                Text(
                    text = stringResource(R.string.save_manager_import_no_overwrite_note),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onConfirm, enabled = preview.importableCount > 0) {
                Text(stringResource(R.string.save_manager_import_confirm))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(android.R.string.cancel))
            }
        }
    )
}

@Composable
private fun SaveManagerHeader(
    topInset: androidx.compose.ui.unit.Dp,
    subtitle: String?,
    isWorking: Boolean,
    onBackClick: () -> Unit
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = topInset, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            ScreenTopBar(
                title = stringResource(R.string.save_manager_title),
                subtitle = subtitle,
                onBackClick = onBackClick,
                modifier = Modifier.weight(1f)
            )
            if (isWorking) {
                CircularProgressIndicator(
                    modifier = Modifier.size(22.dp),
                    strokeWidth = 2.dp
                )
            }
        }
    }
}

@Composable
private fun SaveManagerHeaderSkeleton(topInset: androidx.compose.ui.unit.Dp) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = topInset, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            SkeletonBlock(
                modifier = Modifier
                    .size(48.dp)
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 8.dp, end = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                SkeletonBlock(
                    modifier = Modifier
                        .fillMaxWidth(0.56f)
                        .height(32.dp)
                )
                SkeletonBlock(
                    modifier = Modifier
                        .fillMaxWidth(0.72f)
                        .height(18.dp)
                )
            }
            SkeletonBlock(
                modifier = Modifier
                    .size(22.dp)
            )
        }

        Surface(
            shape = RoundedCornerShape(22.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.26f)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                SkeletonBlock(
                    modifier = Modifier.size(18.dp)
                )
                Column(
                    modifier = Modifier.padding(start = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    SkeletonBlock(
                        modifier = Modifier
                            .width(180.dp)
                            .height(22.dp)
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(16.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun SaveManagerActionsSkeleton() {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        SkeletonBlock(
            modifier = Modifier
                .weight(0.86f)
                .height(52.dp)
        )
        SkeletonBlock(
            modifier = Modifier
                .weight(1.14f)
                .height(52.dp)
        )
    }
}

@Composable
private fun SaveEntryCard(
    entry: SaveStateEntryInfo,
    previewPath: String?,
    showLoadUnavailable: Boolean,
    onLoadClick: () -> Unit,
    onDeleteClick: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        tonalElevation = 1.dp,
        shadowElevation = 3.dp,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(
            width = 1.dp,
            color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top
            ) {
                Surface(
                    modifier = Modifier
                        .width(116.dp)
                        .height(86.dp),
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f),
                    tonalElevation = 0.dp,
                    shadowElevation = 0.dp
                ) {
                    GameCoverArt(
                        coverPath = previewPath,
                        fallbackTitle = entry.gameTitle,
                        modifier = Modifier
                            .fillMaxSize()
                            .clip(RoundedCornerShape(14.dp)),
                        contentScale = ContentScale.Crop
                    )
                }

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(5.dp)
                ) {
                    Text(
                        text = entry.gameTitle,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = if (entry.isAutoSave) {
                            stringResource(
                                R.string.save_manager_entry_meta_auto,
                                entry.serial,
                                stringResource(R.string.save_manager_auto_save_label)
                            )
                        } else {
                            stringResource(
                                R.string.save_manager_entry_meta,
                                entry.serial,
                                entry.slot
                            )
                        },
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = formatSaveEntryDate(entry.lastModified),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(
                            R.string.save_manager_entry_size,
                            formatBytes(entry.sizeBytes)
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    if (showLoadUnavailable && !entry.canLoad) {
                        Text(
                            text = stringResource(R.string.save_manager_load_unavailable),
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.error,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }

            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilledTonalButton(
                    onClick = onLoadClick,
                    enabled = entry.canLoad,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.filledTonalButtonColors(
                        containerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                        contentColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Icon(
                        imageVector = Icons.Rounded.PlayArrow,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = stringResource(R.string.save_manager_load_action),
                        style = MaterialTheme.typography.labelMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                OutlinedButton(
                    onClick = onDeleteClick,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(
                        contentColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Icon(
                        imageVector = Icons.Rounded.DeleteOutline,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = stringResource(R.string.save_manager_delete_action),
                        style = MaterialTheme.typography.labelMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
    }
}

@Composable
private fun SaveEntrySkeletonCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        tonalElevation = 1.dp,
        shadowElevation = 3.dp,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(
            width = 1.dp,
            color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top
            ) {
                SkeletonBlock(
                    modifier = Modifier
                        .width(132.dp)
                        .height(96.dp)
                        .clip(RoundedCornerShape(18.dp))
                )

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.88f)
                            .height(28.dp)
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.62f)
                            .height(28.dp)
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.56f)
                            .height(20.dp)
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.72f)
                            .height(20.dp)
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.42f)
                            .height(20.dp)
                    )
                    SkeletonBlock(
                        modifier = Modifier
                            .fillMaxWidth(0.68f)
                            .height(18.dp)
                    )
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                SkeletonBlock(
                    modifier = Modifier
                        .weight(1f)
                        .height(44.dp)
                )
                SkeletonBlock(
                    modifier = Modifier
                        .weight(1f)
                        .height(44.dp)
                )
            }
        }
    }
}

@Composable
private fun SkeletonBlock(modifier: Modifier = Modifier) {
    val shape = RoundedCornerShape(18.dp)
    Box(
        modifier = modifier
            .clip(shape)
            .background(
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                shape = shape
            )
            .shimmer()
    )
}

@Composable
private fun EmptyStateCard(isFiltered: Boolean) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        tonalElevation = 1.dp,
        shadowElevation = 3.dp,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(
            width = 1.dp,
            color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)
        )
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(R.string.save_manager_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
            )
            Text(
                text = stringResource(
                    if (isFiltered) {
                        R.string.save_manager_empty_body_game
                    } else {
                        R.string.save_manager_empty_body_all
                    }
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@SuppressLint("DefaultLocale")
private fun formatBytes(bytes: Long): String {
    return when {
        bytes >= 1_073_741_824L -> String.format("%.2f GB", bytes / 1_073_741_824.0)
        bytes >= 1_048_576L -> String.format("%.1f MB", bytes / 1_048_576.0)
        bytes >= 1024L -> String.format("%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }
}

private fun formatSaveEntryDate(timestamp: Long): String {
    return DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        .format(Date(timestamp))
}

private fun String?.isUsableDisplayTitle(): Boolean {
    if (this.isNullOrBlank()) return false
    val value = this.trim()
    return !value.startsWith("content://") &&
        !value.startsWith("primary%3A", ignoreCase = true) &&
        !value.contains("%2F", ignoreCase = true) &&
        !value.contains("%3A", ignoreCase = true)
}

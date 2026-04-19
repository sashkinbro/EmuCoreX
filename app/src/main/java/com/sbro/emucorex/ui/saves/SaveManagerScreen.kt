package com.sbro.emucorex.ui.saves

import android.annotation.SuppressLint
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material3.AlertDialog
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.SaveStateEntryInfo
import com.sbro.emucorex.data.SaveStateRepository
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SaveManagerScreen(
    gamePath: String? = null,
    gameTitle: String? = null,
    onLoadClick: (String, Int) -> Unit,
    onBackClick: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember(context) { SaveStateRepository(context) }
    val scope = rememberCoroutineScope()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val backupSuccessMessage = stringResource(R.string.save_manager_backup_success)
    val backupFailureMessage = stringResource(R.string.save_manager_backup_failed)
    val restoreSuccessMessage = stringResource(R.string.save_manager_restore_success)
    val restoreFailureMessage = stringResource(R.string.save_manager_restore_failed)
    val deleteSuccessMessage = stringResource(R.string.save_manager_delete_success)
    val deleteFailureMessage = stringResource(R.string.save_manager_delete_failed)

    var entries by remember(gamePath, gameTitle) { mutableStateOf<List<SaveStateEntryInfo>>(emptyList()) }
    var previewPaths by remember(gamePath, gameTitle) { mutableStateOf<Map<String, String>>(emptyMap()) }
    var isWorking by remember { mutableStateOf(false) }
    var isPreparingEntries by remember(gamePath, gameTitle) { mutableStateOf(true) }
    var isResolvingEntries by remember(gamePath, gameTitle) { mutableStateOf(false) }
    var pendingDelete by remember { mutableStateOf<SaveStateEntryInfo?>(null) }
    var refreshGeneration by remember(gamePath, gameTitle) { mutableStateOf(0) }

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
                    filterGameTitle = gameTitle
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

    LaunchedEffect(gamePath, gameTitle) {
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
            val success = withContext(Dispatchers.IO) {
                repository.restoreStates(uri)
            }
            isWorking = false
            Toast.makeText(
                context,
                if (success) restoreSuccessMessage else restoreFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
            refresh()
        }
    }

    pendingDelete?.let { entry ->
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text(stringResource(R.string.save_manager_delete_confirm_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.save_manager_delete_confirm_body,
                        entry.gameTitle,
                        entry.slot
                    )
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
                            pendingDelete = null
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
                TextButton(onClick = { pendingDelete = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
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
                        entryCount = entries.size,
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
                            onClick = { restoreLauncher.launch(arrayOf("application/zip", "*/*")) },
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
                            onDeleteClick = { pendingDelete = entry }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SaveManagerHeader(
    topInset: androidx.compose.ui.unit.Dp,
    subtitle: String?,
    entryCount: Int,
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
            NavigationBackButton(
                onClick = onBackClick,
                contentColor = MaterialTheme.colorScheme.onBackground
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 4.dp, end = 8.dp)
            ) {
                Text(
                    text = stringResource(R.string.save_manager_title),
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onBackground,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                if (!subtitle.isNullOrBlank()) {
                    Text(
                        text = subtitle,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            if (isWorking) {
                CircularProgressIndicator(
                    modifier = Modifier.size(22.dp),
                    strokeWidth = 2.dp
                )
            }
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
                Icon(
                    imageVector = Icons.Rounded.FolderOpen,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(18.dp)
                )
                Column(modifier = Modifier.padding(start = 12.dp)) {
                    Text(
                        text = stringResource(R.string.save_manager_entries_count, entryCount),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(R.string.save_manager_entries_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
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
        shape = RoundedCornerShape(22.dp),
        tonalElevation = 2.dp,
        shadowElevation = 0.dp,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top
            ) {
                Box(
                    modifier = Modifier
                        .width(112.dp)
                        .height(150.dp)
                ) {
                    GameCoverArt(
                        coverPath = previewPath,
                        fallbackTitle = entry.gameTitle,
                        modifier = Modifier.fillMaxSize()
                    )
                }

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 16.dp)
                        .heightIn(min = 150.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = entry.gameTitle,
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(
                            R.string.save_manager_entry_meta,
                            entry.serial,
                            entry.slot
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(
                            R.string.save_manager_entry_date,
                            DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT)
                                .format(Date(entry.lastModified))
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2,
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
        shape = RoundedCornerShape(22.dp),
        tonalElevation = 2.dp,
        shadowElevation = 0.dp,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top
            ) {
                SkeletonBlock(
                    modifier = Modifier
                        .width(112.dp)
                        .height(150.dp)
                )

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 16.dp)
                        .heightIn(min = 150.dp),
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
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
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

private fun String?.isUsableDisplayTitle(): Boolean {
    if (this.isNullOrBlank()) return false
    val value = this.trim()
    return !value.startsWith("content://") &&
        !value.startsWith("primary%3A", ignoreCase = true) &&
        !value.contains("%2F", ignoreCase = true) &&
        !value.contains("%3A", ignoreCase = true)
}

package com.sbro.emucorex.ui.textures

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
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
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Save
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.TexturePackInfo
import com.sbro.emucorex.data.TexturePackRepository
import com.sbro.emucorex.data.TexturePackSummary
import com.sbro.emucorex.data.RemoteContentInstallState
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import java.text.DateFormat
import java.util.Date
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TextureManagerScreen(
    onBackClick: () -> Unit
) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val repository = remember(context) { TexturePackRepository(context, preferences) }
    val remoteInstallState = remember(context) { RemoteContentInstallState(context) }
    val scope = rememberCoroutineScope()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()

    val replacementsEnabled by preferences.textureReplacementsEnabled.collectAsState(initial = false)
    val asyncLoading by preferences.textureReplacementsAsync.collectAsState(initial = true)
    val precache by preferences.textureReplacementsPrecache.collectAsState(initial = false)
    val dumpingEnabled by preferences.textureDumpingEnabled.collectAsState(initial = false)

    val importSuccessMessage = stringResource(R.string.texture_manager_import_success)
    val importFailureMessage = stringResource(R.string.texture_manager_import_failed)
    val deleteSuccessMessage = stringResource(R.string.texture_manager_delete_success)
    val deleteFailureMessage = stringResource(R.string.texture_manager_delete_failed)
    val clearDumpsSuccessMessage = stringResource(R.string.texture_manager_clear_dumps_success)
    val clearDumpsFailureMessage = stringResource(R.string.texture_manager_clear_dumps_failed)

    var summary by remember { mutableStateOf<TexturePackSummary?>(null) }
    var isLoading by remember { mutableStateOf(true) }
    var isWorking by remember { mutableStateOf(false) }
    var refreshGeneration by remember { mutableIntStateOf(0) }
    val pendingDelete = remember { mutableStateOf<TexturePackInfo?>(null) }
    val pendingClearDumps = remember { mutableStateOf<TexturePackInfo?>(null) }

    fun refresh() {
        val generation = refreshGeneration + 1
        refreshGeneration = generation
        scope.launch {
            isLoading = true
            val loaded = withContext(Dispatchers.IO) { runCatching { repository.listPacks() }.getOrNull() }
            if (refreshGeneration != generation) return@launch
            if (loaded != null) summary = loaded
            isLoading = false
        }
    }

    LaunchedEffect(Unit) {
        refresh()
    }

    val importLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            isWorking = true
            val result = try {
                withContext(Dispatchers.IO) {
                    runCatching { repository.importPackZip(uri) }
                        .getOrElse { com.sbro.emucorex.data.TextureImportResult(success = false) }
                }
            } finally {
                isWorking = false
            }
            Toast.makeText(
                context,
                if (result.success) {
                    importSuccessMessage.format(result.importedFiles)
                } else {
                    importFailureMessage
                },
                Toast.LENGTH_LONG
            ).show()
            refresh()
        }
    }

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background,
        contentColor = MaterialTheme.colorScheme.onBackground
    ) {
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontalSystemBarPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = 0.dp,
                bottom = 24.dp + bottomInset
            )
        ) {
            item {
                TextureManagerHeader(
                    topInset = topInset,
                    isWorking = isWorking,
                    onBackClick = onBackClick
                )
            }

            item {
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    FilledTonalButton(
                        onClick = { importLauncher.launch(arrayOf("application/zip", "application/octet-stream", "*/*")) },
                        enabled = !isWorking,
                        colors = ButtonDefaults.filledTonalButtonColors(
                            containerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                            contentColor = MaterialTheme.colorScheme.primary
                        )
                    ) {
                        Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(stringResource(R.string.texture_manager_import_zip))
                    }
                }
            }

            item {
                TextureOnlineCatalogSection(
                    textureRepository = repository,
                    preferences = preferences,
                    onInstalled = { refresh() }
                )
            }

            item {
                TextureOptionsPanel(
                    replacementsEnabled = replacementsEnabled,
                    asyncLoading = asyncLoading,
                    precache = precache,
                    dumpingEnabled = dumpingEnabled,
                    enabled = !isWorking,
                    onReplacementsChanged = { enabled ->
                        scope.launch { preferences.setTextureReplacementsEnabled(enabled) }
                    },
                    onAsyncChanged = { enabled ->
                        scope.launch { preferences.setTextureReplacementsAsync(enabled) }
                    },
                    onPrecacheChanged = { enabled ->
                        scope.launch { preferences.setTextureReplacementsPrecache(enabled) }
                    },
                    onDumpingChanged = { enabled ->
                        scope.launch { preferences.setTextureDumpingEnabled(enabled) }
                    }
                )
            }

            if (isLoading && summary == null) {
                item {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(22.dp),
                        color = MaterialTheme.colorScheme.surface
                    ) {
                        Row(
                            modifier = Modifier.padding(18.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(22.dp), strokeWidth = 2.dp)
                            Text(
                                text = stringResource(R.string.texture_manager_loading),
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                }
            } else if (summary?.packs.orEmpty().isEmpty()) {
                item {
                    EmptyTexturePacksPanel(rootPath = summary?.rootPath.orEmpty())
                }
            } else {
                item {
                    TexturePacksSectionHeader(
                        summary = summary,
                        isLoading = isLoading
                    )
                }
                items(summary?.packs.orEmpty(), key = { it.serial }) { pack ->
                    TexturePackCard(
                        pack = pack,
                        isWorking = isWorking,
                        onDelete = { pendingDelete.value = pack },
                        onClearDumps = { pendingClearDumps.value = pack }
                    )
                }
            }
        }
    }

    pendingDelete.value?.let { pack ->
        AlertDialog(
            onDismissRequest = { pendingDelete.value = null },
            title = { Text(stringResource(R.string.texture_manager_delete_title)) },
            text = { Text(stringResource(R.string.texture_manager_delete_confirm, pack.serial)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        pendingDelete.value = null
                        scope.launch {
                            isWorking = true
                            val success = try {
                                withContext(Dispatchers.IO) {
                                    runCatching {
                                        val deleted = repository.deletePack(pack.serial)
                                        if (deleted) remoteInstallState.removeTexturesForSerial(pack.serial)
                                        deleted
                                    }.getOrDefault(false)
                                }
                            } finally {
                                isWorking = false
                            }
                            Toast.makeText(
                                context,
                                if (success) deleteSuccessMessage else deleteFailureMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                            refresh()
                        }
                    }
                ) {
                    Text(stringResource(R.string.delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete.value = null }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }

    pendingClearDumps.value?.let { pack ->
        AlertDialog(
            onDismissRequest = { pendingClearDumps.value = null },
            title = { Text(stringResource(R.string.texture_manager_clear_dumps_title)) },
            text = { Text(stringResource(R.string.texture_manager_clear_dumps_confirm, pack.serial)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        pendingClearDumps.value = null
                        scope.launch {
                            isWorking = true
                            val success = try {
                                withContext(Dispatchers.IO) {
                                    runCatching { repository.clearDumps(pack.serial) }.getOrDefault(false)
                                }
                            } finally {
                                isWorking = false
                            }
                            Toast.makeText(
                                context,
                                if (success) clearDumpsSuccessMessage else clearDumpsFailureMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                            refresh()
                        }
                    }
                ) {
                    Text(stringResource(R.string.texture_manager_clear_dumps))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingClearDumps.value = null }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }
}

@Composable
private fun TextureManagerHeader(
    topInset: androidx.compose.ui.unit.Dp,
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
                title = stringResource(R.string.texture_manager_title),
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
private fun TextureOptionsPanel(
    replacementsEnabled: Boolean,
    asyncLoading: Boolean,
    precache: Boolean,
    dumpingEnabled: Boolean,
    enabled: Boolean,
    onReplacementsChanged: (Boolean) -> Unit,
    onAsyncChanged: (Boolean) -> Unit,
    onPrecacheChanged: (Boolean) -> Unit,
    onDumpingChanged: (Boolean) -> Unit
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
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = stringResource(R.string.texture_manager_options),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            TextureSwitchRow(
                title = stringResource(R.string.texture_manager_enable_replacements),
                body = stringResource(R.string.texture_manager_enable_replacements_desc),
                checked = replacementsEnabled,
                enabled = enabled,
                onCheckedChange = onReplacementsChanged
            )
            TextureSwitchRow(
                title = stringResource(R.string.texture_manager_async_loading),
                body = stringResource(R.string.texture_manager_async_loading_desc),
                checked = asyncLoading,
                enabled = enabled && replacementsEnabled,
                onCheckedChange = onAsyncChanged
            )
            TextureSwitchRow(
                title = stringResource(R.string.texture_manager_precache),
                body = stringResource(R.string.texture_manager_precache_desc),
                checked = precache,
                enabled = enabled && replacementsEnabled,
                onCheckedChange = onPrecacheChanged
            )
            TextureSwitchRow(
                title = stringResource(R.string.texture_manager_dumping),
                body = stringResource(R.string.texture_manager_dumping_desc),
                checked = dumpingEnabled,
                enabled = enabled,
                onCheckedChange = onDumpingChanged
            )
        }
    }
}

@Composable
private fun TextureSwitchRow(
    title: String,
    body: String,
    checked: Boolean,
    enabled: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = body,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Switch(
            checked = checked,
            enabled = enabled,
            onCheckedChange = onCheckedChange
        )
    }
}

@Composable
private fun TexturePacksSectionHeader(
    summary: TexturePackSummary?,
    isLoading: Boolean
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(start = 4.dp, top = 2.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = stringResource(R.string.texture_manager_installed_packs),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = stringResource(
                    R.string.texture_manager_summary_meta,
                    summary?.packs?.size ?: 0,
                    summary?.totalReplacementCount ?: 0,
                    summary?.totalDumpCount ?: 0,
                    formatBytes(summary?.totalSizeBytes ?: 0L)
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        if (isLoading) {
            CircularProgressIndicator(
                modifier = Modifier.size(20.dp),
                strokeWidth = 2.dp
            )
        }
    }
}

@Composable
private fun StoragePathRow(rootPath: String) {
    if (rootPath.isBlank()) return
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(3.dp)
        ) {
            Text(
                text = stringResource(R.string.texture_manager_storage),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = rootPath,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 3,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TexturePackCard(
    pack: TexturePackInfo,
    isWorking: Boolean,
    onDelete: () -> Unit,
    onClearDumps: () -> Unit
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
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(54.dp)
                        .background(
                            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f),
                            shape = RoundedCornerShape(16.dp)
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.FolderZip,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp),
                        tint = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
                Spacer(modifier = Modifier.width(12.dp))
                Column(modifier = Modifier.weight(1f)) {
                    pack.gameTitle?.let { title ->
                        Text(
                            text = title,
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Text(
                        text = pack.serial,
                        style = if (pack.gameTitle == null) {
                            MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
                        } else {
                            MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold)
                        },
                        color = if (pack.gameTitle == null) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.primary
                        },
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(R.string.texture_manager_pack_modified, formatDate(pack.lastModifiedAt)),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            Text(
                text = stringResource(
                    R.string.texture_manager_pack_meta,
                    pack.replacementCount,
                    pack.dumpCount,
                    formatBytes(pack.sizeBytes)
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = onClearDumps,
                    enabled = !isWorking && pack.dumpCount > 0,
                    contentPadding = ButtonDefaults.ButtonWithIconContentPadding
                ) {
                    Icon(Icons.Rounded.Save, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(stringResource(R.string.texture_manager_clear_dumps))
                }
                OutlinedButton(
                    onClick = onDelete,
                    enabled = !isWorking,
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                    contentPadding = ButtonDefaults.ButtonWithIconContentPadding
                ) {
                    Icon(Icons.Rounded.DeleteOutline, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(stringResource(R.string.delete))
                }
            }
        }
    }
}

@Composable
private fun EmptyTexturePacksPanel(rootPath: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
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
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(verticalAlignment = Alignment.Top) {
                Box(
                    modifier = Modifier
                        .size(54.dp)
                        .background(
                            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f),
                            shape = RoundedCornerShape(16.dp)
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.FolderZip,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onPrimaryContainer,
                        modifier = Modifier.size(24.dp)
                    )
                }
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 14.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = stringResource(R.string.texture_manager_empty_title),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(R.string.texture_manager_empty_body),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            StoragePathRow(rootPath = rootPath)
        }
    }
}

private fun formatBytes(bytes: Long): String {
    if (bytes <= 0L) return "0 B"
    val units = arrayOf("B", "KB", "MB", "GB")
    var value = bytes.toDouble()
    var unit = 0
    while (value >= 1024.0 && unit < units.lastIndex) {
        value /= 1024.0
        unit++
    }
    return if (unit == 0) {
        "$bytes ${units[unit]}"
    } else {
        String.format(Locale.US, "%.1f %s", value, units[unit])
    }
}

private fun formatDate(timestamp: Long): String {
    if (timestamp <= 0L) return "-"
    return DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(timestamp))
}

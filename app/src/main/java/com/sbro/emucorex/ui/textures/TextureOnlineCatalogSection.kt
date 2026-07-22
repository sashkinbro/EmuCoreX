package com.sbro.emucorex.ui.textures

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.Crossfade
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.sbro.emucorex.R
import com.sbro.emucorex.data.ContentLibraryRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.InstalledRemoteTexture
import com.sbro.emucorex.data.RemoteContentCatalogRepository
import com.sbro.emucorex.data.RemoteContentInstallState
import com.sbro.emucorex.data.RemoteTexturePack
import com.sbro.emucorex.data.SelectedGameIdentity
import com.sbro.emucorex.data.TextureDownloadManager
import com.sbro.emucorex.data.TextureDownloadStatus
import com.sbro.emucorex.data.TextureDownloadTask
import com.sbro.emucorex.data.formatDownloadBytes
import com.sbro.emucorex.data.formatDownloadDuration
import com.sbro.emucorex.ui.common.LibraryGamePicker
import com.sbro.emucorex.ui.common.contentCatalogTitleKey
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

@Composable
internal fun TextureOnlineCatalogSection(
    onInstalled: () -> Unit
) {
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val catalogRepository = remember(context) { RemoteContentCatalogRepository(context) }
    val libraryRepository = remember(context) { ContentLibraryRepository(context) }
    val installState = remember(context) { RemoteContentInstallState(context) }
    val downloadManager = remember(context) { TextureDownloadManager(context) }
    var games by remember { mutableStateOf<List<GameItem>>(emptyList()) }
    var selectedPath by remember { mutableStateOf<String?>(null) }
    var identity by remember { mutableStateOf<SelectedGameIdentity?>(null) }
    var identityPath by remember { mutableStateOf<String?>(null) }
    var resolvingIdentity by remember { mutableStateOf(false) }
    var packs by remember { mutableStateOf<List<RemoteTexturePack>>(emptyList()) }
    var installed by remember { mutableStateOf<Map<String, InstalledRemoteTexture>>(emptyMap()) }
    var loading by remember { mutableStateOf(true) }
    var cached by remember { mutableStateOf(false) }
    var loadFailed by remember { mutableStateOf(false) }
    var downloadTasks by remember { mutableStateOf<List<TextureDownloadTask>>(emptyList()) }
    val notificationPermission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { }

    LaunchedEffect(Unit) {
        val loaded = withContext(Dispatchers.IO) {
            val libraryGames = libraryRepository.loadGames()
            val catalog = catalogRepository.loadTextureCatalog()
            Triple(libraryGames, catalog, installState.installedTextures())
        }
        games = loaded.first
        packs = loaded.second.entries
        cached = loaded.second.fromCache
        loadFailed = loaded.second.entries.isEmpty()
        installed = loaded.third
        selectedPath = selectedPath ?: games.firstOrNull()?.path
        loading = false
        if (loaded.second.cacheHit) {
            val refreshed = withContext(Dispatchers.IO) {
                catalogRepository.loadTextureCatalog(forceRefresh = true)
            }
            if (refreshed.entries.isNotEmpty()) {
                packs = refreshed.entries
                cached = refreshed.fromCache
                loadFailed = false
            }
        }
    }

    LaunchedEffect(Unit) {
        while (isActive) {
            downloadTasks = withContext(Dispatchers.IO) { downloadManager.tasks() }
            val refreshedInstalled = withContext(Dispatchers.IO) { installState.installedTextures() }
            if (refreshedInstalled != installed) {
                installed = refreshedInstalled
                onInstalled()
            }
            delay(750)
        }
    }

    val selectedGame = games.firstOrNull { it.path == selectedPath }
    LaunchedEffect(selectedPath) {
        val game = selectedGame
        if (game == null) {
            identity = null
            identityPath = null
            resolvingIdentity = false
            return@LaunchedEffect
        }
        identity = null
        identityPath = null
        resolvingIdentity = true
        val resolved = withContext(Dispatchers.IO) { libraryRepository.resolveIdentity(game) }
        if (selectedPath == game.path) {
            identity = resolved
            identityPath = game.path
        }
        resolvingIdentity = false
    }
    val selectedSerial = identity?.serial
        ?.takeIf { identityPath == selectedPath }
        ?.uppercase(Locale.US)
    val selectedTitleKey = selectedGame?.title.orEmpty().contentCatalogTitleKey()
    val compatiblePacks = remember(packs, selectedSerial) {
        if (selectedSerial == null) emptyList()
        else packs.filter { pack -> pack.serials.any { it.equals(selectedSerial, ignoreCase = true) } }
            .sortedBy(RemoteTexturePack::name)
    }
    val otherVersionPacks = remember(packs, compatiblePacks, selectedTitleKey) {
        if (selectedTitleKey.isEmpty()) emptyList()
        else packs.filter { pack ->
            pack !in compatiblePacks && pack.gameTitle.contentCatalogTitleKey() == selectedTitleKey
        }.sortedBy(RemoteTexturePack::name)
    }
    val onlineContentState = TextureOnlineContentState(
        phase = when {
            loading || resolvingIdentity || (selectedGame != null && identityPath != selectedPath) ->
                TextureOnlinePhase.LOADING
            loadFailed -> TextureOnlinePhase.ERROR
            selectedGame != null && compatiblePacks.isEmpty() && otherVersionPacks.isEmpty() ->
                TextureOnlinePhase.EMPTY
            else -> TextureOnlinePhase.CONTENT
        },
        compatiblePacks = compatiblePacks,
        otherVersionPacks = otherVersionPacks
    )

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text(
                text = stringResource(R.string.texture_catalog_title),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = stringResource(R.string.texture_catalog_description),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            TextureDownloadQueue(
                tasks = downloadTasks.filter { task ->
                    task.status != TextureDownloadStatus.COMPLETED &&
                        task.status != TextureDownloadStatus.CANCELLED
                },
                onPause = downloadManager::pause,
                onResume = downloadManager::resume,
                onCancel = downloadManager::cancel,
                onRemove = downloadManager::remove
            )
            LibraryGamePicker(
                games = games,
                selectedPath = selectedPath,
                onSelected = { game ->
                    if (game.path != selectedPath) {
                        identity = null
                        identityPath = null
                        resolvingIdentity = true
                        selectedPath = game.path
                    }
                },
                horizontalContentPadding = 16.dp,
                fullBleedPadding = 16.dp
            )
            AnimatedContent(
                targetState = onlineContentState,
                transitionSpec = {
                    (fadeIn(tween(220)) + slideInVertically(tween(220)) { height -> height / 10 }) togetherWith
                        (fadeOut(tween(140)) + slideOutVertically(tween(140)) { height -> -height / 12 })
                },
                label = "onlineTextureContent"
            ) { state ->
                Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                    when (state.phase) {
                        TextureOnlinePhase.LOADING -> Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            CircularProgressIndicator(modifier = Modifier.width(22.dp), strokeWidth = 2.dp)
                            Text(stringResource(R.string.content_catalog_loading))
                        }
                        TextureOnlinePhase.ERROR -> Text(
                            text = stringResource(R.string.content_catalog_failed),
                            color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodyMedium
                        )
                        TextureOnlinePhase.EMPTY -> TextureCatalogEmptyState()
                        TextureOnlinePhase.CONTENT -> {
                            if (state.compatiblePacks.isNotEmpty()) {
                                TextureCatalogSectionTitle(
                                    text = stringResource(R.string.content_compatible_game_version_section)
                                )
                                state.compatiblePacks.forEach { pack ->
                                    val installedPack = installed[pack.id]
                                    val upToDate = installedPack?.version == pack.version &&
                                        installedPack.serial.equals(selectedSerial, ignoreCase = true)
                                    val task = downloadTasks.firstOrNull { download ->
                                        download.packId == pack.id &&
                                            download.serial.equals(selectedSerial, ignoreCase = true)
                                    }
                                    TextureCatalogCard(
                                        pack = pack,
                                        compatible = true,
                                        installed = upToDate,
                                        updateAvailable = installedPack != null && !upToDate,
                                        download = task,
                                        onSource = { runCatching { uriHandler.openUri(pack.sourceUrl) } },
                                        onInstall = {
                                            val serial = selectedSerial ?: return@TextureCatalogCard
                                            if (
                                                Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                                                ContextCompat.checkSelfPermission(
                                                    context,
                                                    Manifest.permission.POST_NOTIFICATIONS
                                                ) != PackageManager.PERMISSION_GRANTED
                                            ) {
                                                notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                                            }
                                            downloadManager.enqueue(pack, serial)
                                        },
                                        onPause = { task?.let { downloadManager.pause(it.key) } },
                                        onResume = { task?.let { downloadManager.resume(it.key) } },
                                        onCancel = { task?.let { downloadManager.cancel(it.key) } },
                                        onRemove = { task?.let { downloadManager.remove(it.key) } }
                                    )
                                }
                            }
                            if (state.otherVersionPacks.isNotEmpty()) {
                                TextureCatalogSectionTitle(
                                    text = stringResource(R.string.content_other_game_versions_section)
                                )
                                state.otherVersionPacks.forEach { pack ->
                                    TextureCatalogCard(
                                        pack = pack,
                                        compatible = false,
                                        installed = false,
                                        updateAvailable = false,
                                        download = null,
                                        onSource = { runCatching { uriHandler.openUri(pack.sourceUrl) } },
                                        onInstall = {},
                                        onPause = {},
                                        onResume = {},
                                        onCancel = {},
                                        onRemove = {}
                                    )
                                }
                            }
                        }
                    }
                }
            }
            if (cached) {
                Text(
                    text = stringResource(R.string.content_catalog_cached),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun TextureCatalogEmptyState() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f),
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 18.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                shape = RoundedCornerShape(14.dp),
                color = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer
            ) {
                Icon(
                    imageVector = Icons.Rounded.Info,
                    contentDescription = null,
                    modifier = Modifier.padding(11.dp).width(22.dp)
                )
            }
            Text(
                text = stringResource(R.string.texture_catalog_no_packs),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun TextureCatalogCard(
    pack: RemoteTexturePack,
    compatible: Boolean,
    installed: Boolean,
    updateAvailable: Boolean,
    download: TextureDownloadTask?,
    onSource: () -> Unit,
    onInstall: () -> Unit,
    onPause: () -> Unit,
    onResume: () -> Unit,
    onCancel: () -> Unit,
    onRemove: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f),
        contentColor = MaterialTheme.colorScheme.onSurface
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.Top) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(pack.name, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    Text(
                        text = stringResource(
                            R.string.texture_catalog_pack_meta,
                            pack.version,
                            formatCatalogBytes(pack.sizeBytes),
                            pack.fileCount
                        ),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                when {
                    installed -> Text(
                        text = stringResource(R.string.content_installed),
                        color = MaterialTheme.colorScheme.primary,
                        style = MaterialTheme.typography.labelLarge
                    )
                    updateAvailable -> Text(
                        text = stringResource(R.string.content_update_available),
                        color = MaterialTheme.colorScheme.tertiary,
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }
            TextureCompatibilityBadge(compatible = compatible)
            if (!compatible) {
                Text(
                    text = stringResource(
                        R.string.content_supported_serials,
                        pack.serials.joinToString()
                    ),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (pack.description.isNotBlank()) {
                Text(
                    text = pack.description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Text(
                text = stringResource(R.string.content_authors, pack.authors.joinToString()),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (download != null && download.status != TextureDownloadStatus.COMPLETED) {
                TextureDownloadProgress(task = download)
            }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (compatible) {
                    Crossfade(
                        targetState = download?.status.toDownloadControlState(),
                        modifier = Modifier.fillMaxWidth(),
                        animationSpec = tween(durationMillis = DOWNLOAD_CONTROL_ANIMATION_MS),
                        label = "texturePackDownloadControls"
                    ) { controlState ->
                        when (controlState) {
                            DownloadControlState.PAUSE -> DownloadActionButton(
                                text = stringResource(R.string.emulation_pause),
                                icon = { Icon(Icons.Rounded.Pause, contentDescription = null) },
                                onClick = onPause
                            )
                            DownloadControlState.RESUME_CANCEL -> Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                DownloadActionButton(
                                    text = stringResource(R.string.detail_resume),
                                    icon = { Icon(Icons.Rounded.PlayArrow, contentDescription = null) },
                                    onClick = onResume,
                                    modifier = Modifier.weight(1f)
                                )
                                DownloadSecondaryAction(
                                    text = stringResource(R.string.cancel),
                                    icon = { Icon(Icons.Rounded.Close, contentDescription = null) },
                                    onClick = onCancel,
                                    modifier = Modifier.weight(1f)
                                )
                            }
                            DownloadControlState.RETRY_REMOVE -> Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                DownloadActionButton(
                                    text = stringResource(R.string.texture_download_retry),
                                    icon = { Icon(Icons.Rounded.Refresh, contentDescription = null) },
                                    onClick = onResume,
                                    modifier = Modifier.weight(1f)
                                )
                                DownloadSecondaryAction(
                                    text = stringResource(R.string.settings_gpu_driver_remove_short),
                                    icon = { Icon(Icons.Rounded.DeleteOutline, contentDescription = null) },
                                    onClick = onRemove,
                                    modifier = Modifier.weight(1f)
                                )
                            }
                            DownloadControlState.INSTALL -> {
                                Button(
                                    onClick = onInstall,
                                    modifier = Modifier.fillMaxWidth()
                                ) {
                                    Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                                    Spacer(Modifier.width(7.dp))
                                    Text(
                                        text = stringResource(
                                            when {
                                                installed -> R.string.content_reinstall
                                                updateAvailable -> R.string.content_update
                                                else -> R.string.content_download_install
                                            }
                                        ),
                                        maxLines = 2,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                            DownloadControlState.HIDDEN -> Spacer(Modifier.height(0.dp))
                        }
                    }
                }
                OutlinedButton(
                    onClick = onSource,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.AutoMirrored.Rounded.OpenInNew, contentDescription = null)
                    Spacer(Modifier.width(7.dp))
                    Text(
                        text = stringResource(R.string.content_source),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
    }
}

@Composable
private fun TextureDownloadQueue(
    tasks: List<TextureDownloadTask>,
    onPause: (String) -> Unit,
    onResume: (String) -> Unit,
    onCancel: (String) -> Unit,
    onRemove: (String) -> Unit
) {
    if (tasks.isEmpty()) return
    Text(
        text = stringResource(R.string.texture_downloads_title),
        style = MaterialTheme.typography.titleMedium,
        fontWeight = FontWeight.Bold
    )
    tasks.forEach { task ->
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
        ) {
            Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(9.dp)) {
                Text(task.packName, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold)
                Text(task.serial, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                TextureDownloadProgress(task)
                Crossfade(
                    targetState = task.status.toDownloadControlState(),
                    modifier = Modifier.fillMaxWidth(),
                    animationSpec = tween(durationMillis = DOWNLOAD_CONTROL_ANIMATION_MS),
                    label = "textureQueueDownloadControls"
                ) { controlState ->
                    when (controlState) {
                        DownloadControlState.PAUSE -> DownloadActionButton(
                            text = stringResource(R.string.emulation_pause),
                            icon = { Icon(Icons.Rounded.Pause, contentDescription = null) },
                            onClick = { onPause(task.key) }
                        )
                        DownloadControlState.RESUME_CANCEL -> Row(
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            DownloadActionButton(
                                text = stringResource(R.string.detail_resume),
                                icon = { Icon(Icons.Rounded.PlayArrow, contentDescription = null) },
                                onClick = { onResume(task.key) },
                                modifier = Modifier.weight(1f)
                            )
                            DownloadSecondaryAction(
                                text = stringResource(R.string.cancel),
                                icon = { Icon(Icons.Rounded.Close, contentDescription = null) },
                                onClick = { onCancel(task.key) },
                                modifier = Modifier.weight(1f)
                            )
                        }
                        DownloadControlState.RETRY_REMOVE -> Row(
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            DownloadActionButton(
                                text = stringResource(R.string.texture_download_retry),
                                icon = { Icon(Icons.Rounded.Refresh, contentDescription = null) },
                                onClick = { onResume(task.key) },
                                modifier = Modifier.weight(1f)
                            )
                            DownloadSecondaryAction(
                                text = stringResource(R.string.settings_gpu_driver_remove_short),
                                icon = { Icon(Icons.Rounded.DeleteOutline, contentDescription = null) },
                                onClick = { onRemove(task.key) },
                                modifier = Modifier.weight(1f)
                            )
                        }
                        DownloadControlState.INSTALL,
                        DownloadControlState.HIDDEN -> Spacer(Modifier.height(0.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun TextureDownloadProgress(task: TextureDownloadTask) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(
            text = textureDownloadStatusText(task.status),
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.SemiBold,
            color = when (task.status) {
                TextureDownloadStatus.FAILED -> MaterialTheme.colorScheme.error
                TextureDownloadStatus.COMPLETED -> Color(0xFF1B6B3A)
                else -> MaterialTheme.colorScheme.primary
            }
        )
        LinearProgressIndicator(progress = { task.progress }, modifier = Modifier.fillMaxWidth())
        val downloaded = formatDownloadBytes(task.downloadedBytes)
        val total = formatDownloadBytes(task.totalBytes)
        val speed = formatDownloadBytes(task.bytesPerSecond)
        val detail = if (task.bytesPerSecond > 0L && task.etaSeconds > 0L) {
            stringResource(
                R.string.texture_download_progress_detail,
                downloaded,
                total,
                speed,
                formatDownloadDuration(task.etaSeconds)
            )
        } else {
            stringResource(R.string.texture_download_progress_estimating, downloaded, total)
        }
        Text(detail, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (task.error.isNotBlank()) {
            Text(
                text = task.error,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun textureDownloadStatusText(status: TextureDownloadStatus): String = stringResource(
    when (status) {
        TextureDownloadStatus.QUEUED -> R.string.texture_download_status_queued
        TextureDownloadStatus.DOWNLOADING -> R.string.texture_download_status_downloading
        TextureDownloadStatus.PAUSED -> R.string.texture_download_status_paused
        TextureDownloadStatus.WAITING_NETWORK -> R.string.texture_download_status_waiting_network
        TextureDownloadStatus.VERIFYING -> R.string.texture_download_status_verifying
        TextureDownloadStatus.INSTALLING -> R.string.texture_download_status_installing
        TextureDownloadStatus.COMPLETED -> R.string.texture_download_status_completed
        TextureDownloadStatus.FAILED -> R.string.texture_download_status_failed
        TextureDownloadStatus.CANCELLED -> R.string.texture_download_status_cancelled
    }
)

private enum class DownloadControlState {
    INSTALL,
    PAUSE,
    RESUME_CANCEL,
    RETRY_REMOVE,
    HIDDEN
}

private fun TextureDownloadStatus?.toDownloadControlState(): DownloadControlState = when (this) {
    TextureDownloadStatus.QUEUED,
    TextureDownloadStatus.DOWNLOADING,
    TextureDownloadStatus.WAITING_NETWORK -> DownloadControlState.PAUSE
    TextureDownloadStatus.PAUSED -> DownloadControlState.RESUME_CANCEL
    TextureDownloadStatus.FAILED -> DownloadControlState.RETRY_REMOVE
    TextureDownloadStatus.VERIFYING,
    TextureDownloadStatus.INSTALLING -> DownloadControlState.HIDDEN
    TextureDownloadStatus.COMPLETED,
    TextureDownloadStatus.CANCELLED,
    null -> DownloadControlState.INSTALL
}

private const val DOWNLOAD_CONTROL_ANIMATION_MS = 180

@Composable
private fun DownloadActionButton(
    text: String,
    icon: @Composable () -> Unit,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Button(onClick = onClick, modifier = modifier.fillMaxWidth()) {
        icon()
        Spacer(Modifier.width(7.dp))
        Text(text, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun DownloadSecondaryAction(
    text: String,
    icon: @Composable () -> Unit,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    OutlinedButton(onClick = onClick, modifier = modifier.fillMaxWidth()) {
        icon()
        Spacer(Modifier.width(7.dp))
        Text(text, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun TextureCatalogSectionTitle(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleMedium,
        fontWeight = FontWeight.Bold
    )
}

@Composable
private fun TextureCompatibilityBadge(compatible: Boolean) {
    val background = if (compatible) {
        Color(0xFF1B6B3A)
    } else {
        MaterialTheme.colorScheme.errorContainer
    }
    val foreground = if (compatible) {
        Color.White
    } else {
        MaterialTheme.colorScheme.onErrorContainer
    }
    Surface(
        shape = RoundedCornerShape(10.dp),
        color = background,
        contentColor = foreground
    ) {
        Text(
            text = stringResource(
                if (compatible) R.string.content_compatible
                else R.string.content_not_compatible
            ),
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold
        )
    }
}

private fun formatCatalogBytes(bytes: Long): String {
    if (bytes < 1024L) return "$bytes B"
    val kb = bytes / 1024.0
    if (kb < 1024.0) return String.format(Locale.getDefault(), "%.1f KB", kb)
    val mb = kb / 1024.0
    if (mb < 1024.0) return String.format(Locale.getDefault(), "%.1f MB", mb)
    return String.format(Locale.getDefault(), "%.2f GB", mb / 1024.0)
}

private enum class TextureOnlinePhase {
    LOADING,
    ERROR,
    EMPTY,
    CONTENT
}

private data class TextureOnlineContentState(
    val phase: TextureOnlinePhase,
    val compatiblePacks: List<RemoteTexturePack>,
    val otherVersionPacks: List<RemoteTexturePack>
)

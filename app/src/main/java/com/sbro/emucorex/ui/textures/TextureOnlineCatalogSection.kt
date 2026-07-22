package com.sbro.emucorex.ui.textures

import android.widget.Toast
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
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.OpenInNew
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
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.ContentLibraryRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.InstalledRemoteTexture
import com.sbro.emucorex.data.RemoteContentCatalogRepository
import com.sbro.emucorex.data.RemoteContentInstallState
import com.sbro.emucorex.data.RemoteTexturePack
import com.sbro.emucorex.data.TexturePackRepository
import com.sbro.emucorex.ui.common.LibraryGamePicker
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
internal fun TextureOnlineCatalogSection(
    textureRepository: TexturePackRepository,
    preferences: AppPreferences,
    onInstalled: () -> Unit
) {
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val scope = rememberCoroutineScope()
    val catalogRepository = remember(context) { RemoteContentCatalogRepository(context) }
    val libraryRepository = remember(context) { ContentLibraryRepository(context) }
    val installState = remember(context) { RemoteContentInstallState(context) }
    var games by remember { mutableStateOf<List<GameItem>>(emptyList()) }
    var selectedPath by remember { mutableStateOf<String?>(null) }
    var packs by remember { mutableStateOf<List<RemoteTexturePack>>(emptyList()) }
    var installed by remember { mutableStateOf<Map<String, InstalledRemoteTexture>>(emptyMap()) }
    var loading by remember { mutableStateOf(true) }
    var cached by remember { mutableStateOf(false) }
    var loadFailed by remember { mutableStateOf(false) }
    var workingPackId by remember { mutableStateOf<String?>(null) }
    var downloadProgress by remember { mutableFloatStateOf(0f) }

    val installSuccess = stringResource(R.string.texture_catalog_install_success)
    val installFailure = stringResource(R.string.texture_catalog_install_failed)

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
    }

    val selectedGame = games.firstOrNull { it.path == selectedPath }
    val selectedSerial = selectedGame?.serial?.uppercase(Locale.US)
    val available = remember(packs, selectedSerial) {
        if (selectedSerial == null) emptyList()
        else packs.filter { pack -> pack.serials.any { it.equals(selectedSerial, ignoreCase = true) } }
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
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
            LibraryGamePicker(
                games = games,
                selectedPath = selectedPath,
                onSelected = { selectedPath = it.path }
            )
            when {
                loading -> Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    CircularProgressIndicator(modifier = Modifier.width(22.dp), strokeWidth = 2.dp)
                    Text(stringResource(R.string.content_catalog_loading))
                }
                loadFailed -> Text(
                    text = stringResource(R.string.content_catalog_failed),
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodyMedium
                )
                selectedGame != null && available.isEmpty() -> Text(
                    text = stringResource(R.string.texture_catalog_no_packs),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyMedium
                )
                else -> available.forEach { pack ->
                    val installedPack = installed[pack.id]
                    val upToDate = installedPack?.version == pack.version &&
                        installedPack.serial.equals(selectedSerial, ignoreCase = true)
                    TextureCatalogCard(
                        pack = pack,
                        installed = upToDate,
                        updateAvailable = installedPack != null && !upToDate,
                        working = workingPackId == pack.id,
                        progress = downloadProgress,
                        onSource = { runCatching { uriHandler.openUri(pack.sourceUrl) } },
                        onInstall = {
                            val serial = selectedSerial ?: return@TextureCatalogCard
                            if (workingPackId != null) return@TextureCatalogCard
                            scope.launch {
                                workingPackId = pack.id
                                downloadProgress = 0f
                                val success = withContext(Dispatchers.IO) {
                                    var archive: java.io.File? = null
                                    try {
                                        archive = catalogRepository.downloadTexturePack(pack) { progress ->
                                            downloadProgress = progress
                                        }
                                        val result = textureRepository.installRemotePack(archive, serial)
                                        if (!result.success) return@withContext false
                                        installState.removeTexturesForSerial(serial)
                                        installState.recordTexture(pack, serial)
                                        true
                                    } catch (_: Throwable) {
                                        false
                                    } finally {
                                        catalogRepository.discardDownload(archive)
                                    }
                                }
                                if (success) {
                                    preferences.setTextureReplacementsEnabled(true)
                                    installed = withContext(Dispatchers.IO) { installState.installedTextures() }
                                    onInstalled()
                                }
                                Toast.makeText(
                                    context,
                                    if (success) installSuccess else installFailure,
                                    Toast.LENGTH_LONG
                                ).show()
                                workingPackId = null
                                downloadProgress = 0f
                            }
                        }
                    )
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
private fun TextureCatalogCard(
    pack: RemoteTexturePack,
    installed: Boolean,
    updateAvailable: Boolean,
    working: Boolean,
    progress: Float,
    onSource: () -> Unit,
    onInstall: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f)
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
            if (working) {
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier.fillMaxWidth()
                )
                Text(
                    text = stringResource(R.string.content_downloading_percent, (progress * 100).toInt()),
                    style = MaterialTheme.typography.labelMedium
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onInstall, enabled = !working) {
                    Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                    Spacer(Modifier.width(7.dp))
                    Text(
                        stringResource(
                            when {
                                installed -> R.string.content_reinstall
                                updateAvailable -> R.string.content_update
                                else -> R.string.content_download_install
                            }
                        )
                    )
                }
                OutlinedButton(onClick = onSource, enabled = !working) {
                    Icon(Icons.Rounded.OpenInNew, contentDescription = null)
                    Spacer(Modifier.width(7.dp))
                    Text(stringResource(R.string.content_source))
                }
            }
        }
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

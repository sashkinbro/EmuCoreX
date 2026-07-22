package com.sbro.emucorex.ui.cheats

import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
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
import androidx.compose.material.icons.rounded.OpenInNew
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
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
import com.sbro.emucorex.data.CheatGameConfig
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.ContentLibraryRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.RemoteCheatPack
import com.sbro.emucorex.data.RemoteContentCatalogRepository
import com.sbro.emucorex.data.SelectedGameIdentity
import com.sbro.emucorex.ui.common.AppAlertDialog
import com.sbro.emucorex.ui.common.LibraryGamePicker
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CheatManagerScreen(onBackClick: () -> Unit) {
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val scope = rememberCoroutineScope()
    val preferences = remember(context) { AppPreferences(context) }
    val cheatRepository = remember(context) { CheatRepository(context) }
    val catalogRepository = remember(context) { RemoteContentCatalogRepository(context) }
    val libraryRepository = remember(context) { ContentLibraryRepository(context) }
    val cheatsEnabled by preferences.enableCheats.collectAsState(initial = false)
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()

    var games by remember { mutableStateOf<List<GameItem>>(emptyList()) }
    var selectedPath by remember { mutableStateOf<String?>(null) }
    var identity by remember { mutableStateOf<SelectedGameIdentity?>(null) }
    var catalog by remember { mutableStateOf<List<RemoteCheatPack>>(emptyList()) }
    var config by remember { mutableStateOf<CheatGameConfig?>(null) }
    var loading by remember { mutableStateOf(true) }
    var resolvingIdentity by remember { mutableStateOf(false) }
    var catalogCached by remember { mutableStateOf(false) }
    var catalogFailed by remember { mutableStateOf(false) }
    var workingId by remember { mutableStateOf<String?>(null) }
    var pendingDelete by remember { mutableStateOf<CheatGameConfig?>(null) }

    val importSuccess = stringResource(R.string.cheat_manager_import_success)
    val importFailure = stringResource(R.string.cheat_manager_import_failed)
    val downloadSuccess = stringResource(R.string.cheat_manager_download_success)
    val downloadFailure = stringResource(R.string.cheat_manager_download_failed)
    val deleteSuccess = stringResource(R.string.cheat_manager_delete_success)

    fun refreshConfig(selectedIdentity: SelectedGameIdentity?) {
        val serial = selectedIdentity?.serial.orEmpty()
        val crc = selectedIdentity?.crc
        val keys = buildList {
            if (serial.isNotBlank() && !crc.isNullOrBlank()) add("${serial}_$crc")
            if (!crc.isNullOrBlank()) add(crc)
            if (serial.isNotBlank()) add(serial)
        }
        config = cheatRepository.getGameConfig(keys, serial, crc)
    }

    LaunchedEffect(Unit) {
        val loaded = withContext(Dispatchers.IO) {
            val libraryGames = libraryRepository.loadGames()
            val remote = catalogRepository.loadCheatCatalog()
            Triple(libraryGames, remote.entries, remote)
        }
        games = loaded.first
        catalog = loaded.second
        catalogCached = loaded.third.fromCache
        catalogFailed = loaded.third.entries.isEmpty()
        selectedPath = games.firstOrNull()?.path
        loading = false
    }

    val selectedGame = games.firstOrNull { it.path == selectedPath }
    LaunchedEffect(selectedPath) {
        val game = selectedGame
        if (game == null) {
            identity = null
            config = null
            return@LaunchedEffect
        }
        resolvingIdentity = true
        val resolved = withContext(Dispatchers.IO) { libraryRepository.resolveIdentity(game) }
        identity = resolved
        refreshConfig(resolved)
        resolvingIdentity = false
    }

    val available = remember(catalog, identity, selectedGame) {
        val serial = identity?.serial
        val crc = identity?.crc
        val normalizedTitle = selectedGame?.title.orEmpty().catalogTitleKey()
        catalog.filter { entry ->
            when {
                !crc.isNullOrBlank() -> entry.crc.equals(crc, ignoreCase = true) &&
                    (entry.serials.isEmpty() || serial == null || entry.serials.any { it.equals(serial, true) })
                serial != null && entry.serials.isNotEmpty() -> entry.serials.any { it.equals(serial, true) }
                normalizedTitle.isNotEmpty() -> entry.title.catalogTitleKey() == normalizedTitle
                else -> false
            }
        }.sortedWith(compareByDescending<RemoteCheatPack> { it.crc.equals(crc, true) }.thenBy { it.title })
    }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    val text = context.contentResolver.openInputStream(uri)?.use { it.readBytes().decodeToString() }
                        ?: return@runCatching 0
                    val fallbackName = runCatching {
                        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                            ?.use { cursor ->
                                if (cursor.moveToFirst()) cursor.getString(0) else null
                            }
                    }.getOrNull()?.substringBeforeLast('.')
                    val gameKey = identity?.let { resolved ->
                        if (!resolved.serial.isNullOrBlank() && !resolved.crc.isNullOrBlank()) {
                            "${resolved.serial}_${resolved.crc}"
                        } else resolved.crc ?: resolved.serial
                    } ?: fallbackName ?: "cheat_${System.currentTimeMillis()}"
                    cheatRepository.importCheatFile(gameKey, text, enableAllByDefault = false)
                }.getOrDefault(0)
            }
            if (result > 0) {
                preferences.setEnableCheats(true)
                refreshConfig(identity)
            }
            Toast.makeText(context, if (result > 0) importSuccess else importFailure, Toast.LENGTH_LONG).show()
        }
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
                bottom = 24.dp + bottomInset
            ),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                ScreenTopBar(
                    title = stringResource(R.string.cheat_manager_title),
                    subtitle = stringResource(R.string.cheat_manager_subtitle),
                    onBackClick = onBackClick,
                    modifier = Modifier.padding(top = topInset)
                )
            }
            item {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(22.dp),
                    color = MaterialTheme.colorScheme.surface,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    stringResource(R.string.cheat_manager_enable),
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.SemiBold
                                )
                                Text(
                                    stringResource(R.string.cheat_manager_enable_desc),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Switch(
                                checked = cheatsEnabled,
                                onCheckedChange = { enabled -> scope.launch { preferences.setEnableCheats(enabled) } }
                            )
                        }
                        OutlinedButton(onClick = { importLauncher.launch(arrayOf("*/*")) }) {
                            Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text(stringResource(R.string.cheat_manager_import_pnach))
                        }
                    }
                }
            }
            item {
                LibraryGamePicker(
                    games = games,
                    selectedPath = selectedPath,
                    onSelected = { selectedPath = it.path }
                )
            }
            if (selectedGame != null) {
                item {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
                    ) {
                        Row(
                            modifier = Modifier.padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            if (resolvingIdentity) CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                            Text(
                                text = if (identity?.crc != null) {
                                    stringResource(R.string.cheat_manager_detected_identity, identity?.serial.orEmpty(), identity?.crc.orEmpty())
                                } else {
                                    stringResource(R.string.cheat_manager_crc_unavailable, identity?.serial.orEmpty())
                                },
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                }
            }
            item {
                Text(
                    text = stringResource(R.string.cheat_manager_available),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
            }
            when {
                loading -> item {
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(modifier = Modifier.size(22.dp), strokeWidth = 2.dp)
                        Text(stringResource(R.string.content_catalog_loading))
                    }
                }
                catalogFailed -> item {
                    Text(stringResource(R.string.content_catalog_failed), color = MaterialTheme.colorScheme.error)
                }
                selectedGame != null && available.isEmpty() -> item {
                    Text(
                        stringResource(R.string.cheat_manager_no_cheats),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                else -> items(available, key = RemoteCheatPack::id) { pack ->
                    CheatCatalogCard(
                        pack = pack,
                        working = workingId == pack.id,
                        onSource = { runCatching { uriHandler.openUri(pack.sourceUrl) } },
                        onDownload = {
                            if (workingId == null) {
                                scope.launch {
                                    workingId = pack.id
                                    val installedConfig = withContext(Dispatchers.IO) {
                                        runCatching {
                                            val text = catalogRepository.downloadCheatText(pack)
                                            val serial = identity?.serial
                                            val gameKey = if (!serial.isNullOrBlank()) "${serial}_${pack.crc}" else pack.crc
                                            val count = cheatRepository.importCheatFile(gameKey, text, enableAllByDefault = false)
                                            if (count <= 0) return@runCatching null
                                            cheatRepository.getGameConfig(gameKey, serial.orEmpty(), pack.crc)
                                        }.getOrNull()
                                    }
                                    if (installedConfig != null) {
                                        preferences.setEnableCheats(true)
                                        config = installedConfig
                                    }
                                    Toast.makeText(
                                        context,
                                        if (installedConfig != null) downloadSuccess else downloadFailure,
                                        Toast.LENGTH_LONG
                                    ).show()
                                    workingId = null
                                }
                            }
                        }
                    )
                }
            }
            if (catalogCached) {
                item {
                    Text(
                        stringResource(R.string.content_catalog_cached),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            config?.let { current ->
                item {
                    Text(
                        text = stringResource(R.string.cheat_manager_installed),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                }
                items(current.blocks, key = { it.id }) { block ->
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(16.dp),
                        color = MaterialTheme.colorScheme.surface
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(block.title, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
                            Switch(
                                checked = block.enabled,
                                onCheckedChange = { enabled ->
                                    val enabledIds = current.blocks
                                        .filter { it.enabled }
                                        .mapTo(mutableSetOf()) { it.id }
                                        .apply { if (enabled) add(block.id) else remove(block.id) }
                                    cheatRepository.setEnabledBlocks(current.gameKey, enabledIds)
                                    config = current.copy(
                                        blocks = current.blocks.map { item ->
                                            if (item.id == block.id) item.copy(enabled = enabled) else item
                                        }
                                    )
                                }
                            )
                        }
                    }
                }
                item {
                    OutlinedButton(onClick = { pendingDelete = current }) {
                        Icon(Icons.Rounded.DeleteOutline, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.cheat_manager_delete))
                    }
                }
            }
        }
    }

    pendingDelete?.let { current ->
        AppAlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text(stringResource(R.string.cheat_manager_delete_title)) },
            text = { Text(stringResource(R.string.cheat_manager_delete_confirm)) },
            confirmButton = {
                TextButton(onClick = {
                    cheatRepository.deleteImportedCheats(current.gameKey, current.serial, current.crc)
                    config = null
                    pendingDelete = null
                    Toast.makeText(context, deleteSuccess, Toast.LENGTH_SHORT).show()
                }) { Text(stringResource(R.string.delete)) }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete = null }) { Text(stringResource(R.string.cancel)) }
            }
        )
    }
}

@Composable
private fun CheatCatalogCard(
    pack: RemoteCheatPack,
    working: Boolean,
    onSource: () -> Unit,
    onDownload: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(pack.title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            Text(
                stringResource(R.string.cheat_manager_pack_meta, pack.crc, pack.blockCount),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (pack.description.isNotBlank()) {
                Text(
                    pack.description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Text(
                stringResource(R.string.content_authors, pack.authors.joinToString()),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onDownload, enabled = !working) {
                    if (working) CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                    else Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                    Spacer(Modifier.width(7.dp))
                    Text(stringResource(R.string.content_download_install))
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

private fun String.catalogTitleKey(): String =
    lowercase(Locale.US)
        .replace(Regex("[^a-z0-9]+"), " ")
        .trim()

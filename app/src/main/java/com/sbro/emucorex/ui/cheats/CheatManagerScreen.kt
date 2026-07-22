package com.sbro.emucorex.ui.cheats

import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.ArrowForward
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.runtime.rememberCoroutineScope
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
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CheatBlock
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
import com.sbro.emucorex.ui.common.cheatCatalogGameTitleKey
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
    var identityPath by remember { mutableStateOf<String?>(null) }
    var catalog by remember { mutableStateOf<List<RemoteCheatPack>>(emptyList()) }
    var config by remember { mutableStateOf<CheatGameConfig?>(null) }
    var loading by remember { mutableStateOf(true) }
    var resolvingIdentity by remember { mutableStateOf(false) }
    var catalogCached by remember { mutableStateOf(false) }
    var catalogFailed by remember { mutableStateOf(false) }
    var workingId by remember { mutableStateOf<String?>(null) }
    var pendingDelete by remember { mutableStateOf<CheatGameConfig?>(null) }
    var selectedCheatCategory by remember { mutableStateOf<CheatCategory?>(null) }
    var cheatSearchVisible by remember { mutableStateOf(false) }
    var cheatSearchQuery by remember { mutableStateOf("") }

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
        if (loaded.third.cacheHit) {
            val refreshed = withContext(Dispatchers.IO) {
                catalogRepository.loadCheatCatalog(forceRefresh = true)
            }
            if (refreshed.entries.isNotEmpty()) {
                catalog = refreshed.entries
                catalogCached = refreshed.fromCache
                catalogFailed = false
            }
        }
    }

    val selectedGame = games.firstOrNull { it.path == selectedPath }
    LaunchedEffect(selectedPath) {
        val game = selectedGame
        if (game == null) {
            identity = null
            identityPath = null
            config = null
            return@LaunchedEffect
        }
        identity = null
        identityPath = null
        resolvingIdentity = true
        val resolved = withContext(Dispatchers.IO) { libraryRepository.resolveIdentity(game) }
        identity = resolved
        identityPath = game.path
        refreshConfig(resolved)
        resolvingIdentity = false
    }

    LaunchedEffect(config?.gameKey) {
        selectedCheatCategory = null
        cheatSearchVisible = false
        cheatSearchQuery = ""
    }

    val installedBlocks = config?.blocks.orEmpty()
    val categoryGroups = remember(installedBlocks) {
        CheatCategory.entries.mapNotNull { category ->
            installedBlocks.filter { block -> block.category() == category }
                .takeIf(List<CheatBlock>::isNotEmpty)
                ?.let { category to it }
        }
    }
    val visibleInstalledBlocks = remember(installedBlocks, selectedCheatCategory, cheatSearchQuery) {
        when {
            cheatSearchQuery.isNotBlank() -> installedBlocks.filter { block ->
                block.title.contains(cheatSearchQuery.trim(), ignoreCase = true)
            }
            selectedCheatCategory != null -> installedBlocks.filter { block ->
                block.category() == selectedCheatCategory
            }
            else -> emptyList()
        }
    }

    val compatiblePacks = remember(catalog, identity, identityPath, selectedGame, selectedPath) {
        if (identity == null || identityPath != selectedPath) return@remember emptyList()
        val serial = identity?.serial
        val crc = identity?.crc
        val normalizedTitle = selectedGame?.title.orEmpty().cheatCatalogGameTitleKey()
        catalog.filter { entry ->
            when {
                !crc.isNullOrBlank() -> entry.crc.equals(crc, ignoreCase = true) &&
                    (entry.serials.isEmpty() || serial == null || entry.serials.any { it.equals(serial, true) })
                serial != null && entry.serials.isNotEmpty() -> entry.serials.any { it.equals(serial, true) }
                normalizedTitle.isNotEmpty() -> entry.title.cheatCatalogGameTitleKey() == normalizedTitle
                else -> false
            }
        }.sortedWith(compareByDescending<RemoteCheatPack> { it.crc.equals(crc, true) }.thenBy { it.title })
    }
    val selectedTitleKey = selectedGame?.title.orEmpty().cheatCatalogGameTitleKey()
    val otherVersionPacks = remember(catalog, compatiblePacks, selectedTitleKey) {
        if (selectedTitleKey.isEmpty()) emptyList()
        else catalog.filter { entry ->
            entry !in compatiblePacks && entry.title.cheatCatalogGameTitleKey() == selectedTitleKey
        }.sortedWith(compareBy<RemoteCheatPack> { it.title }.thenBy { it.crc })
    }
    val onlineContentState = CheatOnlineContentState(
        phase = when {
            loading || resolvingIdentity || identity == null || identityPath != selectedPath ->
                CheatOnlinePhase.LOADING
            catalogFailed -> CheatOnlinePhase.ERROR
            selectedGame != null && compatiblePacks.isEmpty() && otherVersionPacks.isEmpty() ->
                CheatOnlinePhase.EMPTY
            else -> CheatOnlinePhase.CONTENT
        },
        compatiblePacks = compatiblePacks,
        otherVersionPacks = otherVersionPacks,
        serial = selectedGame?.serial
    )

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

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background,
        contentColor = MaterialTheme.colorScheme.onBackground
    ) {
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontalSystemBarPadding),
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
                    onSelected = { game ->
                        if (game.path != selectedPath) {
                            identity = null
                            identityPath = null
                            config = null
                            resolvingIdentity = true
                            selectedPath = game.path
                        }
                    },
                    horizontalContentPadding = ScreenHorizontalPadding,
                    fullBleedPadding = ScreenHorizontalPadding
                )
            }
            if (selectedGame != null) {
                item {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                        contentColor = MaterialTheme.colorScheme.onSurface
                    ) {
                        AnimatedContent(
                            targetState = Triple(
                                resolvingIdentity,
                                identity?.serial ?: selectedGame.serial,
                                identity?.crc
                            ),
                            transitionSpec = { fadeIn() togetherWith fadeOut() },
                            label = "selectedGameIdentity"
                        ) { (resolving, serial, crc) ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(14.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                if (resolving) {
                                    CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                                }
                                Text(
                                    text = when {
                                        resolving -> serial ?: stringResource(R.string.content_serial_unknown)
                                        crc != null -> stringResource(
                                            R.string.cheat_manager_detected_identity,
                                            serial.orEmpty(),
                                            crc
                                        )
                                        else -> stringResource(
                                            R.string.cheat_manager_crc_unavailable,
                                            serial.orEmpty()
                                        )
                                    },
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                            }
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
            item(key = "online-cheat-content") {
                AnimatedContent(
                    targetState = onlineContentState,
                    transitionSpec = {
                        (fadeIn(tween(220)) + slideInVertically(tween(220)) { height -> height / 10 }) togetherWith
                            (fadeOut(tween(140)) + slideOutVertically(tween(140)) { height -> -height / 12 })
                    },
                    label = "onlineCheatContent"
                ) { state ->
                    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                        when (state.phase) {
                            CheatOnlinePhase.LOADING -> Row(
                                horizontalArrangement = Arrangement.spacedBy(10.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                CircularProgressIndicator(modifier = Modifier.size(22.dp), strokeWidth = 2.dp)
                                Text(
                                    text = state.serial ?: stringResource(R.string.content_catalog_loading),
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            CheatOnlinePhase.ERROR -> Text(
                                stringResource(R.string.content_catalog_failed),
                                color = MaterialTheme.colorScheme.error
                            )
                            CheatOnlinePhase.EMPTY -> CheatCatalogEmptyState()
                            CheatOnlinePhase.CONTENT -> {
                                if (state.compatiblePacks.isNotEmpty()) {
                                    CheatCatalogSectionTitle(
                                        text = stringResource(R.string.content_compatible_game_version_section)
                                    )
                                    state.compatiblePacks.forEach { pack ->
                                        CheatCatalogCard(
                                            pack = pack,
                                            compatible = true,
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
                                                                val gameKey = if (!serial.isNullOrBlank()) {
                                                                    "${serial}_${pack.crc}"
                                                                } else {
                                                                    pack.crc
                                                                }
                                                                val count = cheatRepository.importCheatFile(
                                                                    gameKey,
                                                                    text,
                                                                    enableAllByDefault = false,
                                                                    mergeWithExisting = true
                                                                )
                                                                if (count <= 0) return@runCatching null
                                                                cheatRepository.getGameConfig(
                                                                    gameKey,
                                                                    serial.orEmpty(),
                                                                    pack.crc
                                                                )
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
                                if (state.otherVersionPacks.isNotEmpty()) {
                                    CheatCatalogSectionTitle(
                                        text = stringResource(R.string.content_other_game_versions_section)
                                    )
                                    state.otherVersionPacks.forEach { pack ->
                                        CheatCatalogCard(
                                            pack = pack,
                                            compatible = false,
                                            working = false,
                                            onSource = { runCatching { uriHandler.openUri(pack.sourceUrl) } },
                                            onDownload = {}
                                        )
                                    }
                                }
                            }
                        }
                    }
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
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = stringResource(R.string.cheat_manager_installed),
                            modifier = Modifier.weight(1f),
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Bold
                        )
                        IconButton(
                            onClick = {
                                cheatSearchVisible = !cheatSearchVisible
                                selectedCheatCategory = null
                                if (!cheatSearchVisible) cheatSearchQuery = ""
                            }
                        ) {
                            Icon(
                                imageVector = if (cheatSearchVisible) Icons.Rounded.Close else Icons.Rounded.Search,
                                contentDescription = stringResource(R.string.cheat_manager_search)
                            )
                        }
                    }
                }
                if (cheatSearchVisible) {
                    item {
                        OutlinedTextField(
                            value = cheatSearchQuery,
                            onValueChange = { cheatSearchQuery = it },
                            modifier = Modifier.fillMaxWidth(),
                            placeholder = { Text(stringResource(R.string.cheat_manager_search)) },
                            leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                            trailingIcon = if (cheatSearchQuery.isNotEmpty()) {
                                {
                                    IconButton(onClick = { cheatSearchQuery = "" }) {
                                        Icon(
                                            Icons.Rounded.Close,
                                            contentDescription = stringResource(R.string.home_search_clear)
                                        )
                                    }
                                }
                            } else null,
                            singleLine = true,
                            shape = RoundedCornerShape(18.dp)
                        )
                    }
                }
                if (!cheatSearchVisible) {
                    item(key = "cheat-folder-header") {
                        CheatCategoryBrowserHeader(
                            selectedCategory = selectedCheatCategory,
                            count = if (selectedCheatCategory == null) {
                                categoryGroups.sumOf { it.second.size }
                            } else {
                                visibleInstalledBlocks.size
                            },
                            onBack = { selectedCheatCategory = null }
                        )
                    }
                } else {
                    item(key = "cheat-search-header") {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = stringResource(R.string.cheat_manager_search),
                                modifier = Modifier.weight(1f),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = stringResource(
                                    R.string.cheat_manager_category_count,
                                    visibleInstalledBlocks.size
                                ),
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
                if (!cheatSearchVisible && selectedCheatCategory == null) {
                    itemsIndexed(categoryGroups, key = { _, item -> item.first.name }) { index, (category, blocks) ->
                        AnimatedFolderEntry(
                            transitionKey = "folders",
                            itemKey = category.name,
                            index = index
                        ) {
                            CheatCategoryCard(
                                category = category,
                                count = blocks.size,
                                onClick = { selectedCheatCategory = category }
                            )
                        }
                    }
                } else {
                    if (visibleInstalledBlocks.isEmpty()) {
                        item { CheatSearchEmptyState() }
                    } else {
                        itemsIndexed(visibleInstalledBlocks, key = { _, block -> block.id }) { index, block ->
                            AnimatedFolderEntry(
                                transitionKey = selectedCheatCategory?.name ?: "search:$cheatSearchQuery",
                                itemKey = block.id,
                                index = index
                            ) {
                                CheatToggleCard(
                                    block = block,
                                    onEnabledChange = { enabled ->
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
private fun CheatCategoryBrowserHeader(
    selectedCategory: CheatCategory?,
    count: Int,
    onBack: () -> Unit
) {
    AnimatedContent(
        targetState = selectedCategory,
        transitionSpec = {
            if (targetState != null) {
                (slideInHorizontally(tween(260)) { width -> width / 3 } + fadeIn(tween(180))) togetherWith
                    (slideOutHorizontally(tween(190)) { width -> -width / 5 } + fadeOut(tween(140)))
            } else {
                (slideInHorizontally(tween(260)) { width -> -width / 3 } + fadeIn(tween(180))) togetherWith
                    (slideOutHorizontally(tween(190)) { width -> width / 5 } + fadeOut(tween(140)))
            }
        },
        label = "cheatFolderHeader"
    ) { category ->
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (category == null) {
                Icon(
                    imageVector = Icons.Rounded.Folder,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(Modifier.width(9.dp))
                Text(
                    text = stringResource(R.string.cheat_manager_categories),
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            } else {
                TextButton(onClick = onBack, modifier = Modifier.weight(1f)) {
                    Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = null)
                    Spacer(Modifier.width(7.dp))
                    Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                    Spacer(Modifier.width(7.dp))
                    Text(
                        text = stringResource(category.titleRes),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            Text(
                text = stringResource(R.string.cheat_manager_category_count, count),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun AnimatedFolderEntry(
    transitionKey: String,
    itemKey: String,
    index: Int,
    content: @Composable () -> Unit
) {
    val visibility = remember(transitionKey, itemKey) {
        MutableTransitionState(false).apply { targetState = true }
    }
    val delay = (index.coerceAtMost(6) * 24)
    AnimatedVisibility(
        visibleState = visibility,
        enter = fadeIn(tween(durationMillis = 190, delayMillis = delay)) +
            slideInHorizontally(tween(durationMillis = 250, delayMillis = delay)) { width -> width / 5 } +
            scaleIn(tween(durationMillis = 220, delayMillis = delay), initialScale = 0.985f)
    ) {
        content()
    }
}

@Composable
private fun CheatCategoryCard(
    category: CheatCategory,
    count: Int,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 15.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                shape = RoundedCornerShape(13.dp),
                color = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer
            ) {
                Icon(
                    imageVector = Icons.Rounded.Folder,
                    contentDescription = null,
                    modifier = Modifier.padding(10.dp).size(21.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(category.titleRes),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = stringResource(R.string.cheat_manager_category_count, count),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Icon(
                imageVector = Icons.AutoMirrored.Rounded.ArrowForward,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun CheatToggleCard(
    block: CheatBlock,
    onEnabledChange: (Boolean) -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = block.title,
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyLarge
            )
            Switch(
                checked = block.enabled,
                onCheckedChange = onEnabledChange
            )
        }
    }
}

@Composable
private fun CheatSearchEmptyState() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant
    ) {
        Text(
            text = stringResource(R.string.cheat_manager_search_empty),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 18.dp),
            style = MaterialTheme.typography.bodyLarge
        )
    }
}

@Composable
private fun CheatCatalogEmptyState() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
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
                    modifier = Modifier.padding(11.dp).size(22.dp)
                )
            }
            Text(
                text = stringResource(R.string.cheat_manager_no_cheats),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun CheatCatalogCard(
    pack: RemoteCheatPack,
    compatible: Boolean,
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
            ContentCompatibilityBadge(compatible = compatible)
            Text(
                text = stringResource(
                    R.string.content_supported_serials,
                    pack.serials.joinToString().ifBlank {
                        stringResource(R.string.content_serial_unknown)
                    }
                ),
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
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (compatible) {
                    Button(
                        onClick = onDownload,
                        enabled = !working,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        if (working) CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                        else Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                        Spacer(Modifier.width(7.dp))
                        Text(
                            text = stringResource(R.string.content_download_install),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
                OutlinedButton(
                    onClick = onSource,
                    enabled = !working,
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
private fun CheatCatalogSectionTitle(text: String) {
    Text(text = text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
}

@Composable
private fun ContentCompatibilityBadge(compatible: Boolean) {
    Surface(
        shape = RoundedCornerShape(10.dp),
        color = if (compatible) Color(0xFF1B6B3A) else MaterialTheme.colorScheme.errorContainer,
        contentColor = if (compatible) Color.White else MaterialTheme.colorScheme.onErrorContainer
    ) {
        Text(
            text = stringResource(
                if (compatible) R.string.content_compatible else R.string.content_not_compatible
            ),
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold
        )
    }
}

private enum class CheatCategory(val titleRes: Int) {
    PLAYER(R.string.cheat_manager_category_player),
    ITEMS(R.string.cheat_manager_category_items),
    WORLD(R.string.cheat_manager_category_world),
    PROGRESS(R.string.cheat_manager_category_progress),
    VEHICLES(R.string.cheat_manager_category_vehicles),
    STATS(R.string.cheat_manager_category_stats),
    HOTKEYS(R.string.cheat_manager_category_hotkeys),
    OTHER(R.string.cheat_manager_category_other)
}

private enum class CheatOnlinePhase {
    LOADING,
    ERROR,
    EMPTY,
    CONTENT
}

private data class CheatOnlineContentState(
    val phase: CheatOnlinePhase,
    val compatiblePacks: List<RemoteCheatPack>,
    val otherVersionPacks: List<RemoteCheatPack>,
    val serial: String?
)

private fun CheatBlock.category(): CheatCategory {
    val value = title.lowercase(Locale.US)
    return when {
        value.containsAny(" press ", "press ", "hold ", "button", "{l1}", "{l2}", "{r1}", "{r2}", "{select}") ->
            CheatCategory.HOTKEYS
        value.containsAny("health", "money", "pocket change", "stamina", "energy", "trouble", "wanted", "player", "character") ->
            CheatCategory.PLAYER
        value.containsAny("weapon", "ammo", "inventory", "item", "fire cracker", "spud", "slingshot", "projectile", "outfit", "clothing") ->
            CheatCategory.ITEMS
        value.containsAny("time", "hour", "clock", "weather", "day", "night", "season") ->
            CheatCategory.WORLD
        value.containsAny("mission", "chapter", "unlock", "class", "grade", "complete", "progress", "troph", "collectible") ->
            CheatCategory.PROGRESS
        value.containsAny("vehicle", "bike", "bicycle", "car", "kart", "race", "skateboard", "lawnmower") ->
            CheatCategory.VEHICLES
        value.startsWith("max ") || value.startsWith("no ") ||
            value.containsAny("stat", "record", "times ", "distance", "earned", "spent", "attempted", "hits", "killed", "thrown", "purchased") ->
            CheatCategory.STATS
        else -> CheatCategory.OTHER
    }
}

private fun String.containsAny(vararg needles: String): Boolean = needles.any(::contains)

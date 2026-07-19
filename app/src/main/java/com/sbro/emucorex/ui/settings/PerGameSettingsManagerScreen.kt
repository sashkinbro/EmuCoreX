package com.sbro.emucorex.ui.settings

import android.graphics.Color
import android.os.Build
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.Save
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.RendererDefaults
import androidx.compose.material.icons.rounded.Tune
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.graphics.drawable.toDrawable
import com.sbro.emucorex.R
import com.sbro.emucorex.core.buildUpscaleOptions
import com.sbro.emucorex.core.formatUpscaleLabel
import com.sbro.emucorex.core.upscaleKeyToMultiplier
import com.sbro.emucorex.core.upscaleMultiplierValue
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GameLibraryCacheRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameRepository
import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import org.json.JSONObject
import java.text.DateFormat
import kotlin.math.roundToInt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

private enum class GameSettingsManagerTab {
    Graphics,
    System,
    Controls,
    Fixes
}

private val GameSettingsSectionContentPadding = 16.dp

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun PerGameSettingsManagerScreen(
    initialGamePath: String? = null,
    onOpenControlsLayoutEditor: (GameItem) -> Unit,
    onBackClick: () -> Unit
) {
    val context = LocalContext.current
    val repository = remember(context) { PerGameSettingsRepository(context) }
    val preferences = remember(context) { AppPreferences(context) }
    val libraryCacheRepository = remember(context) { GameLibraryCacheRepository(context) }
    val settingsSnapshot by preferences.settingsSnapshot.collectAsState(initial = SettingsSnapshot())
    val rootPaths by preferences.gamePaths.collectAsState(initial = emptyList())
    val preferEnglishTitles by preferences.preferEnglishGameTitles.collectAsState(initial = false)
    val scope = rememberCoroutineScope()
    val lifecycleOwner = LocalLifecycleOwner.current
    val profileMutationMutex = remember { Mutex() }
    var profiles by remember { mutableStateOf(emptyList<PerGameSettings>()) }
    var libraryGames by remember { mutableStateOf(emptyList<GameItem>()) }
    var selectedGamePath by rememberSaveable { mutableStateOf(initialGamePath) }
    var selectedTab by rememberSaveable { mutableStateOf(GameSettingsManagerTab.Graphics) }
    var isOpeningControlsEditor by remember { mutableStateOf(false) }
    var showTopBarMenu by remember { mutableStateOf(false) }
    val pendingDeleteProfile = remember { mutableStateOf<PerGameSettings?>(null) }
    val showResetAllDialog = remember { mutableStateOf(false) }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 10.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val exportSuccess = stringResource(R.string.game_settings_manager_export_success)
    val exportFailure = stringResource(R.string.game_settings_manager_export_failure)
    val importSuccess = stringResource(R.string.game_settings_manager_import_success)
    val importFailure = stringResource(R.string.game_settings_manager_import_failure)
    val controlsLayoutSaveFailure = stringResource(R.string.controls_editor_save_failed)

    fun refreshProfiles() {
        scope.launch {
            profiles = profileMutationMutex.withLock {
                withContext(Dispatchers.IO) { repository.getAll() }
            }
        }
    }

    suspend fun persistDraft(draft: PerGameSettings): List<PerGameSettings> {
        return profileMutationMutex.withLock {
            withContext(Dispatchers.IO) {
                val currentLayout = repository.get(draft.gameKey)?.touchControlsLayout
                repository.save(
                    draft.copy(
                        touchControlsLayout = currentLayout,
                        providedKeys = null
                    )
                )
                repository.getAll()
            }
        }
    }

    fun toast(message: String) {
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
    }

    LaunchedEffect(repository) {
        profiles = withContext(Dispatchers.IO) {
            repository.getAll()
        }
    }

    DisposableEffect(lifecycleOwner, repository) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                isOpeningControlsEditor = false
                refreshProfiles()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    LaunchedEffect(rootPaths, preferEnglishTitles) {
        libraryGames = withContext(Dispatchers.IO) {
            rootPaths.takeIf { it.isNotEmpty() }
                ?.let { libraryCacheRepository.loadSnapshot(GameLibraryCacheRepository.libraryKey(it), preferEnglishTitles).games }
                .orEmpty()
        }
    }

    val exportLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/json")
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            val success = withContext(Dispatchers.IO) {
                runCatching {
                    context.contentResolver.openOutputStream(uri)?.use { output ->
                        output.write(repository.exportJson().toString(2).toByteArray())
                    } != null
                }.getOrDefault(false)
            }
            toast(if (success) exportSuccess else exportFailure)
        }
    }

    val importLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            val success = withContext(Dispatchers.IO) {
                runCatching {
                    val json = context.contentResolver.openInputStream(uri)?.use { input ->
                        JSONObject(input.readBytes().decodeToString())
                    } ?: return@runCatching false
                    repository.importJson(json)
                    true
                }.getOrDefault(false)
            }
            if (success) refreshProfiles()
            toast(if (success) importSuccess else importFailure)
        }
    }

    val availableGames = remember(libraryGames, profiles) {
        val libraryByPath = libraryGames.associateBy { it.path }
        val profileOnlyGames = profiles
            .filterNot { it.gameKey in libraryByPath }
            .map { profile ->
                GameItem(
                    path = profile.gameKey,
                    title = profile.gameTitle,
                    fileName = profile.gameTitle,
                    fileSize = 0L,
                    lastModified = profile.updatedAt,
                    serial = profile.gameSerial
                )
            }
        (libraryGames + profileOnlyGames).sortedBy { it.title.lowercase() }
    }
    val profileGameKeys = remember(profiles) {
        profiles.mapTo(HashSet()) { it.gameKey }
    }
    val selectedGame = remember(availableGames, selectedGamePath) {
        selectedGamePath?.let { path -> availableGames.firstOrNull { it.path == path } }
    }
    val selectedProfile = remember(profiles, selectedGamePath) {
        selectedGamePath?.let { path -> profiles.firstOrNull { it.gameKey == path } }
    }

    LaunchedEffect(initialGamePath, availableGames, profiles) {
        val requestedGame = initialGamePath?.let { path -> availableGames.firstOrNull { it.path == path } }
        when {
            requestedGame != null && selectedGamePath != requestedGame.path -> {
                selectedGamePath = requestedGame.path
            }
            selectedGamePath == null || availableGames.none { it.path == selectedGamePath } -> {
                selectedGamePath = profiles.firstOrNull()?.gameKey ?: availableGames.firstOrNull()?.path
            }
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = 24.dp + bottomInset
        ),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.game_settings_manager_title),
                onBackClick = onBackClick,
                actions = {
                    IconButton(onClick = { showTopBarMenu = true }) {
                        Icon(
                            imageVector = Icons.Rounded.MoreVert,
                            contentDescription = stringResource(R.string.settings_more_options)
                        )
                    }
                    DropdownMenu(
                        expanded = showTopBarMenu,
                        onDismissRequest = { showTopBarMenu = false }
                    ) {
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.game_settings_manager_export_title)) },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Save,
                                    contentDescription = null
                                )
                            },
                            onClick = {
                                showTopBarMenu = false
                                exportLauncher.launch("emucorex-game-settings.json")
                            }
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.game_settings_manager_import_title)) },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.FolderOpen,
                                    contentDescription = null
                                )
                            },
                            onClick = {
                                showTopBarMenu = false
                                importLauncher.launch(arrayOf("application/json", "*/*"))
                            }
                        )
                        selectedProfile?.let { profile ->
                            DropdownMenuItem(
                                text = { Text(stringResource(R.string.delete)) },
                                leadingIcon = {
                                    Icon(
                                        imageVector = Icons.Rounded.DeleteOutline,
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.error
                                    )
                                },
                                onClick = {
                                    showTopBarMenu = false
                                    pendingDeleteProfile.value = profile
                                }
                            )
                        }
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.game_settings_manager_reset_all_title)) },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Restore,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.error
                                )
                            },
                            onClick = {
                                showTopBarMenu = false
                                showResetAllDialog.value = true
                            }
                        )
                    }
                }
            )
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = stringResource(R.string.game_settings_manager_choose_game),
                        style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onBackground
                    )
                    Text(
                        text = selectedGame?.let { game ->
                            listOfNotNull(game.title, game.serial?.takeIf { it.isNotBlank() }).joinToString("  /  ")
                        } ?: stringResource(R.string.game_settings_manager_no_games),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                selectedGame?.let { game ->
                    IconButton(
                        onClick = {
                            scope.launch {
                                profiles = withContext(Dispatchers.IO) {
                                    repository.delete(game.path)
                                    repository.getAll()
                                }
                            }
                        }
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Restore,
                            contentDescription = stringResource(R.string.game_settings_manager_reset_game_title),
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(30.dp)
                        )
                    }
                }
            }
        }

        if (availableGames.isNotEmpty()) {
            item {
                LazyRow(
                    modifier = Modifier
                        .fillMaxWidth()
                        .gameManagerFullBleed(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding
                    )
                ) {
                    items(availableGames, key = { it.path }) { game ->
                        GamePickerCard(
                            game = game,
                            selected = game.path == selectedGamePath,
                            hasProfile = game.path in profileGameKeys,
                            onClick = { selectedGamePath = game.path }
                        )
                    }
                }
            }

            item {
                GameSettingsManagerTabRow(
                    selectedTab = selectedTab,
                    onSelected = { selectedTab = it }
                )
            }

            selectedGame?.let { game ->
                item {
                    val storedProfile = profiles.firstOrNull { it.gameKey == game.path }
                    val defaultProfile = remember(settingsSnapshot, game) {
                        settingsSnapshot.toPerGameSettings(game)
                    }
                    val editableProfile = remember(storedProfile, defaultProfile) {
                        (storedProfile ?: defaultProfile).resolveAgainst(defaultProfile)
                    }
                    var draft by remember(game.path, editableProfile) { mutableStateOf(editableProfile) }
                    var hasUserChange by remember(game.path, editableProfile) { mutableStateOf(false) }
                    val maxUpscaleMultiplier = remember(draft.renderer) {
                        EmulatorBridge.getMaxUpscaleMultiplier(normalizeManagerRenderer(draft.renderer))
                    }

                    LaunchedEffect(draft) {
                        if (hasUserChange) {
                            profiles = persistDraft(draft)
                        } else {
                            hasUserChange = true
                        }
                    }

                    GameSettingsManagerEditorPanel(
                        game = game,
                        draft = draft,
                        defaultProfile = defaultProfile,
                        selectedTab = selectedTab,
                        maxUpscaleMultiplier = maxUpscaleMultiplier,
                        onOpenControlsLayoutEditor = {
                            if (isOpeningControlsEditor) return@GameSettingsManagerEditorPanel
                            isOpeningControlsEditor = true
                            scope.launch {
                                val savedProfiles = runCatching { persistDraft(draft) }.getOrNull()
                                if (savedProfiles != null) {
                                    profiles = savedProfiles
                                    onOpenControlsLayoutEditor(game)
                                } else {
                                    isOpeningControlsEditor = false
                                    toast(controlsLayoutSaveFailure)
                                }
                            }
                        },
                        onDraftChange = { draft = it }
                    )
                }
            }
        } else {
            item {
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
                            .padding(22.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.game_settings_manager_empty_title),
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            textAlign = TextAlign.Center
                        )
                        Text(
                            text = stringResource(R.string.game_settings_manager_empty_desc),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            textAlign = TextAlign.Center
                        )
                    }
                }
            }
        }
    }

    pendingDeleteProfile.value?.let { profile ->
        AlertDialog(
            onDismissRequest = { pendingDeleteProfile.value = null },
            title = { Text(stringResource(R.string.game_settings_manager_delete_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.game_settings_manager_delete_desc,
                        profile.gameTitle
                    )
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            profiles = withContext(Dispatchers.IO) {
                                repository.delete(profile.gameKey)
                                repository.getAll()
                            }
                            pendingDeleteProfile.value = null
                        }
                    }
                ) {
                    Text(stringResource(R.string.delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingDeleteProfile.value = null }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }

    if (showResetAllDialog.value) {
        AlertDialog(
            onDismissRequest = { showResetAllDialog.value = false },
            title = { Text(stringResource(R.string.game_settings_manager_reset_all_title)) },
            text = { Text(stringResource(R.string.game_settings_manager_reset_all_desc)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            profiles = withContext(Dispatchers.IO) {
                                repository.deleteAll()
                                repository.getAll()
                            }
                            showResetAllDialog.value = false
                        }
                    }
                ) {
                    Text(stringResource(R.string.game_settings_manager_reset_all_confirm))
                }
            },
            dismissButton = {
                TextButton(onClick = { showResetAllDialog.value = false }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }
}

@Composable
private fun GamePickerCard(
    game: GameItem,
    selected: Boolean,
    hasProfile: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .width(280.dp)
            .height(96.dp),
        shape = RoundedCornerShape(22.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.62f)
        } else {
            MaterialTheme.colorScheme.surface
        },
        border = BorderStroke(
            width = if (selected) 2.dp else 1.dp,
            color = if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.78f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)
            }
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(64.dp),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.26f)
            ) {
                GameCoverArt(
                    coverPath = game.coverArtPath,
                    fallbackTitle = game.title,
                    modifier = Modifier
                        .fillMaxSize()
                        .clip(RoundedCornerShape(16.dp)),
                    contentScale = ContentScale.Crop
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = if (selected) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                game.serial?.takeIf { it.isNotBlank() }?.let { serial ->
                    Text(
                        text = serial,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                if (hasProfile) {
                    Text(
                        text = stringResource(R.string.game_settings_manager_profile_active_short),
                        style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
    }
}

@Composable
private fun GameSettingsManagerTabRow(
    selectedTab: GameSettingsManagerTab,
    onSelected: (GameSettingsManagerTab) -> Unit
) {
    LazyRow(
        modifier = Modifier
            .fillMaxWidth()
            .gameManagerFullBleed(),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding
        )
    ) {
        items(GameSettingsManagerTab.entries.toList(), key = { it.name }) { tab ->
            FilterChip(
                selected = selectedTab == tab,
                onClick = { onSelected(tab) },
                label = {
                    Text(
                        text = when (tab) {
                            GameSettingsManagerTab.Graphics -> stringResource(R.string.settings_graphics_tab)
                            GameSettingsManagerTab.System -> stringResource(R.string.game_settings_manager_tab_system)
                            GameSettingsManagerTab.Controls -> stringResource(R.string.settings_controls_tab)
                            GameSettingsManagerTab.Fixes -> stringResource(R.string.settings_fixes_tab)
                        }
                    )
                }
            )
        }
    }
}

@Composable
private fun GameSettingsManagerEditorPanel(
    game: GameItem,
    draft: PerGameSettings,
    defaultProfile: PerGameSettings,
    selectedTab: GameSettingsManagerTab,
    maxUpscaleMultiplier: Int,
    onOpenControlsLayoutEditor: () -> Unit,
    onDraftChange: (PerGameSettings) -> Unit
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = selectedTab.title(),
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onBackground
                )
                Text(
                    text = stringResource(R.string.game_settings_manager_editor_desc),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        if (selectedTab == GameSettingsManagerTab.Controls) {
            ManagerActionButton(
                modifier = Modifier.fillMaxWidth(),
                icon = Icons.Rounded.Tune,
                title = stringResource(R.string.game_settings_edit_controls),
                onClick = onOpenControlsLayoutEditor
            )
            Text(
                text = stringResource(R.string.game_settings_edit_controls_desc),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 4.dp)
            )
        }
        GameSettingsTabContent(
            draft = draft,
            defaultProfile = defaultProfile,
            selectedTab = selectedTab,
            maxUpscaleMultiplier = maxUpscaleMultiplier,
            onDraftChange = onDraftChange
        )
    }
}

@Composable
private fun GameSettingsTabContent(
    draft: PerGameSettings,
    defaultProfile: PerGameSettings,
    selectedTab: GameSettingsManagerTab,
    maxUpscaleMultiplier: Int,
    onDraftChange: (PerGameSettings) -> Unit
) {
    val nativeUpscaleLabel = stringResource(R.string.settings_upscale_native)
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        when (selectedTab) {
            GameSettingsManagerTab.Graphics -> {
                EditorSection(title = stringResource(R.string.game_settings_manager_section_profile)) {
                    SelectionRow(
                        title = stringResource(R.string.settings_renderer),
                        options = listOf(
                            12 to stringResource(R.string.settings_renderer_opengl),
                            14 to stringResource(R.string.settings_renderer_vulkan),
                            13 to stringResource(R.string.settings_renderer_software)
                        ),
                        selectedValue = normalizeManagerRenderer(draft.renderer),
                        onSelected = { onDraftChange(draft.copy(renderer = it)) },
                        helpText = stringResource(R.string.settings_help_renderer),
                        onResetToDefault = { onDraftChange(draft.copy(renderer = normalizeManagerRenderer(defaultProfile.renderer))) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_upscale),
                        options = buildUpscaleOptions(nativeUpscaleLabel, maxUpscaleMultiplier),
                        selectedValue = upscaleMultiplierValue(draft.upscaleMultiplier),
                        onSelected = { onDraftChange(draft.copy(upscaleMultiplier = upscaleKeyToMultiplier(it))) },
                        helpText = stringResource(R.string.settings_help_upscale),
                        onResetToDefault = { onDraftChange(draft.copy(upscaleMultiplier = defaultProfile.upscaleMultiplier)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_aspect_ratio),
                        options = listOf(
                            1 to stringResource(R.string.settings_aspect_ratio_auto),
                            2 to stringResource(R.string.settings_aspect_ratio_43),
                            3 to stringResource(R.string.settings_aspect_ratio_169),
                            4 to stringResource(R.string.settings_aspect_ratio_107),
                            0 to stringResource(R.string.emulation_aspect_stretch)
                        ),
                        selectedValue = draft.aspectRatio,
                        onSelected = { onDraftChange(draft.copy(aspectRatio = it)) },
                        helpText = stringResource(R.string.settings_help_aspect_ratio),
                        onResetToDefault = { onDraftChange(draft.copy(aspectRatio = defaultProfile.aspectRatio)) }
                    )
                }
                EditorSection(title = stringResource(R.string.game_settings_manager_section_graphics)) {
                    SelectionRow(
                        title = stringResource(R.string.settings_bilinear_filtering),
                        options = bilinearFilteringOptions(),
                        selectedValue = draft.textureFiltering,
                        onSelected = { onDraftChange(draft.copy(textureFiltering = it)) },
                        helpText = stringResource(R.string.settings_help_bilinear_filtering),
                        onResetToDefault = { onDraftChange(draft.copy(textureFiltering = defaultProfile.textureFiltering)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_trilinear_filtering),
                        options = trilinearFilteringOptions(),
                        selectedValue = draft.trilinearFiltering,
                        onSelected = { onDraftChange(draft.copy(trilinearFiltering = it)) },
                        helpText = stringResource(R.string.settings_help_trilinear_filtering),
                        onResetToDefault = { onDraftChange(draft.copy(trilinearFiltering = defaultProfile.trilinearFiltering)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_hw_download_mode),
                        options = hwDownloadModeOptions(),
                        selectedValue = draft.hwDownloadMode,
                        onSelected = { onDraftChange(draft.copy(hwDownloadMode = it)) },
                        helpText = stringResource(R.string.settings_help_hw_download_mode),
                        onResetToDefault = { onDraftChange(draft.copy(hwDownloadMode = defaultProfile.hwDownloadMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_blending_accuracy),
                        options = blendingAccuracyOptions(),
                        selectedValue = draft.blendingAccuracy,
                        onSelected = { onDraftChange(draft.copy(blendingAccuracy = it)) },
                        helpText = stringResource(R.string.settings_help_blending_accuracy),
                        onResetToDefault = { onDraftChange(draft.copy(blendingAccuracy = defaultProfile.blendingAccuracy)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_texture_preloading),
                        options = texturePreloadingOptions(),
                        selectedValue = draft.texturePreloading,
                        onSelected = { onDraftChange(draft.copy(texturePreloading = it)) },
                        helpText = stringResource(R.string.settings_help_texture_preloading),
                        onResetToDefault = { onDraftChange(draft.copy(texturePreloading = defaultProfile.texturePreloading)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_anisotropic_filtering),
                        options = anisotropicFilteringOptions(),
                        selectedValue = draft.anisotropicFiltering,
                        onSelected = { onDraftChange(draft.copy(anisotropicFiltering = it)) },
                        helpText = stringResource(R.string.settings_help_anisotropic_filtering),
                        onResetToDefault = { onDraftChange(draft.copy(anisotropicFiltering = defaultProfile.anisotropicFiltering)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_fxaa),
                        checked = draft.enableFxaa,
                        onCheckedChange = { onDraftChange(draft.copy(enableFxaa = it)) },
                        helpText = stringResource(R.string.settings_help_fxaa),
                        onResetToDefault = { onDraftChange(draft.copy(enableFxaa = defaultProfile.enableFxaa)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_sgsr),
                        options = sgsrModeOptions(),
                        selectedValue = draft.sgsrMode,
                        onSelected = { onDraftChange(draft.copy(sgsrMode = it)) },
                        helpText = stringResource(R.string.settings_help_sgsr),
                        onResetToDefault = { onDraftChange(draft.copy(sgsrMode = defaultProfile.sgsrMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_cas),
                        options = casModeOptions(),
                        selectedValue = draft.casMode,
                        onSelected = { onDraftChange(draft.copy(casMode = it)) },
                        helpText = stringResource(R.string.settings_help_cas),
                        onResetToDefault = { onDraftChange(draft.copy(casMode = defaultProfile.casMode)) }
                    )
                    if (draft.casMode != 0) {
                        SliderRow(
                            title = stringResource(R.string.settings_cas_sharpness),
                            value = draft.casSharpness.toFloat(),
                            valueLabel = stringResource(R.string.settings_cas_sharpness_value, draft.casSharpness),
                            range = 0f..100f,
                            steps = 99,
                            onValueChange = { onDraftChange(draft.copy(casSharpness = it.toInt())) },
                            helpText = stringResource(R.string.settings_help_cas_sharpness),
                            onResetToDefault = { onDraftChange(draft.copy(casSharpness = defaultProfile.casSharpness)) }
                        )
                    }
                    SelectionRow(
                        title = stringResource(R.string.settings_tv_shader),
                        options = tvShaderOptions(),
                        selectedValue = draft.tvShader,
                        onSelected = { onDraftChange(draft.copy(tvShader = it)) },
                        helpText = stringResource(R.string.settings_help_tv_shader),
                        onResetToDefault = { onDraftChange(draft.copy(tvShader = defaultProfile.tvShader)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_hw_mipmapping),
                        checked = draft.enableHwMipmapping,
                        onCheckedChange = { onDraftChange(draft.copy(enableHwMipmapping = it)) },
                        helpText = stringResource(R.string.settings_help_hw_mipmapping),
                        onResetToDefault = { onDraftChange(draft.copy(enableHwMipmapping = defaultProfile.enableHwMipmapping)) }
                    )
                }
                EditorSection(title = stringResource(R.string.game_settings_manager_section_screen)) {
                    ShadeBoostRows(
                        draft = draft,
                        defaultProfile = defaultProfile,
                        onDraftChange = onDraftChange
                    )
                }
            }
            GameSettingsManagerTab.System -> {
                EditorSection(title = stringResource(R.string.game_settings_manager_section_runtime)) {
                    ToggleRow(
                        title = stringResource(R.string.settings_show_fps),
                        checked = draft.showFps,
                        onCheckedChange = { onDraftChange(draft.copy(showFps = it)) },
                        helpText = stringResource(R.string.settings_help_show_fps),
                        onResetToDefault = { onDraftChange(draft.copy(showFps = defaultProfile.showFps)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_fast_boot),
                        checked = draft.enableFastBoot,
                        onCheckedChange = { onDraftChange(draft.copy(enableFastBoot = it)) },
                        helpText = stringResource(R.string.settings_help_fast_boot),
                        onResetToDefault = { onDraftChange(draft.copy(enableFastBoot = defaultProfile.enableFastBoot)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.emulation_auto_save_on_exit),
                        checked = draft.autoSaveOnExit,
                        onCheckedChange = { onDraftChange(draft.copy(autoSaveOnExit = it)) },
                        helpText = stringResource(R.string.emulation_auto_save_on_exit_desc),
                        onResetToDefault = { onDraftChange(draft.copy(autoSaveOnExit = defaultProfile.autoSaveOnExit)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.emulation_auto_load_on_start),
                        checked = draft.autoLoadOnStart,
                        onCheckedChange = { onDraftChange(draft.copy(autoLoadOnStart = it)) },
                        helpText = stringResource(R.string.emulation_auto_load_on_start_desc),
                        onResetToDefault = { onDraftChange(draft.copy(autoLoadOnStart = defaultProfile.autoLoadOnStart)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_fps_overlay_mode),
                        options = listOf(
                            AppPreferences.FPS_OVERLAY_MODE_SIMPLE to stringResource(R.string.settings_fps_overlay_mode_simple),
                            AppPreferences.FPS_OVERLAY_MODE_DETAILED to stringResource(R.string.settings_fps_overlay_mode_detailed)
                        ),
                        selectedValue = draft.fpsOverlayMode,
                        onSelected = { onDraftChange(draft.copy(fpsOverlayMode = it)) },
                        helpText = stringResource(R.string.settings_help_fps_overlay_mode),
                        onResetToDefault = { onDraftChange(draft.copy(fpsOverlayMode = defaultProfile.fpsOverlayMode)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_frame_limiter),
                        checked = draft.frameLimitEnabled,
                        onCheckedChange = { onDraftChange(draft.copy(frameLimitEnabled = it)) },
                        helpText = stringResource(R.string.settings_help_frame_limiter),
                        onResetToDefault = { onDraftChange(draft.copy(frameLimitEnabled = defaultProfile.frameLimitEnabled)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_ee_cycle_rate),
                        options = eeCycleRateOptions(),
                        selectedValue = draft.eeCycleRate,
                        onSelected = { onDraftChange(draft.copy(eeCycleRate = it)) },
                        helpText = stringResource(R.string.settings_help_ee_cycle_rate),
                        onResetToDefault = { onDraftChange(draft.copy(eeCycleRate = defaultProfile.eeCycleRate)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_ee_cycle_skip),
                        options = eeCycleSkipOptions(),
                        selectedValue = draft.eeCycleSkip,
                        onSelected = { onDraftChange(draft.copy(eeCycleSkip = it)) },
                        helpText = stringResource(R.string.settings_help_ee_cycle_skip),
                        onResetToDefault = { onDraftChange(draft.copy(eeCycleSkip = defaultProfile.eeCycleSkip)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_ee_fpu_round_mode),
                        options = floatRoundModeOptions(),
                        selectedValue = draft.eeFpuRoundMode,
                        onSelected = { onDraftChange(draft.copy(eeFpuRoundMode = it)) },
                        helpText = stringResource(R.string.settings_help_ee_fpu_round_mode),
                        onResetToDefault = { onDraftChange(draft.copy(eeFpuRoundMode = defaultProfile.eeFpuRoundMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_vu0_round_mode),
                        options = floatRoundModeOptions(),
                        selectedValue = draft.vu0RoundMode,
                        onSelected = { onDraftChange(draft.copy(vu0RoundMode = it)) },
                        helpText = stringResource(R.string.settings_help_vu0_round_mode),
                        onResetToDefault = { onDraftChange(draft.copy(vu0RoundMode = defaultProfile.vu0RoundMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_vu1_round_mode),
                        options = floatRoundModeOptions(),
                        selectedValue = draft.vu1RoundMode,
                        onSelected = { onDraftChange(draft.copy(vu1RoundMode = it)) },
                        helpText = stringResource(R.string.settings_help_vu1_round_mode),
                        onResetToDefault = { onDraftChange(draft.copy(vu1RoundMode = defaultProfile.vu1RoundMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_ee_fpu_clamping),
                        options = eeFpuClampingModeOptions(),
                        selectedValue = draft.eeFpuClampingMode,
                        onSelected = { onDraftChange(draft.copy(eeFpuClampingMode = it)) },
                        helpText = stringResource(R.string.settings_help_ee_fpu_clamping),
                        onResetToDefault = { onDraftChange(draft.copy(eeFpuClampingMode = defaultProfile.eeFpuClampingMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_vu0_clamping),
                        options = vuClampingModeOptions(),
                        selectedValue = draft.vu0ClampingMode,
                        onSelected = { onDraftChange(draft.copy(vu0ClampingMode = it)) },
                        helpText = stringResource(R.string.settings_help_vu0_clamping),
                        onResetToDefault = { onDraftChange(draft.copy(vu0ClampingMode = defaultProfile.vu0ClampingMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_vu1_clamping),
                        options = vuClampingModeOptions(),
                        selectedValue = draft.vu1ClampingMode,
                        onSelected = { onDraftChange(draft.copy(vu1ClampingMode = it)) },
                        helpText = stringResource(R.string.settings_help_vu1_clamping),
                        onResetToDefault = { onDraftChange(draft.copy(vu1ClampingMode = defaultProfile.vu1ClampingMode)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_game_fixes),
                        checked = draft.enableGameFixes,
                        onCheckedChange = { onDraftChange(draft.copy(enableGameFixes = it)) },
                        helpText = stringResource(R.string.settings_help_game_fixes),
                        onResetToDefault = { onDraftChange(draft.copy(enableGameFixes = defaultProfile.enableGameFixes)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_ee_timing_hack),
                        checked = draft.enableEeTimingHack,
                        onCheckedChange = { onDraftChange(draft.copy(enableEeTimingHack = it)) },
                        helpText = stringResource(R.string.settings_help_ee_timing_hack),
                        onResetToDefault = {
                            onDraftChange(draft.copy(enableEeTimingHack = defaultProfile.enableEeTimingHack))
                        }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_target_fps_mode),
                        options = listOf(
                            0 to stringResource(R.string.settings_target_fps_auto),
                            1 to stringResource(R.string.settings_target_fps_manual)
                        ),
                        selectedValue = if (draft.targetFps <= 0) 0 else 1,
                        onSelected = { mode ->
                            onDraftChange(
                                draft.copy(
                                    targetFps = if (mode == 0) 0 else resolveManualTargetFps(draft.targetFps, defaultProfile.targetFps)
                                )
                            )
                        },
                        helpText = stringResource(R.string.settings_help_target_fps),
                        onResetToDefault = { onDraftChange(draft.copy(targetFps = defaultProfile.targetFps)) }
                    )
                    if (draft.targetFps > 0) {
                        SliderRow(
                            title = stringResource(R.string.settings_target_fps),
                            value = draft.targetFps.toFloat(),
                            valueLabel = draft.targetFps.toString(),
                            range = 20f..120f,
                            steps = 99,
                            onValueChange = { onDraftChange(draft.copy(targetFps = it.toInt())) },
                            helpText = stringResource(R.string.settings_help_target_fps),
                            onResetToDefault = { onDraftChange(draft.copy(targetFps = defaultProfile.targetFps)) }
                        )
                    }
                    SliderRow(
                        title = stringResource(R.string.settings_ntsc_framerate),
                        value = draft.ntscFramerate,
                        valueLabel = formatFramerateHz(draft.ntscFramerate),
                        range = 20f..120f,
                        steps = 199,
                        onValueChange = { onDraftChange(draft.copy(ntscFramerate = it)) },
                        valueLabelForValue = { formatFramerateHz(it) },
                        helpText = stringResource(R.string.settings_help_ntsc_framerate),
                        onResetToDefault = { onDraftChange(draft.copy(ntscFramerate = defaultProfile.ntscFramerate)) }
                    )
                    SliderRow(
                        title = stringResource(R.string.settings_pal_framerate),
                        value = draft.palFramerate,
                        valueLabel = formatFramerateHz(draft.palFramerate),
                        range = 20f..120f,
                        steps = 199,
                        onValueChange = { onDraftChange(draft.copy(palFramerate = it)) },
                        valueLabelForValue = { formatFramerateHz(it) },
                        helpText = stringResource(R.string.settings_help_pal_framerate),
                        onResetToDefault = { onDraftChange(draft.copy(palFramerate = defaultProfile.palFramerate)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_instant_vu1),
                        checked = draft.enableInstantVu1,
                        onCheckedChange = { onDraftChange(draft.copy(enableInstantVu1 = it)) },
                        helpText = stringResource(R.string.settings_help_instant_vu1),
                        onResetToDefault = { onDraftChange(draft.copy(enableInstantVu1 = defaultProfile.enableInstantVu1)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_mtvu),
                        checked = draft.enableMtvu,
                        onCheckedChange = { onDraftChange(draft.copy(enableMtvu = it)) },
                        helpText = stringResource(R.string.settings_help_mtvu),
                        onResetToDefault = { onDraftChange(draft.copy(enableMtvu = defaultProfile.enableMtvu)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_fast_cdvd),
                        checked = draft.enableFastCdvd,
                        onCheckedChange = { onDraftChange(draft.copy(enableFastCdvd = it)) },
                        helpText = stringResource(R.string.settings_help_fast_cdvd),
                        onResetToDefault = { onDraftChange(draft.copy(enableFastCdvd = defaultProfile.enableFastCdvd)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_skip_duplicate_frames),
                        checked = draft.skipDuplicateFrames,
                        onCheckedChange = { onDraftChange(draft.copy(skipDuplicateFrames = it)) },
                        helpText = stringResource(R.string.settings_help_skip_duplicate_frames),
                        onResetToDefault = { onDraftChange(draft.copy(skipDuplicateFrames = defaultProfile.skipDuplicateFrames)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_frame_skip),
                        options = listOf(
                            0 to stringResource(R.string.settings_frame_skip_off),
                            1 to "1",
                            2 to "2",
                            3 to "3",
                            4 to "4"
                        ),
                        selectedValue = draft.frameSkip,
                        onSelected = { onDraftChange(draft.copy(frameSkip = it)) },
                        helpText = stringResource(R.string.settings_help_frame_skip),
                        onResetToDefault = { onDraftChange(draft.copy(frameSkip = defaultProfile.frameSkip)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_enable_cheats),
                        checked = draft.enableCheats,
                        onCheckedChange = { onDraftChange(draft.copy(enableCheats = it)) },
                        helpText = stringResource(R.string.settings_help_cheats),
                        onResetToDefault = { onDraftChange(draft.copy(enableCheats = defaultProfile.enableCheats)) }
                    )
                }
            }
            GameSettingsManagerTab.Controls -> {
                EditorSection(title = stringResource(R.string.settings_customization_touch_controls_section)) {
                    SelectionRow(
                        title = stringResource(R.string.settings_customization_touch_controls_style),
                        options = listOf(
                            -1 to stringResource(R.string.settings_use_global),
                            TouchControlVisualStyle.CLASSIC.preferenceValue to stringResource(R.string.settings_customization_touch_style_classic),
                            TouchControlVisualStyle.LEGACY.preferenceValue to stringResource(R.string.settings_customization_touch_style_glass),
                            TouchControlVisualStyle.MODERN.preferenceValue to stringResource(R.string.settings_customization_touch_style_neon),
                            TouchControlVisualStyle.ARCADE.preferenceValue to stringResource(R.string.settings_customization_touch_style_arcade),
                            TouchControlVisualStyle.MINIMAL.preferenceValue to stringResource(R.string.settings_customization_touch_style_minimal)
                        ),
                        selectedValue = draft.touchControlVisualStyle?.preferenceValue ?: -1,
                        onSelected = { value ->
                            onDraftChange(
                                draft.copy(
                                    touchControlVisualStyle = value.takeIf { it >= 0 }
                                        ?.let { TouchControlVisualStyle.fromPreference(it) }
                                )
                            )
                        },
                        helpText = stringResource(R.string.settings_customization_touch_controls_help),
                        onResetToDefault = { onDraftChange(draft.copy(touchControlVisualStyle = null)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_customization_touch_press_effect),
                        options = listOf(
                            -1 to stringResource(R.string.settings_use_global),
                            TouchControlPressEffect.GROW.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_grow),
                            TouchControlPressEffect.SHRINK.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_shrink),
                            TouchControlPressEffect.SPRING.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_spring),
                            TouchControlPressEffect.GLOW.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_glow)
                        ),
                        selectedValue = draft.touchControlPressEffect?.preferenceValue ?: -1,
                        onSelected = { value ->
                            onDraftChange(
                                draft.copy(
                                    touchControlPressEffect = value.takeIf { it >= 0 }
                                        ?.let { TouchControlPressEffect.fromPreference(it) }
                                )
                            )
                        },
                        helpText = stringResource(R.string.settings_customization_touch_press_effect_help),
                        onResetToDefault = { onDraftChange(draft.copy(touchControlPressEffect = null)) }
                    )
                }
                EditorSection(title = stringResource(R.string.settings_controls_tab)) {
                    ToggleRow(
                        title = stringResource(R.string.settings_racing_mode),
                        checked = draft.racingMode,
                        onCheckedChange = { onDraftChange(draft.copy(racingMode = it)) },
                        helpText = stringResource(R.string.settings_help_racing_mode),
                        onResetToDefault = { onDraftChange(draft.copy(racingMode = defaultProfile.racingMode)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_touch_haptics),
                        checked = draft.touchHaptics,
                        onCheckedChange = { onDraftChange(draft.copy(touchHaptics = it)) },
                        helpText = stringResource(R.string.settings_help_touch_haptics),
                        onResetToDefault = { onDraftChange(draft.copy(touchHaptics = defaultProfile.touchHaptics)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_touch_haptics_preset),
                        options = touchHapticsPresetOptions(),
                        selectedValue = draft.touchHapticsPreset,
                        onSelected = { onDraftChange(draft.copy(touchHapticsPreset = it)) },
                        helpText = stringResource(R.string.settings_help_touch_haptics_preset),
                        onResetToDefault = { onDraftChange(draft.copy(touchHapticsPreset = defaultProfile.touchHapticsPreset)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_gyro_mode),
                        options = gyroModeOptions(),
                        selectedValue = draft.gyroMode,
                        onSelected = { onDraftChange(draft.copy(gyroMode = it)) },
                        helpText = stringResource(R.string.settings_help_gyro_mode),
                        onResetToDefault = { onDraftChange(draft.copy(gyroMode = defaultProfile.gyroMode)) }
                    )
                    if (draft.gyroMode != AppPreferences.GYRO_MODE_OFF) {
                        SliderRow(stringResource(R.string.settings_gyro_sensitivity), draft.gyroSensitivity.toFloat(), "${draft.gyroSensitivity}%", 25f..300f, 10, { onDraftChange(draft.copy(gyroSensitivity = it.roundToInt())) }, helpText = stringResource(R.string.settings_help_gyro_sensitivity), onResetToDefault = { onDraftChange(draft.copy(gyroSensitivity = defaultProfile.gyroSensitivity)) })
                        SliderRow(stringResource(R.string.settings_gyro_smoothing), draft.gyroSmoothing.toFloat(), "${draft.gyroSmoothing}%", 0f..90f, 8, { onDraftChange(draft.copy(gyroSmoothing = it.roundToInt())) }, helpText = stringResource(R.string.settings_help_gyro_smoothing), onResetToDefault = { onDraftChange(draft.copy(gyroSmoothing = defaultProfile.gyroSmoothing)) })
                        ToggleRow(stringResource(R.string.settings_gyro_invert_x), draft.gyroInvertX, { onDraftChange(draft.copy(gyroInvertX = it)) }, onResetToDefault = { onDraftChange(draft.copy(gyroInvertX = defaultProfile.gyroInvertX)) })
                        if (draft.gyroMode == AppPreferences.GYRO_MODE_AIM) {
                            ToggleRow(stringResource(R.string.settings_gyro_invert_y), draft.gyroInvertY, { onDraftChange(draft.copy(gyroInvertY = it)) }, onResetToDefault = { onDraftChange(draft.copy(gyroInvertY = defaultProfile.gyroInvertY)) })
                        }
                    }
                    ToggleRow(
                        title = stringResource(R.string.settings_gamepad_right_stick_up_to_r2),
                        checked = draft.gamepadRightStickUpToR2,
                        onCheckedChange = { onDraftChange(draft.copy(gamepadRightStickUpToR2 = it)) },
                        helpText = stringResource(R.string.settings_help_gamepad_right_stick_up_to_r2),
                        onResetToDefault = { onDraftChange(draft.copy(gamepadRightStickUpToR2 = defaultProfile.gamepadRightStickUpToR2)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_gamepad_right_stick_down_to_l2),
                        checked = draft.gamepadRightStickDownToL2,
                        onCheckedChange = { onDraftChange(draft.copy(gamepadRightStickDownToL2 = it)) },
                        helpText = stringResource(R.string.settings_help_gamepad_right_stick_down_to_l2),
                        onResetToDefault = { onDraftChange(draft.copy(gamepadRightStickDownToL2 = defaultProfile.gamepadRightStickDownToL2)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_gamepad_button_haptics),
                        checked = draft.gamepadButtonHaptics,
                        onCheckedChange = { onDraftChange(draft.copy(gamepadButtonHaptics = it)) },
                        helpText = stringResource(R.string.settings_help_gamepad_button_haptics),
                        onResetToDefault = { onDraftChange(draft.copy(gamepadButtonHaptics = defaultProfile.gamepadButtonHaptics)) }
                    )
                    SliderRow(
                        title = stringResource(R.string.settings_pressure_modifier_amount),
                        value = draft.pressureModifierAmount.toFloat(),
                        valueLabel = "${draft.pressureModifierAmount}%",
                        range = 1f..100f,
                        steps = 98,
                        onValueChange = { onDraftChange(draft.copy(pressureModifierAmount = it.roundToInt())) },
                        helpText = stringResource(R.string.settings_help_pressure_modifier_amount),
                        onResetToDefault = { onDraftChange(draft.copy(pressureModifierAmount = defaultProfile.pressureModifierAmount)) }
                    )
                }
            }
            GameSettingsManagerTab.Fixes -> {
                EditorSection(title = stringResource(R.string.settings_image_processing)) {
                    SelectionRow(
                        title = stringResource(R.string.settings_deinterlacing),
                        options = deinterlacingOptions(),
                        selectedValue = draft.deinterlaceMode,
                        onSelected = { onDraftChange(draft.copy(deinterlaceMode = it)) },
                        helpText = stringResource(R.string.settings_help_deinterlacing),
                        onResetToDefault = { onDraftChange(draft.copy(deinterlaceMode = defaultProfile.deinterlaceMode)) }
                    )
                    SelectionRow(
                        title = stringResource(R.string.settings_dithering),
                        options = ditheringOptions(),
                        selectedValue = draft.dithering,
                        onSelected = { onDraftChange(draft.copy(dithering = it)) },
                        helpText = stringResource(R.string.settings_help_dithering),
                        onResetToDefault = { onDraftChange(draft.copy(dithering = defaultProfile.dithering)) }
                    )
                }
                EditorSection(title = stringResource(R.string.settings_patches_section)) {
                    ToggleRow(
                        title = stringResource(R.string.settings_widescreen_patches),
                        checked = draft.enableWidescreenPatches,
                        onCheckedChange = { onDraftChange(draft.copy(enableWidescreenPatches = it)) },
                        helpText = stringResource(R.string.settings_help_widescreen_patches),
                        onResetToDefault = { onDraftChange(draft.copy(enableWidescreenPatches = defaultProfile.enableWidescreenPatches)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_no_interlacing_patches),
                        checked = draft.enableNoInterlacingPatches,
                        onCheckedChange = { onDraftChange(draft.copy(enableNoInterlacingPatches = it)) },
                        helpText = stringResource(R.string.settings_help_no_interlacing_patches),
                        onResetToDefault = { onDraftChange(draft.copy(enableNoInterlacingPatches = defaultProfile.enableNoInterlacingPatches)) }
                    )
                    ToggleRow(
                        title = stringResource(R.string.settings_anti_blur),
                        checked = draft.antiBlur,
                        onCheckedChange = { onDraftChange(draft.copy(antiBlur = it)) },
                        helpText = stringResource(R.string.settings_help_anti_blur),
                        onResetToDefault = { onDraftChange(draft.copy(antiBlur = defaultProfile.antiBlur)) }
                    )
                }
                HardwareFixesRows(
                    draft = draft,
                    defaultProfile = defaultProfile,
                    onDraftChange = onDraftChange
                )
                UpscalingFixesRows(
                    draft = draft,
                    defaultProfile = defaultProfile,
                    onDraftChange = onDraftChange
                )
            }
        }
    }
}

@Composable
@Suppress("unused")
fun PerGameSettingsQuickEditorDialog(
    game: GameItem,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val repository = remember(context) { PerGameSettingsRepository(context) }
    val preferences = remember(context) { AppPreferences(context) }
    val settingsSnapshot by preferences.settingsSnapshot.collectAsState(initial = SettingsSnapshot())
    val initialProfile = remember(game.path, game.title, game.serial, settingsSnapshot) {
        repository.get(game.path) ?: settingsSnapshot.toPerGameSettings(game)
    }

    GameSettingsEditorDialog(
        profile = initialProfile,
        onDismiss = onDismiss,
        onSave = { updated ->
            repository.save(updated)
        }
    )
}

@Composable
@Suppress("unused")
private fun GameSettingsProfileCard(
    profile: PerGameSettings,
    onEdit: () -> Unit,
    onReset: () -> Unit,
    onDelete: () -> Unit
) {
    val context = LocalContext.current
    val dateText = remember(profile.updatedAt) {
        DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(profile.updatedAt)
    }
    val coverPath = remember(profile.gameKey, profile.gameSerial, profile.gameTitle) {
        GameRepository().findCoverForGame(
            path = profile.gameKey,
            context = context,
            serial = profile.gameSerial,
            title = profile.gameTitle
        )
    }
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
                .padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Surface(
                    modifier = Modifier
                        .width(66.dp)
                        .height(90.dp),
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.26f),
                    border = BorderStroke(
                        1.dp,
                        MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                    )
                ) {
                    GameCoverArt(
                        coverPath = coverPath,
                        fallbackTitle = profile.gameTitle,
                        modifier = Modifier
                            .fillMaxSize()
                            .clip(RoundedCornerShape(16.dp)),
                        contentScale = ContentScale.Crop
                    )
                }

                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(
                            text = profile.gameTitle,
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        val subtitle = buildList {
                            profile.gameSerial?.takeIf { it.isNotBlank() }?.let(::add)
                            add(dateText)
                        }.joinToString("  /  ")
                        Text(
                            text = subtitle,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        ProfileBadge(text = stringResource(rendererLabel(profile.renderer)))
                        ProfileBadge(
                            text = formatUpscaleLabel(
                                value = profile.upscaleMultiplier,
                                nativeLabel = stringResource(R.string.settings_upscale_native)
                            )
                        )
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                ProfileActionIconButton(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Rounded.Tune,
                    title = stringResource(R.string.edit),
                    onClick = onEdit
                )
                ProfileActionIconButton(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Rounded.Restore,
                    title = stringResource(R.string.game_settings_manager_reset_game_title),
                    onClick = onReset
                )
                ProfileActionIconButton(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Rounded.DeleteOutline,
                    title = stringResource(R.string.delete),
                    onClick = onDelete,
                    destructive = true
                )
            }
        }
    }
}

@Composable
private fun GameSettingsEditorDialog(
    profile: PerGameSettings,
    onDismiss: () -> Unit,
    onSave: (PerGameSettings) -> Unit
) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val settingsSnapshot by preferences.settingsSnapshot.collectAsState(initial = SettingsSnapshot())
    val nativeUpscaleLabel = stringResource(R.string.settings_upscale_native)
    val defaultProfile = remember(settingsSnapshot, profile.gameKey, profile.gameTitle, profile.gameSerial) {
        settingsSnapshot.toPerGameSettings(
            GameItem(
                path = profile.gameKey,
                title = profile.gameTitle,
                fileName = profile.gameTitle,
                fileSize = 0L,
                lastModified = profile.updatedAt,
                serial = profile.gameSerial
            )
        )
    }
    val editableProfile = remember(profile, defaultProfile) {
        profile.resolveAgainst(defaultProfile)
    }
    var draft by remember(editableProfile) { mutableStateOf(editableProfile) }
    var hasUserChange by remember(editableProfile) { mutableStateOf(false) }
    val containerSize = LocalWindowInfo.current.containerSize
    val density = LocalDensity.current
    val windowWidth = with(density) { containerSize.width.toDp() }
    val windowHeight = with(density) { containerSize.height.toDp() }
    val isLandscape = windowWidth > windowHeight
    val maxDialogHeight = if (isLandscape) {
        (windowHeight - 48.dp).coerceAtLeast(300.dp)
    } else {
        (windowHeight - 72.dp).coerceAtLeast(440.dp)
    }
    val dialogWidthFraction = if (isLandscape) 0.98f else 0.94f
    val dialogMaxWidth = if (isLandscape) 1600.dp else 720.dp
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        LaunchedEffect(draft) {
            if (hasUserChange) {
                onSave(draft.copy(providedKeys = null))
            } else {
                hasUserChange = true
            }
        }
        val maxUpscaleMultiplier = remember(draft.renderer) {
            EmulatorBridge.getMaxUpscaleMultiplier(normalizeManagerRenderer(draft.renderer))
        }
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(
                    start = if (isLandscape) 10.dp else 14.dp,
                    top = if (isLandscape) 10.dp else 14.dp,
                    end = if (isLandscape) 10.dp else 14.dp,
                    bottom = if (isLandscape) 4.dp else 8.dp
                )
                .imePadding(),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth(dialogWidthFraction)
                    .widthIn(max = dialogMaxWidth),
                shape = RoundedCornerShape(30.dp),
                color = MaterialTheme.colorScheme.surface
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = maxDialogHeight)
                        .verticalScroll(rememberScrollState())
                        .padding(start = 24.dp, top = 22.dp, end = 24.dp, bottom = 30.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    GameSettingsEditorHeader(
                        profile = draft
                    )
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.game_settings_manager_editor_desc),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        EditorSection(title = stringResource(R.string.game_settings_manager_section_profile)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_renderer),
                                options = listOf(
                                    12 to stringResource(R.string.settings_renderer_opengl),
                                    14 to stringResource(R.string.settings_renderer_vulkan),
                                    13 to stringResource(R.string.settings_renderer_software)
                                ),
                                selectedValue = normalizeManagerRenderer(draft.renderer),
                                onSelected = { draft = draft.copy(renderer = it) },
                                helpText = stringResource(R.string.settings_help_renderer),
                                onResetToDefault = { draft = draft.copy(renderer = normalizeManagerRenderer(defaultProfile.renderer)) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_upscale),
                                options = buildUpscaleOptions(nativeUpscaleLabel, maxUpscaleMultiplier),
                                selectedValue = upscaleMultiplierValue(draft.upscaleMultiplier),
                                onSelected = { draft = draft.copy(upscaleMultiplier = upscaleKeyToMultiplier(it)) },
                                helpText = stringResource(R.string.settings_help_upscale),
                                onResetToDefault = { draft = draft.copy(upscaleMultiplier = defaultProfile.upscaleMultiplier) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_aspect_ratio),
                                options = listOf(
                                    1 to stringResource(R.string.settings_aspect_ratio_auto),
                                    2 to stringResource(R.string.settings_aspect_ratio_43),
                                    3 to stringResource(R.string.settings_aspect_ratio_169),
                                    4 to stringResource(R.string.settings_aspect_ratio_107),
                                    0 to stringResource(R.string.emulation_aspect_stretch)
                                ),
                                selectedValue = draft.aspectRatio,
                                onSelected = { draft = draft.copy(aspectRatio = it) },
                                helpText = stringResource(R.string.settings_help_aspect_ratio),
                                onResetToDefault = { draft = draft.copy(aspectRatio = defaultProfile.aspectRatio) }
                            )
                        }
                        EditorSection(title = stringResource(R.string.settings_customization_touch_controls_section)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_customization_touch_controls_style),
                                options = listOf(
                                    -1 to stringResource(R.string.settings_use_global),
                                    TouchControlVisualStyle.CLASSIC.preferenceValue to stringResource(R.string.settings_customization_touch_style_classic),
                                    TouchControlVisualStyle.LEGACY.preferenceValue to stringResource(R.string.settings_customization_touch_style_glass),
                                    TouchControlVisualStyle.MODERN.preferenceValue to stringResource(R.string.settings_customization_touch_style_neon),
                                    TouchControlVisualStyle.ARCADE.preferenceValue to stringResource(R.string.settings_customization_touch_style_arcade),
                                    TouchControlVisualStyle.MINIMAL.preferenceValue to stringResource(R.string.settings_customization_touch_style_minimal)
                                ),
                                selectedValue = draft.touchControlVisualStyle?.preferenceValue ?: -1,
                                onSelected = { value ->
                                    draft = draft.copy(
                                        touchControlVisualStyle = value.takeIf { it >= 0 }
                                            ?.let { TouchControlVisualStyle.fromPreference(it) }
                                    )
                                },
                                helpText = stringResource(R.string.settings_customization_touch_controls_help),
                                onResetToDefault = { draft = draft.copy(touchControlVisualStyle = null) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_customization_touch_press_effect),
                                options = listOf(
                                    -1 to stringResource(R.string.settings_use_global),
                                    TouchControlPressEffect.GROW.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_grow),
                                    TouchControlPressEffect.SHRINK.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_shrink),
                                    TouchControlPressEffect.SPRING.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_spring),
                                    TouchControlPressEffect.GLOW.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_glow)
                                ),
                                selectedValue = draft.touchControlPressEffect?.preferenceValue ?: -1,
                                onSelected = { value ->
                                    draft = draft.copy(
                                        touchControlPressEffect = value.takeIf { it >= 0 }
                                            ?.let { TouchControlPressEffect.fromPreference(it) }
                                    )
                                },
                                helpText = stringResource(R.string.settings_customization_touch_press_effect_help),
                                onResetToDefault = { draft = draft.copy(touchControlPressEffect = null) }
                            )
                        }
                        EditorSection(title = stringResource(R.string.game_settings_manager_section_runtime)) {
                            ToggleRow(
                                title = stringResource(R.string.settings_show_fps),
                                checked = draft.showFps,
                                onCheckedChange = { draft = draft.copy(showFps = it) },
                                helpText = stringResource(R.string.settings_help_show_fps),
                                onResetToDefault = { draft = draft.copy(showFps = defaultProfile.showFps) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_fast_boot),
                                checked = draft.enableFastBoot,
                                onCheckedChange = { draft = draft.copy(enableFastBoot = it) },
                                helpText = stringResource(R.string.settings_help_fast_boot),
                                onResetToDefault = { draft = draft.copy(enableFastBoot = defaultProfile.enableFastBoot) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_racing_mode),
                                checked = draft.racingMode,
                                onCheckedChange = { draft = draft.copy(racingMode = it) },
                                helpText = stringResource(R.string.settings_help_racing_mode),
                                onResetToDefault = { draft = draft.copy(racingMode = defaultProfile.racingMode) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_touch_haptics),
                                checked = draft.touchHaptics,
                                onCheckedChange = { draft = draft.copy(touchHaptics = it) },
                                helpText = stringResource(R.string.settings_help_touch_haptics),
                                onResetToDefault = { draft = draft.copy(touchHaptics = defaultProfile.touchHaptics) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_touch_haptics_preset),
                                options = touchHapticsPresetOptions(),
                                selectedValue = draft.touchHapticsPreset,
                                onSelected = { draft = draft.copy(touchHapticsPreset = it) },
                                helpText = stringResource(R.string.settings_help_touch_haptics_preset),
                                onResetToDefault = { draft = draft.copy(touchHapticsPreset = defaultProfile.touchHapticsPreset) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_gyro_mode),
                                options = gyroModeOptions(),
                                selectedValue = draft.gyroMode,
                                onSelected = { draft = draft.copy(gyroMode = it) },
                                helpText = stringResource(R.string.settings_help_gyro_mode),
                                onResetToDefault = { draft = draft.copy(gyroMode = defaultProfile.gyroMode) }
                            )
                            if (draft.gyroMode != AppPreferences.GYRO_MODE_OFF) {
                                SliderRow(stringResource(R.string.settings_gyro_sensitivity), draft.gyroSensitivity.toFloat(), "${draft.gyroSensitivity}%", 25f..300f, 10, { draft = draft.copy(gyroSensitivity = it.roundToInt()) }, helpText = stringResource(R.string.settings_help_gyro_sensitivity), onResetToDefault = { draft = draft.copy(gyroSensitivity = defaultProfile.gyroSensitivity) })
                                SliderRow(stringResource(R.string.settings_gyro_smoothing), draft.gyroSmoothing.toFloat(), "${draft.gyroSmoothing}%", 0f..90f, 8, { draft = draft.copy(gyroSmoothing = it.roundToInt()) }, helpText = stringResource(R.string.settings_help_gyro_smoothing), onResetToDefault = { draft = draft.copy(gyroSmoothing = defaultProfile.gyroSmoothing) })
                                ToggleRow(stringResource(R.string.settings_gyro_invert_x), draft.gyroInvertX, { draft = draft.copy(gyroInvertX = it) }, onResetToDefault = { draft = draft.copy(gyroInvertX = defaultProfile.gyroInvertX) })
                                if (draft.gyroMode == AppPreferences.GYRO_MODE_AIM) {
                                    ToggleRow(stringResource(R.string.settings_gyro_invert_y), draft.gyroInvertY, { draft = draft.copy(gyroInvertY = it) }, onResetToDefault = { draft = draft.copy(gyroInvertY = defaultProfile.gyroInvertY) })
                                }
                            }
                            ToggleRow(
                                title = stringResource(R.string.settings_gamepad_right_stick_up_to_r2),
                                checked = draft.gamepadRightStickUpToR2,
                                onCheckedChange = { draft = draft.copy(gamepadRightStickUpToR2 = it) },
                                helpText = stringResource(R.string.settings_help_gamepad_right_stick_up_to_r2),
                                onResetToDefault = { draft = draft.copy(gamepadRightStickUpToR2 = defaultProfile.gamepadRightStickUpToR2) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_gamepad_right_stick_down_to_l2),
                                checked = draft.gamepadRightStickDownToL2,
                                onCheckedChange = { draft = draft.copy(gamepadRightStickDownToL2 = it) },
                                helpText = stringResource(R.string.settings_help_gamepad_right_stick_down_to_l2),
                                onResetToDefault = { draft = draft.copy(gamepadRightStickDownToL2 = defaultProfile.gamepadRightStickDownToL2) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_gamepad_button_haptics),
                                checked = draft.gamepadButtonHaptics,
                                onCheckedChange = { draft = draft.copy(gamepadButtonHaptics = it) },
                                helpText = stringResource(R.string.settings_help_gamepad_button_haptics),
                                onResetToDefault = { draft = draft.copy(gamepadButtonHaptics = defaultProfile.gamepadButtonHaptics) }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_pressure_modifier_amount),
                                value = draft.pressureModifierAmount.toFloat(),
                                valueLabel = "${draft.pressureModifierAmount}%",
                                range = 1f..100f,
                                steps = 98,
                                onValueChange = { draft = draft.copy(pressureModifierAmount = it.roundToInt()) },
                                helpText = stringResource(R.string.settings_help_pressure_modifier_amount),
                                onResetToDefault = { draft = draft.copy(pressureModifierAmount = defaultProfile.pressureModifierAmount) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_fps_overlay_mode),
                                options = listOf(
                                    AppPreferences.FPS_OVERLAY_MODE_SIMPLE to stringResource(R.string.settings_fps_overlay_mode_simple),
                                    AppPreferences.FPS_OVERLAY_MODE_DETAILED to stringResource(R.string.settings_fps_overlay_mode_detailed)
                                ),
                                selectedValue = draft.fpsOverlayMode,
                                onSelected = { draft = draft.copy(fpsOverlayMode = it) },
                                helpText = stringResource(R.string.settings_help_fps_overlay_mode),
                                onResetToDefault = { draft = draft.copy(fpsOverlayMode = defaultProfile.fpsOverlayMode) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_frame_limiter),
                                checked = draft.frameLimitEnabled,
                                onCheckedChange = { draft = draft.copy(frameLimitEnabled = it) },
                                helpText = stringResource(R.string.settings_help_frame_limiter),
                                onResetToDefault = { draft = draft.copy(frameLimitEnabled = defaultProfile.frameLimitEnabled) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_ee_cycle_rate),
                                options = eeCycleRateOptions(),
                                selectedValue = draft.eeCycleRate,
                                onSelected = { draft = draft.copy(eeCycleRate = it) },
                                helpText = stringResource(R.string.settings_help_ee_cycle_rate),
                                onResetToDefault = { draft = draft.copy(eeCycleRate = defaultProfile.eeCycleRate) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_ee_cycle_skip),
                                options = eeCycleSkipOptions(),
                                selectedValue = draft.eeCycleSkip,
                                onSelected = { draft = draft.copy(eeCycleSkip = it) },
                                helpText = stringResource(R.string.settings_help_ee_cycle_skip),
                                onResetToDefault = { draft = draft.copy(eeCycleSkip = defaultProfile.eeCycleSkip) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_target_fps_mode),
                                options = listOf(
                                    0 to stringResource(R.string.settings_target_fps_auto),
                                    1 to stringResource(R.string.settings_target_fps_manual)
                                ),
                                selectedValue = if (draft.targetFps <= 0) 0 else 1,
                                onSelected = { mode ->
                                    draft = draft.copy(
                                        targetFps = if (mode == 0) 0 else resolveManualTargetFps(draft.targetFps, defaultProfile.targetFps)
                                    )
                                },
                                helpText = stringResource(R.string.settings_help_target_fps),
                                onResetToDefault = { draft = draft.copy(targetFps = defaultProfile.targetFps) }
                            )
                            if (draft.targetFps > 0) {
                                SliderRow(
                                    title = stringResource(R.string.settings_target_fps),
                                    value = draft.targetFps.toFloat(),
                                    valueLabel = draft.targetFps.toString(),
                                    range = 20f..120f,
                                    steps = 99,
                                    onValueChange = { draft = draft.copy(targetFps = it.toInt()) },
                                    helpText = stringResource(R.string.settings_help_target_fps),
                                    onResetToDefault = { draft = draft.copy(targetFps = defaultProfile.targetFps) }
                                )
                            }
                            SliderRow(
                                title = stringResource(R.string.settings_ntsc_framerate),
                                value = draft.ntscFramerate,
                                valueLabel = formatFramerateHz(draft.ntscFramerate),
                                range = 20f..120f,
                                steps = 199,
                                onValueChange = { draft = draft.copy(ntscFramerate = it) },
                                valueLabelForValue = { formatFramerateHz(it) },
                                helpText = stringResource(R.string.settings_help_ntsc_framerate),
                                onResetToDefault = { draft = draft.copy(ntscFramerate = defaultProfile.ntscFramerate) }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_pal_framerate),
                                value = draft.palFramerate,
                                valueLabel = formatFramerateHz(draft.palFramerate),
                                range = 20f..120f,
                                steps = 199,
                                onValueChange = { draft = draft.copy(palFramerate = it) },
                                valueLabelForValue = { formatFramerateHz(it) },
                                helpText = stringResource(R.string.settings_help_pal_framerate),
                                onResetToDefault = { draft = draft.copy(palFramerate = defaultProfile.palFramerate) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_instant_vu1),
                                checked = draft.enableInstantVu1,
                                onCheckedChange = { draft = draft.copy(enableInstantVu1 = it) },
                                helpText = stringResource(R.string.settings_help_instant_vu1),
                                onResetToDefault = { draft = draft.copy(enableInstantVu1 = defaultProfile.enableInstantVu1) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_mtvu),
                                checked = draft.enableMtvu,
                                onCheckedChange = { draft = draft.copy(enableMtvu = it) },
                                helpText = stringResource(R.string.settings_help_mtvu),
                                onResetToDefault = { draft = draft.copy(enableMtvu = defaultProfile.enableMtvu) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_fast_cdvd),
                                checked = draft.enableFastCdvd,
                                onCheckedChange = { draft = draft.copy(enableFastCdvd = it) },
                                helpText = stringResource(R.string.settings_help_fast_cdvd),
                                onResetToDefault = { draft = draft.copy(enableFastCdvd = defaultProfile.enableFastCdvd) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_skip_duplicate_frames),
                                checked = draft.skipDuplicateFrames,
                                onCheckedChange = { draft = draft.copy(skipDuplicateFrames = it) },
                                helpText = stringResource(R.string.settings_help_skip_duplicate_frames),
                                onResetToDefault = { draft = draft.copy(skipDuplicateFrames = defaultProfile.skipDuplicateFrames) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_frame_skip),
                                options = listOf(
                                    0 to stringResource(R.string.settings_frame_skip_off),
                                    1 to "1",
                                    2 to "2",
                                    3 to "3",
                                    4 to "4"
                                ),
                                selectedValue = draft.frameSkip,
                                onSelected = { draft = draft.copy(frameSkip = it) },
                                helpText = stringResource(R.string.settings_help_frame_skip),
                                onResetToDefault = { draft = draft.copy(frameSkip = defaultProfile.frameSkip) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_enable_cheats),
                                checked = draft.enableCheats,
                                onCheckedChange = { draft = draft.copy(enableCheats = it) },
                                helpText = stringResource(R.string.settings_help_cheats),
                                onResetToDefault = { draft = draft.copy(enableCheats = defaultProfile.enableCheats) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_game_fixes),
                                checked = draft.enableGameFixes,
                                onCheckedChange = { draft = draft.copy(enableGameFixes = it) },
                                helpText = stringResource(R.string.settings_help_game_fixes),
                                onResetToDefault = { draft = draft.copy(enableGameFixes = defaultProfile.enableGameFixes) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_ee_timing_hack),
                                checked = draft.enableEeTimingHack,
                                onCheckedChange = { draft = draft.copy(enableEeTimingHack = it) },
                                helpText = stringResource(R.string.settings_help_ee_timing_hack),
                                onResetToDefault = {
                                    draft = draft.copy(enableEeTimingHack = defaultProfile.enableEeTimingHack)
                                }
                            )
                        }
                        EditorSection(title = stringResource(R.string.game_settings_manager_section_graphics)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_bilinear_filtering),
                                options = bilinearFilteringOptions(),
                                selectedValue = draft.textureFiltering,
                                onSelected = { draft = draft.copy(textureFiltering = it) },
                                helpText = stringResource(R.string.settings_help_bilinear_filtering),
                                onResetToDefault = { draft = draft.copy(textureFiltering = defaultProfile.textureFiltering) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_trilinear_filtering),
                                options = trilinearFilteringOptions(),
                                selectedValue = draft.trilinearFiltering,
                                onSelected = { draft = draft.copy(trilinearFiltering = it) },
                                helpText = stringResource(R.string.settings_help_trilinear_filtering),
                                onResetToDefault = { draft = draft.copy(trilinearFiltering = defaultProfile.trilinearFiltering) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_hw_download_mode),
                                options = hwDownloadModeOptions(),
                                selectedValue = draft.hwDownloadMode,
                                onSelected = { draft = draft.copy(hwDownloadMode = it) },
                                helpText = stringResource(R.string.settings_help_hw_download_mode),
                                onResetToDefault = { draft = draft.copy(hwDownloadMode = defaultProfile.hwDownloadMode) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_blending_accuracy),
                                options = blendingAccuracyOptions(),
                                selectedValue = draft.blendingAccuracy,
                                onSelected = { draft = draft.copy(blendingAccuracy = it) },
                                helpText = stringResource(R.string.settings_help_blending_accuracy),
                                onResetToDefault = { draft = draft.copy(blendingAccuracy = defaultProfile.blendingAccuracy) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_texture_preloading),
                                options = texturePreloadingOptions(),
                                selectedValue = draft.texturePreloading,
                                onSelected = { draft = draft.copy(texturePreloading = it) },
                                helpText = stringResource(R.string.settings_help_texture_preloading),
                                onResetToDefault = { draft = draft.copy(texturePreloading = defaultProfile.texturePreloading) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_anisotropic_filtering),
                                options = anisotropicFilteringOptions(),
                                selectedValue = draft.anisotropicFiltering,
                                onSelected = { draft = draft.copy(anisotropicFiltering = it) },
                                helpText = stringResource(R.string.settings_help_anisotropic_filtering),
                                onResetToDefault = { draft = draft.copy(anisotropicFiltering = defaultProfile.anisotropicFiltering) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_fxaa),
                                checked = draft.enableFxaa,
                                onCheckedChange = { draft = draft.copy(enableFxaa = it) },
                                helpText = stringResource(R.string.settings_help_fxaa),
                                onResetToDefault = { draft = draft.copy(enableFxaa = defaultProfile.enableFxaa) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_sgsr),
                                options = sgsrModeOptions(),
                                selectedValue = draft.sgsrMode,
                                onSelected = { draft = draft.copy(sgsrMode = it) },
                                helpText = stringResource(R.string.settings_help_sgsr),
                                onResetToDefault = { draft = draft.copy(sgsrMode = defaultProfile.sgsrMode) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_cas),
                                options = casModeOptions(),
                                selectedValue = draft.casMode,
                                onSelected = { draft = draft.copy(casMode = it) },
                                helpText = stringResource(R.string.settings_help_cas),
                                onResetToDefault = { draft = draft.copy(casMode = defaultProfile.casMode) }
                            )
                            if (draft.casMode != 0) {
                                SliderRow(
                                    title = stringResource(R.string.settings_cas_sharpness),
                                    value = draft.casSharpness.toFloat(),
                                    valueLabel = stringResource(
                                        R.string.settings_cas_sharpness_value,
                                        draft.casSharpness
                                    ),
                                    range = 0f..100f,
                                    steps = 99,
                                    onValueChange = { draft = draft.copy(casSharpness = it.toInt()) },
                                    helpText = stringResource(R.string.settings_help_cas_sharpness),
                                    onResetToDefault = { draft = draft.copy(casSharpness = defaultProfile.casSharpness) }
                                )
                            }
                            SelectionRow(
                                title = stringResource(R.string.settings_tv_shader),
                                options = tvShaderOptions(),
                                selectedValue = draft.tvShader,
                                onSelected = { draft = draft.copy(tvShader = it) },
                                helpText = stringResource(R.string.settings_help_tv_shader),
                                onResetToDefault = { draft = draft.copy(tvShader = defaultProfile.tvShader) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_hw_mipmapping),
                                checked = draft.enableHwMipmapping,
                                onCheckedChange = { draft = draft.copy(enableHwMipmapping = it) },
                                helpText = stringResource(R.string.settings_help_hw_mipmapping),
                                onResetToDefault = { draft = draft.copy(enableHwMipmapping = defaultProfile.enableHwMipmapping) }
                            )
                        }
                        EditorSection(title = stringResource(R.string.game_settings_manager_section_screen)) {
                            SliderRow(
                                title = stringResource(R.string.settings_shadeboost_brightness),
                                value = draft.shadeBoostBrightness.toFloat(),
                                valueLabel = draft.shadeBoostBrightness.toString(),
                                valueLabelForValue = { it.roundToInt().toString() },
                                range = 1f..100f,
                                steps = 98,
                                onValueChange = {
                                    val brightness = it.toInt()
                                    draft = draft.copy(
                                        shadeBoostEnabled = isShadeBoostActive(
                                            brightness = brightness,
                                            contrast = draft.shadeBoostContrast,
                                            saturation = draft.shadeBoostSaturation,
                                            gamma = draft.shadeBoostGamma
                                        ),
                                        shadeBoostBrightness = brightness
                                    )
                                },
                                helpText = stringResource(R.string.settings_help_shadeboost_brightness),
                                onResetToDefault = {
                                    draft = draft.copy(
                                        shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                                        shadeBoostBrightness = defaultProfile.shadeBoostBrightness
                                    )
                                }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_shadeboost_contrast),
                                value = draft.shadeBoostContrast.toFloat(),
                                valueLabel = draft.shadeBoostContrast.toString(),
                                valueLabelForValue = { it.roundToInt().toString() },
                                range = 1f..100f,
                                steps = 98,
                                onValueChange = {
                                    val contrast = it.toInt()
                                    draft = draft.copy(
                                        shadeBoostEnabled = isShadeBoostActive(
                                            brightness = draft.shadeBoostBrightness,
                                            contrast = contrast,
                                            saturation = draft.shadeBoostSaturation,
                                            gamma = draft.shadeBoostGamma
                                        ),
                                        shadeBoostContrast = contrast
                                    )
                                },
                                helpText = stringResource(R.string.settings_help_shadeboost_contrast),
                                onResetToDefault = {
                                    draft = draft.copy(
                                        shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                                        shadeBoostContrast = defaultProfile.shadeBoostContrast
                                    )
                                }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_shadeboost_saturation),
                                value = draft.shadeBoostSaturation.toFloat(),
                                valueLabel = draft.shadeBoostSaturation.toString(),
                                valueLabelForValue = { it.roundToInt().toString() },
                                range = 1f..100f,
                                steps = 98,
                                onValueChange = {
                                    val saturation = it.toInt()
                                    draft = draft.copy(
                                        shadeBoostEnabled = isShadeBoostActive(
                                            brightness = draft.shadeBoostBrightness,
                                            contrast = draft.shadeBoostContrast,
                                            saturation = saturation,
                                            gamma = draft.shadeBoostGamma
                                        ),
                                        shadeBoostSaturation = saturation
                                    )
                                },
                                helpText = stringResource(R.string.settings_help_shadeboost_saturation),
                                onResetToDefault = {
                                    draft = draft.copy(
                                        shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                                        shadeBoostSaturation = defaultProfile.shadeBoostSaturation
                                    )
                                }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_shadeboost_gamma),
                                value = draft.shadeBoostGamma.toFloat(),
                                valueLabel = draft.shadeBoostGamma.toString(),
                                valueLabelForValue = { it.roundToInt().toString() },
                                range = 1f..100f,
                                steps = 98,
                                onValueChange = {
                                    val gamma = it.toInt()
                                    draft = draft.copy(
                                        shadeBoostEnabled = isShadeBoostActive(
                                            brightness = draft.shadeBoostBrightness,
                                            contrast = draft.shadeBoostContrast,
                                            saturation = draft.shadeBoostSaturation,
                                            gamma = gamma
                                        ),
                                        shadeBoostGamma = gamma
                                    )
                                },
                                helpText = stringResource(R.string.settings_help_shadeboost_gamma),
                                onResetToDefault = {
                                    draft = draft.copy(
                                        shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                                        shadeBoostGamma = defaultProfile.shadeBoostGamma
                                    )
                                }
                            )
                        }
                        EditorSection(title = stringResource(R.string.settings_image_processing)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_deinterlacing),
                                options = deinterlacingOptions(),
                                selectedValue = draft.deinterlaceMode,
                                onSelected = { draft = draft.copy(deinterlaceMode = it) },
                                helpText = stringResource(R.string.settings_help_deinterlacing),
                                onResetToDefault = { draft = draft.copy(deinterlaceMode = defaultProfile.deinterlaceMode) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_dithering),
                                options = ditheringOptions(),
                                selectedValue = draft.dithering,
                                onSelected = { draft = draft.copy(dithering = it) },
                                helpText = stringResource(R.string.settings_help_dithering),
                                onResetToDefault = { draft = draft.copy(dithering = defaultProfile.dithering) }
                            )
                        }
                        EditorSection(title = stringResource(R.string.settings_patches_section)) {
                            ToggleRow(
                                title = stringResource(R.string.settings_widescreen_patches),
                                checked = draft.enableWidescreenPatches,
                                onCheckedChange = { draft = draft.copy(enableWidescreenPatches = it) },
                                helpText = stringResource(R.string.settings_help_widescreen_patches),
                                onResetToDefault = { draft = draft.copy(enableWidescreenPatches = defaultProfile.enableWidescreenPatches) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_no_interlacing_patches),
                                checked = draft.enableNoInterlacingPatches,
                                onCheckedChange = { draft = draft.copy(enableNoInterlacingPatches = it) },
                                helpText = stringResource(R.string.settings_help_no_interlacing_patches),
                                onResetToDefault = { draft = draft.copy(enableNoInterlacingPatches = defaultProfile.enableNoInterlacingPatches) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_anti_blur),
                                checked = draft.antiBlur,
                                onCheckedChange = { draft = draft.copy(antiBlur = it) },
                                helpText = stringResource(R.string.settings_help_anti_blur),
                                onResetToDefault = { draft = draft.copy(antiBlur = defaultProfile.antiBlur) }
                            )
                        }
                        EditorSection(title = stringResource(R.string.settings_hardware_fixes)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_cpu_sprite_render_size),
                                options = cpuSpriteRenderSizeOptions(),
                                selectedValue = draft.cpuSpriteRenderSize,
                                onSelected = { draft = draft.copy(cpuSpriteRenderSize = it) },
                                helpText = stringResource(R.string.settings_help_cpu_sprite_render_size),
                                onResetToDefault = { draft = draft.copy(cpuSpriteRenderSize = defaultProfile.cpuSpriteRenderSize) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_cpu_sprite_render_level),
                                options = cpuSpriteRenderLevelOptions(),
                                selectedValue = draft.cpuSpriteRenderLevel,
                                onSelected = { draft = draft.copy(cpuSpriteRenderLevel = it) },
                                helpText = stringResource(R.string.settings_help_cpu_sprite_render_level),
                                onResetToDefault = { draft = draft.copy(cpuSpriteRenderLevel = defaultProfile.cpuSpriteRenderLevel) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_software_clut_render),
                                options = softwareClutRenderOptions(),
                                selectedValue = draft.softwareClutRender,
                                onSelected = { draft = draft.copy(softwareClutRender = it) },
                                helpText = stringResource(R.string.settings_help_software_clut_render),
                                onResetToDefault = { draft = draft.copy(softwareClutRender = defaultProfile.softwareClutRender) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_gpu_target_clut),
                                options = gpuTargetClutOptions(),
                                selectedValue = draft.gpuTargetClutMode,
                                onSelected = { draft = draft.copy(gpuTargetClutMode = it) },
                                helpText = stringResource(R.string.settings_help_gpu_target_clut),
                                onResetToDefault = { draft = draft.copy(gpuTargetClutMode = defaultProfile.gpuTargetClutMode) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_auto_flush_hardware),
                                options = autoFlushHardwareOptions(),
                                selectedValue = draft.autoFlushHardware,
                                onSelected = { draft = draft.copy(autoFlushHardware = it) },
                                helpText = stringResource(R.string.settings_help_auto_flush_hardware),
                                onResetToDefault = { draft = draft.copy(autoFlushHardware = defaultProfile.autoFlushHardware) }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_skip_draw_start),
                                value = draft.skipDrawStart.toFloat(),
                                valueLabel = draft.skipDrawStart.toString(),
                                range = 0f..100f,
                                steps = 99,
                                onValueChange = { draft = draft.copy(skipDrawStart = it.toInt()) },
                                helpText = stringResource(R.string.settings_help_skip_draw_start),
                                onResetToDefault = { draft = draft.copy(skipDrawStart = defaultProfile.skipDrawStart) }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_skip_draw_end),
                                value = draft.skipDrawEnd.toFloat(),
                                valueLabel = draft.skipDrawEnd.toString(),
                                range = 0f..100f,
                                steps = 99,
                                onValueChange = { draft = draft.copy(skipDrawEnd = it.toInt()) },
                                helpText = stringResource(R.string.settings_help_skip_draw_end),
                                onResetToDefault = { draft = draft.copy(skipDrawEnd = defaultProfile.skipDrawEnd) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_cpu_framebuffer_conversion),
                                checked = draft.cpuFramebufferConversion,
                                onCheckedChange = { draft = draft.copy(cpuFramebufferConversion = it) },
                                helpText = stringResource(R.string.settings_help_cpu_framebuffer_conversion),
                                onResetToDefault = { draft = draft.copy(cpuFramebufferConversion = defaultProfile.cpuFramebufferConversion) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_disable_depth_conversion),
                                checked = draft.disableDepthConversion,
                                onCheckedChange = { draft = draft.copy(disableDepthConversion = it) },
                                helpText = stringResource(R.string.settings_help_disable_depth_conversion),
                                onResetToDefault = { draft = draft.copy(disableDepthConversion = defaultProfile.disableDepthConversion) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_disable_safe_features),
                                checked = draft.disableSafeFeatures,
                                onCheckedChange = { draft = draft.copy(disableSafeFeatures = it) },
                                helpText = stringResource(R.string.settings_help_disable_safe_features),
                                onResetToDefault = { draft = draft.copy(disableSafeFeatures = defaultProfile.disableSafeFeatures) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_disable_render_fixes),
                                checked = draft.disableRenderFixes,
                                onCheckedChange = { draft = draft.copy(disableRenderFixes = it) },
                                helpText = stringResource(R.string.settings_help_disable_render_fixes),
                                onResetToDefault = { draft = draft.copy(disableRenderFixes = defaultProfile.disableRenderFixes) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_preload_frame_data),
                                checked = draft.preloadFrameData,
                                onCheckedChange = { draft = draft.copy(preloadFrameData = it) },
                                helpText = stringResource(R.string.settings_help_preload_frame_data),
                                onResetToDefault = { draft = draft.copy(preloadFrameData = defaultProfile.preloadFrameData) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_disable_partial_invalidation),
                                checked = draft.disablePartialInvalidation,
                                onCheckedChange = { draft = draft.copy(disablePartialInvalidation = it) },
                                helpText = stringResource(R.string.settings_help_disable_partial_invalidation),
                                onResetToDefault = { draft = draft.copy(disablePartialInvalidation = defaultProfile.disablePartialInvalidation) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_texture_inside_rt),
                                options = textureInsideRtOptions(),
                                selectedValue = draft.textureInsideRt,
                                onSelected = { draft = draft.copy(textureInsideRt = it) },
                                helpText = stringResource(R.string.settings_help_texture_inside_rt),
                                onResetToDefault = { draft = draft.copy(textureInsideRt = defaultProfile.textureInsideRt) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_read_targets_on_close),
                                checked = draft.readTargetsOnClose,
                                onCheckedChange = { draft = draft.copy(readTargetsOnClose = it) },
                                helpText = stringResource(R.string.settings_help_read_targets_on_close),
                                onResetToDefault = { draft = draft.copy(readTargetsOnClose = defaultProfile.readTargetsOnClose) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_estimate_texture_region),
                                checked = draft.estimateTextureRegion,
                                onCheckedChange = { draft = draft.copy(estimateTextureRegion = it) },
                                helpText = stringResource(R.string.settings_help_estimate_texture_region),
                                onResetToDefault = { draft = draft.copy(estimateTextureRegion = defaultProfile.estimateTextureRegion) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_gpu_palette_conversion),
                                checked = draft.gpuPaletteConversion,
                                onCheckedChange = { draft = draft.copy(gpuPaletteConversion = it) },
                                helpText = stringResource(R.string.settings_help_gpu_palette_conversion),
                                onResetToDefault = { draft = draft.copy(gpuPaletteConversion = defaultProfile.gpuPaletteConversion) }
                            )
                        }
                        EditorSection(title = stringResource(R.string.settings_upscaling_fixes)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_half_pixel_offset),
                                options = halfPixelOffsetOptions(),
                                selectedValue = draft.halfPixelOffset,
                                onSelected = { draft = draft.copy(halfPixelOffset = it) },
                                helpText = stringResource(R.string.settings_help_half_pixel_offset),
                                onResetToDefault = { draft = draft.copy(halfPixelOffset = defaultProfile.halfPixelOffset) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_native_scaling),
                                options = nativeScalingOptions(),
                                selectedValue = draft.nativeScaling,
                                onSelected = { draft = draft.copy(nativeScaling = it) },
                                helpText = stringResource(R.string.settings_help_native_scaling),
                                onResetToDefault = { draft = draft.copy(nativeScaling = defaultProfile.nativeScaling) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_round_sprite),
                                options = roundSpriteOptions(),
                                selectedValue = draft.roundSprite,
                                onSelected = { draft = draft.copy(roundSprite = it) },
                                helpText = stringResource(R.string.settings_help_round_sprite),
                                onResetToDefault = { draft = draft.copy(roundSprite = defaultProfile.roundSprite) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_bilinear_upscale),
                                options = bilinearUpscaleOptions(),
                                selectedValue = draft.bilinearUpscale,
                                onSelected = { draft = draft.copy(bilinearUpscale = it) },
                                helpText = stringResource(R.string.settings_help_bilinear_upscale),
                                onResetToDefault = { draft = draft.copy(bilinearUpscale = defaultProfile.bilinearUpscale) }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_texture_offset_x),
                                value = draft.textureOffsetX.toFloat(),
                                valueLabel = draft.textureOffsetX.toString(),
                                range = -512f..512f,
                                steps = 1023,
                                onValueChange = { draft = draft.copy(textureOffsetX = it.toInt()) },
                                helpText = stringResource(R.string.settings_help_texture_offset_x),
                                onResetToDefault = { draft = draft.copy(textureOffsetX = defaultProfile.textureOffsetX) }
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_texture_offset_y),
                                value = draft.textureOffsetY.toFloat(),
                                valueLabel = draft.textureOffsetY.toString(),
                                range = -512f..512f,
                                steps = 1023,
                                onValueChange = { draft = draft.copy(textureOffsetY = it.toInt()) },
                                helpText = stringResource(R.string.settings_help_texture_offset_y),
                                onResetToDefault = { draft = draft.copy(textureOffsetY = defaultProfile.textureOffsetY) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_align_sprite),
                                checked = draft.alignSprite,
                                onCheckedChange = { draft = draft.copy(alignSprite = it) },
                                helpText = stringResource(R.string.settings_help_align_sprite),
                                onResetToDefault = { draft = draft.copy(alignSprite = defaultProfile.alignSprite) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_merge_sprite),
                                checked = draft.mergeSprite,
                                onCheckedChange = { draft = draft.copy(mergeSprite = it) },
                                helpText = stringResource(R.string.settings_help_merge_sprite),
                                onResetToDefault = { draft = draft.copy(mergeSprite = defaultProfile.mergeSprite) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_force_even_sprite_position),
                                checked = draft.forceEvenSpritePosition,
                                onCheckedChange = { draft = draft.copy(forceEvenSpritePosition = it) },
                                helpText = stringResource(R.string.settings_help_force_even_sprite_position),
                                onResetToDefault = { draft = draft.copy(forceEvenSpritePosition = defaultProfile.forceEvenSpritePosition) }
                            )
                            ToggleRow(
                                title = stringResource(R.string.settings_native_palette_draw),
                                checked = draft.nativePaletteDraw,
                                onCheckedChange = { draft = draft.copy(nativePaletteDraw = it) },
                                helpText = stringResource(R.string.settings_help_native_palette_draw),
                                onResetToDefault = { draft = draft.copy(nativePaletteDraw = defaultProfile.nativePaletteDraw) }
                            )
                        }
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f))
                    Button(
                        onClick = onDismiss,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(18.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.primary,
                            contentColor = MaterialTheme.colorScheme.onPrimary
                        )
                    ) {
                        Text(
                            text = stringResource(R.string.cancel),
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.padding(vertical = 6.dp)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun GameSettingsEditorHeader(
    profile: PerGameSettings,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Surface(
            modifier = Modifier.size(58.dp),
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.78f)
        ) {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    imageVector = Icons.Rounded.Tune,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(30.dp)
                )
            }
        }
        Spacer(modifier = Modifier.width(14.dp))

        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = stringResource(R.string.game_settings_manager_editor_eyebrow),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.primary
            )
            Text(
                text = profile.gameTitle,
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            profile.gameSerial?.takeIf { it.isNotBlank() }?.let { serial ->
                Text(
                    text = serial,
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun EditorSection(
    title: String,
    content: @Composable () -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onSurface
        )
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(22.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 2.dp,
            border = BorderStroke(
                width = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
            )
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(GameSettingsSectionContentPadding),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                content()
            }
        }
    }
}

@Composable
private fun SelectionRow(
    title: String,
    options: List<Pair<Int, String>>,
    selectedValue: Int,
    onSelected: (Int) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier.combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = {},
                onLongClick = onResetToDefault?.let {
                    {
                        it()
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    }
                }
            ),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        if (options.size > 3) {
            LazyRow(
                modifier = Modifier
                    .fillMaxWidth()
                    .sectionContentFullBleed(GameSettingsSectionContentPadding),
                contentPadding = PaddingValues(horizontal = GameSettingsSectionContentPadding),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(options, key = { it.first }) { (value, label) ->
                    FilterChip(
                        selected = selectedValue == value,
                        onClick = { onSelected(value) },
                        label = { Text(label) }
                    )
                }
            }
        } else {
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                options.forEach { (value, label) ->
                    FilterChip(
                        selected = selectedValue == value,
                        onClick = { onSelected(value) },
                        label = { Text(label) }
                    )
                }
            }
        }
    }
}

@Composable
private fun ToggleRow(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    val shape = RoundedCornerShape(14.dp)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = { onCheckedChange(!checked) },
                onLongClick = onResetToDefault?.let {
                    {
                        it()
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    }
                }
            )
            .gamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            )
            .padding(horizontal = 10.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Row(
            modifier = Modifier.weight(1f),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                modifier = Modifier.weight(1f, fill = false),
                color = MaterialTheme.colorScheme.onSurface
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        Switch(
            checked = checked,
            onCheckedChange = null
        )
    }
}

@Composable
private fun SliderRow(
    title: String,
    value: Float,
    valueLabel: String,
    range: ClosedFloatingPointRange<Float>,
    steps: Int,
    onValueChange: (Float) -> Unit,
    valueLabelForValue: ((Float) -> String)? = null,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    var sliderValue by remember { mutableFloatStateOf(value) }
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)

    LaunchedEffect(value) {
        sliderValue = value
    }

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = {},
                    onLongClick = onResetToDefault?.let {
                        {
                            it()
                            Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                        }
                    }
                ),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    modifier = Modifier.weight(1f, fill = false),
                    color = MaterialTheme.colorScheme.onSurface
                )
                helpText?.let {
                    SettingHelpButton(title = title, description = it)
                }
            }
            Text(
                text = valueLabelForValue?.invoke(sliderValue) ?: valueLabel,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary
            )
        }
        Slider(
            value = sliderValue,
            onValueChange = {
                sliderValue = it
                onValueChange(it)
            },
            valueRange = range,
            steps = steps
        )
    }
}

@Composable
@Suppress("unused")
private fun ManagerActionButton(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    title: String,
    onClick: () -> Unit,
    destructive: Boolean = false
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = if (destructive) {
            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.22f)
        } else {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.08f)
        },
        border = BorderStroke(
            width = 1.dp,
            color = if (destructive) {
                MaterialTheme.colorScheme.error.copy(alpha = 0.12f)
            } else {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)
            }
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = if (destructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
            )
            Text(
                text = title,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = if (destructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun ProfileActionIconButton(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    title: String,
    onClick: () -> Unit,
    destructive: Boolean = false
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(18.dp),
        color = if (destructive) {
            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.22f)
        } else {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.08f)
        },
        border = BorderStroke(
            width = 1.dp,
            color = if (destructive) {
                MaterialTheme.colorScheme.error.copy(alpha = 0.12f)
            } else {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)
            }
        ),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(46.dp),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = title,
                tint = if (destructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(22.dp)
            )
        }
    }
}

@Composable
private fun ProfileBadge(text: String) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.primary
        )
    }
}

private fun Modifier.gameManagerFullBleed(): Modifier {
    return layout { measurable, constraints ->
        val sidePadding = ScreenHorizontalPadding.roundToPx()
        val expandedConstraints = constraints.copy(
            minWidth = (constraints.minWidth + sidePadding * 2).coerceAtLeast(0),
            maxWidth = (constraints.maxWidth + sidePadding * 2).coerceAtLeast(0)
        )
        val placeable = measurable.measure(expandedConstraints)
        layout(constraints.maxWidth, placeable.height) {
            placeable.placeRelative(-sidePadding, 0)
        }
    }
}

private fun Modifier.sectionContentFullBleed(horizontalPadding: Dp): Modifier {
    return layout { measurable, constraints ->
        val sidePadding = horizontalPadding.roundToPx()
        val expandedConstraints = constraints.copy(
            minWidth = (constraints.minWidth + sidePadding * 2).coerceAtLeast(0),
            maxWidth = (constraints.maxWidth + sidePadding * 2).coerceAtLeast(0)
        )
        val placeable = measurable.measure(expandedConstraints)
        layout(constraints.maxWidth, placeable.height) {
            placeable.placeRelative(-sidePadding, 0)
        }
    }
}

@Composable
private fun GameSettingsManagerTab.title(): String = when (this) {
    GameSettingsManagerTab.Graphics -> stringResource(R.string.settings_graphics_tab)
    GameSettingsManagerTab.System -> stringResource(R.string.game_settings_manager_tab_system)
    GameSettingsManagerTab.Controls -> stringResource(R.string.settings_controls_tab)
    GameSettingsManagerTab.Fixes -> stringResource(R.string.settings_fixes_tab)
}

@Composable
private fun ShadeBoostRows(
    draft: PerGameSettings,
    defaultProfile: PerGameSettings,
    onDraftChange: (PerGameSettings) -> Unit
) {
    SliderRow(
        title = stringResource(R.string.settings_shadeboost_brightness),
        value = draft.shadeBoostBrightness.toFloat(),
        valueLabel = draft.shadeBoostBrightness.toString(),
        valueLabelForValue = { it.roundToInt().toString() },
        range = 1f..100f,
        steps = 98,
        onValueChange = {
            val brightness = it.toInt()
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = isShadeBoostActive(
                        brightness = brightness,
                        contrast = draft.shadeBoostContrast,
                        saturation = draft.shadeBoostSaturation,
                        gamma = draft.shadeBoostGamma
                    ),
                    shadeBoostBrightness = brightness
                )
            )
        },
        helpText = stringResource(R.string.settings_help_shadeboost_brightness),
        onResetToDefault = {
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                    shadeBoostBrightness = defaultProfile.shadeBoostBrightness
                )
            )
        }
    )
    SliderRow(
        title = stringResource(R.string.settings_shadeboost_contrast),
        value = draft.shadeBoostContrast.toFloat(),
        valueLabel = draft.shadeBoostContrast.toString(),
        valueLabelForValue = { it.roundToInt().toString() },
        range = 1f..100f,
        steps = 98,
        onValueChange = {
            val contrast = it.toInt()
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = isShadeBoostActive(
                        brightness = draft.shadeBoostBrightness,
                        contrast = contrast,
                        saturation = draft.shadeBoostSaturation,
                        gamma = draft.shadeBoostGamma
                    ),
                    shadeBoostContrast = contrast
                )
            )
        },
        helpText = stringResource(R.string.settings_help_shadeboost_contrast),
        onResetToDefault = {
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                    shadeBoostContrast = defaultProfile.shadeBoostContrast
                )
            )
        }
    )
    SliderRow(
        title = stringResource(R.string.settings_shadeboost_saturation),
        value = draft.shadeBoostSaturation.toFloat(),
        valueLabel = draft.shadeBoostSaturation.toString(),
        valueLabelForValue = { it.roundToInt().toString() },
        range = 1f..100f,
        steps = 98,
        onValueChange = {
            val saturation = it.toInt()
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = isShadeBoostActive(
                        brightness = draft.shadeBoostBrightness,
                        contrast = draft.shadeBoostContrast,
                        saturation = saturation,
                        gamma = draft.shadeBoostGamma
                    ),
                    shadeBoostSaturation = saturation
                )
            )
        },
        helpText = stringResource(R.string.settings_help_shadeboost_saturation),
        onResetToDefault = {
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                    shadeBoostSaturation = defaultProfile.shadeBoostSaturation
                )
            )
        }
    )
    SliderRow(
        title = stringResource(R.string.settings_shadeboost_gamma),
        value = draft.shadeBoostGamma.toFloat(),
        valueLabel = draft.shadeBoostGamma.toString(),
        valueLabelForValue = { it.roundToInt().toString() },
        range = 1f..100f,
        steps = 98,
        onValueChange = {
            val gamma = it.toInt()
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = isShadeBoostActive(
                        brightness = draft.shadeBoostBrightness,
                        contrast = draft.shadeBoostContrast,
                        saturation = draft.shadeBoostSaturation,
                        gamma = gamma
                    ),
                    shadeBoostGamma = gamma
                )
            )
        },
        helpText = stringResource(R.string.settings_help_shadeboost_gamma),
        onResetToDefault = {
            onDraftChange(
                draft.copy(
                    shadeBoostEnabled = defaultProfile.shadeBoostEnabled,
                    shadeBoostGamma = defaultProfile.shadeBoostGamma
                )
            )
        }
    )
}

@Composable
private fun HardwareFixesRows(
    draft: PerGameSettings,
    defaultProfile: PerGameSettings,
    onDraftChange: (PerGameSettings) -> Unit
) {
    EditorSection(title = stringResource(R.string.settings_hardware_fixes)) {
        SelectionRow(
            title = stringResource(R.string.settings_cpu_sprite_render_size),
            options = cpuSpriteRenderSizeOptions(),
            selectedValue = draft.cpuSpriteRenderSize,
            onSelected = { onDraftChange(draft.copy(cpuSpriteRenderSize = it)) },
            helpText = stringResource(R.string.settings_help_cpu_sprite_render_size),
            onResetToDefault = { onDraftChange(draft.copy(cpuSpriteRenderSize = defaultProfile.cpuSpriteRenderSize)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_cpu_sprite_render_level),
            options = cpuSpriteRenderLevelOptions(),
            selectedValue = draft.cpuSpriteRenderLevel,
            onSelected = { onDraftChange(draft.copy(cpuSpriteRenderLevel = it)) },
            helpText = stringResource(R.string.settings_help_cpu_sprite_render_level),
            onResetToDefault = { onDraftChange(draft.copy(cpuSpriteRenderLevel = defaultProfile.cpuSpriteRenderLevel)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_software_clut_render),
            options = softwareClutRenderOptions(),
            selectedValue = draft.softwareClutRender,
            onSelected = { onDraftChange(draft.copy(softwareClutRender = it)) },
            helpText = stringResource(R.string.settings_help_software_clut_render),
            onResetToDefault = { onDraftChange(draft.copy(softwareClutRender = defaultProfile.softwareClutRender)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_gpu_target_clut),
            options = gpuTargetClutOptions(),
            selectedValue = draft.gpuTargetClutMode,
            onSelected = { onDraftChange(draft.copy(gpuTargetClutMode = it)) },
            helpText = stringResource(R.string.settings_help_gpu_target_clut),
            onResetToDefault = { onDraftChange(draft.copy(gpuTargetClutMode = defaultProfile.gpuTargetClutMode)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_auto_flush_hardware),
            options = autoFlushHardwareOptions(),
            selectedValue = draft.autoFlushHardware,
            onSelected = { onDraftChange(draft.copy(autoFlushHardware = it)) },
            helpText = stringResource(R.string.settings_help_auto_flush_hardware),
            onResetToDefault = { onDraftChange(draft.copy(autoFlushHardware = defaultProfile.autoFlushHardware)) }
        )
        SliderRow(
            title = stringResource(R.string.settings_skip_draw_start),
            value = draft.skipDrawStart.toFloat(),
            valueLabel = draft.skipDrawStart.toString(),
            range = 0f..100f,
            steps = 99,
            onValueChange = { onDraftChange(draft.copy(skipDrawStart = it.toInt())) },
            helpText = stringResource(R.string.settings_help_skip_draw_start),
            onResetToDefault = { onDraftChange(draft.copy(skipDrawStart = defaultProfile.skipDrawStart)) }
        )
        SliderRow(
            title = stringResource(R.string.settings_skip_draw_end),
            value = draft.skipDrawEnd.toFloat(),
            valueLabel = draft.skipDrawEnd.toString(),
            range = 0f..100f,
            steps = 99,
            onValueChange = { onDraftChange(draft.copy(skipDrawEnd = it.toInt())) },
            helpText = stringResource(R.string.settings_help_skip_draw_end),
            onResetToDefault = { onDraftChange(draft.copy(skipDrawEnd = defaultProfile.skipDrawEnd)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_cpu_framebuffer_conversion),
            checked = draft.cpuFramebufferConversion,
            onCheckedChange = { onDraftChange(draft.copy(cpuFramebufferConversion = it)) },
            helpText = stringResource(R.string.settings_help_cpu_framebuffer_conversion),
            onResetToDefault = { onDraftChange(draft.copy(cpuFramebufferConversion = defaultProfile.cpuFramebufferConversion)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_disable_depth_conversion),
            checked = draft.disableDepthConversion,
            onCheckedChange = { onDraftChange(draft.copy(disableDepthConversion = it)) },
            helpText = stringResource(R.string.settings_help_disable_depth_conversion),
            onResetToDefault = { onDraftChange(draft.copy(disableDepthConversion = defaultProfile.disableDepthConversion)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_disable_safe_features),
            checked = draft.disableSafeFeatures,
            onCheckedChange = { onDraftChange(draft.copy(disableSafeFeatures = it)) },
            helpText = stringResource(R.string.settings_help_disable_safe_features),
            onResetToDefault = { onDraftChange(draft.copy(disableSafeFeatures = defaultProfile.disableSafeFeatures)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_disable_render_fixes),
            checked = draft.disableRenderFixes,
            onCheckedChange = { onDraftChange(draft.copy(disableRenderFixes = it)) },
            helpText = stringResource(R.string.settings_help_disable_render_fixes),
            onResetToDefault = { onDraftChange(draft.copy(disableRenderFixes = defaultProfile.disableRenderFixes)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_preload_frame_data),
            checked = draft.preloadFrameData,
            onCheckedChange = { onDraftChange(draft.copy(preloadFrameData = it)) },
            helpText = stringResource(R.string.settings_help_preload_frame_data),
            onResetToDefault = { onDraftChange(draft.copy(preloadFrameData = defaultProfile.preloadFrameData)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_disable_partial_invalidation),
            checked = draft.disablePartialInvalidation,
            onCheckedChange = { onDraftChange(draft.copy(disablePartialInvalidation = it)) },
            helpText = stringResource(R.string.settings_help_disable_partial_invalidation),
            onResetToDefault = { onDraftChange(draft.copy(disablePartialInvalidation = defaultProfile.disablePartialInvalidation)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_texture_inside_rt),
            options = textureInsideRtOptions(),
            selectedValue = draft.textureInsideRt,
            onSelected = { onDraftChange(draft.copy(textureInsideRt = it)) },
            helpText = stringResource(R.string.settings_help_texture_inside_rt),
            onResetToDefault = { onDraftChange(draft.copy(textureInsideRt = defaultProfile.textureInsideRt)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_read_targets_on_close),
            checked = draft.readTargetsOnClose,
            onCheckedChange = { onDraftChange(draft.copy(readTargetsOnClose = it)) },
            helpText = stringResource(R.string.settings_help_read_targets_on_close),
            onResetToDefault = { onDraftChange(draft.copy(readTargetsOnClose = defaultProfile.readTargetsOnClose)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_estimate_texture_region),
            checked = draft.estimateTextureRegion,
            onCheckedChange = { onDraftChange(draft.copy(estimateTextureRegion = it)) },
            helpText = stringResource(R.string.settings_help_estimate_texture_region),
            onResetToDefault = { onDraftChange(draft.copy(estimateTextureRegion = defaultProfile.estimateTextureRegion)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_gpu_palette_conversion),
            checked = draft.gpuPaletteConversion,
            onCheckedChange = { onDraftChange(draft.copy(gpuPaletteConversion = it)) },
            helpText = stringResource(R.string.settings_help_gpu_palette_conversion),
            onResetToDefault = { onDraftChange(draft.copy(gpuPaletteConversion = defaultProfile.gpuPaletteConversion)) }
        )
    }
}

@Composable
private fun UpscalingFixesRows(
    draft: PerGameSettings,
    defaultProfile: PerGameSettings,
    onDraftChange: (PerGameSettings) -> Unit
) {
    EditorSection(title = stringResource(R.string.settings_upscaling_fixes)) {
        SelectionRow(
            title = stringResource(R.string.settings_half_pixel_offset),
            options = halfPixelOffsetOptions(),
            selectedValue = draft.halfPixelOffset,
            onSelected = { onDraftChange(draft.copy(halfPixelOffset = it)) },
            helpText = stringResource(R.string.settings_help_half_pixel_offset),
            onResetToDefault = { onDraftChange(draft.copy(halfPixelOffset = defaultProfile.halfPixelOffset)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_native_scaling),
            options = nativeScalingOptions(),
            selectedValue = draft.nativeScaling,
            onSelected = { onDraftChange(draft.copy(nativeScaling = it)) },
            helpText = stringResource(R.string.settings_help_native_scaling),
            onResetToDefault = { onDraftChange(draft.copy(nativeScaling = defaultProfile.nativeScaling)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_round_sprite),
            options = roundSpriteOptions(),
            selectedValue = draft.roundSprite,
            onSelected = { onDraftChange(draft.copy(roundSprite = it)) },
            helpText = stringResource(R.string.settings_help_round_sprite),
            onResetToDefault = { onDraftChange(draft.copy(roundSprite = defaultProfile.roundSprite)) }
        )
        SelectionRow(
            title = stringResource(R.string.settings_bilinear_upscale),
            options = bilinearUpscaleOptions(),
            selectedValue = draft.bilinearUpscale,
            onSelected = { onDraftChange(draft.copy(bilinearUpscale = it)) },
            helpText = stringResource(R.string.settings_help_bilinear_upscale),
            onResetToDefault = { onDraftChange(draft.copy(bilinearUpscale = defaultProfile.bilinearUpscale)) }
        )
        SliderRow(
            title = stringResource(R.string.settings_texture_offset_x),
            value = draft.textureOffsetX.toFloat(),
            valueLabel = draft.textureOffsetX.toString(),
            range = -512f..512f,
            steps = 1023,
            onValueChange = { onDraftChange(draft.copy(textureOffsetX = it.toInt())) },
            helpText = stringResource(R.string.settings_help_texture_offset_x),
            onResetToDefault = { onDraftChange(draft.copy(textureOffsetX = defaultProfile.textureOffsetX)) }
        )
        SliderRow(
            title = stringResource(R.string.settings_texture_offset_y),
            value = draft.textureOffsetY.toFloat(),
            valueLabel = draft.textureOffsetY.toString(),
            range = -512f..512f,
            steps = 1023,
            onValueChange = { onDraftChange(draft.copy(textureOffsetY = it.toInt())) },
            helpText = stringResource(R.string.settings_help_texture_offset_y),
            onResetToDefault = { onDraftChange(draft.copy(textureOffsetY = defaultProfile.textureOffsetY)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_align_sprite),
            checked = draft.alignSprite,
            onCheckedChange = { onDraftChange(draft.copy(alignSprite = it)) },
            helpText = stringResource(R.string.settings_help_align_sprite),
            onResetToDefault = { onDraftChange(draft.copy(alignSprite = defaultProfile.alignSprite)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_merge_sprite),
            checked = draft.mergeSprite,
            onCheckedChange = { onDraftChange(draft.copy(mergeSprite = it)) },
            helpText = stringResource(R.string.settings_help_merge_sprite),
            onResetToDefault = { onDraftChange(draft.copy(mergeSprite = defaultProfile.mergeSprite)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_force_even_sprite_position),
            checked = draft.forceEvenSpritePosition,
            onCheckedChange = { onDraftChange(draft.copy(forceEvenSpritePosition = it)) },
            helpText = stringResource(R.string.settings_help_force_even_sprite_position),
            onResetToDefault = { onDraftChange(draft.copy(forceEvenSpritePosition = defaultProfile.forceEvenSpritePosition)) }
        )
        ToggleRow(
            title = stringResource(R.string.settings_native_palette_draw),
            checked = draft.nativePaletteDraw,
            onCheckedChange = { onDraftChange(draft.copy(nativePaletteDraw = it)) },
            helpText = stringResource(R.string.settings_help_native_palette_draw),
            onResetToDefault = { onDraftChange(draft.copy(nativePaletteDraw = defaultProfile.nativePaletteDraw)) }
        )
    }
}

@Composable
private fun eeCycleRateOptions(): List<Pair<Int, String>> = listOf(
    -3 to "50%",
    -2 to "60%",
    -1 to "75%",
    0 to "100%",
    1 to "130%",
    2 to "180%",
    3 to "300%"
)

@Composable
private fun eeCycleSkipOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_ee_cycle_disabled),
    1 to stringResource(R.string.settings_ee_cycle_mild),
    2 to stringResource(R.string.settings_ee_cycle_moderate),
    3 to stringResource(R.string.settings_ee_cycle_maximum)
)

@Composable
private fun floatRoundModeOptions(): List<Pair<Int, String>> = listOf(
    AppPreferences.FLOAT_ROUND_NEAREST to stringResource(R.string.settings_float_round_nearest),
    AppPreferences.FLOAT_ROUND_NEGATIVE to stringResource(R.string.settings_float_round_negative),
    AppPreferences.FLOAT_ROUND_POSITIVE to stringResource(R.string.settings_float_round_positive),
    AppPreferences.FLOAT_ROUND_CHOP to stringResource(R.string.settings_float_round_chop)
)

@Composable
private fun eeFpuClampingModeOptions(): List<Pair<Int, String>> = listOf(
    AppPreferences.CLAMPING_NONE to stringResource(R.string.settings_clamping_none),
    AppPreferences.CLAMPING_NORMAL to stringResource(R.string.settings_clamping_normal),
    AppPreferences.CLAMPING_EXTRA to stringResource(R.string.settings_clamping_extra),
    AppPreferences.CLAMPING_FULL to stringResource(R.string.settings_clamping_full)
)

@Composable
private fun vuClampingModeOptions(): List<Pair<Int, String>> = listOf(
    AppPreferences.CLAMPING_NONE to stringResource(R.string.settings_clamping_none),
    AppPreferences.CLAMPING_NORMAL to stringResource(R.string.settings_clamping_normal),
    AppPreferences.CLAMPING_EXTRA to stringResource(R.string.settings_clamping_extra),
    AppPreferences.CLAMPING_FULL to stringResource(R.string.settings_clamping_extra_sign)
)

@Composable
private fun blendingAccuracyOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_blending_accuracy_minimum),
    1 to stringResource(R.string.settings_blending_accuracy_basic),
    2 to stringResource(R.string.settings_blending_accuracy_medium),
    3 to stringResource(R.string.settings_blending_accuracy_high),
    4 to stringResource(R.string.settings_blending_accuracy_full),
    5 to stringResource(R.string.settings_blending_accuracy_maximum)
)

@Composable
private fun texturePreloadingOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_texture_preloading_none),
    1 to stringResource(R.string.settings_texture_preloading_partial),
    2 to stringResource(R.string.settings_texture_preloading_full)
)

@Composable
private fun anisotropicFilteringOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_aniso_off),
    2 to "2x",
    4 to "4x",
    8 to "8x",
    16 to "16x"
)

@Composable
private fun bilinearFilteringOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_bilinear_filtering_nearest),
    1 to stringResource(R.string.settings_bilinear_filtering_forced),
    2 to stringResource(R.string.settings_bilinear_filtering_ps2),
    3 to stringResource(R.string.settings_bilinear_filtering_no_sprite)
)

@Composable
private fun trilinearFilteringOptions(): List<Pair<Int, String>> = listOf(
    -1 to stringResource(R.string.settings_trilinear_filtering_auto),
    0 to stringResource(R.string.settings_trilinear_filtering_off),
    1 to stringResource(R.string.settings_trilinear_filtering_ps2),
    2 to stringResource(R.string.settings_trilinear_filtering_forced)
)

@Composable
private fun hwDownloadModeOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_hw_download_mode_accurate),
    1 to stringResource(R.string.settings_hw_download_mode_force_full),
    2 to stringResource(R.string.settings_hw_download_mode_no_readbacks),
    3 to stringResource(R.string.settings_hw_download_mode_unsynchronized),
    4 to stringResource(R.string.settings_hw_download_mode_disabled)
)

@Composable
private fun touchHapticsPresetOptions(): List<Pair<Int, String>> = listOf(
    AppPreferences.TOUCH_HAPTICS_PRESET_SOFT to stringResource(R.string.settings_touch_haptics_preset_soft),
    AppPreferences.TOUCH_HAPTICS_PRESET_BALANCED to stringResource(R.string.settings_touch_haptics_preset_balanced),
    AppPreferences.TOUCH_HAPTICS_PRESET_CRISP to stringResource(R.string.settings_touch_haptics_preset_crisp),
    AppPreferences.TOUCH_HAPTICS_PRESET_STRONG to stringResource(R.string.settings_touch_haptics_preset_strong)
)

@Composable
private fun gyroModeOptions(): List<Pair<Int, String>> = listOf(
    AppPreferences.GYRO_MODE_OFF to stringResource(R.string.settings_gyro_off),
    AppPreferences.GYRO_MODE_AIM to stringResource(R.string.settings_gyro_aim),
    AppPreferences.GYRO_MODE_STEERING to stringResource(R.string.settings_gyro_steering)
)

@Composable
private fun casModeOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_cas_mode_off),
    1 to stringResource(R.string.settings_cas_mode_sharpen_only),
    2 to stringResource(R.string.settings_cas_mode_sharpen_resize)
)

@Composable
private fun sgsrModeOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_sgsr_off),
    1 to stringResource(R.string.settings_sgsr_quality),
    2 to stringResource(R.string.settings_sgsr_balanced),
    3 to stringResource(R.string.settings_sgsr_performance)
)

@Composable
private fun tvShaderOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_tv_shader_none),
    1 to stringResource(R.string.settings_tv_shader_scanline),
    2 to stringResource(R.string.settings_tv_shader_diagonal),
    3 to stringResource(R.string.settings_tv_shader_triangular),
    4 to stringResource(R.string.settings_tv_shader_wave),
    5 to stringResource(R.string.settings_tv_shader_lottes_crt),
    6 to stringResource(R.string.settings_tv_shader_4x_rgss),
    7 to stringResource(R.string.settings_tv_shader_nx_agss)
)

@Composable
private fun deinterlacingOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_deinterlacing_automatic),
    1 to stringResource(R.string.settings_deinterlacing_off),
    2 to stringResource(R.string.settings_deinterlacing_weave_tff),
    3 to stringResource(R.string.settings_deinterlacing_weave_bff),
    4 to stringResource(R.string.settings_deinterlacing_bob_tff),
    5 to stringResource(R.string.settings_deinterlacing_bob_bff),
    6 to stringResource(R.string.settings_deinterlacing_blend_tff),
    7 to stringResource(R.string.settings_deinterlacing_blend_bff),
    8 to stringResource(R.string.settings_deinterlacing_adaptive_tff),
    9 to stringResource(R.string.settings_deinterlacing_adaptive_bff)
)

@Composable
private fun ditheringOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_dithering_off),
    1 to stringResource(R.string.settings_dithering_scaled),
    2 to stringResource(R.string.settings_dithering_unscaled),
    3 to stringResource(R.string.settings_dithering_force_32bit)
)

@Composable
private fun cpuSpriteRenderSizeOptions(): List<Pair<Int, String>> = (0..10).map { value ->
    value to if (value == 0) stringResource(R.string.settings_disabled_short) else value.toString()
}

@Composable
private fun cpuSpriteRenderLevelOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_cpu_sprite_render_level_sprites),
    1 to stringResource(R.string.settings_cpu_sprite_render_level_triangles),
    2 to stringResource(R.string.settings_cpu_sprite_render_level_blended)
)

@Composable
private fun softwareClutRenderOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_disabled_short),
    1 to stringResource(R.string.settings_normal_short),
    2 to stringResource(R.string.settings_aggressive_short)
)

@Composable
private fun gpuTargetClutOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_hw_download_mode_disabled),
    1 to stringResource(R.string.settings_gpu_target_clut_exact),
    2 to stringResource(R.string.settings_gpu_target_clut_inside)
)

@Composable
private fun autoFlushHardwareOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_hw_download_mode_disabled),
    1 to stringResource(R.string.settings_auto_flush_sprites),
    2 to stringResource(R.string.settings_auto_flush_all)
)

@Composable
private fun textureInsideRtOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_hw_download_mode_disabled),
    1 to stringResource(R.string.settings_texture_inside_rt_inside),
    2 to stringResource(R.string.settings_texture_inside_rt_merge)
)

@Composable
private fun halfPixelOffsetOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_half_pixel_off),
    1 to stringResource(R.string.settings_half_pixel_normal),
    2 to stringResource(R.string.settings_half_pixel_special),
    3 to stringResource(R.string.settings_half_pixel_special_aggressive),
    4 to stringResource(R.string.settings_half_pixel_native),
    5 to stringResource(R.string.settings_half_pixel_native_tex)
)

@Composable
private fun nativeScalingOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_native_scaling_off),
    1 to stringResource(R.string.settings_native_scaling_normal),
    2 to stringResource(R.string.settings_native_scaling_aggressive),
    3 to stringResource(R.string.settings_native_scaling_normal_maintain_upscale),
    4 to stringResource(R.string.settings_native_scaling_aggressive_maintain_upscale)
)

@Composable
private fun roundSpriteOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_half_pixel_off),
    1 to stringResource(R.string.settings_round_sprite_half),
    2 to stringResource(R.string.settings_round_sprite_full)
)

@Composable
private fun bilinearUpscaleOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_trilinear_filtering_auto),
    1 to stringResource(R.string.settings_bilinear_upscale_force_bilinear),
    2 to stringResource(R.string.settings_bilinear_upscale_force_nearest)
)

@Composable
@Suppress("unused")
private fun DialogWindowWidth(
    enabled: Boolean,
    widthFraction: Float,
    maxWidthDp: Int
) {
    val view = LocalView.current
    val density = LocalDensity.current
    val containerSize = LocalWindowInfo.current.containerSize
    val windowWidth = with(density) { containerSize.width.toDp() }
    val isLandscape = containerSize.width > containerSize.height

    SideEffect {
        val window = (view.parent as? DialogWindowProvider)?.window ?: return@SideEffect
        window.setGravity(Gravity.CENTER)
        window.setBackgroundDrawable(Color.TRANSPARENT.toDrawable())
        window.decorView.setPadding(0, 0, 0, 0)
        if (!enabled) {
            val attributes = window.attributes
            attributes.x = 0
            window.attributes = attributes
            window.setLayout(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT
            )
            return@SideEffect
        }

        val requestedWidthPx = with(density) {
            (windowWidth * widthFraction)
                .coerceAtMost(maxWidthDp.dp)
                .roundToPx()
        }
        val attributes = window.attributes
        attributes.x = landscapeCenterOffsetPx(view, isLandscape)
        window.attributes = attributes
        window.setLayout(requestedWidthPx, WindowManager.LayoutParams.WRAP_CONTENT)
    }
}

private fun landscapeCenterOffsetPx(view: View, isLandscape: Boolean): Int {
    if (!isLandscape) return 0
    val insets = view.rootWindowInsets ?: return 0
    val leftRight = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        val bars = insets.getInsets(
            android.view.WindowInsets.Type.systemBars() or
                android.view.WindowInsets.Type.displayCutout()
        )
        bars.left to bars.right
    } else {
        @Suppress("DEPRECATION")
        insets.systemWindowInsetLeft to insets.systemWindowInsetRight
    }
    return (leftRight.first - leftRight.second) / 2
}

private fun rendererLabel(renderer: Int): Int = when (normalizeManagerRenderer(renderer)) {
    12 -> R.string.settings_renderer_opengl
    13 -> R.string.settings_renderer_software
    else -> R.string.settings_renderer_vulkan
}

private fun normalizeManagerRenderer(renderer: Int): Int {
    return RendererDefaults.normalizeAndroidRenderer(renderer)
}

private fun isShadeBoostActive(
    brightness: Int,
    contrast: Int,
    saturation: Int,
    gamma: Int
): Boolean {
    return brightness != 50 || contrast != 50 || saturation != 50 || gamma != 50
}

private fun resolveManualTargetFps(currentTargetFps: Int, defaultTargetFps: Int): Int {
    return when {
        currentTargetFps > 0 -> currentTargetFps
        defaultTargetFps > 0 -> defaultTargetFps
        else -> 60
    }
}

private fun formatFramerateHz(value: Float): String {
    val rounded = kotlin.math.round(value * 100f) / 100f
    val whole = rounded.toInt()
    return if (rounded == whole.toFloat()) {
        "$whole Hz"
    } else {
        "$rounded Hz"
    }
}

private fun SettingsSnapshot.toPerGameSettings(game: GameItem): PerGameSettings {
    return PerGameSettings(
        gameKey = game.path,
        gameTitle = game.title,
        gameSerial = game.serial,
        renderer = renderer,
        upscaleMultiplier = upscaleMultiplier,
        aspectRatio = aspectRatio,
        showFps = showFps,
        fpsOverlayMode = fpsOverlayMode,
        racingMode = racingMode,
        touchHaptics = touchHaptics,
        touchHapticsPreset = touchHapticsPreset,
        gyroMode = gyroMode,
        gyroSensitivity = gyroSensitivity,
        gyroSmoothing = gyroSmoothing,
        gyroInvertX = gyroInvertX,
        gyroInvertY = gyroInvertY,
        gamepadRightStickUpToR2 = gamepadRightStickUpToR2,
        gamepadRightStickDownToL2 = gamepadRightStickDownToL2,
        gamepadButtonHaptics = gamepadButtonHaptics,
        pressureModifierAmount = pressureModifierAmount,
        autoSaveOnExit = false,
        autoLoadOnStart = false,
        enableFastBoot = enableFastBoot,
        enableInstantVu1 = enableInstantVu1,
        enableMtvu = enableMtvu,
        enableFastCdvd = enableFastCdvd,
        enableCheats = enableCheats,
        enableGameFixes = enableGameFixes,
        enableEeTimingHack = enableEeTimingHack,
        eeFpuRoundMode = eeFpuRoundMode,
        vu0RoundMode = vu0RoundMode,
        vu1RoundMode = vu1RoundMode,
        eeFpuClampingMode = eeFpuClampingMode,
        vu0ClampingMode = vu0ClampingMode,
        vu1ClampingMode = vu1ClampingMode,
        hwDownloadMode = hwDownloadMode,
        eeCycleRate = eeCycleRate,
        eeCycleSkip = eeCycleSkip,
        frameSkip = frameSkip,
        skipDuplicateFrames = skipDuplicateFrames,
        frameLimitEnabled = frameLimitEnabled,
        targetFps = targetFps,
        ntscFramerate = ntscFramerate,
        palFramerate = palFramerate,
        textureFiltering = textureFiltering,
        trilinearFiltering = trilinearFiltering,
        blendingAccuracy = blendingAccuracy,
        texturePreloading = texturePreloading,
        enableFxaa = enableFxaa,
        casMode = casMode,
        sgsrMode = sgsrMode,
        casSharpness = casSharpness,
        tvShader = tvShader,
        shadeBoostEnabled = shadeBoostEnabled,
        shadeBoostBrightness = shadeBoostBrightness,
        shadeBoostContrast = shadeBoostContrast,
        shadeBoostSaturation = shadeBoostSaturation,
        shadeBoostGamma = shadeBoostGamma,
        anisotropicFiltering = anisotropicFiltering,
        enableHwMipmapping = enableHwMipmapping,
        antiBlur = antiBlur,
        deinterlaceMode = deinterlaceMode,
        dithering = dithering,
        enableWidescreenPatches = enableWidescreenPatches,
        enableNoInterlacingPatches = enableNoInterlacingPatches,
        cpuSpriteRenderSize = cpuSpriteRenderSize,
        cpuSpriteRenderLevel = cpuSpriteRenderLevel,
        softwareClutRender = softwareClutRender,
        gpuTargetClutMode = gpuTargetClutMode,
        skipDrawStart = skipDrawStart,
        skipDrawEnd = skipDrawEnd,
        autoFlushHardware = autoFlushHardware,
        cpuFramebufferConversion = cpuFramebufferConversion,
        disableDepthConversion = disableDepthConversion,
        disableSafeFeatures = disableSafeFeatures,
        disableRenderFixes = disableRenderFixes,
        preloadFrameData = preloadFrameData,
        disablePartialInvalidation = disablePartialInvalidation,
        textureInsideRt = textureInsideRt,
        readTargetsOnClose = readTargetsOnClose,
        estimateTextureRegion = estimateTextureRegion,
        gpuPaletteConversion = gpuPaletteConversion,
        halfPixelOffset = halfPixelOffset,
        nativeScaling = nativeScaling,
        roundSprite = roundSprite,
        bilinearUpscale = bilinearUpscale,
        textureOffsetX = textureOffsetX,
        textureOffsetY = textureOffsetY,
        alignSprite = alignSprite,
        mergeSprite = mergeSprite,
        forceEvenSpritePosition = forceEvenSpritePosition,
        nativePaletteDraw = nativePaletteDraw
    )
}

private fun PerGameSettings.resolveAgainst(defaultProfile: PerGameSettings): PerGameSettings {
    val keys = providedKeys ?: return copy(providedKeys = null)
    fun <T> pick(key: String, current: T, fallback: T): T = if (key in keys) current else fallback
    return defaultProfile.copy(
        gameKey = gameKey,
        gameTitle = gameTitle,
        gameSerial = gameSerial,
        renderer = pick("renderer", renderer, defaultProfile.renderer),
        upscaleMultiplier = pick("upscaleMultiplier", upscaleMultiplier, defaultProfile.upscaleMultiplier),
        aspectRatio = pick("aspectRatio", aspectRatio, defaultProfile.aspectRatio),
        showFps = pick("showFps", showFps, defaultProfile.showFps),
        fpsOverlayMode = pick("fpsOverlayMode", fpsOverlayMode, defaultProfile.fpsOverlayMode),
        racingMode = pick("racingMode", racingMode, defaultProfile.racingMode),
        touchHaptics = pick("touchHaptics", touchHaptics, defaultProfile.touchHaptics),
        touchHapticsPreset = pick("touchHapticsPreset", touchHapticsPreset, defaultProfile.touchHapticsPreset),
        gyroMode = pick("gyroMode", gyroMode, defaultProfile.gyroMode),
        gyroSensitivity = pick("gyroSensitivity", gyroSensitivity, defaultProfile.gyroSensitivity),
        gyroSmoothing = pick("gyroSmoothing", gyroSmoothing, defaultProfile.gyroSmoothing),
        gyroInvertX = pick("gyroInvertX", gyroInvertX, defaultProfile.gyroInvertX),
        gyroInvertY = pick("gyroInvertY", gyroInvertY, defaultProfile.gyroInvertY),
        gamepadRightStickUpToR2 = pick("gamepadRightStickUpToR2", gamepadRightStickUpToR2, defaultProfile.gamepadRightStickUpToR2),
        gamepadRightStickDownToL2 = pick("gamepadRightStickDownToL2", gamepadRightStickDownToL2, defaultProfile.gamepadRightStickDownToL2),
        gamepadButtonHaptics = pick("gamepadButtonHaptics", gamepadButtonHaptics, defaultProfile.gamepadButtonHaptics),
        pressureModifierAmount = pick("pressureModifierAmount", pressureModifierAmount, defaultProfile.pressureModifierAmount),
        autoSaveOnExit = pick("autoSaveOnExit", autoSaveOnExit, defaultProfile.autoSaveOnExit),
        autoLoadOnStart = pick("autoLoadOnStart", autoLoadOnStart, defaultProfile.autoLoadOnStart),
        enableFastBoot = pick("enableFastBoot", enableFastBoot, defaultProfile.enableFastBoot),
        enableInstantVu1 = pick("enableInstantVu1", enableInstantVu1, defaultProfile.enableInstantVu1),
        enableMtvu = pick("enableMtvu", enableMtvu, defaultProfile.enableMtvu),
        enableFastCdvd = pick("enableFastCdvd", enableFastCdvd, defaultProfile.enableFastCdvd),
        enableCheats = pick("enableCheats", enableCheats, defaultProfile.enableCheats),
        enableGameFixes = pick("enableGameFixes", enableGameFixes, defaultProfile.enableGameFixes),
        enableEeTimingHack = pick("enableEeTimingHack", enableEeTimingHack, defaultProfile.enableEeTimingHack),
        eeFpuRoundMode = pick("eeFpuRoundMode", eeFpuRoundMode, defaultProfile.eeFpuRoundMode),
        vu0RoundMode = pick("vu0RoundMode", vu0RoundMode, defaultProfile.vu0RoundMode),
        vu1RoundMode = pick("vu1RoundMode", vu1RoundMode, defaultProfile.vu1RoundMode),
        eeFpuClampingMode = pick("eeFpuClampingMode", eeFpuClampingMode, defaultProfile.eeFpuClampingMode),
        vu0ClampingMode = pick("vu0ClampingMode", vu0ClampingMode, defaultProfile.vu0ClampingMode),
        vu1ClampingMode = pick("vu1ClampingMode", vu1ClampingMode, defaultProfile.vu1ClampingMode),
        hwDownloadMode = pick("hwDownloadMode", hwDownloadMode, defaultProfile.hwDownloadMode),
        eeCycleRate = pick("eeCycleRate", eeCycleRate, defaultProfile.eeCycleRate),
        eeCycleSkip = pick("eeCycleSkip", eeCycleSkip, defaultProfile.eeCycleSkip),
        frameSkip = pick("frameSkip", frameSkip, defaultProfile.frameSkip),
        skipDuplicateFrames = pick("skipDuplicateFrames", skipDuplicateFrames, defaultProfile.skipDuplicateFrames),
        frameLimitEnabled = pick("frameLimitEnabled", frameLimitEnabled, defaultProfile.frameLimitEnabled),
        targetFps = pick("targetFps", targetFps, defaultProfile.targetFps),
        ntscFramerate = pick("ntscFramerate", ntscFramerate, defaultProfile.ntscFramerate),
        palFramerate = pick("palFramerate", palFramerate, defaultProfile.palFramerate),
        textureFiltering = pick("textureFiltering", textureFiltering, defaultProfile.textureFiltering),
        trilinearFiltering = pick("trilinearFiltering", trilinearFiltering, defaultProfile.trilinearFiltering),
        blendingAccuracy = pick("blendingAccuracy", blendingAccuracy, defaultProfile.blendingAccuracy),
        texturePreloading = pick("texturePreloading", texturePreloading, defaultProfile.texturePreloading),
        enableFxaa = pick("enableFxaa", enableFxaa, defaultProfile.enableFxaa),
        casMode = pick("casMode", casMode, defaultProfile.casMode),
        sgsrMode = pick("sgsrMode", sgsrMode, defaultProfile.sgsrMode),
        casSharpness = pick("casSharpness", casSharpness, defaultProfile.casSharpness),
        tvShader = pick("tvShader", tvShader, defaultProfile.tvShader),
        shadeBoostEnabled = pick("shadeBoostEnabled", shadeBoostEnabled, defaultProfile.shadeBoostEnabled),
        shadeBoostBrightness = pick("shadeBoostBrightness", shadeBoostBrightness, defaultProfile.shadeBoostBrightness),
        shadeBoostContrast = pick("shadeBoostContrast", shadeBoostContrast, defaultProfile.shadeBoostContrast),
        shadeBoostSaturation = pick("shadeBoostSaturation", shadeBoostSaturation, defaultProfile.shadeBoostSaturation),
        shadeBoostGamma = pick("shadeBoostGamma", shadeBoostGamma, defaultProfile.shadeBoostGamma),
        anisotropicFiltering = pick("anisotropicFiltering", anisotropicFiltering, defaultProfile.anisotropicFiltering),
        enableHwMipmapping = pick("enableHwMipmapping", enableHwMipmapping, defaultProfile.enableHwMipmapping),
        antiBlur = pick("antiBlur", antiBlur, defaultProfile.antiBlur),
        deinterlaceMode = pick("deinterlaceMode", deinterlaceMode, defaultProfile.deinterlaceMode),
        dithering = pick("dithering", dithering, defaultProfile.dithering),
        enableWidescreenPatches = pick("enableWidescreenPatches", enableWidescreenPatches, defaultProfile.enableWidescreenPatches),
        enableNoInterlacingPatches = pick("enableNoInterlacingPatches", enableNoInterlacingPatches, defaultProfile.enableNoInterlacingPatches),
        cpuSpriteRenderSize = pick("cpuSpriteRenderSize", cpuSpriteRenderSize, defaultProfile.cpuSpriteRenderSize),
        cpuSpriteRenderLevel = pick("cpuSpriteRenderLevel", cpuSpriteRenderLevel, defaultProfile.cpuSpriteRenderLevel),
        softwareClutRender = pick("softwareClutRender", softwareClutRender, defaultProfile.softwareClutRender),
        gpuTargetClutMode = pick("gpuTargetClutMode", gpuTargetClutMode, defaultProfile.gpuTargetClutMode),
        skipDrawStart = pick("skipDrawStart", skipDrawStart, defaultProfile.skipDrawStart),
        skipDrawEnd = pick("skipDrawEnd", skipDrawEnd, defaultProfile.skipDrawEnd),
        autoFlushHardware = pick("autoFlushHardware", autoFlushHardware, defaultProfile.autoFlushHardware),
        cpuFramebufferConversion = pick("cpuFramebufferConversion", cpuFramebufferConversion, defaultProfile.cpuFramebufferConversion),
        disableDepthConversion = pick("disableDepthConversion", disableDepthConversion, defaultProfile.disableDepthConversion),
        disableSafeFeatures = pick("disableSafeFeatures", disableSafeFeatures, defaultProfile.disableSafeFeatures),
        disableRenderFixes = pick("disableRenderFixes", disableRenderFixes, defaultProfile.disableRenderFixes),
        preloadFrameData = pick("preloadFrameData", preloadFrameData, defaultProfile.preloadFrameData),
        disablePartialInvalidation = pick("disablePartialInvalidation", disablePartialInvalidation, defaultProfile.disablePartialInvalidation),
        textureInsideRt = pick("textureInsideRt", textureInsideRt, defaultProfile.textureInsideRt),
        readTargetsOnClose = pick("readTargetsOnClose", readTargetsOnClose, defaultProfile.readTargetsOnClose),
        estimateTextureRegion = pick("estimateTextureRegion", estimateTextureRegion, defaultProfile.estimateTextureRegion),
        gpuPaletteConversion = pick("gpuPaletteConversion", gpuPaletteConversion, defaultProfile.gpuPaletteConversion),
        halfPixelOffset = pick("halfPixelOffset", halfPixelOffset, defaultProfile.halfPixelOffset),
        nativeScaling = pick("nativeScaling", nativeScaling, defaultProfile.nativeScaling),
        roundSprite = pick("roundSprite", roundSprite, defaultProfile.roundSprite),
        bilinearUpscale = pick("bilinearUpscale", bilinearUpscale, defaultProfile.bilinearUpscale),
        textureOffsetX = pick("textureOffsetX", textureOffsetX, defaultProfile.textureOffsetX),
        textureOffsetY = pick("textureOffsetY", textureOffsetY, defaultProfile.textureOffsetY),
        alignSprite = pick("alignSprite", alignSprite, defaultProfile.alignSprite),
        mergeSprite = pick("mergeSprite", mergeSprite, defaultProfile.mergeSprite),
        forceEvenSpritePosition = pick("forceEvenSpritePosition", forceEvenSpritePosition, defaultProfile.forceEvenSpritePosition),
        nativePaletteDraw = pick("nativePaletteDraw", nativePaletteDraw, defaultProfile.nativePaletteDraw),
        touchControlVisualStyle = pick("touchControlVisualStyle", touchControlVisualStyle, defaultProfile.touchControlVisualStyle),
        touchControlPressEffect = pick("touchControlPressEffect", touchControlPressEffect, defaultProfile.touchControlPressEffect),
        providedKeys = null,
        updatedAt = updatedAt
    )
}

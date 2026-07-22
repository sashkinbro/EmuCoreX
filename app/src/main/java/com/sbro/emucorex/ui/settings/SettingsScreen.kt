package com.sbro.emucorex.ui.settings

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.animateScrollBy
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ExitToApp
import androidx.compose.material.icons.automirrored.rounded.VolumeOff
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Bolt
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.FastForward
import androidx.compose.material.icons.rounded.FormatSize
import androidx.compose.material.icons.rounded.Forum
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Home
import androidx.compose.material.icons.rounded.KeyboardArrowDown
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.RateReview
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SaveAs
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.ScreenRotation
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.StayPrimaryPortrait
import androidx.compose.material.icons.rounded.SwapHoriz
import androidx.compose.material.icons.rounded.SwapVert
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Vibration
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.Wallpaper
import androidx.compose.material.icons.rounded.WarningAmber
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.AndroidGyroscopeInput
import com.sbro.emucorex.core.AudioDefaults
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.GpuDriverCompatibility
import com.sbro.emucorex.core.GpuHardwareProfiles
import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.core.buildUpscaleOptions
import com.sbro.emucorex.core.upscaleKeyToMultiplier
import com.sbro.emucorex.core.upscaleMultiplierValue
import com.sbro.emucorex.core.utils.NetworkAdapterCollector
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppFontChoice
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.CheatFileEntry
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.CoverArtRepository
import com.sbro.emucorex.data.HomeBackgroundRepository
import com.sbro.emucorex.data.HomeBackgroundType
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.DrawerItemId
import com.sbro.emucorex.data.GameMenuTabId
import com.sbro.emucorex.data.GameMenuSectionId
import com.sbro.emucorex.data.GameMenuLayoutStyle
import com.sbro.emucorex.data.DrawerVisualStyle
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.data.OverlayLayoutSnapshot
import com.sbro.emucorex.data.PerformanceOverlayMetrics
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsBackupRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.ui.common.EmulatorDataLocationDialog
import com.sbro.emucorex.ui.home.calculateHomeGridColumnCount
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.ProvideGamepadShoulderActions
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.SettingsStyledDialog
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.skipGamepadTextFieldFocus
import com.sbro.emucorex.ui.customization.HomeBackgroundMedia
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

private enum class SettingsTab {
    General, Graphics, Controls, Emulation, Audio, Fixes, Library, Network, Customization, GameMenu, Updates, Pro, About
}

@OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    initialTab: String = "general",
    onBackClick: (() -> Unit)? = null,
    onOpenLanguageScreen: (() -> Unit)? = null,
    onOpenMemoryCardManager: (() -> Unit)? = null,
    onOpenGpuDriverManager: (() -> Unit)? = null,
    onOpenGameDbBrowser: (() -> Unit)? = null,
    onOpenControlsLayoutEditor: (() -> Unit)? = null,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 10.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    var selectedTab by rememberSaveable(initialTab) { mutableStateOf(initialTab.toSettingsTab()) }
    val cheatRepository = remember(context) { CheatRepository(context) }
    var cheatEntries by remember { mutableStateOf(cheatRepository.listImportedCheatFiles()) }
    val cheatEditorGameKey = remember { mutableStateOf<String?>(null) }
    val cheatEditorFileName = remember { mutableStateOf<String?>(null) }
    val cheatEditorText = remember { mutableStateOf("") }
    val pendingGamepadActionId = remember { mutableStateOf<String?>(null) }
    var pendingGamepadPadIndex by rememberSaveable { mutableIntStateOf(0) }
    var showTopBarMenu by remember { mutableStateOf(false) }
    val showResetAllSettingsDialog = remember { mutableStateOf(false) }
    var showBackupExportDialog by rememberSaveable { mutableStateOf(false) }
    var includeSaveStatesInBackup by rememberSaveable { mutableStateOf(false) }
    val showCoverUrlDialog = remember { mutableStateOf(false) }
    val showBiosDialog = remember { mutableStateOf(false) }
    var showEmulatorDataLocationDialog by remember { mutableStateOf(false) }
    val pendingCoverUrl = remember { mutableStateOf("") }
    var searchEnabled by remember { mutableStateOf(false) }
    var searchQuery by remember { mutableStateOf("") }
    val selectedTabFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }
    val scope = rememberCoroutineScope()
    val backupRepository = remember(context) {
        SettingsBackupRepository(
            context = context,
            preferences = AppPreferences(context),
            perGameSettingsRepository = PerGameSettingsRepository(context),
            cheatRepository = CheatRepository(context)
        )
    }
    val backupExportSuccessMessage = stringResource(R.string.settings_backup_export_success)
    val backupExportFailureMessage = stringResource(R.string.settings_backup_export_failed)
    val backupRestoreSuccessMessage = stringResource(R.string.settings_backup_restore_success)
    val backupRestoreFailureMessage = stringResource(R.string.settings_backup_restore_failed)
    val cheatsImportSuccessMessage = stringResource(R.string.settings_cheats_import_success)
    val cheatsImportFailureMessage = stringResource(R.string.settings_cheats_import_failed)
    val cheatsSavedMessage = stringResource(R.string.settings_cheats_saved)
    val cheatsDeletedMessage = stringResource(R.string.settings_cheats_deleted)
    val coverUrlCopiedMessage = stringResource(R.string.settings_cover_download_url_copied)
    val coverUrlInvalidMessage = stringResource(R.string.settings_cover_download_url_invalid)
    stringResource(R.string.settings_not_set)
    val settingsScrollState = rememberScrollState()
    val proPurchaseMessage = uiState.proPurchaseMessageResId?.let { stringResource(it) }
    LaunchedEffect(proPurchaseMessage) {
        val message = proPurchaseMessage ?: return@LaunchedEffect
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        viewModel.clearProPurchaseMessage()
    }
    val customizationMessage = uiState.customizationMessageResId?.let { stringResource(it) }
    LaunchedEffect(customizationMessage) {
        val message = customizationMessage ?: return@LaunchedEffect
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        viewModel.clearCustomizationMessage()
    }

    if (!uiState.isLoaded) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background),
            contentAlignment = Alignment.Center
        ) {
            CircularProgressIndicator()
        }
        return
    }

    LaunchedEffect(Unit) {
        viewModel.checkMediatekCompatibilityNotice()
    }

    val biosPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? -> uri?.let(viewModel::setBiosPath) }

    val gamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? -> uri?.let(viewModel::setGamePath) }

    val homeBackgroundPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? -> uri?.let(viewModel::installHomeBackground) }

    val customFontPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? -> uri?.let(viewModel::installCustomFont) }

    val launchBiosPicker = rememberDebouncedClick(onClick = { biosPicker.launch(arrayOf("*/*")) })
    val openBiosDialog = rememberDebouncedClick(onClick = { showBiosDialog.value = true })
    val launchGamePicker = rememberDebouncedClick(onClick = { gamePicker.launch(null) })
    val openEmulatorDataLocationDialog = rememberDebouncedClick(
        onClick = {
            viewModel.refreshEmulatorDataLocations()
            showEmulatorDataLocationDialog = true
        }
    )
    val openLanguageSheet = rememberDebouncedClick(onClick = { onOpenLanguageScreen?.invoke() })
    val refreshCheatEntries = remember {
        {
            cheatEntries = cheatRepository.listImportedCheatFiles()
        }
    }

    val settingsTabs = remember { SettingsTab.entries.toList() }
    fun selectRelativeTab(offset: Int) {
        val currentIndex = settingsTabs.indexOf(selectedTab).coerceAtLeast(0)
        selectedTab = settingsTabs[(currentIndex + offset + settingsTabs.size) % settingsTabs.size]
        searchEnabled = false
        searchQuery = ""
    }

    if (showEmulatorDataLocationDialog) {
        EmulatorDataLocationDialog(
            selectedLocation = EmulatorStorage.selectedStandardLocation(
                uiState.emulatorDataPath,
                uiState.sdCardDataPath
            ),
            sdCardAvailable = uiState.sdCardDataPath != null,
            onSelect = { location ->
                showEmulatorDataLocationDialog = false
                viewModel.setEmulatorDataLocation(location)
            },
            onDismiss = { showEmulatorDataLocationDialog = false }
        )
    }

    LaunchedEffect(selectedTab, shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus) {
            selectedTabFocusRequester.requestFocus()
        }
    }
    RequestFocusOnResume(
        focusRequester = selectedTabFocusRequester,
        enabled = shouldRequestGamepadFocus
    )
    DisposableEffect(pendingGamepadActionId.value) {
        val actionId = pendingGamepadActionId.value
        if (actionId != null) {
            GamepadManager.startBindingCapture(pendingGamepadPadIndex) { keyCode ->
                viewModel.setGamepadBinding(pendingGamepadPadIndex, actionId, keyCode)
                pendingGamepadActionId.value = null
            }
        } else {
            GamepadManager.cancelBindingCapture()
        }
        onDispose {
            GamepadManager.cancelBindingCapture()
        }
    }

    val settingsBackupExporter = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/zip")
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val success = withContext(Dispatchers.IO) {
                backupRepository.backup(
                    destination = uri,
                    includeSaveStates = includeSaveStatesInBackup
                )
            }
            Toast.makeText(
                context,
                if (success) backupExportSuccessMessage else backupExportFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
        }
    }
    val settingsBackupImporter = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val success = withContext(Dispatchers.IO) {
                backupRepository.restore(uri)
            }
            Toast.makeText(
                context,
                if (success) backupRestoreSuccessMessage else backupRestoreFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
        }
    }
    val cheatImporter = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val fileName = DocumentPathResolver.getDisplayName(context, uri.toString())
            val contents = runCatching {
                context.contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
            }.getOrNull()
            if (contents.isNullOrBlank()) {
                Toast.makeText(context, cheatsImportFailureMessage, Toast.LENGTH_SHORT).show()
                return@launch
            }
            val gameKey = fileName.substringBeforeLast('.').ifBlank { "cheat_${System.currentTimeMillis()}" }
            val blockCount = cheatRepository.importCheatFile(
                gameKey = gameKey,
                contents = contents,
                enableAllByDefault = true
            )
            if (blockCount <= 0) {
                Toast.makeText(context, cheatsImportFailureMessage, Toast.LENGTH_SHORT).show()
                return@launch
            }
            viewModel.setEnableCheats(true)
            refreshCheatEntries()
            Toast.makeText(context, cheatsImportSuccessMessage, Toast.LENGTH_SHORT).show()
        }
    }
    ProvideGamepadShoulderActions(
        enabled = !searchEnabled,
        onPrevious = { selectRelativeTab(-1) },
        onNext = { selectRelativeTab(1) }
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(settingsScrollState)
        ) {
            SettingsCompactTopBar(
                title = stringResource(R.string.settings_title),
                subtitle = if (searchEnabled) stringResource(R.string.settings_search_subtitle) else selectedTab.label(),
                topInset = topInset,
                onBackClick = onBackClick,
                menuExpanded = showTopBarMenu,
                onMenuExpandedChange = { showTopBarMenu = it },
                onResetAllSettingsClick = {
                    showTopBarMenu = false
                    showResetAllSettingsDialog.value = true
                },
                searchEnabled = searchEnabled,
                searchQuery = searchQuery,
                onSearchEnabledChange = {
                    searchEnabled = it
                    if (!it) searchQuery = ""
                },
                onSearchQueryChange = { searchQuery = it }
            )

            SettingsTabRow(
                selectedTab = selectedTab,
                onSelected = { tab ->
                    if (selectedTab == tab) return@SettingsTabRow
                    selectedTab = tab
                },
                selectedTabFocusRequester = selectedTabFocusRequester
            )

            SettingsContent(
                uiState = uiState,
                selectedTab = selectedTab,
                context = context,
                launchBiosPicker = openBiosDialog,
                launchGamePicker = launchGamePicker,
                openEmulatorDataLocationDialog = openEmulatorDataLocationDialog,
                launchHomeBackgroundPicker = {
                    homeBackgroundPicker.launch(arrayOf("image/*", "video/*"))
                },
                launchCustomFontPicker = {
                    customFontPicker.launch(
                        arrayOf(
                            "font/ttf",
                            "font/otf",
                            "application/x-font-ttf",
                            "application/x-font-opentype",
                            "application/vnd.ms-opentype",
                            "application/octet-stream"
                        )
                    )
                },
                onOpenCoverUrlEditor = {
                    pendingCoverUrl.value = uiState.coverDownloadBaseUrl.orEmpty()
                    showCoverUrlDialog.value = true
                },
                launchSettingsBackupExport = {
                    includeSaveStatesInBackup = false
                    showBackupExportDialog = true
                },
                launchSettingsBackupImport = { settingsBackupImporter.launch(arrayOf("application/zip", "*/*")) },
                launchCheatImport = { cheatImporter.launch(arrayOf("*/*")) },
                openLanguageSheet = openLanguageSheet,
                cheatEntries = cheatEntries,
                onOpenCheatEditor = { gameKey ->
                    cheatEditorGameKey.value = gameKey
                    cheatEditorFileName.value = cheatRepository.listImportedCheatFiles()
                        .firstOrNull { it.gameKey == gameKey }
                        ?.fileName ?: "$gameKey.pnach"
                    cheatEditorText.value = cheatRepository.getImportedCheatText(gameKey).orEmpty()
                },
                onRequestGamepadBinding = { padIndex, actionId ->
                    pendingGamepadPadIndex = padIndex
                    pendingGamepadActionId.value = actionId
                },
                searchQuery = searchQuery,
                onSearchResultSelected = { tab ->
                    selectedTab = tab
                    searchEnabled = false
                    searchQuery = ""
                },
                onOpenMemoryCardManager = onOpenMemoryCardManager,
                onOpenGpuDriverManager = onOpenGpuDriverManager,
                onOpenGameDbBrowser = onOpenGameDbBrowser,
                onOpenControlsLayoutEditor = onOpenControlsLayoutEditor,
                viewModel = viewModel,
                topInset = 0.dp,
                modifier = Modifier
                    .fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(bottomInset))
        }
    }

    if (cheatEditorGameKey.value != null) {
        CheatEditorSheet(
            fileName = cheatEditorFileName.value.orEmpty(),
            value = cheatEditorText.value,
            onValueChange = { cheatEditorText.value = it },
            onDismiss = {
                cheatEditorGameKey.value = null
                cheatEditorFileName.value = null
                cheatEditorText.value = ""
            },
            onSave = {
                cheatEditorGameKey.value?.let { gameKey ->
                    val updatedBlocks = cheatRepository.updateImportedCheatText(gameKey, cheatEditorText.value)
                    if (updatedBlocks > 0) {
                        refreshCheatEntries()
                        Toast.makeText(context, cheatsSavedMessage, Toast.LENGTH_SHORT).show()
                        cheatEditorGameKey.value = null
                        cheatEditorFileName.value = null
                        cheatEditorText.value = ""
                    } else {
                        Toast.makeText(context, cheatsImportFailureMessage, Toast.LENGTH_SHORT).show()
                    }
                }
            },
            onDelete = {
                cheatEditorGameKey.value?.let { gameKey ->
                    cheatRepository.deleteImportedCheats(gameKey, null, null)
                    refreshCheatEntries()
                    Toast.makeText(context, cheatsDeletedMessage, Toast.LENGTH_SHORT).show()
                    cheatEditorGameKey.value = null
                    cheatEditorFileName.value = null
                    cheatEditorText.value = ""
                }
            }
        )
    }

    if (uiState.showMediatekCompatibilityNotice) {
        AlertDialog(
            onDismissRequest = viewModel::dismissMediatekCompatibilityNotice,
            icon = {
                Icon(
                    imageVector = Icons.Rounded.WarningAmber,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.tertiary
                )
            },
            title = {
                Text(stringResource(R.string.settings_mediatek_notice_title))
            },
            text = {
                Text(stringResource(R.string.settings_mediatek_notice_message))
            },
            confirmButton = {
                TextButton(onClick = viewModel::dismissMediatekCompatibilityNotice) {
                    Text(stringResource(android.R.string.ok))
                }
            }
        )
    }

    if (pendingGamepadActionId.value != null) {
        val dialogFocusRequester = remember { FocusRequester() }

        LaunchedEffect(Unit) {
            dialogFocusRequester.requestFocus()
        }

        AlertDialog(
            onDismissRequest = { pendingGamepadActionId.value = null },
            modifier = Modifier
                .focusRequester(dialogFocusRequester)
                .focusable()
                .onPreviewKeyEvent { keyEvent ->
                    GamepadManager.handleBindingCapture(keyEvent.nativeKeyEvent)
                },
            title = {
                Text(stringResource(R.string.settings_gamepad_mapping_listening_title))
            },
            text = {
                Text(
                    stringResource(
                        R.string.settings_gamepad_mapping_listening_player_desc,
                        gamepadPlayerLabel(pendingGamepadPadIndex),
                        gamepadActionLabel(pendingGamepadActionId.value.orEmpty())
                    )
                )
            },
            confirmButton = {
                TextButton(onClick = { pendingGamepadActionId.value = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showResetAllSettingsDialog.value) {
        AlertDialog(
            onDismissRequest = { showResetAllSettingsDialog.value = false },
            title = {
                Text(stringResource(R.string.settings_reset_all_title))
            },
            text = {
                Text(stringResource(R.string.settings_reset_all_confirm))
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        showResetAllSettingsDialog.value = false
                        viewModel.resetAllSettings()
                    }
                ) {
                    Text(stringResource(R.string.settings_reset_all_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showResetAllSettingsDialog.value = false }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showBackupExportDialog) {
        SettingsStyledDialog(
            title = stringResource(R.string.settings_backup_export_title),
            eyebrow = stringResource(R.string.settings_backup_section_title),
            icon = Icons.Rounded.Save,
            onDismissRequest = { showBackupExportDialog = false },
        ) {
            Text(
                text = stringResource(R.string.settings_backup_export_desc),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        includeSaveStatesInBackup = !includeSaveStatesInBackup
                    },
                shape = RoundedCornerShape(18.dp),
                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.26f),
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.primary.copy(alpha = 0.22f)
                )
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 13.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.SaveAs,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(22.dp)
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = stringResource(R.string.settings_backup_include_save_states),
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = stringResource(R.string.settings_backup_include_save_states_desc),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(
                        checked = includeSaveStatesInBackup,
                        onCheckedChange = { includeSaveStatesInBackup = it }
                    )
                }
            }

            Button(
                onClick = {
                    showBackupExportDialog = false
                    settingsBackupExporter.launch("emucorex-settings-backup.zip")
                },
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(
                    text = stringResource(R.string.settings_backup_export_action),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(vertical = 6.dp)
                )
            }
            TextButton(
                onClick = { showBackupExportDialog = false },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(stringResource(android.R.string.cancel))
            }
        }
    }

    if (showBiosDialog.value) {
        val biosDisplayName = uiState.biosPath?.let { DocumentPathResolver.getFallbackDisplayName(it) }
            ?: stringResource(R.string.settings_not_set)
        AlertDialog(
            onDismissRequest = { showBiosDialog.value = false },
            title = {
                Text(stringResource(R.string.settings_bios_picker_title))
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        text = stringResource(R.string.settings_bios_picker_desc),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHighest.copy(alpha = 0.55f)
                    ) {
                        Column(
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            Text(
                                text = stringResource(R.string.settings_bios_picker_current),
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = biosDisplayName,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        showBiosDialog.value = false
                        launchBiosPicker()
                    }
                ) {
                    Text(stringResource(R.string.settings_bios_picker_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showBiosDialog.value = false }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showCoverUrlDialog.value) {
        val coverUrlFocusRequester = remember { FocusRequester() }
        val exampleBundle = remember {
            "${CoverArtRepository.DEFAULT_COVER_BASE_URL} ${CoverArtRepository.DEFAULT_COVER_3D_BASE_URL}"
        }
        LaunchedEffect(showCoverUrlDialog.value) {
            if (showCoverUrlDialog.value) {
                coverUrlFocusRequester.requestFocus()
            }
        }
        AlertDialog(
            onDismissRequest = { showCoverUrlDialog.value = false },
            title = {
                Text(stringResource(R.string.settings_cover_download_url_dialog_title))
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        text = stringResource(R.string.settings_cover_download_url_dialog_body),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    OutlinedTextField(
                        value = pendingCoverUrl.value,
                        onValueChange = { pendingCoverUrl.value = it },
                        modifier = Modifier
                            .fillMaxWidth()
                            .focusRequester(coverUrlFocusRequester),
                        minLines = 2,
                        maxLines = 4,
                        shape = RoundedCornerShape(18.dp),
                        label = { Text(stringResource(R.string.settings_cover_download_url)) },
                        placeholder = { Text(stringResource(R.string.settings_cover_download_url_placeholder)) }
                    )
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHighest.copy(alpha = 0.55f)
                    ) {
                        Column(
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                text = stringResource(R.string.settings_cover_download_url_example),
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            CoverUrlExampleRow(
                                label = stringResource(R.string.settings_cover_download_url_example_hint),
                                onClick = {
                                    pendingCoverUrl.value = exampleBundle
                                    scope.launch { coverUrlFocusRequester.requestFocus() }
                                },
                                onLongClick = {
                                    val clipboardManager = context.getSystemService(android.content.ClipboardManager::class.java)
                                    clipboardManager?.setPrimaryClip(
                                        ClipData.newPlainText("cover_urls", exampleBundle)
                                    )
                                    Toast.makeText(
                                        context,
                                        coverUrlCopiedMessage,
                                        Toast.LENGTH_SHORT
                                    ).show()
                                }
                            )
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        val parts = pendingCoverUrl.value.trim()
                            .split(Regex("\\s+"))
                            .filter { it.isNotBlank() }
                        val value = parts.joinToString(" ")
                        val hasInvalidPart = parts.any {
                            !it.startsWith("http://") && !it.startsWith("https://")
                        }
                        if (hasInvalidPart || parts.size > 2) {
                            Toast.makeText(
                                context,
                                coverUrlInvalidMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                            return@TextButton
                        }
                        viewModel.setCoverDownloadBaseUrl(value.ifBlank { null })
                        showCoverUrlDialog.value = false
                    }
                ) {
                    Text(stringResource(R.string.save))
                }
            },
            dismissButton = {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(
                        onClick = {
                            pendingCoverUrl.value = ""
                            viewModel.setCoverDownloadBaseUrl(null)
                            showCoverUrlDialog.value = false
                        }
                    ) {
                        Text(stringResource(R.string.settings_cover_download_url_use_default))
                    }
                    TextButton(onClick = { showCoverUrlDialog.value = false }) {
                        Text(stringResource(android.R.string.cancel))
                    }
                }
            }
        )
    }
}

@Composable
private fun SettingsCompactTopBar(
    title: String,
    subtitle: String,
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: (() -> Unit)?,
    menuExpanded: Boolean,
    onMenuExpandedChange: (Boolean) -> Unit,
    onResetAllSettingsClick: () -> Unit,
    searchEnabled: Boolean,
    searchQuery: String,
    onSearchEnabledChange: (Boolean) -> Unit,
    onSearchQueryChange: (String) -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset + 8.dp,
                bottom = 4.dp
            ),
        shape = RoundedCornerShape(26.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        shadowElevation = 6.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (onBackClick != null) {
                NavigationBackButton(
                    onClick = onBackClick,
                    contentColor = MaterialTheme.colorScheme.onSurface
                )
            } else {
                Spacer(modifier = Modifier.width(12.dp))
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 14.dp, end = 8.dp)
            ) {
                if (searchEnabled) {
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = onSearchQueryChange,
                        modifier = Modifier
                            .fillMaxWidth()
                            .skipGamepadTextFieldFocus(),
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp),
                        placeholder = { Text(stringResource(R.string.settings_search_placeholder)) }
                    )
                } else {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = subtitle,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            IconButton(onClick = { onSearchEnabledChange(!searchEnabled) }) {
                Icon(
                    imageVector = if (searchEnabled) Icons.Rounded.Close else Icons.Rounded.Search,
                    contentDescription = stringResource(R.string.settings_search),
                    tint = MaterialTheme.colorScheme.onSurface
                )
            }
            Box {
                IconButton(onClick = { onMenuExpandedChange(true) }) {
                    Icon(
                        imageVector = Icons.Rounded.MoreVert,
                        contentDescription = stringResource(R.string.settings_more_options),
                        tint = MaterialTheme.colorScheme.onSurface
                    )
                }
                DropdownMenu(
                    expanded = menuExpanded,
                    onDismissRequest = { onMenuExpandedChange(false) },
                    shape = RoundedCornerShape(20.dp)
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.settings_reset_all_action)) },
                        onClick = onResetAllSettingsClick
                    )
                }
            }
        }
    }
}

@Composable
private fun SettingsTabRow(
    selectedTab: SettingsTab,
    onSelected: (SettingsTab) -> Unit,
    selectedTabFocusRequester: FocusRequester
) {
    val tabs = remember { SettingsTab.entries.toList() }
    val listState = rememberLazyListState()
    Box(modifier = Modifier.fillMaxWidth()) {
        LaunchedEffect(selectedTab) {
            val selectedIndex = tabs.indexOf(selectedTab).coerceAtLeast(0)
            var selectedItem = listState.layoutInfo.visibleItemsInfo
                .firstOrNull { it.index == selectedIndex }
            if (selectedItem == null) {
                listState.scrollToItem(selectedIndex)
                withFrameNanos { }
                selectedItem = listState.layoutInfo.visibleItemsInfo
                    .firstOrNull { it.index == selectedIndex }
            }

            selectedItem?.let { item ->
                val layoutInfo = listState.layoutInfo
                val delta = centeredTabScrollDelta(
                    itemOffset = item.offset,
                    itemSize = item.size,
                    viewportStart = layoutInfo.viewportStartOffset,
                    viewportEnd = layoutInfo.viewportEndOffset
                )
                if (kotlin.math.abs(delta) > 1f) {
                    listState.animateScrollBy(delta)
                }
            }
        }

        LazyRow(
            state = listState,
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 6.dp),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding
            ),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(items = tabs, key = { it.name }) { tab ->
                val interactionSource = remember { MutableInteractionSource() }
                FilterChip(
                    modifier = if (tab == selectedTab) {
                        Modifier.focusRequester(selectedTabFocusRequester)
                    } else {
                        Modifier
                    },
                    selected = selectedTab == tab,
                    onClick = { onSelected(tab) },
                    interactionSource = interactionSource,
                    colors = premiumFilterChipColors(),
                    label = { Text(tab.label()) },
                    leadingIcon = {
                        Icon(
                            imageVector = tab.icon(),
                            contentDescription = null,
                            modifier = Modifier.size(16.dp)
                        )
                    }
                )
            }
        }
    }
}

@Composable
private fun SettingsContent(
    uiState: SettingsUiState,
    selectedTab: SettingsTab,
    searchQuery: String,
    context: android.content.Context,
    launchBiosPicker: () -> Unit,
    launchGamePicker: () -> Unit,
    openEmulatorDataLocationDialog: () -> Unit,
    launchHomeBackgroundPicker: () -> Unit,
    launchCustomFontPicker: () -> Unit,
    onOpenCoverUrlEditor: () -> Unit,
    launchSettingsBackupExport: () -> Unit,
    launchSettingsBackupImport: () -> Unit,
    launchCheatImport: () -> Unit,
    openLanguageSheet: () -> Unit,
    cheatEntries: List<CheatFileEntry>,
    onOpenCheatEditor: (String) -> Unit,
    onRequestGamepadBinding: (Int, String) -> Unit,
    onSearchResultSelected: (SettingsTab) -> Unit,
    viewModel: SettingsViewModel,
    topInset: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
    onOpenMemoryCardManager: (() -> Unit)? = null,
    onOpenGpuDriverManager: (() -> Unit)? = null,
    onOpenGameDbBrowser: (() -> Unit)? = null,
    onOpenControlsLayoutEditor: (() -> Unit)? = null
) {
    val gamepadActions = remember { GamepadManager.mappableButtonActions() }
    val defaults = remember { SettingsSnapshot() }
    val overlayDefaults = remember { OverlayLayoutSnapshot() }
    val searchEntries = rememberSettingsSearchEntries()
    val notSetLabel = stringResource(R.string.settings_not_set)
    var selectedGamepadPadIndex by rememberSaveable { mutableIntStateOf(0) }
    Box(
        modifier = modifier
            .fillMaxWidth()
            .imePadding()
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = topInset + 12.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            if (searchQuery.isNotBlank()) {
                SettingsSearchResults(
                    query = searchQuery,
                    entries = searchEntries,
                    onOpen = onSearchResultSelected
                )
            } else when (selectedTab) {
                SettingsTab.General -> {
                    SettingsSection(title = stringResource(R.string.settings_general_tab)) {
                        SettingsItem(
                            icon = Icons.Rounded.Language,
                            label = stringResource(R.string.settings_language),
                            value = languageLabel(uiState.languageTag),
                            onClick = openLanguageSheet
                        )
                        ThemeSelector(
                            selected = uiState.themeMode,
                            isProUnlocked = uiState.isProUnlocked,
                            onSelected = viewModel::setThemeMode,
                            onProLockedSelected = {
                                (context as? Activity)?.let(viewModel::purchasePro)
                            }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.StayPrimaryPortrait,
                            title = stringResource(R.string.settings_keep_screen_on),
                            subtitle = stringResource(R.string.settings_keep_screen_on_desc),
                            checked = uiState.keepScreenOn,
                            onCheckedChange = viewModel::setKeepScreenOn,
                            helpText = stringResource(R.string.settings_help_keep_screen_on),
                            onResetToDefault = { viewModel.setKeepScreenOn(defaults.keepScreenOn) }
                        )
                        ToggleItem(
                            icon = Icons.AutoMirrored.Rounded.ExitToApp,
                            title = stringResource(R.string.settings_back_button_exits_game),
                            subtitle = stringResource(R.string.settings_back_button_exits_game_desc),
                            checked = uiState.backButtonExitsGame,
                            onCheckedChange = viewModel::setBackButtonExitsGame,
                            helpText = stringResource(R.string.settings_help_back_button_exits_game),
                            onResetToDefault = { viewModel.setBackButtonExitsGame(defaults.backButtonExitsGame) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Save,
                            title = stringResource(R.string.settings_confirm_save_load_actions),
                            subtitle = stringResource(R.string.settings_confirm_save_load_actions_desc),
                            checked = uiState.confirmSaveLoadActions,
                            onCheckedChange = viewModel::setConfirmSaveLoadActions,
                            helpText = stringResource(R.string.settings_help_confirm_save_load_actions),
                            onResetToDefault = { viewModel.setConfirmSaveLoadActions(defaults.confirmSaveLoadActions) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_show_recent_games),
                            subtitle = stringResource(R.string.settings_show_recent_games_desc),
                            checked = uiState.showRecentGames,
                            onCheckedChange = viewModel::setShowRecentGames,
                            helpText = stringResource(R.string.settings_help_recent_games),
                            onResetToDefault = { viewModel.setShowRecentGames(defaults.showRecentGames) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Search,
                            title = stringResource(R.string.settings_show_home_search),
                            subtitle = stringResource(R.string.settings_show_home_search_desc),
                            checked = uiState.showHomeSearch,
                            onCheckedChange = viewModel::setShowHomeSearch,
                            helpText = stringResource(R.string.settings_help_home_search),
                            onResetToDefault = { viewModel.setShowHomeSearch(defaults.showHomeSearch) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            title = stringResource(R.string.settings_show_debug_options),
                            subtitle = stringResource(R.string.settings_show_debug_options_desc),
                            checked = uiState.showDebugOptions,
                            onCheckedChange = viewModel::setShowDebugOptions,
                            helpText = stringResource(R.string.settings_help_debug_options),
                            onResetToDefault = { viewModel.setShowDebugOptions(defaults.showDebugOptions) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Language,
                            title = stringResource(R.string.settings_prefer_english_game_titles),
                            subtitle = stringResource(R.string.settings_prefer_english_game_titles_desc),
                            checked = uiState.preferEnglishGameTitles,
                            onCheckedChange = viewModel::setPreferEnglishGameTitles,
                            helpText = stringResource(R.string.settings_help_prefer_english_game_titles),
                            onResetToDefault = { viewModel.setPreferEnglishGameTitles(defaults.preferEnglishGameTitles) }
                        )
                    }
                }

                SettingsTab.Customization -> {
                    CustomizationSettingsTab(
                        uiState = uiState,
                        onPickBackground = launchHomeBackgroundPicker,
                        onPickCustomFont = launchCustomFontPicker,
                        viewModel = viewModel
                    )
                }

                SettingsTab.GameMenu -> {
                    GameMenuSettingsTab(uiState = uiState, viewModel = viewModel)
                }

                SettingsTab.Graphics -> {
                    val mediatekAngleAvailable = remember { EmulatorBridge.isBundledAngleAvailable() }
                    SettingsSection(title = stringResource(R.string.settings_graphics)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_renderer),
                            options = listOf(
                                12 to stringResource(R.string.settings_renderer_opengl),
                                14 to stringResource(R.string.settings_renderer_vulkan),
                                13 to stringResource(R.string.settings_renderer_software)
                            ),
                            selectedValue = uiState.renderer,
                            onSelect = viewModel::setRenderer,
                            helpText = stringResource(R.string.settings_help_renderer),
                            onResetToDefault = { viewModel.setRenderer(defaults.renderer) }
                        )
                        if (GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers() && !GpuHardwareProfiles.isMediaTekHardware()) {
                            val activeDriverName = uiState.customDriverPath
                                ?.takeIf { uiState.gpuDriverType == 1 }
                                ?.let { java.io.File(it).parentFile?.name ?: java.io.File(it).name }
                            SettingsItem(
                                icon = Icons.Rounded.Tune,
                                label = stringResource(R.string.settings_gpu_driver_manager_title),
                                value = activeDriverName ?: stringResource(R.string.settings_gpu_driver_system),
                                onClick = onOpenGpuDriverManager ?: {}
                            )
                        }
                        if (GpuHardwareProfiles.isMediaTekHardware() && mediatekAngleAvailable) {
                            ToggleItem(
                                icon = Icons.Rounded.SettingsSuggest,
                                title = stringResource(R.string.settings_mediatek_angle_opengl),
                                subtitle = stringResource(R.string.settings_mediatek_angle_opengl_desc),
                                checked = uiState.mediatekAngleOpenGl,
                                onCheckedChange = viewModel::setMediatekAngleOpenGl,
                                helpText = stringResource(R.string.settings_help_mediatek_angle_opengl),
                                onResetToDefault = { viewModel.setMediatekAngleOpenGl(false) }
                            )
                        }
                        val maxUpscaleMultiplier = remember(uiState.renderer) {
                            EmulatorBridge.getMaxUpscaleMultiplier(uiState.renderer)
                        }
                        val nativeUpscaleLabel = stringResource(R.string.settings_upscale_native)
                        ChoiceSection(
                            title = stringResource(R.string.settings_upscale),
                            options = buildUpscaleOptions(nativeUpscaleLabel, maxUpscaleMultiplier),
                            selectedValue = upscaleMultiplierValue(uiState.upscaleMultiplier),
                            onSelect = { viewModel.setUpscaleMultiplier(upscaleKeyToMultiplier(it)) },
                            helpText = stringResource(R.string.settings_help_upscale),
                            onResetToDefault = { viewModel.setUpscaleMultiplier(defaults.upscaleMultiplier) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_aspect_ratio),
                            options = listOf(
                                1 to stringResource(R.string.settings_aspect_ratio_auto),
                                2 to stringResource(R.string.settings_aspect_ratio_43),
                                3 to stringResource(R.string.settings_aspect_ratio_169),
                                4 to stringResource(R.string.settings_aspect_ratio_107),
                                0 to stringResource(R.string.emulation_aspect_stretch)
                            ),
                            selectedValue = uiState.aspectRatio,
                            onSelect = viewModel::setAspectRatio,
                            helpText = stringResource(R.string.settings_help_aspect_ratio),
                            onResetToDefault = { viewModel.setAspectRatio(defaults.aspectRatio) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_anisotropic_filtering),
                            options = listOf(
                                0 to stringResource(R.string.settings_aniso_off),
                                2 to "2x",
                                4 to "4x",
                                8 to "8x",
                                16 to "16x"
                            ),
                            selectedValue = uiState.anisotropicFiltering,
                            onSelect = viewModel::setAnisotropicFiltering,
                            helpText = stringResource(R.string.settings_help_anisotropic_filtering),
                            onResetToDefault = { viewModel.setAnisotropicFiltering(defaults.anisotropicFiltering) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_fxaa),
                            subtitle = stringResource(R.string.settings_fxaa_desc),
                            checked = uiState.enableFxaa,
                            onCheckedChange = viewModel::setEnableFxaa,
                            helpText = stringResource(R.string.settings_help_fxaa),
                            onResetToDefault = { viewModel.setEnableFxaa(defaults.enableFxaa) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_sgsr),
                            options = sgsrModeOptions(),
                            selectedValue = uiState.sgsrMode,
                            onSelect = viewModel::setSgsrMode,
                            helpText = stringResource(R.string.settings_help_sgsr),
                            onResetToDefault = { viewModel.setSgsrMode(defaults.sgsrMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_cas),
                            options = casModeOptions(),
                            selectedValue = uiState.casMode,
                            onSelect = viewModel::setCasMode,
                            helpText = stringResource(R.string.settings_help_cas),
                            onResetToDefault = { viewModel.setCasMode(defaults.casMode) }
                        )
                        if (uiState.casMode != 0) {
                            SliderItem(
                                icon = Icons.Rounded.GraphicEq,
                                title = stringResource(R.string.settings_cas_sharpness),
                                subtitle = stringResource(
                                    R.string.settings_cas_sharpness_value,
                                    uiState.casSharpness
                                ),
                                value = uiState.casSharpness.toFloat(),
                                range = 0f..100f,
                                steps = 99,
                                onValueChange = { viewModel.setCasSharpness(it.toInt()) },
                                helpText = stringResource(R.string.settings_help_cas_sharpness),
                                onResetToDefault = { viewModel.setCasSharpness(defaults.casSharpness) }
                            )
                        }
                        ChoiceSection(
                            title = stringResource(R.string.settings_tv_shader),
                            options = tvShaderOptions(),
                            selectedValue = uiState.tvShader,
                            onSelect = viewModel::setTvShader,
                            helpText = stringResource(R.string.settings_help_tv_shader),
                            onResetToDefault = { viewModel.setTvShader(defaults.tvShader) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_rendering_section)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_hw_download_mode),
                            options = hwDownloadModeOptions(),
                            selectedValue = uiState.hwDownloadMode,
                            onSelect = viewModel::setHwDownloadMode,
                            helpText = stringResource(R.string.settings_help_hw_download_mode),
                            onResetToDefault = { viewModel.setHwDownloadMode(defaults.hwDownloadMode) }
                        )
                        SettingsInlineNote(
                            text = stringResource(R.string.settings_hw_download_mode_desc)
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_blending_accuracy),
                            options = blendingAccuracyOptions(),
                            selectedValue = uiState.blendingAccuracy,
                            onSelect = viewModel::setBlendingAccuracy,
                            helpText = stringResource(R.string.settings_help_blending_accuracy),
                            onResetToDefault = {
                                viewModel.setBlendingAccuracy(defaults.blendingAccuracy)
                            }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_texture_preloading),
                            options = texturePreloadingOptions(),
                            selectedValue = uiState.texturePreloading,
                            onSelect = viewModel::setTexturePreloading,
                            helpText = stringResource(R.string.settings_help_texture_preloading),
                            onResetToDefault = { viewModel.setTexturePreloading(defaults.texturePreloading) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_bilinear_filtering),
                            options = bilinearFilteringOptions(),
                            selectedValue = uiState.textureFiltering,
                            onSelect = viewModel::setTextureFiltering,
                            helpText = stringResource(R.string.settings_help_bilinear_filtering),
                            onResetToDefault = { viewModel.setTextureFiltering(defaults.textureFiltering) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_trilinear_filtering),
                            options = trilinearFilteringOptions(),
                            selectedValue = uiState.trilinearFiltering,
                            onSelect = viewModel::setTrilinearFiltering,
                            helpText = stringResource(R.string.settings_help_trilinear_filtering),
                            onResetToDefault = { viewModel.setTrilinearFiltering(defaults.trilinearFiltering) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_hw_mipmapping),
                            subtitle = stringResource(R.string.settings_hw_mipmapping_desc),
                            checked = uiState.enableHwMipmapping,
                            onCheckedChange = viewModel::setEnableHwMipmapping,
                            helpText = stringResource(R.string.settings_help_hw_mipmapping),
                            onResetToDefault = { viewModel.setEnableHwMipmapping(defaults.enableHwMipmapping) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.emulation_screen_tab)) {
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_shadeboost_brightness),
                            subtitle = uiState.shadeBoostBrightness.toString(),
                            valueLabel = { it.roundToInt().toString() },
                            value = uiState.shadeBoostBrightness.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { viewModel.setShadeBoostBrightness(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_brightness),
                            onResetToDefault = { viewModel.setShadeBoostBrightness(defaults.shadeBoostBrightness) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_shadeboost_contrast),
                            subtitle = uiState.shadeBoostContrast.toString(),
                            valueLabel = { it.roundToInt().toString() },
                            value = uiState.shadeBoostContrast.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { viewModel.setShadeBoostContrast(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_contrast),
                            onResetToDefault = { viewModel.setShadeBoostContrast(defaults.shadeBoostContrast) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_shadeboost_saturation),
                            subtitle = uiState.shadeBoostSaturation.toString(),
                            valueLabel = { it.roundToInt().toString() },
                            value = uiState.shadeBoostSaturation.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { viewModel.setShadeBoostSaturation(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_saturation),
                            onResetToDefault = { viewModel.setShadeBoostSaturation(defaults.shadeBoostSaturation) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_shadeboost_gamma),
                            subtitle = uiState.shadeBoostGamma.toString(),
                            valueLabel = { it.roundToInt().toString() },
                            value = uiState.shadeBoostGamma.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { viewModel.setShadeBoostGamma(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_gamma),
                            onResetToDefault = { viewModel.setShadeBoostGamma(defaults.shadeBoostGamma) }
                        )
                    }
                }

                SettingsTab.Audio -> {
                    SettingsSection(title = stringResource(R.string.settings_audio_control)) {
                        ToggleItem(
                            icon = Icons.AutoMirrored.Rounded.VolumeOff,
                            title = stringResource(R.string.settings_audio_mute),
                            subtitle = stringResource(R.string.settings_audio_mute_desc),
                            checked = uiState.audioMuted,
                            onCheckedChange = viewModel::setAudioMuted,
                            helpText = stringResource(R.string.settings_help_audio_mute),
                            onResetToDefault = { viewModel.setAudioMuted(defaults.audioMuted) }
                        )
                        SliderItem(
                            icon = Icons.AutoMirrored.Rounded.VolumeUp,
                            title = stringResource(R.string.settings_audio_volume),
                            subtitle = "${uiState.audioVolume}%",
                            value = uiState.audioVolume.toFloat(),
                            range = AudioDefaults.VOLUME_MIN.toFloat()..AudioDefaults.VOLUME_MAX.toFloat(),
                            steps = 0,
                            onValueChange = { viewModel.setAudioVolume(it.roundToInt()) },
                            valueLabel = { "${it.roundToInt()}%" },
                            helpText = stringResource(R.string.settings_help_audio_volume),
                            onResetToDefault = { viewModel.setAudioVolume(defaults.audioVolume) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.FastForward,
                            title = stringResource(R.string.settings_audio_fast_forward_volume),
                            subtitle = "${uiState.audioFastForwardVolume}%",
                            value = uiState.audioFastForwardVolume.toFloat(),
                            range = AudioDefaults.VOLUME_MIN.toFloat()..AudioDefaults.VOLUME_MAX.toFloat(),
                            steps = 0,
                            onValueChange = { viewModel.setAudioFastForwardVolume(it.roundToInt()) },
                            valueLabel = { "${it.roundToInt()}%" },
                            helpText = stringResource(R.string.settings_help_audio_fast_forward_volume),
                            onResetToDefault = { viewModel.setAudioFastForwardVolume(defaults.audioFastForwardVolume) }
                        )
                    }

                    SettingsSection(title = stringResource(R.string.settings_audio_processing)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_audio_backend),
                            options = audioBackendOptions(),
                            selectedValue = uiState.audioBackend,
                            onSelect = viewModel::setAudioBackend,
                            helpText = stringResource(R.string.settings_help_audio_backend),
                            onResetToDefault = { viewModel.setAudioBackend(defaults.audioBackend) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Bolt,
                            title = stringResource(R.string.settings_audio_lightweight_spu2),
                            subtitle = stringResource(R.string.settings_audio_lightweight_spu2_desc),
                            checked = uiState.audioLightweightSpu2,
                            onCheckedChange = viewModel::setAudioLightweightSpu2,
                            helpText = stringResource(R.string.settings_help_audio_lightweight_spu2),
                            onResetToDefault = {
                                viewModel.setAudioLightweightSpu2(defaults.audioLightweightSpu2)
                            }
                        )
                        if (!uiState.audioLightweightSpu2) {
                            ChoiceSection(
                                title = stringResource(R.string.settings_audio_interpolation),
                                options = audioInterpolationOptions(),
                                selectedValue = uiState.audioInterpolation,
                                onSelect = viewModel::setAudioInterpolation,
                                helpText = stringResource(R.string.settings_help_audio_interpolation),
                                onResetToDefault = { viewModel.setAudioInterpolation(defaults.audioInterpolation) }
                            )
                            ChoiceSection(
                                title = stringResource(R.string.settings_audio_sync_mode),
                                options = audioSyncModeOptions(),
                                selectedValue = uiState.audioSyncMode,
                                onSelect = viewModel::setAudioSyncMode,
                                helpText = stringResource(R.string.settings_help_audio_sync_mode),
                                onResetToDefault = { viewModel.setAudioSyncMode(defaults.audioSyncMode) }
                            )
                        }
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_audio_buffer_size),
                            subtitle = "${uiState.audioBufferMs} ms",
                            value = uiState.audioBufferMs.toFloat(),
                            range = AudioDefaults.BUFFER_MS_MIN.toFloat()..AudioDefaults.BUFFER_MS_MAX.toFloat(),
                            steps = 0,
                            onValueChange = {
                                val rounded = ((it / 10f).roundToInt() * 10)
                                viewModel.setAudioBufferMs(rounded)
                            },
                            valueLabel = { "${((it / 10f).roundToInt() * 10)} ms" },
                            helpText = stringResource(R.string.settings_help_audio_buffer_size),
                            onResetToDefault = { viewModel.setAudioBufferMs(defaults.audioBufferMs) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_audio_minimal_latency),
                            subtitle = stringResource(R.string.settings_audio_minimal_latency_desc),
                            checked = uiState.audioMinimalOutputLatency,
                            onCheckedChange = viewModel::setAudioMinimalOutputLatency,
                            helpText = stringResource(R.string.settings_help_audio_minimal_latency),
                            onResetToDefault = {
                                viewModel.setAudioMinimalOutputLatency(defaults.audioMinimalOutputLatency)
                            }
                        )
                        if (!uiState.audioMinimalOutputLatency) {
                            SliderItem(
                                icon = Icons.Rounded.Speed,
                                title = stringResource(R.string.settings_audio_output_latency),
                                subtitle = "${uiState.audioOutputLatencyMs} ms",
                                value = uiState.audioOutputLatencyMs.toFloat(),
                                range = AudioDefaults.OUTPUT_LATENCY_MS_MIN.toFloat()..
                                    AudioDefaults.OUTPUT_LATENCY_MS_MAX.toFloat(),
                                steps = 0,
                                onValueChange = { viewModel.setAudioOutputLatencyMs(it.roundToInt()) },
                                valueLabel = { "${it.roundToInt()} ms" },
                                helpText = stringResource(R.string.settings_help_audio_output_latency),
                                onResetToDefault = {
                                    viewModel.setAudioOutputLatencyMs(defaults.audioOutputLatencyMs)
                                }
                            )
                        }
                    }
                }

                SettingsTab.Controls -> {
                    SettingsSection(title = stringResource(R.string.settings_touch_controls)) {
                        ActionItem(
                            icon = Icons.Rounded.Tune,
                            title = stringResource(R.string.settings_edit_global_controls),
                            subtitle = stringResource(R.string.settings_edit_global_controls_desc),
                            actionIcon = Icons.Rounded.Gamepad,
                            actionLabel = stringResource(R.string.settings_edit_controls_action),
                            onClick = { onOpenControlsLayoutEditor?.invoke() },
                            enabled = onOpenControlsLayoutEditor != null
                        )
                        SliderItem(
                            icon = Icons.Rounded.TouchApp,
                            title = stringResource(R.string.settings_overlay_scale),
                            subtitle = "${uiState.overlayScale}%",
                            value = uiState.overlayScale.toFloat(),
                            range = 50f..150f,
                            steps = 9,
                            onValueChange = { viewModel.setOverlayScale(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_overlay_scale),
                            onResetToDefault = { viewModel.setOverlayScale(overlayDefaults.overlayScale) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_overlay_opacity),
                            subtitle = "${uiState.overlayOpacity}%",
                            value = uiState.overlayOpacity.toFloat(),
                            range = AppPreferences.OVERLAY_OPACITY_MIN.toFloat()..
                                AppPreferences.OVERLAY_OPACITY_MAX.toFloat(),
                            steps = AppPreferences.OVERLAY_OPACITY_MAX -
                                AppPreferences.OVERLAY_OPACITY_MIN - 1,
                            onValueChange = { viewModel.setOverlayOpacity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_overlay_opacity),
                            onResetToDefault = { viewModel.setOverlayOpacity(overlayDefaults.overlayOpacity) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.TouchApp,
                            title = stringResource(R.string.settings_touchscreen_right_stick),
                            subtitle = stringResource(R.string.settings_touchscreen_right_stick_desc),
                            checked = uiState.touchscreenRightStick,
                            onCheckedChange = viewModel::setTouchscreenRightStick,
                            helpText = stringResource(R.string.settings_help_touchscreen_right_stick),
                            onResetToDefault = {
                                viewModel.setTouchscreenRightStick(
                                    AppPreferences.DEFAULT_TOUCHSCREEN_RIGHT_STICK
                                )
                            }
                        )
                        if (uiState.touchscreenRightStick) {
                            SliderItem(
                                icon = Icons.Rounded.TouchApp,
                                title = stringResource(R.string.settings_touchscreen_right_stick_sensitivity),
                                subtitle = "${uiState.touchscreenRightStickSensitivity}%",
                                value = uiState.touchscreenRightStickSensitivity.toFloat(),
                                range = AppPreferences.TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN.toFloat()..
                                    AppPreferences.TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX.toFloat(),
                                steps = AppPreferences.TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX -
                                    AppPreferences.TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN - 1,
                                onValueChange = {
                                    viewModel.setTouchscreenRightStickSensitivity(it.roundToInt())
                                },
                                helpText = stringResource(
                                    R.string.settings_help_touchscreen_right_stick_sensitivity
                                ),
                                onResetToDefault = {
                                    viewModel.setTouchscreenRightStickSensitivity(
                                        AppPreferences.DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY
                                    )
                                }
                            )
                        }
                        ToggleItem(
                            icon = Icons.Rounded.TouchApp,
                            title = stringResource(R.string.settings_racing_mode),
                            subtitle = stringResource(R.string.settings_racing_mode_desc),
                            checked = uiState.racingMode,
                            onCheckedChange = viewModel::setRacingMode,
                            helpText = stringResource(R.string.settings_help_racing_mode),
                            onResetToDefault = { viewModel.setRacingMode(defaults.racingMode) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_touch_haptics),
                            subtitle = stringResource(R.string.settings_touch_haptics_desc),
                            checked = uiState.touchHaptics,
                            onCheckedChange = viewModel::setTouchHaptics,
                            helpText = stringResource(R.string.settings_help_touch_haptics),
                            onResetToDefault = { viewModel.setTouchHaptics(defaults.touchHaptics) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_touch_haptics_preset),
                            options = touchHapticsPresetOptions(),
                            selectedValue = uiState.touchHapticsPreset,
                            onSelect = viewModel::setTouchHapticsPreset,
                            helpText = stringResource(R.string.settings_help_touch_haptics_preset),
                            onResetToDefault = { viewModel.setTouchHapticsPreset(defaults.touchHapticsPreset) }
                        )
                        SettingsInlineNote(text = stringResource(R.string.settings_touch_haptics_preset_desc))
                        SliderItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_touch_haptics_strength),
                            subtitle = "${uiState.touchHapticsStrength}%",
                            valueLabel = { "${it.roundToInt()}%" },
                            value = uiState.touchHapticsStrength.toFloat(),
                            range = 10f..100f,
                            steps = 8,
                            onValueChange = { viewModel.setTouchHapticsStrength(it.roundToInt()) },
                            helpText = stringResource(R.string.settings_help_touch_haptics_strength),
                            onResetToDefault = { viewModel.setTouchHapticsStrength(defaults.touchHapticsStrength) }
                        )
                        ActionItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_touch_haptics_test),
                            subtitle = stringResource(R.string.settings_touch_haptics_test_desc),
                            actionIcon = Icons.Rounded.PlayArrow,
                            actionLabel = stringResource(R.string.settings_pad_vibration_test_action),
                            onClick = {
                                viewModel.testTouchHaptics(
                                    strengthPercent = uiState.touchHapticsStrength,
                                    preset = uiState.touchHapticsPreset
                                )
                            },
                            helpText = stringResource(R.string.settings_help_touch_haptics_test)
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_gyro_mode),
                            options = gyroModeOptions(),
                            selectedValue = uiState.gyroMode,
                            onSelect = viewModel::setGyroMode,
                            helpText = stringResource(R.string.settings_help_gyro_mode),
                            onResetToDefault = { viewModel.setGyroMode(defaults.gyroMode) }
                        )
                        if (uiState.gyroMode != AppPreferences.GYRO_MODE_OFF &&
                            !AndroidGyroscopeInput.isModeAvailable(context, uiState.gyroMode)
                        ) {
                            SettingsInlineNote(text = stringResource(R.string.settings_gyro_unavailable))
                        }
                        if (uiState.gyroMode != AppPreferences.GYRO_MODE_OFF) {
                            SliderItem(
                                icon = Icons.Rounded.ScreenRotation,
                                title = stringResource(R.string.settings_gyro_sensitivity),
                                subtitle = "${uiState.gyroSensitivity}%",
                                valueLabel = { "${it.roundToInt()}%" },
                                value = uiState.gyroSensitivity.toFloat(),
                                range = 25f..300f,
                                steps = 10,
                                onValueChange = { viewModel.setGyroSensitivity(it.roundToInt()) },
                                helpText = stringResource(R.string.settings_help_gyro_sensitivity),
                                onResetToDefault = { viewModel.setGyroSensitivity(defaults.gyroSensitivity) }
                            )
                            SliderItem(
                                icon = Icons.Rounded.Tune,
                                title = stringResource(R.string.settings_gyro_smoothing),
                                subtitle = "${uiState.gyroSmoothing}%",
                                valueLabel = { "${it.roundToInt()}%" },
                                value = uiState.gyroSmoothing.toFloat(),
                                range = 0f..90f,
                                steps = 8,
                                onValueChange = { viewModel.setGyroSmoothing(it.roundToInt()) },
                                helpText = stringResource(R.string.settings_help_gyro_smoothing),
                                onResetToDefault = { viewModel.setGyroSmoothing(defaults.gyroSmoothing) }
                            )
                            ToggleItem(
                                icon = Icons.Rounded.SwapHoriz,
                                title = stringResource(R.string.settings_gyro_invert_x),
                                subtitle = stringResource(R.string.settings_gyro_invert_x_desc),
                                checked = uiState.gyroInvertX,
                                onCheckedChange = viewModel::setGyroInvertX,
                                onResetToDefault = { viewModel.setGyroInvertX(defaults.gyroInvertX) }
                            )
                            if (uiState.gyroMode == AppPreferences.GYRO_MODE_AIM) {
                                ToggleItem(
                                    icon = Icons.Rounded.SwapVert,
                                    title = stringResource(R.string.settings_gyro_invert_y),
                                    subtitle = stringResource(R.string.settings_gyro_invert_y_desc),
                                    checked = uiState.gyroInvertY,
                                    onCheckedChange = viewModel::setGyroInvertY,
                                    onResetToDefault = { viewModel.setGyroInvertY(defaults.gyroInvertY) }
                                )
                            }
                        }
                        SliderItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_left_stick_sensitivity),
                            subtitle = "${uiState.leftStickSensitivity}%",
                            value = uiState.leftStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 14,
                            onValueChange = { viewModel.setLeftStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_left_stick_sensitivity),
                            onResetToDefault = { viewModel.setLeftStickSensitivity(overlayDefaults.leftStickSensitivity) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_right_stick_sensitivity),
                            subtitle = "${uiState.rightStickSensitivity}%",
                            value = uiState.rightStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 14,
                            onValueChange = { viewModel.setRightStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_right_stick_sensitivity),
                            onResetToDefault = { viewModel.setRightStickSensitivity(overlayDefaults.rightStickSensitivity) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_invert_left_stick),
                            subtitle = stringResource(R.string.settings_invert_left_stick_desc),
                            checked = uiState.invertLeftStick,
                            onCheckedChange = viewModel::setInvertLeftStick,
                            helpText = stringResource(R.string.settings_help_invert_left_stick),
                            onResetToDefault = { viewModel.setInvertLeftStick(false) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_invert_left_stick_horizontal),
                            subtitle = stringResource(R.string.settings_invert_left_stick_horizontal_desc),
                            checked = uiState.invertLeftStickHorizontal,
                            onCheckedChange = viewModel::setInvertLeftStickHorizontal,
                            helpText = stringResource(R.string.settings_help_invert_left_stick_horizontal),
                            onResetToDefault = { viewModel.setInvertLeftStickHorizontal(false) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_invert_right_stick),
                            subtitle = stringResource(R.string.settings_invert_right_stick_desc),
                            checked = uiState.invertRightStick,
                            onCheckedChange = viewModel::setInvertRightStick,
                            helpText = stringResource(R.string.settings_help_invert_right_stick),
                            onResetToDefault = { viewModel.setInvertRightStick(false) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_invert_right_stick_horizontal),
                            subtitle = stringResource(R.string.settings_invert_right_stick_horizontal_desc),
                            checked = uiState.invertRightStickHorizontal,
                            onCheckedChange = viewModel::setInvertRightStickHorizontal,
                            helpText = stringResource(R.string.settings_help_invert_right_stick_horizontal),
                            onResetToDefault = { viewModel.setInvertRightStickHorizontal(false) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_gamepad_section)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_gamepad_mode),
                            options = listOf(
                                1 to stringResource(R.string.settings_gamepad_mode_replace_touch),
                                0 to stringResource(R.string.settings_gamepad_mode_touch_plus_gamepad)
                            ),
                            selectedValue = if (uiState.enableAutoGamepad) 1 else 0,
                            onSelect = { viewModel.setEnableAutoGamepad(it == 1) },
                            helpText = stringResource(R.string.settings_help_gamepad_mode),
                            onResetToDefault = { viewModel.setEnableAutoGamepad(defaults.enableAutoGamepad) }
                        )
                        SettingsInlineNote(text = stringResource(R.string.settings_gamepad_mode_desc))
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_gamepad_hide_overlay),
                            subtitle = stringResource(R.string.settings_gamepad_hide_overlay_desc),
                            checked = uiState.hideOverlayOnGamepad,
                            onCheckedChange = viewModel::setHideOverlayOnGamepad,
                            helpText = stringResource(R.string.settings_help_hide_overlay_on_gamepad),
                            onResetToDefault = { viewModel.setHideOverlayOnGamepad(overlayDefaults.hideOverlayOnGamepad) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            title = stringResource(R.string.settings_auto_progressive_scan),
                            subtitle = stringResource(R.string.settings_auto_progressive_scan_desc),
                            checked = uiState.autoProgressiveScan,
                            onCheckedChange = viewModel::setAutoProgressiveScan,
                            helpText = stringResource(R.string.settings_help_auto_progressive_scan),
                            onResetToDefault = { viewModel.setAutoProgressiveScan(defaults.autoProgressiveScan) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_pad_vibration),
                            subtitle = stringResource(R.string.settings_pad_vibration_desc),
                            checked = uiState.padVibration,
                            onCheckedChange = viewModel::setPadVibration,
                            helpText = stringResource(R.string.settings_help_pad_vibration),
                            onResetToDefault = { viewModel.setPadVibration(defaults.padVibration) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_gamepad_button_haptics),
                            subtitle = stringResource(R.string.settings_gamepad_button_haptics_desc),
                            checked = uiState.gamepadButtonHaptics,
                            onCheckedChange = viewModel::setGamepadButtonHaptics,
                            helpText = stringResource(R.string.settings_help_gamepad_button_haptics),
                            onResetToDefault = { viewModel.setGamepadButtonHaptics(defaults.gamepadButtonHaptics) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.TouchApp,
                            title = stringResource(R.string.settings_pressure_modifier_amount),
                            subtitle = "${uiState.pressureModifierAmount}%",
                            valueLabel = { "${it.roundToInt()}%" },
                            value = uiState.pressureModifierAmount.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { viewModel.setPressureModifierAmount(it.roundToInt()) },
                            helpText = stringResource(R.string.settings_help_pressure_modifier_amount),
                            onResetToDefault = { viewModel.setPressureModifierAmount(defaults.pressureModifierAmount) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_pad_vibration_strength),
                            subtitle = "${uiState.padVibrationStrength}%",
                            valueLabel = { "${it.roundToInt()}%" },
                            value = uiState.padVibrationStrength.toFloat(),
                            range = 0f..150f,
                            steps = 0,
                            onValueChange = { viewModel.setPadVibrationStrength(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_pad_vibration_strength),
                            onResetToDefault = { viewModel.setPadVibrationStrength(defaults.padVibrationStrength) }
                        )
                        ActionItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_pad_vibration_test),
                            subtitle = stringResource(R.string.settings_pad_vibration_test_desc),
                            actionIcon = Icons.Rounded.PlayArrow,
                            actionLabel = stringResource(R.string.settings_pad_vibration_test_action),
                            onClick = { viewModel.testPadVibration(uiState.padVibrationStrength, 320L) },
                            helpText = stringResource(R.string.settings_help_pad_vibration_test)
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_pad_vibration_fallback),
                            subtitle = stringResource(R.string.settings_pad_vibration_fallback_desc),
                            checked = uiState.padVibrationFallback,
                            onCheckedChange = viewModel::setPadVibrationFallback,
                            helpText = stringResource(R.string.settings_help_pad_vibration_fallback),
                            onResetToDefault = { viewModel.setPadVibrationFallback(defaults.padVibrationFallback) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Tune,
                            title = stringResource(R.string.settings_gamepad_stick_deadzone),
                            subtitle = "${uiState.gamepadStickDeadzone}%",
                            value = uiState.gamepadStickDeadzone.toFloat(),
                            range = 0f..35f,
                            steps = 6,
                            onValueChange = { viewModel.setGamepadStickDeadzone(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_gamepad_stick_deadzone),
                            onResetToDefault = { viewModel.setGamepadStickDeadzone(defaults.gamepadStickDeadzone) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_gamepad_left_stick_sensitivity),
                            subtitle = "${uiState.gamepadLeftStickSensitivity}%",
                            value = uiState.gamepadLeftStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 14,
                            onValueChange = { viewModel.setGamepadLeftStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_gamepad_left_stick_sensitivity),
                            onResetToDefault = { viewModel.setGamepadLeftStickSensitivity(defaults.gamepadLeftStickSensitivity) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_gamepad_right_stick_sensitivity),
                            subtitle = "${uiState.gamepadRightStickSensitivity}%",
                            value = uiState.gamepadRightStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 14,
                            onValueChange = { viewModel.setGamepadRightStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_gamepad_right_stick_sensitivity),
                            onResetToDefault = { viewModel.setGamepadRightStickSensitivity(defaults.gamepadRightStickSensitivity) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_gamepad_right_stick_up_to_r2),
                            subtitle = stringResource(R.string.settings_gamepad_right_stick_up_to_r2_desc),
                            checked = uiState.gamepadRightStickUpToR2,
                            onCheckedChange = viewModel::setGamepadRightStickUpToR2,
                            helpText = stringResource(R.string.settings_help_gamepad_right_stick_up_to_r2),
                            onResetToDefault = { viewModel.setGamepadRightStickUpToR2(defaults.gamepadRightStickUpToR2) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_gamepad_right_stick_down_to_l2),
                            subtitle = stringResource(R.string.settings_gamepad_right_stick_down_to_l2_desc),
                            checked = uiState.gamepadRightStickDownToL2,
                            onCheckedChange = viewModel::setGamepadRightStickDownToL2,
                            helpText = stringResource(R.string.settings_help_gamepad_right_stick_down_to_l2),
                            onResetToDefault = { viewModel.setGamepadRightStickDownToL2(defaults.gamepadRightStickDownToL2) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_gamepad_mapping_title)) {
                        val selectedBindings = uiState.gamepadBindingsByPad[selectedGamepadPadIndex].orEmpty()
                        val connectedControllerName = GamepadManager.connectedControllerName(selectedGamepadPadIndex)
                        LazyRow(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 16.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            contentPadding = PaddingValues(end = 4.dp)
                        ) {
                            items(listOf(0, 1)) { padIndex ->
                                FilterChip(
                                    selected = selectedGamepadPadIndex == padIndex,
                                    onClick = { selectedGamepadPadIndex = padIndex },
                                    label = { Text(gamepadPlayerLabel(padIndex)) }
                                )
                            }
                        }
                        SettingsInlineNote(
                            text = connectedControllerName?.let {
                                stringResource(
                                    R.string.settings_gamepad_mapping_player_connected,
                                    gamepadPlayerLabel(selectedGamepadPadIndex),
                                    it
                                )
                            } ?: stringResource(
                                R.string.settings_gamepad_mapping_player_disconnected,
                                gamepadPlayerLabel(selectedGamepadPadIndex)
                            )
                        )
                        for (action in gamepadActions) {
                            val assignedKeyCode = GamepadManager.resolveBindingForAction(
                                actionId = action.id,
                                customBindings = selectedBindings
                            )
                            val isCustomBinding = selectedBindings.containsKey(action.id)
                            GamepadBindingRow(
                                title = gamepadActionLabel(action.id),
                                value = assignedKeyCode?.let(GamepadManager::keyCodeLabel)
                                    ?: stringResource(R.string.settings_not_set),
                                autoLabel = if (isCustomBinding || action.defaultKeyCodes.isEmpty()) null else {
                                    stringResource(R.string.settings_gamepad_mapping_auto_format)
                                },
                                onBindClick = { onRequestGamepadBinding(selectedGamepadPadIndex, action.id) },
                                onClearClick = if (isCustomBinding) {
                                    { viewModel.clearGamepadBinding(selectedGamepadPadIndex, action.id) }
                                } else {
                                    null
                                }
                            )
                        }
                        SettingsItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            label = stringResource(R.string.settings_gamepad_mapping_reset_title),
                            value = stringResource(R.string.settings_gamepad_mapping_reset_desc),
                            onClick = { viewModel.resetGamepadBindingsForPad(selectedGamepadPadIndex) }
                        )
                    }
                }

                SettingsTab.Library -> {
                    val biosDisplayName = remember(uiState.biosPath, context, notSetLabel) {
                        uiState.biosPath?.let { DocumentPathResolver.getFallbackDisplayName(it) }
                            ?: notSetLabel
                    }
                    val gameDisplayName = if (uiState.gamePaths.isEmpty()) {
                        notSetLabel
                    } else {
                        stringResource(R.string.settings_game_folders_count, uiState.gamePaths.size)
                    }
                    val emulatorDataDisplayName = when {
                        uiState.emulatorDataPath.isNullOrBlank() -> stringResource(
                            R.string.emulator_data_location_internal
                        )
                        uiState.sdCardDataPath != null && uiState.emulatorDataPath == uiState.sdCardDataPath -> {
                            stringResource(R.string.emulator_data_location_sd_card)
                        }
                        else -> DocumentPathResolver.getFallbackDisplayName(uiState.emulatorDataPath)
                    }
                    val repository = remember(context) {
                        MemoryCardRepository(context, AppPreferences(context))
                    }
                    var memoryCardCount by remember { mutableIntStateOf(0) }
                    var slot1Name by remember { mutableStateOf<String?>(null) }
                    var slot2Name by remember { mutableStateOf<String?>(null) }
                    val builtInCoverSourceLabel = stringResource(R.string.settings_cover_download_url_builtin)
                    val coverDownloadDisabledLabel = stringResource(R.string.settings_cover_download_url_disabled)
                    val customCoverSourceLabel = stringResource(R.string.settings_cover_download_url_custom)
                    val coverUrlDisplay = if (!uiState.coverDownloadBaseUrl.isNullOrBlank()) {
                        customCoverSourceLabel
                    } else if (uiState.coverArtStyle == AppPreferences.COVER_ART_STYLE_DISABLED) {
                        coverDownloadDisabledLabel
                    } else {
                        builtInCoverSourceLabel
                    }

                    LaunchedEffect(repository) {
                        val assignments = repository.ensureDefaultCardsAssigned()
                        val cards = repository.listCards()
                        memoryCardCount = cards.size
                        slot1Name = assignments.slot1
                        slot2Name = assignments.slot2
                    }

                    SettingsSection(title = stringResource(R.string.settings_paths)) {
                        SettingsItem(
                            icon = Icons.Rounded.Memory,
                            label = stringResource(R.string.settings_bios_path),
                            value = biosDisplayName,
                            onClick = launchBiosPicker,
                            helpText = stringResource(R.string.settings_help_bios_path)
                        )
                        SettingsItem(
                            icon = Icons.Rounded.FolderOpen,
                            label = stringResource(R.string.settings_game_path),
                            value = gameDisplayName,
                            onClick = launchGamePicker,
                            helpText = stringResource(R.string.settings_help_game_path)
                        )
                        uiState.gamePaths.forEach { path ->
                            SettingsItem(
                                icon = Icons.Rounded.DeleteOutline,
                                label = DocumentPathResolver.getFallbackDisplayName(path),
                                value = stringResource(R.string.game_folders_remove),
                                onClick = { viewModel.removeGamePath(path) }
                            )
                        }
                        SettingsItem(
                            icon = Icons.Rounded.SaveAs,
                            label = stringResource(R.string.emulator_data_location_title),
                            value = emulatorDataDisplayName,
                            onClick = openEmulatorDataLocationDialog,
                            helpText = stringResource(R.string.emulator_data_location_description)
                        )
                    }

                    SettingsSection(title = stringResource(R.string.settings_memory_cards_tab)) {
                        SettingsItem(
                            icon = Icons.Rounded.Memory,
                            label = stringResource(R.string.settings_memory_cards_open),
                            value = stringResource(R.string.settings_memory_cards_open_desc),
                            onClick = { onOpenMemoryCardManager?.invoke() }
                        )
                        SettingsInlineNote(
                            text = stringResource(
                                R.string.settings_memory_cards_summary,
                                memoryCardCount,
                                slot1Name ?: stringResource(R.string.memory_card_slot_empty),
                                slot2Name ?: stringResource(R.string.memory_card_slot_empty)
                            )
                        )
                    }

                    SettingsSection(title = stringResource(R.string.settings_covers_tab)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_cover_art_style),
                            options = listOf(
                                AppPreferences.COVER_ART_STYLE_DISABLED to stringResource(R.string.settings_cover_art_style_off),
                                AppPreferences.COVER_ART_STYLE_DEFAULT to stringResource(R.string.settings_cover_art_style_flat),
                                AppPreferences.COVER_ART_STYLE_3D to stringResource(R.string.settings_cover_art_style_3d)
                            ),
                            selectedValue = uiState.coverArtStyle,
                            onSelect = viewModel::setCoverArtStyle,
                            helpText = stringResource(R.string.settings_help_cover_art_style),
                            onResetToDefault = { viewModel.setCoverArtStyle(AppPreferences.COVER_ART_STYLE_DEFAULT) }
                        )
                        SettingsItem(
                            icon = Icons.Rounded.Link,
                            label = stringResource(R.string.settings_cover_download_url),
                            value = coverUrlDisplay,
                            onClick = onOpenCoverUrlEditor,
                            helpText = stringResource(R.string.settings_help_cover_download_url)
                        )
                    }

                    SettingsSection(title = stringResource(R.string.settings_backup_section_title)) {
                        SettingsItem(
                            icon = Icons.Rounded.Save,
                            label = stringResource(R.string.settings_backup_export_title),
                            value = stringResource(R.string.settings_backup_export_desc),
                            onClick = launchSettingsBackupExport
                        )
                        SettingsItem(
                            icon = Icons.Rounded.FolderOpen,
                            label = stringResource(R.string.settings_backup_restore_title),
                            value = stringResource(R.string.settings_backup_restore_desc),
                            onClick = launchSettingsBackupImport
                        )
                    }
                }

                SettingsTab.Emulation -> {
                    SettingsSection(title = stringResource(R.string.emulation_performance_stats)) {
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_show_fps),
                            subtitle = stringResource(R.string.settings_show_fps_desc),
                            checked = uiState.showFps,
                            onCheckedChange = viewModel::setShowFps,
                            helpText = stringResource(R.string.settings_help_show_fps),
                            onResetToDefault = { viewModel.setShowFps(defaults.showFps) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_fps_overlay_mode),
                            options = listOf(
                                FPS_OVERLAY_MODE_SIMPLE to stringResource(R.string.settings_fps_overlay_mode_simple),
                                FPS_OVERLAY_MODE_DETAILED to stringResource(R.string.settings_fps_overlay_mode_detailed)
                            ),
                            selectedValue = uiState.fpsOverlayMode,
                            onSelect = viewModel::setFpsOverlayMode,
                            helpText = stringResource(R.string.settings_help_fps_overlay_mode),
                            onResetToDefault = { viewModel.setFpsOverlayMode(defaults.fpsOverlayMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_fps_overlay_position),
                            options = fpsOverlayCornerOptions(),
                            selectedValue = uiState.fpsOverlayCorner,
                            onSelect = viewModel::setFpsOverlayCorner,
                            helpText = stringResource(R.string.settings_help_fps_overlay_position),
                            onResetToDefault = { viewModel.setFpsOverlayCorner(defaults.fpsOverlayCorner) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.FormatSize,
                            title = stringResource(R.string.settings_fps_overlay_scale),
                            subtitle = stringResource(R.string.settings_fps_overlay_scale_value, uiState.fpsOverlayScale),
                            value = uiState.fpsOverlayScale.toFloat(),
                            range = AppPreferences.MIN_FPS_OVERLAY_SCALE.toFloat()..AppPreferences.MAX_FPS_OVERLAY_SCALE.toFloat(),
                            steps = 24,
                            onValueChange = { viewModel.setFpsOverlayScale(it.toInt()) },
                            valueLabel = { "${it.toInt()}%" },
                            helpText = stringResource(R.string.settings_help_fps_overlay_scale),
                            onResetToDefault = { viewModel.setFpsOverlayScale(defaults.fpsOverlayScale) }
                        )
                        if (uiState.fpsOverlayMode == FPS_OVERLAY_MODE_DETAILED) {
                            BitmaskChoiceSection(
                                title = stringResource(R.string.settings_fps_overlay_metrics),
                                options = fpsOverlayMetricOptions(),
                                selectedMask = uiState.fpsOverlayMetrics,
                                onToggle = { metric ->
                                    viewModel.setFpsOverlayMetrics(uiState.fpsOverlayMetrics xor metric)
                                },
                                helpText = stringResource(R.string.settings_help_fps_overlay_metrics),
                                onResetToDefault = { viewModel.setFpsOverlayMetrics(defaults.fpsOverlayMetrics) }
                            )
                        }
                    }
                    SettingsSection(title = stringResource(R.string.settings_jit_section)) {
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_enable_ee_recompiler),
                            subtitle = stringResource(R.string.settings_enable_ee_recompiler_desc),
                            checked = uiState.enableEeRecompiler,
                            onCheckedChange = viewModel::setEnableEeRecompiler,
                            helpText = stringResource(R.string.settings_help_enable_ee_recompiler),
                            onResetToDefault = { viewModel.setEnableEeRecompiler(defaults.enableEeRecompiler) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_enable_iop_recompiler),
                            subtitle = stringResource(R.string.settings_enable_iop_recompiler_desc),
                            checked = uiState.enableIopRecompiler,
                            onCheckedChange = viewModel::setEnableIopRecompiler,
                            helpText = stringResource(R.string.settings_help_enable_iop_recompiler),
                            onResetToDefault = { viewModel.setEnableIopRecompiler(defaults.enableIopRecompiler) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_enable_vu0_recompiler),
                            subtitle = stringResource(R.string.settings_enable_vu0_recompiler_desc),
                            checked = uiState.enableVu0Recompiler,
                            onCheckedChange = viewModel::setEnableVu0Recompiler,
                            helpText = stringResource(R.string.settings_help_enable_vu0_recompiler),
                            onResetToDefault = { viewModel.setEnableVu0Recompiler(defaults.enableVu0Recompiler) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_enable_vu1_recompiler),
                            subtitle = stringResource(R.string.settings_enable_vu1_recompiler_desc),
                            checked = uiState.enableVu1Recompiler,
                            onCheckedChange = viewModel::setEnableVu1Recompiler,
                            helpText = stringResource(R.string.settings_help_enable_vu1_recompiler),
                            onResetToDefault = { viewModel.setEnableVu1Recompiler(defaults.enableVu1Recompiler) }
                        )
                        SettingsInlineNote(
                            text = stringResource(R.string.settings_jit_section_note)
                        )
                        if (!uiState.enableVu1Recompiler && uiState.enableMtvu) {
                            SettingsInlineNote(
                                text = stringResource(R.string.settings_jit_mtvu_note)
                            )
                        }
                    }
                    SettingsSection(title = stringResource(R.string.settings_cpu_float_modes)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_ee_fpu_round_mode),
                            options = floatRoundModeOptions(),
                            selectedValue = uiState.eeFpuRoundMode,
                            onSelect = viewModel::setEeFpuRoundMode,
                            helpText = stringResource(R.string.settings_help_ee_fpu_round_mode),
                            onResetToDefault = { viewModel.setEeFpuRoundMode(defaults.eeFpuRoundMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_vu0_round_mode),
                            options = floatRoundModeOptions(),
                            selectedValue = uiState.vu0RoundMode,
                            onSelect = viewModel::setVu0RoundMode,
                            helpText = stringResource(R.string.settings_help_vu0_round_mode),
                            onResetToDefault = { viewModel.setVu0RoundMode(defaults.vu0RoundMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_vu1_round_mode),
                            options = floatRoundModeOptions(),
                            selectedValue = uiState.vu1RoundMode,
                            onSelect = viewModel::setVu1RoundMode,
                            helpText = stringResource(R.string.settings_help_vu1_round_mode),
                            onResetToDefault = { viewModel.setVu1RoundMode(defaults.vu1RoundMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_ee_fpu_clamping),
                            options = eeFpuClampingModeOptions(),
                            selectedValue = uiState.eeFpuClampingMode,
                            onSelect = viewModel::setEeFpuClampingMode,
                            helpText = stringResource(R.string.settings_help_ee_fpu_clamping),
                            onResetToDefault = { viewModel.setEeFpuClampingMode(defaults.eeFpuClampingMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_vu0_clamping),
                            options = vuClampingModeOptions(),
                            selectedValue = uiState.vu0ClampingMode,
                            onSelect = viewModel::setVu0ClampingMode,
                            helpText = stringResource(R.string.settings_help_vu0_clamping),
                            onResetToDefault = { viewModel.setVu0ClampingMode(defaults.vu0ClampingMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_vu1_clamping),
                            options = vuClampingModeOptions(),
                            selectedValue = uiState.vu1ClampingMode,
                            onSelect = viewModel::setVu1ClampingMode,
                            helpText = stringResource(R.string.settings_help_vu1_clamping),
                            onResetToDefault = { viewModel.setVu1ClampingMode(defaults.vu1ClampingMode) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Tune,
                            title = stringResource(R.string.settings_game_fixes),
                            subtitle = stringResource(R.string.settings_game_fixes_desc),
                            checked = uiState.enableGameFixes,
                            onCheckedChange = viewModel::setEnableGameFixes,
                            helpText = stringResource(R.string.settings_help_game_fixes),
                            onResetToDefault = { viewModel.setEnableGameFixes(defaults.enableGameFixes) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Schedule,
                            title = stringResource(R.string.settings_ee_timing_hack),
                            subtitle = stringResource(R.string.settings_ee_timing_hack_desc),
                            checked = uiState.enableEeTimingHack,
                            onCheckedChange = viewModel::setEnableEeTimingHack,
                            helpText = stringResource(R.string.settings_help_ee_timing_hack),
                            onResetToDefault = { viewModel.setEnableEeTimingHack(defaults.enableEeTimingHack) }
                        )
                        if (onOpenGameDbBrowser != null) {
                            ActionItem(
                                icon = Icons.Rounded.Visibility,
                                title = stringResource(R.string.gamedb_browser_settings_card_title),
                                subtitle = stringResource(R.string.gamedb_browser_settings_card_desc),
                                actionIcon = Icons.Rounded.Search,
                                actionLabel = stringResource(R.string.gamedb_browser_browse),
                                onClick = onOpenGameDbBrowser
                            )
                        }
                        SettingsInlineNote(
                            text = stringResource(R.string.settings_cpu_float_modes_note)
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_speed_hacks)) {
                        val mediatekAngleAvailable = EmulatorBridge.isBundledAngleAvailable()
                        ChoiceSection(
                            title = stringResource(R.string.onboarding_profile_title),
                            options = listOf(
                                PerformanceProfiles.SAFE to stringResource(R.string.onboarding_profile_safe_title),
                                PerformanceProfiles.FAST to stringResource(R.string.onboarding_profile_fast_title)
                            ),
                            selectedValue = uiState.performanceProfile,
                            onSelect = viewModel::setPerformanceProfile,
                            helpText = stringResource(R.string.onboarding_profile_subtitle),
                            onResetToDefault = { viewModel.setPerformanceProfile(defaults.performanceProfile) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_frame_limiter),
                            subtitle = stringResource(R.string.settings_frame_limiter_desc),
                            checked = uiState.frameLimitEnabled,
                            onCheckedChange = viewModel::setFrameLimitEnabled,
                            helpText = stringResource(R.string.settings_help_frame_limiter),
                            onResetToDefault = { viewModel.setFrameLimitEnabled(defaults.frameLimitEnabled) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_vsync),
                            subtitle = stringResource(R.string.settings_vsync_desc),
                            checked = uiState.vSyncEnabled,
                            onCheckedChange = viewModel::setVSyncEnabled,
                            helpText = stringResource(R.string.settings_help_vsync),
                            onResetToDefault = { viewModel.setVSyncEnabled(defaults.vSyncEnabled) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_fast_forward_speed),
                            subtitle = stringResource(
                                R.string.settings_fast_forward_speed_desc,
                                formatSpeedMultiplier(uiState.fastForwardSpeed)
                            ),
                            value = uiState.fastForwardSpeed,
                            range = AppPreferences.MIN_FAST_FORWARD_SPEED..AppPreferences.MAX_FAST_FORWARD_SPEED,
                            steps = 14,
                            onValueChange = viewModel::setFastForwardSpeed,
                            valueLabel = { formatSpeedMultiplier(it) },
                            helpText = stringResource(R.string.settings_help_fast_forward_speed),
                            onResetToDefault = { viewModel.setFastForwardSpeed(defaults.fastForwardSpeed) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_target_fps_mode),
                            options = listOf(
                                0 to stringResource(R.string.settings_target_fps_auto),
                                1 to stringResource(R.string.settings_target_fps_manual)
                            ),
                            selectedValue = if (uiState.targetFps <= 0) 0 else 1,
                            onSelect = { mode ->
                                viewModel.setTargetFps(
                                    if (mode == 0) 0 else resolveManualTargetFps(uiState.targetFps, defaults.targetFps)
                                )
                            },
                            helpText = stringResource(R.string.settings_help_target_fps),
                            onResetToDefault = { viewModel.setTargetFps(defaults.targetFps) }
                        )
                        if (uiState.targetFps > 0) {
                            SliderItem(
                                icon = Icons.Rounded.Speed,
                                title = stringResource(R.string.settings_target_fps),
                                subtitle = stringResource(
                                    R.string.settings_target_fps_desc,
                                    uiState.targetFps
                                ),
                                value = uiState.targetFps.toFloat(),
                                range = 20f..120f,
                                steps = 99,
                                onValueChange = { viewModel.setTargetFps(it.toInt()) },
                                helpText = stringResource(R.string.settings_help_target_fps),
                                onResetToDefault = { viewModel.setTargetFps(defaults.targetFps) }
                            )
                        }
                        SliderItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_ntsc_framerate),
                            subtitle = stringResource(
                                R.string.settings_region_framerate_desc,
                                formatFramerateHz(uiState.ntscFramerate)
                            ),
                            value = uiState.ntscFramerate,
                            range = 20f..120f,
                            steps = 199,
                            onValueChange = viewModel::setNtscFramerate,
                            valueLabel = { formatFramerateHz(it) },
                            helpText = stringResource(R.string.settings_help_ntsc_framerate),
                            onResetToDefault = { viewModel.setNtscFramerate(defaults.ntscFramerate) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_pal_framerate),
                            subtitle = stringResource(
                                R.string.settings_region_framerate_desc,
                                formatFramerateHz(uiState.palFramerate)
                            ),
                            value = uiState.palFramerate,
                            range = 20f..120f,
                            steps = 199,
                            onValueChange = viewModel::setPalFramerate,
                            valueLabel = { formatFramerateHz(it) },
                            helpText = stringResource(R.string.settings_help_pal_framerate),
                            onResetToDefault = { viewModel.setPalFramerate(defaults.palFramerate) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_ee_cycle_rate),
                            options = listOf(
                                -3 to "50%",
                                -2 to "60%",
                                -1 to "75%",
                                0 to "100%",
                                1 to "130%",
                                2 to "180%",
                                3 to "300%"
                            ),
                            selectedValue = uiState.eeCycleRate,
                            onSelect = viewModel::setEeCycleRate,
                            helpText = stringResource(R.string.settings_help_ee_cycle_rate),
                            onResetToDefault = { viewModel.setEeCycleRate(defaults.eeCycleRate) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_ee_cycle_skip),
                            options = listOf(
                                0 to stringResource(R.string.settings_ee_cycle_disabled),
                                1 to stringResource(R.string.settings_ee_cycle_mild),
                                2 to stringResource(R.string.settings_ee_cycle_moderate),
                                3 to stringResource(R.string.settings_ee_cycle_maximum)
                            ),
                            selectedValue = uiState.eeCycleSkip,
                            onSelect = viewModel::setEeCycleSkip,
                            helpText = stringResource(R.string.settings_help_ee_cycle_skip),
                            onResetToDefault = { viewModel.setEeCycleSkip(defaults.eeCycleSkip) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_wait_loop_speedhack),
                            subtitle = stringResource(R.string.settings_wait_loop_speedhack_desc),
                            checked = uiState.enableWaitLoopSpeedhack,
                            onCheckedChange = viewModel::setEnableWaitLoopSpeedhack,
                            helpText = stringResource(R.string.settings_help_wait_loop_speedhack),
                            onResetToDefault = { viewModel.setEnableWaitLoopSpeedhack(defaults.enableWaitLoopSpeedhack) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_intc_stat_speedhack),
                            subtitle = stringResource(R.string.settings_intc_stat_speedhack_desc),
                            checked = uiState.enableIntcStatSpeedhack,
                            onCheckedChange = viewModel::setEnableIntcStatSpeedhack,
                            helpText = stringResource(R.string.settings_help_intc_stat_speedhack),
                            onResetToDefault = { viewModel.setEnableIntcStatSpeedhack(defaults.enableIntcStatSpeedhack) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_vu_flag_hack),
                            subtitle = stringResource(R.string.settings_vu_flag_hack_desc),
                            checked = uiState.enableVuFlagHack,
                            onCheckedChange = viewModel::setEnableVuFlagHack,
                            helpText = stringResource(R.string.settings_help_vu_flag_hack),
                            onResetToDefault = { viewModel.setEnableVuFlagHack(defaults.enableVuFlagHack) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_instant_vu1),
                            subtitle = stringResource(R.string.settings_instant_vu1_desc),
                            checked = uiState.enableInstantVu1,
                            onCheckedChange = viewModel::setEnableInstantVu1,
                            helpText = stringResource(R.string.settings_help_instant_vu1),
                            onResetToDefault = { viewModel.setEnableInstantVu1(defaults.enableInstantVu1) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_skip_duplicate_frames),
                            subtitle = stringResource(R.string.settings_skip_duplicate_frames_desc),
                            checked = uiState.skipDuplicateFrames,
                            onCheckedChange = viewModel::setSkipDuplicateFrames,
                            helpText = stringResource(R.string.settings_help_skip_duplicate_frames),
                            onResetToDefault = { viewModel.setSkipDuplicateFrames(defaults.skipDuplicateFrames) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_frame_skip),
                            options = listOf(
                                0 to stringResource(R.string.settings_frame_skip_off),
                                1 to "1",
                                2 to "2",
                                3 to "3",
                                4 to "4"
                            ),
                            selectedValue = uiState.frameSkip,
                            onSelect = viewModel::setFrameSkip,
                            helpText = stringResource(R.string.settings_help_frame_skip),
                            onResetToDefault = { viewModel.setFrameSkip(defaults.frameSkip) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_mtvu),
                            subtitle = stringResource(R.string.settings_mtvu_desc),
                            checked = uiState.enableMtvu && uiState.enableVu1Recompiler,
                            onCheckedChange = viewModel::setEnableMtvu,
                            helpText = stringResource(R.string.settings_help_mtvu),
                            onResetToDefault = { viewModel.setEnableMtvu(defaults.enableMtvu) },
                            enabled = uiState.enableVu1Recompiler
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_thread_pinning),
                            subtitle = stringResource(R.string.settings_thread_pinning_desc),
                            checked = uiState.enableThreadPinning,
                            onCheckedChange = viewModel::setEnableThreadPinning,
                            helpText = stringResource(R.string.settings_help_thread_pinning),
                            onResetToDefault = { viewModel.setEnableThreadPinning(defaults.enableThreadPinning) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_fast_boot),
                            subtitle = stringResource(R.string.settings_fast_boot_desc),
                            checked = uiState.enableFastBoot,
                            onCheckedChange = viewModel::setEnableFastBoot,
                            helpText = stringResource(R.string.settings_help_fast_boot),
                            onResetToDefault = { viewModel.setEnableFastBoot(defaults.enableFastBoot) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_fast_cdvd),
                            subtitle = stringResource(R.string.settings_fast_cdvd_desc),
                            checked = uiState.enableFastCdvd,
                            onCheckedChange = viewModel::setEnableFastCdvd,
                            helpText = stringResource(R.string.settings_help_fast_cdvd),
                            onResetToDefault = { viewModel.setEnableFastCdvd(defaults.enableFastCdvd) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_cheats_tab)) {
                        ToggleItem(
                            icon = Icons.Rounded.Star,
                            title = stringResource(R.string.settings_enable_cheats),
                            subtitle = stringResource(R.string.settings_enable_cheats_desc),
                            checked = uiState.enableCheats,
                            onCheckedChange = viewModel::setEnableCheats,
                            helpText = stringResource(R.string.settings_help_cheats),
                            onResetToDefault = { viewModel.setEnableCheats(defaults.enableCheats) }
                        )
                        SettingsItem(
                            icon = Icons.Rounded.FolderOpen,
                            label = stringResource(R.string.settings_cheats_import_title),
                            value = stringResource(R.string.settings_cheats_import_desc),
                            onClick = launchCheatImport
                        )
                        SettingsInlineNote(
                            text = stringResource(R.string.settings_cheats_note)
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_cheats_files_title)) {
                        if (cheatEntries.isEmpty()) {
                            CheatEmptyState(
                                title = stringResource(R.string.settings_cheats_empty_title),
                                body = stringResource(R.string.settings_cheats_empty),
                                icon = Icons.Rounded.Star
                            )
                        } else {
                            cheatEntries.forEach { entry ->
                                val cheatFileSummary = stringResource(
                                    R.string.settings_cheats_file_summary,
                                    entry.fileName,
                                    entry.blockCount
                                )
                                SettingsItem(
                                    icon = Icons.Rounded.SaveAs,
                                    label = entry.displayName,
                                    value = cheatFileSummary,
                                    onClick = { onOpenCheatEditor(entry.gameKey) }
                                )
                            }
                        }
                    }
                }

                SettingsTab.Fixes -> {
                    SettingsSection(title = stringResource(R.string.settings_image_processing)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_deinterlacing),
                            options = deinterlacingOptions(),
                            selectedValue = uiState.deinterlaceMode,
                            onSelect = viewModel::setDeinterlaceMode,
                            helpText = stringResource(R.string.settings_help_deinterlacing),
                            onResetToDefault = { viewModel.setDeinterlaceMode(defaults.deinterlaceMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_dithering),
                            options = ditheringOptions(),
                            selectedValue = uiState.dithering,
                            onSelect = viewModel::setDithering,
                            helpText = stringResource(R.string.settings_help_dithering),
                            onResetToDefault = { viewModel.setDithering(defaults.dithering) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_patches_section)) {
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_widescreen_patches),
                            subtitle = stringResource(R.string.settings_widescreen_patches_desc),
                            checked = uiState.enableWidescreenPatches,
                            onCheckedChange = viewModel::setEnableWidescreenPatches,
                            helpText = stringResource(R.string.settings_help_widescreen_patches),
                            onResetToDefault = { viewModel.setEnableWidescreenPatches(defaults.enableWidescreenPatches) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_no_interlacing_patches),
                            subtitle = stringResource(R.string.settings_no_interlacing_patches_desc),
                            checked = uiState.enableNoInterlacingPatches,
                            onCheckedChange = viewModel::setEnableNoInterlacingPatches,
                            helpText = stringResource(R.string.settings_help_no_interlacing_patches),
                            onResetToDefault = { viewModel.setEnableNoInterlacingPatches(defaults.enableNoInterlacingPatches) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_anti_blur),
                            subtitle = stringResource(R.string.settings_anti_blur_desc),
                            checked = uiState.antiBlur,
                            onCheckedChange = viewModel::setAntiBlur,
                            helpText = stringResource(R.string.settings_help_anti_blur),
                            onResetToDefault = { viewModel.setAntiBlur(defaults.antiBlur) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_hardware_fixes)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_cpu_sprite_render_size),
                            options = cpuSpriteRenderSizeOptions(),
                            selectedValue = uiState.cpuSpriteRenderSize,
                            onSelect = viewModel::setCpuSpriteRenderSize,
                            helpText = stringResource(R.string.settings_help_cpu_sprite_render_size),
                            onResetToDefault = { viewModel.setCpuSpriteRenderSize(defaults.cpuSpriteRenderSize) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_cpu_sprite_render_level),
                            options = cpuSpriteRenderLevelOptions(),
                            selectedValue = uiState.cpuSpriteRenderLevel,
                            onSelect = viewModel::setCpuSpriteRenderLevel,
                            helpText = stringResource(R.string.settings_help_cpu_sprite_render_level),
                            onResetToDefault = { viewModel.setCpuSpriteRenderLevel(defaults.cpuSpriteRenderLevel) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_software_clut_render),
                            options = softwareClutRenderOptions(),
                            selectedValue = uiState.softwareClutRender,
                            onSelect = viewModel::setSoftwareClutRender,
                            helpText = stringResource(R.string.settings_help_software_clut_render),
                            onResetToDefault = { viewModel.setSoftwareClutRender(defaults.softwareClutRender) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_gpu_target_clut),
                            options = gpuTargetClutOptions(),
                            selectedValue = uiState.gpuTargetClutMode,
                            onSelect = viewModel::setGpuTargetClutMode,
                            helpText = stringResource(R.string.settings_help_gpu_target_clut),
                            onResetToDefault = { viewModel.setGpuTargetClutMode(defaults.gpuTargetClutMode) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_auto_flush_hardware),
                            options = autoFlushHardwareOptions(),
                            selectedValue = uiState.autoFlushHardware,
                            onSelect = viewModel::setAutoFlushHardware,
                            helpText = stringResource(R.string.settings_help_auto_flush_hardware),
                            onResetToDefault = { viewModel.setAutoFlushHardware(defaults.autoFlushHardware) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            title = stringResource(R.string.settings_skip_draw_start),
                            subtitle = uiState.skipDrawStart.toString(),
                            value = uiState.skipDrawStart.toFloat(),
                            range = 0f..100f,
                            steps = 99,
                            onValueChange = { viewModel.setSkipDrawStart(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_skip_draw_start),
                            onResetToDefault = { viewModel.setSkipDrawStart(defaults.skipDrawStart) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            title = stringResource(R.string.settings_skip_draw_end),
                            subtitle = uiState.skipDrawEnd.toString(),
                            value = uiState.skipDrawEnd.toFloat(),
                            range = 0f..100f,
                            steps = 99,
                            onValueChange = { viewModel.setSkipDrawEnd(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_skip_draw_end),
                            onResetToDefault = { viewModel.setSkipDrawEnd(defaults.skipDrawEnd) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_cpu_framebuffer_conversion),
                            subtitle = stringResource(R.string.settings_cpu_framebuffer_conversion_desc),
                            checked = uiState.cpuFramebufferConversion,
                            onCheckedChange = viewModel::setCpuFramebufferConversion,
                            helpText = stringResource(R.string.settings_help_cpu_framebuffer_conversion),
                            onResetToDefault = { viewModel.setCpuFramebufferConversion(defaults.cpuFramebufferConversion) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_depth_conversion),
                            subtitle = stringResource(R.string.settings_disable_depth_conversion_desc),
                            checked = uiState.disableDepthConversion,
                            onCheckedChange = viewModel::setDisableDepthConversion,
                            helpText = stringResource(R.string.settings_help_disable_depth_conversion),
                            onResetToDefault = { viewModel.setDisableDepthConversion(defaults.disableDepthConversion) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_safe_features),
                            subtitle = stringResource(R.string.settings_disable_safe_features_desc),
                            checked = uiState.disableSafeFeatures,
                            onCheckedChange = viewModel::setDisableSafeFeatures,
                            helpText = stringResource(R.string.settings_help_disable_safe_features),
                            onResetToDefault = { viewModel.setDisableSafeFeatures(defaults.disableSafeFeatures) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_render_fixes),
                            subtitle = stringResource(R.string.settings_disable_render_fixes_desc),
                            checked = uiState.disableRenderFixes,
                            onCheckedChange = viewModel::setDisableRenderFixes,
                            helpText = stringResource(R.string.settings_help_disable_render_fixes),
                            onResetToDefault = { viewModel.setDisableRenderFixes(defaults.disableRenderFixes) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_preload_frame_data),
                            subtitle = stringResource(R.string.settings_preload_frame_data_desc),
                            checked = uiState.preloadFrameData,
                            onCheckedChange = viewModel::setPreloadFrameData,
                            helpText = stringResource(R.string.settings_help_preload_frame_data),
                            onResetToDefault = { viewModel.setPreloadFrameData(defaults.preloadFrameData) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_partial_invalidation),
                            subtitle = stringResource(R.string.settings_disable_partial_invalidation_desc),
                            checked = uiState.disablePartialInvalidation,
                            onCheckedChange = viewModel::setDisablePartialInvalidation,
                            helpText = stringResource(R.string.settings_help_disable_partial_invalidation),
                            onResetToDefault = { viewModel.setDisablePartialInvalidation(defaults.disablePartialInvalidation) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_texture_inside_rt),
                            options = textureInsideRtOptions(),
                            selectedValue = uiState.textureInsideRt,
                            onSelect = viewModel::setTextureInsideRt,
                            helpText = stringResource(R.string.settings_help_texture_inside_rt),
                            onResetToDefault = { viewModel.setTextureInsideRt(defaults.textureInsideRt) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_read_targets_on_close),
                            subtitle = stringResource(R.string.settings_read_targets_on_close_desc),
                            checked = uiState.readTargetsOnClose,
                            onCheckedChange = viewModel::setReadTargetsOnClose,
                            helpText = stringResource(R.string.settings_help_read_targets_on_close),
                            onResetToDefault = { viewModel.setReadTargetsOnClose(defaults.readTargetsOnClose) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_estimate_texture_region),
                            subtitle = stringResource(R.string.settings_estimate_texture_region_desc),
                            checked = uiState.estimateTextureRegion,
                            onCheckedChange = viewModel::setEstimateTextureRegion,
                            helpText = stringResource(R.string.settings_help_estimate_texture_region),
                            onResetToDefault = { viewModel.setEstimateTextureRegion(defaults.estimateTextureRegion) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_gpu_palette_conversion),
                            subtitle = stringResource(R.string.settings_gpu_palette_conversion_desc),
                            checked = uiState.gpuPaletteConversion,
                            onCheckedChange = viewModel::setGpuPaletteConversion,
                            helpText = stringResource(R.string.settings_help_gpu_palette_conversion),
                            onResetToDefault = { viewModel.setGpuPaletteConversion(defaults.gpuPaletteConversion) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_upscaling_fixes)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_half_pixel_offset),
                            options = halfPixelOffsetOptions(),
                            selectedValue = uiState.halfPixelOffset,
                            onSelect = viewModel::setHalfPixelOffset,
                            helpText = stringResource(R.string.settings_help_half_pixel_offset),
                            onResetToDefault = { viewModel.setHalfPixelOffset(defaults.halfPixelOffset) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_native_scaling),
                            options = nativeScalingOptions(),
                            selectedValue = uiState.nativeScaling,
                            onSelect = viewModel::setNativeScaling,
                            helpText = stringResource(R.string.settings_help_native_scaling),
                            onResetToDefault = { viewModel.setNativeScaling(defaults.nativeScaling) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_round_sprite),
                            options = roundSpriteOptions(),
                            selectedValue = uiState.roundSprite,
                            onSelect = viewModel::setRoundSprite,
                            helpText = stringResource(R.string.settings_help_round_sprite),
                            onResetToDefault = { viewModel.setRoundSprite(defaults.roundSprite) }
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_bilinear_upscale),
                            options = bilinearUpscaleOptions(),
                            selectedValue = uiState.bilinearUpscale,
                            onSelect = viewModel::setBilinearUpscale,
                            helpText = stringResource(R.string.settings_help_bilinear_upscale),
                            onResetToDefault = { viewModel.setBilinearUpscale(defaults.bilinearUpscale) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_texture_offset_x),
                            subtitle = uiState.textureOffsetX.toString(),
                            value = uiState.textureOffsetX.toFloat(),
                            range = -512f..512f,
                            steps = 1023,
                            onValueChange = { viewModel.setTextureOffsetX(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_texture_offset_x),
                            onResetToDefault = { viewModel.setTextureOffsetX(defaults.textureOffsetX) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_texture_offset_y),
                            subtitle = uiState.textureOffsetY.toString(),
                            value = uiState.textureOffsetY.toFloat(),
                            range = -512f..512f,
                            steps = 1023,
                            onValueChange = { viewModel.setTextureOffsetY(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_texture_offset_y),
                            onResetToDefault = { viewModel.setTextureOffsetY(defaults.textureOffsetY) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_align_sprite),
                            subtitle = stringResource(R.string.settings_align_sprite_desc),
                            checked = uiState.alignSprite,
                            onCheckedChange = viewModel::setAlignSprite,
                            helpText = stringResource(R.string.settings_help_align_sprite),
                            onResetToDefault = { viewModel.setAlignSprite(defaults.alignSprite) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_merge_sprite),
                            subtitle = stringResource(R.string.settings_merge_sprite_desc),
                            checked = uiState.mergeSprite,
                            onCheckedChange = viewModel::setMergeSprite,
                            helpText = stringResource(R.string.settings_help_merge_sprite),
                            onResetToDefault = { viewModel.setMergeSprite(defaults.mergeSprite) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_force_even_sprite_position),
                            subtitle = stringResource(R.string.settings_force_even_sprite_position_desc),
                            checked = uiState.forceEvenSpritePosition,
                            onCheckedChange = viewModel::setForceEvenSpritePosition,
                            helpText = stringResource(R.string.settings_help_force_even_sprite_position),
                            onResetToDefault = { viewModel.setForceEvenSpritePosition(defaults.forceEvenSpritePosition) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_native_palette_draw),
                            subtitle = stringResource(R.string.settings_native_palette_draw_desc),
                            checked = uiState.nativePaletteDraw,
                            onCheckedChange = viewModel::setNativePaletteDraw,
                            helpText = stringResource(R.string.settings_help_native_palette_draw),
                            onResetToDefault = { viewModel.setNativePaletteDraw(defaults.nativePaletteDraw) }
                        )
                    }
                }

                SettingsTab.Updates -> {
                    AppUpdateTab(
                        state = uiState.appUpdate,
                        onLoadReleaseHistory = { force -> viewModel.loadAppReleaseHistory(showErrors = true, force = force) }
                    )
                }

                SettingsTab.Network -> {
                    NetworkSettingsTab(uiState, context, defaults, viewModel)
                }

                SettingsTab.Pro -> {
                    ProSettingsTab(
                        uiState = uiState,
                        onPurchase = { (context as? Activity)?.let(viewModel::purchasePro) },
                        onRestore = viewModel::restoreProPurchases,
                        onApplyCrimson = { viewModel.setThemeMode(ThemeMode.PRO) }
                    )
                }

                SettingsTab.About -> {
                    SettingsSection(title = stringResource(R.string.settings_about)) {
                        SettingsItem(
                            icon = Icons.Rounded.Info,
                            label = stringResource(R.string.settings_version),
                            value = uiState.appVersion,
                            onClick = { }
                        )
                        SettingsItem(
                            icon = Icons.Rounded.Memory,
                            label = stringResource(R.string.settings_emulator_core),
                            value = stringResource(R.string.settings_emulator_core_desc),
                            onClick = { }
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_app),
                            body = stringResource(R.string.settings_about_app_desc)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_studio),
                            body = stringResource(R.string.settings_about_studio_desc)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_website),
                            body = stringResource(R.string.settings_about_website_desc),
                            linkLabel = stringResource(R.string.settings_about_website_link),
                            linkUrl = stringResource(R.string.settings_about_website_url)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_app_source),
                            body = stringResource(R.string.settings_about_app_source_desc),
                            linkLabel = stringResource(R.string.settings_about_app_source_link),
                            linkUrl = stringResource(R.string.settings_about_app_source_url)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_core_source),
                            body = stringResource(R.string.settings_about_core_source_desc),
                            linkLabel = stringResource(R.string.settings_about_core_source_link),
                            linkUrl = stringResource(R.string.settings_about_core_source_url)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_support_project),
                            body = stringResource(R.string.settings_about_support_project_desc),
                            linkLabel = stringResource(R.string.settings_about_support_project_link),
                            linkUrl = stringResource(R.string.settings_about_support_project_url)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_more_apps),
                            body = stringResource(R.string.settings_about_more_apps_desc),
                            linkLabel = stringResource(R.string.settings_about_more_apps_link),
                            linkUrl = stringResource(R.string.settings_about_more_apps_url)
                        )
                        AboutNote(
                            title = stringResource(R.string.settings_about_privacy_policy),
                            body = stringResource(R.string.settings_about_privacy_policy_desc),
                            linkLabel = stringResource(R.string.settings_about_privacy_policy_link),
                            linkUrl = stringResource(R.string.settings_about_privacy_policy_url)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun CustomizationSettingsTab(
    uiState: SettingsUiState,
    onPickBackground: () -> Unit,
    onPickCustomFont: () -> Unit,
    viewModel: SettingsViewModel
) {
    val context = LocalContext.current
    val density = LocalDensity.current
    val windowSize = LocalWindowInfo.current.containerSize
    val windowWidthDp = with(density) { windowSize.width.toDp().value.roundToInt() }.coerceAtLeast(1)
    val windowHeightDp = with(density) { windowSize.height.toDp().value.roundToInt() }.coerceAtLeast(1)
    val previewColumns = calculateHomeGridColumnCount(
        screenWidthDp = windowWidthDp,
        screenHeightDp = windowHeightDp,
        smallestScreenWidthDp = minOf(windowWidthDp, windowHeightDp),
        gridScale = uiState.homeGridScale
    )
    val backgroundRepository = remember(context) { HomeBackgroundRepository(context) }
    val backgroundFile = backgroundRepository.existingFile(uiState.homeBackgroundType)
    val backgroundLabel = when (uiState.homeBackgroundType) {
        HomeBackgroundType.NONE -> stringResource(R.string.settings_customization_background_none)
        HomeBackgroundType.IMAGE -> stringResource(R.string.settings_customization_background_image)
        HomeBackgroundType.GIF -> stringResource(R.string.settings_customization_background_gif)
        HomeBackgroundType.VIDEO -> stringResource(R.string.settings_customization_background_video)
    }

    SettingsSection(title = stringResource(R.string.settings_customization_preview)) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .height(210.dp),
            shape = RoundedCornerShape(22.dp),
            color = MaterialTheme.colorScheme.background,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
        ) {
            Box(modifier = Modifier.fillMaxSize()) {
                HomeBackgroundMedia(
                    type = uiState.homeBackgroundType,
                    file = backgroundFile,
                    revision = uiState.homeBackgroundRevision,
                    modifier = Modifier.fillMaxSize()
                )
                if (uiState.homeBackgroundType != HomeBackgroundType.NONE) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(
                                MaterialTheme.colorScheme.background.copy(
                                    alpha = uiState.homeBackgroundDim / 100f
                                )
                            )
                    )
                }
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Text(
                        text = stringResource(R.string.app_name),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onBackground
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = stringResource(R.string.settings_customization_preview_caption),
                            modifier = Modifier.weight(1f),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.78f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Surface(
                            shape = RoundedCornerShape(50),
                            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.9f),
                            border = BorderStroke(
                                1.dp,
                                MaterialTheme.colorScheme.primary.copy(alpha = 0.38f)
                            )
                        ) {
                            Text(
                                text = pluralStringResource(
                                    R.plurals.settings_customization_games_per_row,
                                    previewColumns,
                                    previewColumns
                                ),
                                modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                    }
                    Row(
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
                        verticalAlignment = Alignment.Bottom
                    ) {
                        repeat(previewColumns) { index ->
                            Surface(
                                modifier = Modifier
                                    .width(52.dp * uiState.homeGridScale)
                                    .aspectRatio(0.72f),
                                shape = RoundedCornerShape(10.dp),
                                color = when (index % 3) {
                                    0 -> MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.92f)
                                    1 -> MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.92f)
                                    else -> MaterialTheme.colorScheme.tertiaryContainer.copy(alpha = 0.92f)
                                },
                                border = BorderStroke(
                                    1.dp,
                                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)
                                )
                            ) {
                                Column(
                                    modifier = Modifier.padding(6.dp),
                                    verticalArrangement = Arrangement.Bottom
                                ) {
                                    Spacer(modifier = Modifier.weight(1f))
                                    Box(
                                        modifier = Modifier
                                            .fillMaxWidth(0.78f)
                                            .height(3.dp)
                                            .clip(RoundedCornerShape(50))
                                            .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.32f))
                                    )
                                    Spacer(modifier = Modifier.height(3.dp))
                                    Box(
                                        modifier = Modifier
                                            .fillMaxWidth(0.52f)
                                            .height(2.dp)
                                            .clip(RoundedCornerShape(50))
                                            .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f))
                                    )
                                }
                            }
                        }
                    }
                }
                if (uiState.isBackgroundImporting) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.35f)),
                        contentAlignment = Alignment.Center
                    ) {
                        CircularProgressIndicator()
                    }
                }
            }
        }
    }

    SettingsSection(title = stringResource(R.string.settings_customization_background_section)) {
        SettingsItem(
            icon = Icons.Rounded.Wallpaper,
            label = stringResource(R.string.settings_customization_background),
            value = backgroundLabel,
            onClick = onPickBackground,
            helpText = stringResource(R.string.settings_customization_background_help)
        )
        if (uiState.homeBackgroundType != HomeBackgroundType.NONE) {
            SliderItem(
                icon = Icons.Rounded.Visibility,
                title = stringResource(R.string.settings_customization_background_dim),
                subtitle = "",
                value = uiState.homeBackgroundDim.toFloat(),
                range = 0f..85f,
                steps = 16,
                onValueChange = { viewModel.setHomeBackgroundDim(it.roundToInt()) },
                valueLabel = { "${it.roundToInt()}%" },
                onResetToDefault = {
                    viewModel.setHomeBackgroundDim(AppPreferences.DEFAULT_HOME_BACKGROUND_DIM)
                }
            )
            SettingsItem(
                icon = Icons.Rounded.DeleteOutline,
                label = stringResource(R.string.settings_customization_remove_background),
                value = stringResource(R.string.settings_customization_remove_background_desc),
                onClick = viewModel::clearHomeBackground
            )
        }
    }

    SettingsSection(title = stringResource(R.string.settings_customization_library_section)) {
        SliderItem(
            icon = Icons.Rounded.Wallpaper,
            title = stringResource(R.string.settings_customization_grid_size),
            subtitle = "",
            value = uiState.homeGridScale,
            range = AppPreferences.MIN_HOME_GRID_SCALE..AppPreferences.MAX_HOME_GRID_SCALE,
            steps = 19,
            onValueChange = viewModel::setHomeGridScale,
            valueLabel = { "${(it * 100).roundToInt()}%" },
            helpText = stringResource(R.string.settings_customization_grid_size_help),
            onResetToDefault = {
                viewModel.setHomeGridScale(AppPreferences.DEFAULT_HOME_GRID_SCALE)
            }
        )
    }

    SettingsSection(title = stringResource(R.string.settings_customization_drawer_section)) {
        SettingsInlineNote(stringResource(R.string.settings_customization_drawer_summary))
        DrawerVisualStylePicker(
            selected = uiState.drawerVisualStyle,
            onSelect = viewModel::setDrawerVisualStyle
        )
        val groups = listOf(
            stringResource(R.string.shell_quick_actions) to listOf(
                DrawerItemId.LIBRARY,
                DrawerItemId.CATALOG_SEARCH,
                DrawerItemId.ACHIEVEMENTS,
                DrawerItemId.PROFILE
            ),
            stringResource(R.string.shell_executables_section) to listOf(
                DrawerItemId.LAUNCH_GAME,
                DrawerItemId.LAUNCH_BIOS
            ),
            stringResource(R.string.shell_app_section) to listOf(
                DrawerItemId.GAME_SETTINGS,
                DrawerItemId.DATA_TRANSFER,
                DrawerItemId.RESET_SETTINGS
            ),
            stringResource(R.string.shell_tools_section) to listOf(
                DrawerItemId.MEMORY_CARDS,
                DrawerItemId.TEXTURE_MANAGER,
                DrawerItemId.SAVE_STATES
            ),
            stringResource(R.string.settings_customization_drawer_other) to listOf(
                DrawerItemId.APP_SETTINGS,
                DrawerItemId.SUPPORTED_FORMATS,
                DrawerItemId.FEEDBACK,
                DrawerItemId.DISCORD
            )
        )
        groups.forEach { (title, items) ->
            Text(
                text = title,
                modifier = Modifier.padding(start = 20.dp, end = 20.dp, top = 6.dp),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.primary
            )
            items.forEach { item ->
                DrawerItemEditorRow(
                    icon = drawerItemIcon(item),
                    title = drawerItemLabel(item),
                    visible = item !in uiState.hiddenDrawerItems,
                    required = item.required,
                    onVisibleChange = { viewModel.setDrawerItemVisible(item, it) }
                )
            }
        }
        SettingsInlineNote(stringResource(R.string.settings_customization_drawer_required_note))
    }

    SettingsSection(title = stringResource(R.string.settings_customization_touch_controls_section)) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .height(124.dp),
            shape = RoundedCornerShape(22.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
        ) {
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 22.dp, vertical = 18.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                VectorAnalogStick(
                    analogSize = 76.dp,
                    visualStyle = uiState.touchControlVisualStyle,
                    pressEffect = uiState.touchControlPressEffect,
                    pressed = true,
                    interactive = false
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    VectorOverlayButton(
                        drawableRes = R.drawable.ic_controller_square_button,
                        width = 44.dp,
                        height = 44.dp,
                        visualStyle = uiState.touchControlVisualStyle,
                        pressEffect = uiState.touchControlPressEffect,
                        interactive = false
                    )
                    VectorOverlayButton(
                        drawableRes = R.drawable.ic_controller_cross_button,
                        width = 44.dp,
                        height = 44.dp,
                        visualStyle = uiState.touchControlVisualStyle,
                        pressEffect = uiState.touchControlPressEffect,
                        pressed = true,
                        interactive = false
                    )
                }
            }
        }
        ChoiceSection(
            title = stringResource(R.string.settings_customization_touch_controls_style),
            options = listOf(
                TouchControlVisualStyle.CLASSIC.preferenceValue to stringResource(R.string.settings_customization_touch_style_classic),
                TouchControlVisualStyle.LEGACY.preferenceValue to stringResource(R.string.settings_customization_touch_style_glass),
                TouchControlVisualStyle.MODERN.preferenceValue to stringResource(R.string.settings_customization_touch_style_neon),
                TouchControlVisualStyle.ARCADE.preferenceValue to stringResource(R.string.settings_customization_touch_style_arcade),
                TouchControlVisualStyle.MINIMAL.preferenceValue to stringResource(R.string.settings_customization_touch_style_minimal)
            ),
            selectedValue = uiState.touchControlVisualStyle.preferenceValue,
            onSelect = { value -> viewModel.setTouchControlVisualStyle(TouchControlVisualStyle.fromPreference(value)) },
            helpText = stringResource(R.string.settings_customization_touch_controls_help),
            onResetToDefault = { viewModel.setTouchControlVisualStyle(TouchControlVisualStyle.CLASSIC) }
        )
        ChoiceSection(
            title = stringResource(R.string.settings_customization_touch_press_effect),
            options = listOf(
                TouchControlPressEffect.GROW.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_grow),
                TouchControlPressEffect.SHRINK.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_shrink),
                TouchControlPressEffect.SPRING.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_spring),
                TouchControlPressEffect.GLOW.preferenceValue to stringResource(R.string.settings_customization_touch_press_effect_glow)
            ),
            selectedValue = uiState.touchControlPressEffect.preferenceValue,
            onSelect = { value ->
                viewModel.setTouchControlPressEffect(TouchControlPressEffect.fromPreference(value))
            },
            helpText = stringResource(R.string.settings_customization_touch_press_effect_help),
            onResetToDefault = { viewModel.setTouchControlPressEffect(TouchControlPressEffect.GROW) }
        )
    }

    SettingsSection(title = stringResource(R.string.settings_customization_text_section)) {
        ChoiceSection(
            title = stringResource(R.string.settings_customization_font),
            options = listOf(
                AppFontChoice.SYSTEM.preferenceValue to stringResource(R.string.settings_customization_font_system),
                AppFontChoice.RUBIK.preferenceValue to stringResource(R.string.settings_customization_font_rubik),
                AppFontChoice.EXO_2.preferenceValue to stringResource(R.string.settings_customization_font_exo2)
            ) + if (uiState.customFontName != null) {
                listOf(AppFontChoice.CUSTOM.preferenceValue to stringResource(R.string.settings_customization_font_custom))
            } else {
                emptyList()
            },
            selectedValue = uiState.appFontChoice.preferenceValue,
            onSelect = { value ->
                viewModel.setAppFontChoice(AppFontChoice.fromPreference(value))
            },
            helpText = stringResource(R.string.settings_customization_font_help),
            onResetToDefault = { viewModel.setAppFontChoice(AppFontChoice.SYSTEM) }
        )
        SettingsItem(
            icon = Icons.Rounded.FolderOpen,
            label = stringResource(R.string.settings_customization_import_font),
            value = uiState.customFontName
                ?: stringResource(R.string.settings_customization_import_font_desc),
            onClick = onPickCustomFont,
            helpText = stringResource(R.string.settings_customization_import_font_help)
        )
        if (uiState.customFontName != null) {
            SettingsItem(
                icon = Icons.Rounded.DeleteOutline,
                label = stringResource(R.string.settings_customization_remove_font),
                value = stringResource(R.string.settings_customization_remove_font_desc),
                onClick = viewModel::clearCustomFont
            )
        }
        SliderItem(
            icon = Icons.Rounded.FormatSize,
            title = stringResource(R.string.settings_customization_font_size),
            subtitle = "",
            value = uiState.appFontScale,
            range = AppPreferences.MIN_APP_FONT_SCALE..AppPreferences.MAX_APP_FONT_SCALE,
            steps = 14,
            onValueChange = viewModel::setAppFontScale,
            valueLabel = { "${(it * 100).roundToInt()}%" },
            helpText = stringResource(R.string.settings_customization_font_size_help),
            onResetToDefault = {
                viewModel.setAppFontScale(AppPreferences.DEFAULT_APP_FONT_SCALE)
            }
        )
    }

    SettingsSection(title = stringResource(R.string.settings_customization_reset_section)) {
        SettingsItem(
            icon = Icons.Rounded.Restore,
            label = stringResource(R.string.settings_customization_reset),
            value = stringResource(R.string.settings_customization_reset_desc),
            onClick = viewModel::resetCustomization
        )
    }
}

@Composable
private fun GameMenuSettingsTab(
    uiState: SettingsUiState,
    viewModel: SettingsViewModel
) {
    SettingsSection(title = stringResource(R.string.settings_game_menu_preview_section)) {
        SettingsInlineNote(stringResource(R.string.settings_game_menu_content_summary))
        GameMenuLayoutStylePicker(
            selected = uiState.gameMenuLayoutStyle,
            visibleTabs = uiState.gameMenuTabOrder.filterNot(uiState.hiddenGameMenuTabs::contains),
            onSelect = viewModel::setGameMenuLayoutStyle
        )
    }

    SettingsSection(title = stringResource(R.string.settings_game_menu_tabs_section)) {
        uiState.gameMenuTabOrder.forEachIndexed { index, tab ->
            GameMenuEditorRow(
                icon = gameMenuTabIcon(tab),
                title = gameMenuTabLabel(tab),
                visible = tab !in uiState.hiddenGameMenuTabs,
                required = tab == GameMenuTabId.SESSION,
                canMoveUp = index > 0,
                canMoveDown = index < uiState.gameMenuTabOrder.lastIndex,
                onVisibleChange = { viewModel.setGameMenuTabVisible(tab, it) },
                onMoveUp = { viewModel.moveGameMenuTab(tab, -1) },
                onMoveDown = { viewModel.moveGameMenuTab(tab, 1) }
            )
        }
    }

    uiState.gameMenuTabOrder.forEach { tab ->
        val sections = uiState.gameMenuSectionOrder.filter { it.tab == tab }
        SettingsSection(
            title = stringResource(
                R.string.settings_game_menu_tab_content_format,
                gameMenuTabLabel(tab)
            )
        ) {
            sections.forEachIndexed { index, section ->
                GameMenuSectionRow(
                    title = gameMenuSectionLabel(section),
                    visible = section !in uiState.hiddenGameMenuSections,
                    canMoveUp = index > 0,
                    canMoveDown = index < sections.lastIndex,
                    onVisibleChange = { viewModel.setGameMenuSectionVisible(section, it) },
                    onMoveUp = { viewModel.moveGameMenuSection(section, -1) },
                    onMoveDown = { viewModel.moveGameMenuSection(section, 1) }
                )
            }
            if (tab == GameMenuTabId.SESSION) {
                SettingsInlineNote(stringResource(R.string.settings_game_menu_session_safety_note))
            }
        }
    }

    SettingsSection(title = stringResource(R.string.settings_game_menu_reset_section)) {
        SettingsItem(
            icon = Icons.Rounded.Restore,
            label = stringResource(R.string.settings_game_menu_reset),
            value = stringResource(R.string.settings_game_menu_full_reset_desc),
            onClick = viewModel::resetGameMenuCustomization
        )
    }
}

@Composable
private fun GameMenuLayoutStylePicker(
    selected: GameMenuLayoutStyle,
    visibleTabs: List<GameMenuTabId>,
    onSelect: (GameMenuLayoutStyle) -> Unit
) {
    Text(
        text = stringResource(R.string.settings_game_menu_layout_section),
        modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp),
        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
        color = MaterialTheme.colorScheme.primary
    )
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        items(GameMenuLayoutStyle.entries, key = { it.name }) { style ->
            VisualStylePreviewCard(
                title = gameMenuLayoutStyleLabel(style),
                selected = selected == style,
                onClick = { onSelect(style) }
            ) {
                GameMenuLayoutMiniature(style = style, tabCount = visibleTabs.size.coerceAtLeast(1))
            }
        }
    }
    SettingsInlineNote(stringResource(R.string.settings_game_menu_layout_help))
}

@Composable
private fun DrawerVisualStylePicker(
    selected: DrawerVisualStyle,
    onSelect: (DrawerVisualStyle) -> Unit
) {
    Text(
        text = stringResource(R.string.settings_customization_drawer_style),
        modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp),
        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
        color = MaterialTheme.colorScheme.primary
    )
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        items(DrawerVisualStyle.entries, key = { it.name }) { style ->
            VisualStylePreviewCard(
                title = drawerVisualStyleLabel(style),
                selected = selected == style,
                onClick = { onSelect(style) }
            ) {
                DrawerStyleMiniature(style)
            }
        }
    }
}

@Composable
private fun VisualStylePreviewCard(
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
    preview: @Composable () -> Unit
) {
    val shape = RoundedCornerShape(18.dp)
    Surface(
        modifier = Modifier
            .width(176.dp)
            .height(132.dp)
            .clickable(onClick = onClick),
        shape = shape,
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.48f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.30f)
        },
        border = BorderStroke(
            if (selected) 2.dp else 1.dp,
            if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
        )
    ) {
        Column(
            modifier = Modifier.padding(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.background.copy(alpha = 0.72f))
                    .padding(7.dp)
            ) {
                preview()
            }
            Text(
                text = title,
                modifier = Modifier.fillMaxWidth(),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun GameMenuLayoutMiniature(style: GameMenuLayoutStyle, tabCount: Int) {
    val panelColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.88f)
    val navColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.24f)
    when (style) {
        GameMenuLayoutStyle.SIDEBAR -> Row(
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.End
        ) {
            MiniatureContentPanel(Modifier.fillMaxHeight().weight(1f), panelColor)
            Spacer(Modifier.width(5.dp))
            MiniatureVerticalTabs(Modifier.fillMaxHeight().width(20.dp), tabCount, navColor)
        }

        GameMenuLayoutStyle.DASHBOARD -> Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 3.dp, vertical = 5.dp)
                .clip(RoundedCornerShape(7.dp))
                .background(panelColor)
        ) {
            MiniatureVerticalTabs(Modifier.fillMaxHeight().width(38.dp), tabCount, navColor, labelled = true)
            MiniatureContentPanel(Modifier.fillMaxHeight().weight(1f), MaterialTheme.colorScheme.surface.copy(alpha = 0.82f))
        }

        GameMenuLayoutStyle.COMMAND_CENTER -> Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.BottomCenter
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .fillMaxHeight(0.78f)
                    .clip(RoundedCornerShape(topStart = 10.dp, topEnd = 10.dp))
                    .background(panelColor)
                    .padding(5.dp),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                MiniatureHorizontalTabs(Modifier.fillMaxWidth().height(16.dp), tabCount, navColor)
                MiniatureContentPanel(Modifier.fillMaxSize(), MaterialTheme.colorScheme.surface.copy(alpha = 0.72f))
            }
        }

        GameMenuLayoutStyle.COMPACT -> Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.CenterEnd
        ) {
            Column(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(0.64f)
                    .clip(RoundedCornerShape(7.dp))
                    .background(panelColor)
                    .padding(5.dp),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                MiniatureHorizontalTabs(Modifier.fillMaxWidth().height(14.dp), tabCount, navColor)
                MiniatureContentPanel(Modifier.fillMaxSize(), MaterialTheme.colorScheme.surface.copy(alpha = 0.72f))
            }
        }
    }
}

@Composable
private fun DrawerStyleMiniature(style: DrawerVisualStyle) {
    val shape = when (style) {
        DrawerVisualStyle.CLASSIC -> RoundedCornerShape(9.dp)
        DrawerVisualStyle.COMPACT -> RoundedCornerShape(3.dp)
        DrawerVisualStyle.GLASS -> RoundedCornerShape(13.dp)
        DrawerVisualStyle.CONSOLE -> RoundedCornerShape(2.dp)
    }
    val panelWidth = when (style) {
        DrawerVisualStyle.COMPACT -> 0.66f
        DrawerVisualStyle.CONSOLE -> 0.90f
        else -> 0.78f
    }
    val rowHeight = when (style) {
        DrawerVisualStyle.COMPACT -> 11.dp
        DrawerVisualStyle.CONSOLE -> 18.dp
        else -> 15.dp
    }
    val rowColor = when (style) {
        DrawerVisualStyle.GLASS -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
        DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.76f)
        else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.82f)
    }
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.CenterStart) {
        Column(
            modifier = Modifier
                .fillMaxHeight()
                .fillMaxWidth(panelWidth)
                .clip(shape)
                .background(
                    if (style == DrawerVisualStyle.GLASS) {
                        MaterialTheme.colorScheme.surface.copy(alpha = 0.62f)
                    } else {
                        MaterialTheme.colorScheme.surface.copy(alpha = 0.92f)
                    }
                )
                .padding(if (style == DrawerVisualStyle.COMPACT) 5.dp else 7.dp),
            verticalArrangement = Arrangement.spacedBy(if (style == DrawerVisualStyle.COMPACT) 4.dp else 6.dp)
        ) {
            repeat(if (style == DrawerVisualStyle.COMPACT) 5 else 4) { index ->
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(rowHeight)
                        .clip(shape)
                        .background(if (index == 0) MaterialTheme.colorScheme.primary.copy(alpha = 0.30f) else rowColor)
                )
            }
        }
    }
}

@Composable
private fun MiniatureContentPanel(modifier: Modifier, color: Color) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(7.dp))
            .background(color)
            .padding(7.dp),
        verticalArrangement = Arrangement.spacedBy(5.dp)
    ) {
        Box(Modifier.fillMaxWidth(0.62f).height(8.dp).clip(RoundedCornerShape(4.dp)).background(MaterialTheme.colorScheme.primary.copy(alpha = 0.34f)))
        repeat(3) {
            Box(Modifier.fillMaxWidth().height(7.dp).clip(RoundedCornerShape(4.dp)).background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.10f)))
        }
    }
}

@Composable
private fun MiniatureVerticalTabs(
    modifier: Modifier,
    tabCount: Int,
    color: Color,
    labelled: Boolean = false
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(7.dp))
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.78f))
            .padding(4.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        repeat(tabCount.coerceAtMost(4)) { index ->
            Box(
                Modifier
                    .fillMaxWidth(if (labelled) 1f else 0.85f)
                    .height(if (labelled) 11.dp else 9.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(if (index == 0) color else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.09f))
            )
        }
    }
}

@Composable
private fun MiniatureHorizontalTabs(modifier: Modifier, tabCount: Int, color: Color) {
    Row(modifier = modifier, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        repeat(tabCount.coerceAtMost(5)) { index ->
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(4.dp))
                    .background(if (index == 0) color else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.09f))
            )
        }
    }
}

@Composable
private fun gameMenuLayoutStyleLabel(style: GameMenuLayoutStyle): String = stringResource(
    when (style) {
        GameMenuLayoutStyle.SIDEBAR -> R.string.settings_game_menu_layout_sidebar
        GameMenuLayoutStyle.DASHBOARD -> R.string.settings_game_menu_layout_dashboard
        GameMenuLayoutStyle.COMMAND_CENTER -> R.string.settings_game_menu_layout_command_center
        GameMenuLayoutStyle.COMPACT -> R.string.settings_game_menu_layout_compact
    }
)

@Composable
private fun drawerVisualStyleLabel(style: DrawerVisualStyle): String = stringResource(
    when (style) {
        DrawerVisualStyle.CLASSIC -> R.string.settings_drawer_style_classic
        DrawerVisualStyle.COMPACT -> R.string.settings_drawer_style_compact
        DrawerVisualStyle.GLASS -> R.string.settings_drawer_style_glass
        DrawerVisualStyle.CONSOLE -> R.string.settings_drawer_style_console
    }
)

@Composable
private fun DrawerItemEditorRow(
    icon: ImageVector,
    title: String,
    visible: Boolean,
    required: Boolean,
    onVisibleChange: (Boolean) -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(22.dp)
            )
            Text(
                text = title,
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 12.dp),
                style = MaterialTheme.typography.bodyLarge
            )
            if (required) {
                Icon(
                    imageVector = Icons.Rounded.Lock,
                    contentDescription = stringResource(R.string.settings_game_menu_required),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier
                        .padding(12.dp)
                        .size(20.dp)
                )
            } else {
                Switch(checked = visible, onCheckedChange = onVisibleChange)
            }
        }
    }
}

private fun drawerItemIcon(item: DrawerItemId): ImageVector = when (item) {
    DrawerItemId.LIBRARY -> Icons.Rounded.Home
    DrawerItemId.CATALOG_SEARCH -> Icons.Rounded.Search
    DrawerItemId.ACHIEVEMENTS -> Icons.Rounded.Star
    DrawerItemId.PROFILE -> Icons.Rounded.Person
    DrawerItemId.LAUNCH_GAME, DrawerItemId.LAUNCH_BIOS -> Icons.Rounded.PlayArrow
    DrawerItemId.GAME_SETTINGS -> Icons.Rounded.Tune
    DrawerItemId.DATA_TRANSFER -> Icons.Rounded.SwapVert
    DrawerItemId.RESET_SETTINGS -> Icons.Rounded.Restore
    DrawerItemId.MEMORY_CARDS, DrawerItemId.SUPPORTED_FORMATS -> Icons.Rounded.Memory
    DrawerItemId.TEXTURE_MANAGER -> Icons.Rounded.FolderOpen
    DrawerItemId.SAVE_STATES -> Icons.Rounded.Save
    DrawerItemId.APP_SETTINGS -> Icons.Rounded.SettingsSuggest
    DrawerItemId.FEEDBACK -> Icons.Rounded.RateReview
    DrawerItemId.DISCORD -> Icons.Rounded.Forum
}

@Composable
private fun drawerItemLabel(item: DrawerItemId): String = when (item) {
    DrawerItemId.LIBRARY -> stringResource(R.string.shell_library)
    DrawerItemId.CATALOG_SEARCH -> stringResource(R.string.shell_catalog_search)
    DrawerItemId.ACHIEVEMENTS -> stringResource(R.string.settings_achievements_tab)
    DrawerItemId.PROFILE -> stringResource(R.string.profile_title)
    DrawerItemId.LAUNCH_GAME -> stringResource(R.string.shell_launch_game)
    DrawerItemId.LAUNCH_BIOS -> stringResource(R.string.shell_launch_bios)
    DrawerItemId.GAME_SETTINGS -> stringResource(R.string.shell_game_settings_manager)
    DrawerItemId.DATA_TRANSFER -> stringResource(R.string.shell_data_transfer)
    DrawerItemId.RESET_SETTINGS -> stringResource(R.string.settings_reset_all_action)
    DrawerItemId.MEMORY_CARDS -> stringResource(R.string.shell_memory_cards)
    DrawerItemId.TEXTURE_MANAGER -> stringResource(R.string.shell_texture_manager)
    DrawerItemId.SAVE_STATES -> stringResource(R.string.shell_save_states)
    DrawerItemId.APP_SETTINGS -> stringResource(R.string.shell_app_settings)
    DrawerItemId.SUPPORTED_FORMATS -> stringResource(R.string.shell_supported_formats)
    DrawerItemId.FEEDBACK -> stringResource(R.string.feedback_title)
    DrawerItemId.DISCORD -> stringResource(R.string.shell_discord_server)
}

@Composable
private fun GameMenuEditorRow(
    icon: ImageVector,
    title: String,
    visible: Boolean,
    required: Boolean,
    canMoveUp: Boolean,
    canMoveDown: Boolean,
    onVisibleChange: (Boolean) -> Unit,
    onMoveUp: () -> Unit,
    onMoveDown: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(icon, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(22.dp))
            Text(title, modifier = Modifier.weight(1f).padding(start = 12.dp), style = MaterialTheme.typography.bodyLarge)
            IconButton(onClick = onMoveUp, enabled = canMoveUp) { Icon(Icons.Rounded.KeyboardArrowUp, null) }
            IconButton(onClick = onMoveDown, enabled = canMoveDown) { Icon(Icons.Rounded.KeyboardArrowDown, null) }
            if (required) {
                Icon(Icons.Rounded.Lock, stringResource(R.string.settings_game_menu_required), modifier = Modifier.padding(12.dp).size(20.dp))
            } else {
                Switch(checked = visible, onCheckedChange = onVisibleChange)
            }
        }
    }
}

@Composable
private fun GameMenuSectionRow(
    title: String,
    visible: Boolean,
    canMoveUp: Boolean,
    canMoveDown: Boolean,
    onVisibleChange: (Boolean) -> Unit,
    onMoveUp: () -> Unit,
    onMoveDown: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(title, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
            IconButton(onClick = onMoveUp, enabled = canMoveUp) {
                Icon(Icons.Rounded.KeyboardArrowUp, null)
            }
            IconButton(onClick = onMoveDown, enabled = canMoveDown) {
                Icon(Icons.Rounded.KeyboardArrowDown, null)
            }
            Switch(checked = visible, onCheckedChange = onVisibleChange)
        }
    }
}

@Composable
private fun gameMenuTabLabel(tab: GameMenuTabId): String = when (tab) {
    GameMenuTabId.SESSION -> stringResource(R.string.emulation_session_tab)
    GameMenuTabId.CONTROLS -> stringResource(R.string.settings_controls_tab)
    GameMenuTabId.EMULATION -> stringResource(R.string.settings_emulation_tab)
    GameMenuTabId.GRAPHICS -> stringResource(R.string.settings_graphics_tab)
    GameMenuTabId.FIXES -> stringResource(R.string.settings_fixes_tab)
    GameMenuTabId.ACHIEVEMENTS -> stringResource(R.string.emulation_achievements_tab)
}

private fun gameMenuTabIcon(tab: GameMenuTabId): ImageVector = when (tab) {
    GameMenuTabId.SESSION -> Icons.Rounded.MoreVert
    GameMenuTabId.CONTROLS -> Icons.Rounded.Gamepad
    GameMenuTabId.EMULATION -> Icons.Rounded.SettingsSuggest
    GameMenuTabId.GRAPHICS -> Icons.Rounded.Wallpaper
    GameMenuTabId.FIXES -> Icons.Rounded.Star
    GameMenuTabId.ACHIEVEMENTS -> Icons.Rounded.Lock
}

@Composable
private fun gameMenuSectionLabel(section: GameMenuSectionId): String = when (section) {
    GameMenuSectionId.SAVE_STATES -> stringResource(R.string.settings_game_menu_section_save_states)
    GameMenuSectionId.AUTO_SAVE -> stringResource(R.string.settings_game_menu_section_auto_save)
    GameMenuSectionId.QUICK_ACTIONS -> stringResource(R.string.settings_game_menu_section_quick_actions)
    GameMenuSectionId.AUTOMATION -> stringResource(R.string.settings_game_menu_section_automation)
    GameMenuSectionId.GAME_PROFILE -> stringResource(R.string.settings_game_menu_section_game_profile)
    GameMenuSectionId.SESSION_DEBUG_TOOLS -> stringResource(R.string.settings_game_menu_section_debug_tools)
    GameMenuSectionId.CONTROLS_GENERAL -> stringResource(R.string.settings_game_menu_section_controls_general)
    GameMenuSectionId.CONTROLS_TOUCH -> stringResource(R.string.settings_touch_controls_section)
    GameMenuSectionId.CONTROLS_GAMEPAD -> stringResource(R.string.settings_gamepad_controls_section)
    GameMenuSectionId.EMULATION_PERFORMANCE -> stringResource(R.string.emulation_performance_stats)
    GameMenuSectionId.EMULATION_SPEED -> stringResource(R.string.settings_speed_hacks)
    GameMenuSectionId.EMULATION_CHEATS -> stringResource(R.string.settings_enable_cheats)
    GameMenuSectionId.GRAPHICS_DISPLAY -> stringResource(R.string.settings_game_menu_section_graphics_display)
    GameMenuSectionId.GRAPHICS_RENDERING -> stringResource(R.string.settings_rendering_section)
    GameMenuSectionId.GRAPHICS_SCREEN -> stringResource(R.string.emulation_screen_tab)
    GameMenuSectionId.FIXES_PATCHES -> stringResource(R.string.settings_patches_section)
    GameMenuSectionId.FIXES_HARDWARE -> stringResource(R.string.settings_hardware_fixes)
    GameMenuSectionId.FIXES_UPSCALING -> stringResource(R.string.settings_upscaling_fixes)
    GameMenuSectionId.ACHIEVEMENTS_PROGRESS -> stringResource(R.string.emulation_achievements_tab)
}

@Composable
private fun NetworkSettingsTab(
    uiState: SettingsUiState,
    context: android.content.Context,
    defaults: SettingsSnapshot,
    viewModel: SettingsViewModel
) {
    val adapters = remember(context) {
        NetworkAdapterCollector.collectAdapters(context)
            .filter { adapter ->
                adapter.isUp && !adapter.isLoopback && adapter.ipAddresses.any { !it.contains(':') }
            }
    }
    val devices = remember(adapters, uiState.dev9EthernetDevice) {
        buildList {
            add("Auto" to "Auto")
            adapters.forEach { adapter ->
                add(adapter.name to "${adapter.displayName} (${adapter.name})")
            }
            if (uiState.dev9EthernetDevice != "Auto" && none { it.first == uiState.dev9EthernetDevice }) {
                add(uiState.dev9EthernetDevice to uiState.dev9EthernetDevice)
            }
        }.distinctBy { it.first }
    }
    val dnsModes = listOf(
        AppPreferences.DEV9_DNS_MODE_AUTO to stringResource(R.string.settings_network_dns_mode_auto),
        AppPreferences.DEV9_DNS_MODE_MANUAL to stringResource(R.string.settings_network_dns_mode_manual),
        AppPreferences.DEV9_DNS_MODE_INTERNAL to stringResource(R.string.settings_network_dns_mode_internal)
    )

    SettingsSection(title = stringResource(R.string.settings_network_tab)) {
        SettingsInlineNote(stringResource(R.string.settings_network_summary))
        ToggleItem(
            icon = Icons.Rounded.Link,
            title = stringResource(R.string.settings_network_enable),
            subtitle = stringResource(R.string.settings_network_enable_desc),
            checked = uiState.dev9EthernetEnabled,
            onCheckedChange = viewModel::setDev9EthernetEnabled,
            helpText = stringResource(R.string.settings_network_enable_help),
            onResetToDefault = { viewModel.setDev9EthernetEnabled(defaults.dev9EthernetEnabled) }
        )
        SettingsItem(
            icon = Icons.Rounded.Link,
            label = stringResource(R.string.settings_network_api),
            value = stringResource(R.string.settings_network_api_sockets),
            onClick = {}
        )
        ChoiceSection(
            title = stringResource(R.string.settings_network_adapter),
            options = devices.mapIndexed { index, (_, label) -> index to label },
            selectedValue = devices.indexOfFirst { it.first == uiState.dev9EthernetDevice }.coerceAtLeast(0),
            onSelect = { index -> devices.getOrNull(index)?.first?.let(viewModel::setDev9EthernetDevice) },
            helpText = stringResource(R.string.settings_network_adapter_help),
            onResetToDefault = { viewModel.setDev9EthernetDevice(defaults.dev9EthernetDevice) }
        )
        ChoiceSection(
            title = stringResource(R.string.settings_network_dns_preset),
            options = listOf(
                0 to stringResource(R.string.settings_network_dns_preset_system),
                1 to stringResource(R.string.settings_network_dns_preset_ps2online),
                2 to stringResource(R.string.settings_network_dns_preset_psrewired)
            ),
            selectedValue = when (uiState.dev9Dns1Mode) {
                AppPreferences.DEV9_DNS_MODE_MANUAL -> when (uiState.dev9Dns1) {
                    "45.7.228.197" -> 1
                    "67.222.156.250" -> 2
                    else -> 0
                }
                else -> 0
            },
            onSelect = { preset ->
                when (preset) {
                    1 -> {
                        viewModel.setDev9Dns1Mode(AppPreferences.DEV9_DNS_MODE_MANUAL)
                        viewModel.setDev9Dns1("45.7.228.197")
                    }
                    2 -> {
                        viewModel.setDev9Dns1Mode(AppPreferences.DEV9_DNS_MODE_MANUAL)
                        viewModel.setDev9Dns1("67.222.156.250")
                    }
                    else -> {
                        viewModel.setDev9Dns1Mode(AppPreferences.DEV9_DNS_MODE_AUTO)
                        viewModel.setDev9Dns1("0.0.0.0")
                    }
                }
            },
            helpText = stringResource(R.string.settings_network_dns_preset_help)
        )
        DnsModeSetting(
            title = stringResource(R.string.settings_network_dns1_mode),
            addressTitle = stringResource(R.string.settings_network_dns1),
            mode = uiState.dev9Dns1Mode,
            address = uiState.dev9Dns1,
            modes = dnsModes,
            onModeChange = viewModel::setDev9Dns1Mode,
            onAddressChange = viewModel::setDev9Dns1
        )
        DnsModeSetting(
            title = stringResource(R.string.settings_network_dns2_mode),
            addressTitle = stringResource(R.string.settings_network_dns2),
            mode = uiState.dev9Dns2Mode,
            address = uiState.dev9Dns2,
            modes = dnsModes,
            onModeChange = viewModel::setDev9Dns2Mode,
            onAddressChange = viewModel::setDev9Dns2
        )
        ToggleItem(
            icon = Icons.Rounded.Link,
            title = stringResource(R.string.settings_network_intercept_dhcp),
            subtitle = stringResource(R.string.settings_network_intercept_dhcp_desc),
            checked = uiState.dev9InterceptDhcp,
            onCheckedChange = viewModel::setDev9InterceptDhcp,
            onResetToDefault = { viewModel.setDev9InterceptDhcp(defaults.dev9InterceptDhcp) }
        )
        ToggleItem(
            icon = Icons.Rounded.Info,
            title = stringResource(R.string.settings_network_log_dhcp),
            subtitle = stringResource(R.string.settings_network_log_dhcp_desc),
            checked = uiState.dev9LogDhcp,
            onCheckedChange = viewModel::setDev9LogDhcp,
            onResetToDefault = { viewModel.setDev9LogDhcp(defaults.dev9LogDhcp) }
        )
        ToggleItem(
            icon = Icons.Rounded.Info,
            title = stringResource(R.string.settings_network_log_dns),
            subtitle = stringResource(R.string.settings_network_log_dns_desc),
            checked = uiState.dev9LogDns,
            onCheckedChange = viewModel::setDev9LogDns,
            onResetToDefault = { viewModel.setDev9LogDns(defaults.dev9LogDns) }
        )
    }
}

@Composable
private fun DnsModeSetting(
    title: String,
    addressTitle: String,
    mode: String,
    address: String,
    modes: List<Pair<String, String>>,
    onModeChange: (String) -> Unit,
    onAddressChange: (String) -> Unit
) {
    ChoiceSection(
        title = title,
        options = modes.mapIndexed { index, (_, label) -> index to label },
        selectedValue = modes.indexOfFirst { it.first == mode }.coerceAtLeast(0),
        onSelect = { index -> modes.getOrNull(index)?.first?.let(onModeChange) },
        helpText = stringResource(R.string.settings_network_dns_mode_help)
    )
    if (mode == AppPreferences.DEV9_DNS_MODE_MANUAL) {
        var draft by remember(address) { mutableStateOf(address) }
        val valid = remember(draft) { isValidIpv4(draft) }
        OutlinedTextField(
            value = draft,
            onValueChange = { value ->
                draft = value.filter { it.isDigit() || it == '.' }.take(15)
                if (isValidIpv4(draft)) onAddressChange(draft)
            },
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .skipGamepadTextFieldFocus(),
            label = { Text(addressTitle) },
            supportingText = {
                Text(stringResource(if (valid) R.string.settings_network_dns_address_desc else R.string.settings_network_dns_invalid))
            },
            isError = !valid,
            singleLine = true
        )
    }
}

private fun isValidIpv4(value: String): Boolean {
    val parts = value.split('.')
    return parts.size == 4 && parts.all { part ->
        part.isNotEmpty() && part.length <= 3 && part.toIntOrNull() in 0..255
    }
}

@Composable
private fun ProSettingsTab(
    uiState: SettingsUiState,
    onPurchase: () -> Unit,
    onRestore: () -> Unit,
    onApplyCrimson: () -> Unit
) {
    SettingsSection(title = stringResource(R.string.settings_pro_title)) {
        ProStatusCard(
            isUnlocked = uiState.isProUnlocked,
            price = when {
                uiState.isProUnlocked -> stringResource(R.string.settings_pro_purchased)
                uiState.proPrice != null -> uiState.proPrice
                uiState.isProProductLoading -> stringResource(R.string.pro_price_loading)
                else -> stringResource(R.string.pro_price_unavailable)
            },
            purchaseInProgress = uiState.isProPurchaseInProgress,
            onPurchase = onPurchase,
            onRestore = onRestore,
            onApplyCrimson = onApplyCrimson
        )
        Text(
            text = stringResource(R.string.settings_pro_included_title),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier
                .padding(horizontal = 16.dp)
                .padding(top = 4.dp)
        )
        ProFeatureRow(
            title = stringResource(R.string.settings_pro_feature_crimson_title),
            description = stringResource(R.string.settings_pro_feature_crimson_desc),
            active = uiState.isProUnlocked
        )
        ProFeatureRow(
            title = stringResource(R.string.settings_pro_feature_icon_title),
            description = stringResource(R.string.settings_pro_feature_icon_desc),
            active = uiState.isProUnlocked
        )
        ProFeatureRow(
            title = stringResource(R.string.settings_pro_feature_profile_title),
            description = stringResource(R.string.settings_pro_feature_profile_desc),
            active = uiState.isProUnlocked
        )
        ProFeatureRow(
            title = stringResource(R.string.settings_pro_feature_stats_title),
            description = stringResource(R.string.settings_pro_feature_stats_desc),
            active = uiState.isProUnlocked
        )
        ProFeatureRow(
            title = stringResource(R.string.settings_pro_feature_card_title),
            description = stringResource(R.string.settings_pro_feature_card_desc),
            active = uiState.isProUnlocked
        )
        ProFeatureRow(
            title = stringResource(R.string.settings_pro_feature_support_title),
            description = stringResource(R.string.settings_pro_feature_support_desc),
            active = uiState.isProUnlocked
        )
    }
}

@Composable
private fun ProStatusCard(
    isUnlocked: Boolean,
    price: String,
    purchaseInProgress: Boolean,
    onPurchase: () -> Unit,
    onRestore: () -> Unit,
    onApplyCrimson: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        shape = RoundedCornerShape(20.dp),
        color = if (isUnlocked) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f)
        },
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(44.dp)
                        .clip(RoundedCornerShape(14.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Star,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(24.dp)
                    )
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(
                            if (isUnlocked) R.string.settings_pro_unlocked_title
                            else R.string.settings_pro_locked_title
                        ),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(
                            if (isUnlocked) R.string.settings_pro_unlocked_body
                            else R.string.settings_pro_locked_body
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                ProMetaPill(
                    label = stringResource(R.string.settings_pro_status_label),
                    value = if (isUnlocked) stringResource(R.string.settings_pro_active) else stringResource(R.string.settings_pro_not_active),
                    modifier = Modifier.weight(1f)
                )
                ProMetaPill(
                    label = stringResource(
                        if (isUnlocked) R.string.settings_pro_purchase_label
                        else R.string.settings_pro_price_label
                    ),
                    value = price,
                    modifier = Modifier.weight(1f)
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                if (isUnlocked) {
                    Button(
                        onClick = onApplyCrimson,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(16.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.settings_pro_apply_theme),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                } else {
                    OutlinedButton(
                        onClick = onRestore,
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(16.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.settings_pro_restore),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Button(
                        onClick = onPurchase,
                        enabled = !purchaseInProgress,
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(16.dp)
                    ) {
                        Text(
                            text = stringResource(
                                if (purchaseInProgress) R.string.pro_purchase_busy
                                else R.string.settings_pro_buy
                            ),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ProMetaPill(
    label: String,
    value: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = value,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun ProFeatureRow(
    title: String,
    description: String,
    active: Boolean
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.24f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(
                        if (active) MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
                        else MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.Star,
                    contentDescription = null,
                    tint = if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(19.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun AboutNote(
    title: String,
    body: String,
    linkLabel: String? = null,
    linkUrl: String? = null
) {
    val context = LocalContext.current
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding, vertical = 4.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.24f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (!linkLabel.isNullOrBlank() && !linkUrl.isNullOrBlank()) {
                Text(
                    text = linkLabel,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
                    color = MaterialTheme.colorScheme.primary,
                    textDecoration = TextDecoration.Underline,
                    modifier = Modifier.clickable { openUriInChrome(context, linkUrl) }
                )
            }
        }
    }
}

private fun openUriInChrome(context: android.content.Context, url: String) {
    val uri = url.toUri()
    val chromeIntent = Intent(Intent.ACTION_VIEW, uri).apply {
        setPackage("com.android.chrome")
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
    val fallbackIntent = Intent(Intent.ACTION_VIEW, uri).apply {
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
    try {
        context.startActivity(chromeIntent)
    } catch (_: ActivityNotFoundException) {
        context.startActivity(fallbackIntent)
    }
}

private fun normalizeSettingsSearchToken(value: String): String {
    return value
        .lowercase()
        .replace(Regex("[^\\p{L}\\p{N}]+"), " ")
        .trim()
}

@Composable
private fun ThemeSelector(
    selected: ThemeMode,
    isProUnlocked: Boolean,
    onSelected: (ThemeMode) -> Unit,
    onProLockedSelected: () -> Unit
) {
    ChoiceSection(
        title = stringResource(R.string.settings_theme),
        options = listOf(
            0 to stringResource(R.string.settings_theme_system),
            1 to stringResource(R.string.settings_theme_light),
            2 to stringResource(R.string.settings_theme_dark),
            3 to if (isProUnlocked) {
                stringResource(R.string.settings_theme_pro)
            } else {
                stringResource(R.string.settings_theme_pro_locked)
            }
        ),
        selectedValue = when (selected) {
            ThemeMode.SYSTEM -> 0
            ThemeMode.LIGHT -> 1
            ThemeMode.DARK -> 2
            ThemeMode.PRO -> 3
        },
        onResetToDefault = { onSelected(ThemeMode.SYSTEM) },
        onSelect = { value ->
            when (value) {
                1 -> onSelected(ThemeMode.LIGHT)
                2 -> onSelected(ThemeMode.DARK)
                3 -> if (isProUnlocked) onSelected(ThemeMode.PRO) else onProLockedSelected()
                else -> onSelected(ThemeMode.SYSTEM)
            }
        }
    )
}

@Composable
private fun SettingsSection(
    title: String,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
        )
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 2.dp
        ) {
            Column(
                modifier = Modifier.padding(vertical = 16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                content = content
            )
        }
    }
}

@Composable
private fun SettingsItem(
    icon: ImageVector,
    label: String,
    value: String,
    onClick: () -> Unit,
    helpText: String? = null
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
        onClick = debouncedClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Spacer(modifier = Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = label,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    helpText?.let {
                        SettingHelpButton(title = label, description = it)
                    }
                }
                Text(
                    text = value,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun CoverUrlExampleRow(
    label: String,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(shape = RoundedCornerShape(14.dp))
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick,
                onLongClick = onLongClick
            ),
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp)
        )
    }
}

private data class SettingsSearchEntry(
    val tab: SettingsTab,
    val title: String,
    val summary: String
)

@Composable
private fun rememberSettingsSearchEntries(): List<SettingsSearchEntry> {
    @Composable
    fun entry(tab: SettingsTab, @StringRes titleRes: Int): SettingsSearchEntry {
        return SettingsSearchEntry(tab = tab, title = stringResource(titleRes), summary = tab.label())
    }
    return listOfNotNull(
        entry(SettingsTab.General, R.string.settings_language),
        entry(SettingsTab.General, R.string.settings_theme),
        entry(SettingsTab.Customization, R.string.settings_customization_background),
        entry(SettingsTab.Customization, R.string.settings_customization_grid_size),
        entry(SettingsTab.Customization, R.string.settings_customization_font),
        entry(SettingsTab.Customization, R.string.settings_customization_font_size),
        entry(SettingsTab.Customization, R.string.settings_customization_touch_controls_style),
        entry(SettingsTab.Customization, R.string.settings_customization_drawer_style),
        entry(SettingsTab.Customization, R.string.settings_customization_touch_press_effect),
        entry(SettingsTab.GameMenu, R.string.settings_game_menu_tabs_section),
        entry(SettingsTab.GameMenu, R.string.settings_game_menu_layout_section),
        entry(SettingsTab.GameMenu, R.string.settings_game_menu_session_sections),
        entry(SettingsTab.Pro, R.string.settings_pro_title),
        entry(SettingsTab.General, R.string.settings_keep_screen_on),
        entry(SettingsTab.General, R.string.settings_back_button_exits_game),
        entry(SettingsTab.General, R.string.settings_confirm_save_load_actions),
        entry(SettingsTab.General, R.string.settings_show_recent_games),
        entry(SettingsTab.General, R.string.settings_show_home_search),
        entry(SettingsTab.General, R.string.settings_prefer_english_game_titles),
        entry(SettingsTab.Graphics, R.string.settings_renderer),
        if (GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers()) entry(SettingsTab.Graphics, R.string.settings_gpu_driver) else null,
        if (GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers()) entry(SettingsTab.Graphics, R.string.settings_gpu_driver_manager_title) else null,
        entry(SettingsTab.Graphics, R.string.settings_upscale),
        entry(SettingsTab.Graphics, R.string.settings_aspect_ratio),
        entry(SettingsTab.Graphics, R.string.settings_bilinear_filtering),
        entry(SettingsTab.Graphics, R.string.settings_trilinear_filtering),
        entry(SettingsTab.Graphics, R.string.settings_hw_download_mode),
        entry(SettingsTab.Graphics, R.string.settings_blending_accuracy),
        entry(SettingsTab.Graphics, R.string.settings_texture_preloading),
        entry(SettingsTab.Graphics, R.string.settings_anisotropic_filtering),
        entry(SettingsTab.Graphics, R.string.settings_fxaa),
        entry(SettingsTab.Graphics, R.string.settings_cas),
        entry(SettingsTab.Graphics, R.string.settings_tv_shader),
        entry(SettingsTab.Graphics, R.string.settings_hw_mipmapping),
        entry(SettingsTab.Graphics, R.string.settings_shadeboost),
        entry(SettingsTab.Audio, R.string.settings_audio_volume),
        entry(SettingsTab.Audio, R.string.settings_audio_fast_forward_volume),
        entry(SettingsTab.Audio, R.string.settings_audio_mute),
        entry(SettingsTab.Audio, R.string.settings_audio_interpolation),
        entry(SettingsTab.Audio, R.string.settings_audio_sync_mode),
        entry(SettingsTab.Audio, R.string.settings_audio_buffer_size),
        entry(SettingsTab.Audio, R.string.settings_audio_minimal_latency),
        entry(SettingsTab.Audio, R.string.settings_audio_output_latency),
        entry(SettingsTab.Fixes, R.string.settings_widescreen_patches),
        entry(SettingsTab.Fixes, R.string.settings_no_interlacing_patches),
        entry(SettingsTab.Fixes, R.string.settings_deinterlacing),
        entry(SettingsTab.Fixes, R.string.settings_dithering),
        entry(SettingsTab.Fixes, R.string.settings_anti_blur),
        entry(SettingsTab.Controls, R.string.settings_overlay_scale),
        entry(SettingsTab.Controls, R.string.settings_overlay_opacity),
        entry(SettingsTab.Controls, R.string.settings_touchscreen_right_stick),
        entry(SettingsTab.Controls, R.string.settings_touchscreen_right_stick_sensitivity),
        entry(SettingsTab.Controls, R.string.settings_racing_mode),
        entry(SettingsTab.Controls, R.string.settings_left_stick_sensitivity),
        entry(SettingsTab.Controls, R.string.settings_right_stick_sensitivity),
        entry(SettingsTab.Controls, R.string.settings_invert_left_stick),
        entry(SettingsTab.Controls, R.string.settings_invert_left_stick_horizontal),
        entry(SettingsTab.Controls, R.string.settings_invert_right_stick),
        entry(SettingsTab.Controls, R.string.settings_invert_right_stick_horizontal),
        entry(SettingsTab.Controls, R.string.settings_gamepad_mode),
        entry(SettingsTab.Controls, R.string.settings_gamepad_hide_overlay),
        entry(SettingsTab.Controls, R.string.settings_auto_progressive_scan),
        entry(SettingsTab.Controls, R.string.settings_touch_haptics),
        entry(SettingsTab.Controls, R.string.settings_touch_haptics_preset),
        entry(SettingsTab.Controls, R.string.settings_touch_haptics_strength),
        entry(SettingsTab.Controls, R.string.settings_touch_haptics_test),
        entry(SettingsTab.Controls, R.string.settings_gyro_mode),
        entry(SettingsTab.Controls, R.string.settings_gyro_sensitivity),
        entry(SettingsTab.Controls, R.string.settings_gyro_smoothing),
        entry(SettingsTab.Controls, R.string.settings_gamepad_stick_deadzone),
        entry(SettingsTab.Controls, R.string.settings_gamepad_left_stick_sensitivity),
        entry(SettingsTab.Controls, R.string.settings_gamepad_right_stick_sensitivity),
        entry(SettingsTab.Controls, R.string.settings_gamepad_right_stick_up_to_r2),
        entry(SettingsTab.Controls, R.string.settings_gamepad_right_stick_down_to_l2),
        entry(SettingsTab.Controls, R.string.settings_pad_vibration),
        entry(SettingsTab.Controls, R.string.settings_pad_vibration_strength),
        entry(SettingsTab.Controls, R.string.settings_pad_vibration_test),
        entry(SettingsTab.Controls, R.string.settings_pad_vibration_fallback),
        entry(SettingsTab.Library, R.string.settings_bios_path),
        entry(SettingsTab.Library, R.string.settings_game_path),
        entry(SettingsTab.Library, R.string.emulator_data_location_title),
        entry(SettingsTab.Library, R.string.settings_memory_cards_tab),
        entry(SettingsTab.Library, R.string.settings_cover_art_style),
        entry(SettingsTab.Library, R.string.settings_cover_download_url),
        entry(SettingsTab.Library, R.string.settings_backup_export_title),
        entry(SettingsTab.Library, R.string.settings_backup_restore_title),
        entry(SettingsTab.Emulation, R.string.settings_show_fps),
        entry(SettingsTab.Emulation, R.string.settings_fps_overlay_mode),
        entry(SettingsTab.Emulation, R.string.settings_fps_overlay_position),
        entry(SettingsTab.Emulation, R.string.settings_fps_overlay_scale),
        entry(SettingsTab.Emulation, R.string.settings_fps_overlay_metrics),
        entry(SettingsTab.Emulation, R.string.settings_enable_ee_recompiler),
        entry(SettingsTab.Emulation, R.string.settings_enable_iop_recompiler),
        entry(SettingsTab.Emulation, R.string.settings_enable_vu0_recompiler),
        entry(SettingsTab.Emulation, R.string.settings_enable_vu1_recompiler),
        entry(SettingsTab.Emulation, R.string.settings_game_fixes),
        entry(SettingsTab.Emulation, R.string.settings_ee_timing_hack),
        entry(SettingsTab.Emulation, R.string.settings_ee_fpu_round_mode),
        entry(SettingsTab.Emulation, R.string.settings_vu0_round_mode),
        entry(SettingsTab.Emulation, R.string.settings_vu1_round_mode),
        entry(SettingsTab.Emulation, R.string.settings_ee_fpu_clamping),
        entry(SettingsTab.Emulation, R.string.settings_vu0_clamping),
        entry(SettingsTab.Emulation, R.string.settings_vu1_clamping),
        entry(SettingsTab.Emulation, R.string.settings_gpu_chipset),
        entry(SettingsTab.Emulation, R.string.settings_gpu_accelerator),
        entry(SettingsTab.Emulation, R.string.settings_frame_limiter),
        entry(SettingsTab.Emulation, R.string.settings_vsync),
        entry(SettingsTab.Emulation, R.string.settings_fast_forward_speed),
        entry(SettingsTab.Emulation, R.string.settings_target_fps),
        entry(SettingsTab.Emulation, R.string.settings_ntsc_framerate),
        entry(SettingsTab.Emulation, R.string.settings_pal_framerate),
        entry(SettingsTab.Emulation, R.string.settings_ee_cycle_rate),
        entry(SettingsTab.Emulation, R.string.settings_ee_cycle_skip),
        entry(SettingsTab.Emulation, R.string.settings_mtvu),
        entry(SettingsTab.Emulation, R.string.settings_fast_cdvd),
        entry(SettingsTab.Emulation, R.string.settings_enable_cheats),
        entry(SettingsTab.Emulation, R.string.settings_frame_skip),
        entry(SettingsTab.Fixes, R.string.settings_cpu_sprite_render_size),
        entry(SettingsTab.Fixes, R.string.settings_gpu_target_clut),
        entry(SettingsTab.Fixes, R.string.settings_half_pixel_offset),
        entry(SettingsTab.Fixes, R.string.settings_bilinear_upscale),
        entry(SettingsTab.Network, R.string.settings_network_enable),
        entry(SettingsTab.Network, R.string.settings_network_adapter),
        entry(SettingsTab.Network, R.string.settings_network_dns1_mode),
        entry(SettingsTab.Network, R.string.settings_network_dns2_mode),
        entry(SettingsTab.Network, R.string.settings_network_intercept_dhcp),
        entry(SettingsTab.Pro, R.string.settings_theme_pro),
        entry(SettingsTab.Updates, R.string.settings_updates_tab)
    )
}

@Composable
private fun SettingsSearchResults(
    query: String,
    entries: List<SettingsSearchEntry>,
    onOpen: (SettingsTab) -> Unit
) {
    val normalizedQuery = remember(query) { normalizeSettingsSearchToken(query) }
    val filtered = remember(entries, normalizedQuery) {
        entries.filter { entry ->
            val haystack = normalizeSettingsSearchToken("${entry.title} ${entry.summary}")
            haystack.contains(normalizedQuery)
        }
    }
    SettingsSection(title = stringResource(R.string.settings_search_results_title)) {
        if (filtered.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = stringResource(R.string.settings_search_no_results),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        } else {
            filtered.forEach { entry ->
                SettingsItem(
                    icon = entry.tab.icon(),
                    label = entry.title,
                    value = entry.summary,
                    onClick = { onOpen(entry.tab) }
                )
            }
        }
    }
}

@Composable
private fun GamepadBindingRow(
    title: String,
    value: String,
    autoLabel: String?,
    onBindClick: () -> Unit,
    onClearClick: (() -> Unit)?
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
        onClick = onBindClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.Gamepad,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = value,
                        style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.primary
                    )
                    autoLabel?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            onClearClick?.let {
                TextButton(onClick = it) {
                    Text(stringResource(R.string.settings_gamepad_mapping_clear))
                }
            }
        }
    }
}

@Composable
private fun ToggleItem(
    icon: ImageVector,
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null,
    enabled: Boolean = true
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    val shape = RoundedCornerShape(18.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .gamepadFocusableCard(
                enabled = enabled,
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            )
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = enabled,
                onClick = { onCheckedChange(!checked) },
                onLongClick = onResetToDefault?.let {
                    {
                        it()
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    }
                }
        ),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Row(
            modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Spacer(modifier = Modifier.width(14.dp))
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(end = 16.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    helpText?.let {
                        SettingHelpButton(title = title, description = it)
                    }
                }
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Switch(
                checked = checked,
                onCheckedChange = null,
                enabled = enabled,
                modifier = Modifier.padding(end = 2.dp)
            )
        }
    }
}

@Composable
private fun ActionItem(
    icon: ImageVector,
    title: String,
    subtitle: String,
    actionIcon: ImageVector,
    actionLabel: String,
    onClick: () -> Unit,
    helpText: String? = null,
    enabled: Boolean = true
) {
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp))
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = enabled,
                onClick = onClick
            ),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Spacer(modifier = Modifier.width(14.dp))
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(end = 12.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    helpText?.let {
                        SettingHelpButton(title = title, description = it)
                    }
                }
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            TextButton(
                onClick = onClick,
                enabled = enabled
            ) {
                Icon(
                    imageVector = actionIcon,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp)
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(actionLabel)
            }
        }
    }
}

@Composable
private fun SliderItem(
    icon: ImageVector,
    title: String,
    subtitle: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    steps: Int,
    onValueChange: (Float) -> Unit,
    valueLabel: ((Float) -> String)? = null,
    onValueChangeLive: ((Float) -> Unit)? = null,
    onValueChangeFinished: ((Float) -> Unit)? = null,
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

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
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
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(38.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(20.dp)
                    )
                }
                Spacer(modifier = Modifier.width(14.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.weight(1f, fill = false)
                        )
                        helpText?.let {
                            SettingHelpButton(title = title, description = it)
                        }
                    }
                    Text(
                        text = valueLabel?.invoke(sliderValue) ?: subtitle,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Medium
                    )
                }
            }
            Slider(
                value = sliderValue,
                onValueChange = {
                    sliderValue = it
                    onValueChangeLive?.invoke(it)
                },
                onValueChangeFinished = {
                    onValueChange(sliderValue)
                    onValueChangeFinished?.invoke(sliderValue)
                },
                valueRange = range,
                steps = steps,
                colors = SliderDefaults.colors(
                    thumbColor = MaterialTheme.colorScheme.primary,
                    activeTrackColor = MaterialTheme.colorScheme.primary
                ),
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

@Composable
private fun ChoiceSection(
    title: String,
    options: List<Pair<Int, String>>,
    selectedValue: Int,
    onSelect: (Int) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 16.dp)
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
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        LazyRow(
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(options) { (value, label) ->
                FilterChip(
                    selected = selectedValue == value,
                    onClick = { onSelect(value) },
                    colors = premiumFilterChipColors(),
                    label = { Text(text = label) }
                )
            }
        }
    }
}

@Composable
private fun BitmaskChoiceSection(
    title: String,
    options: List<Pair<Int, String>>,
    selectedMask: Int,
    onToggle: (Int) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 16.dp)
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
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        LazyRow(
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(options) { (metric, label) ->
                FilterChip(
                    selected = PerformanceOverlayMetrics.isEnabled(selectedMask, metric),
                    onClick = { onToggle(metric) },
                    colors = premiumFilterChipColors(),
                    label = { Text(text = label) }
                )
            }
        }
    }
}

@Composable
private fun premiumFilterChipColors() = FilterChipDefaults.filterChipColors(
    containerColor = Color.Transparent,
    labelColor = MaterialTheme.colorScheme.onSurfaceVariant,
    iconColor = MaterialTheme.colorScheme.primary,
    selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
    selectedLabelColor = MaterialTheme.colorScheme.onPrimaryContainer,
    selectedLeadingIconColor = MaterialTheme.colorScheme.onPrimaryContainer,
    selectedTrailingIconColor = MaterialTheme.colorScheme.onPrimaryContainer
)

@Composable
private fun SettingsInlineNote(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(horizontal = 16.dp)
    )
}

@Composable
private fun CheatEmptyState(
    title: String,
    body: String,
    icon: ImageVector
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Spacer(modifier = Modifier.width(12.dp))
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = body,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun hwDownloadModeOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_hw_download_mode_accurate),
    1 to stringResource(R.string.settings_hw_download_mode_force_full),
    2 to stringResource(R.string.settings_hw_download_mode_no_readbacks),
    3 to stringResource(R.string.settings_hw_download_mode_unsynchronized),
    4 to stringResource(R.string.settings_hw_download_mode_disabled),
    5 to stringResource(R.string.settings_hw_download_mode_asynchronous)
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
private fun audioBackendOptions(): List<Pair<Int, String>> = listOf(
    AudioDefaults.BACKEND_AAUDIO to stringResource(R.string.settings_audio_backend_aaudio),
    AudioDefaults.BACKEND_OPENSLES to stringResource(R.string.settings_audio_backend_opensles)
)

@Composable
private fun audioInterpolationOptions(): List<Pair<Int, String>> = listOf(
    AudioDefaults.INTERPOLATION_NEAREST to stringResource(R.string.settings_audio_interpolation_nearest),
    AudioDefaults.INTERPOLATION_LINEAR to stringResource(R.string.settings_audio_interpolation_linear),
    AudioDefaults.INTERPOLATION_GAUSSIAN to stringResource(R.string.settings_audio_interpolation_gaussian),
    AudioDefaults.INTERPOLATION_CUBIC to stringResource(R.string.settings_audio_interpolation_cubic)
)

@Composable
private fun audioSyncModeOptions(): List<Pair<Int, String>> = listOf(
    AudioDefaults.SYNC_TIME_STRETCH to stringResource(R.string.settings_audio_sync_time_stretch),
    AudioDefaults.SYNC_DISABLED to stringResource(R.string.settings_audio_sync_disabled)
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

private fun formatSpeedMultiplier(value: Float): String {
    return "%.2fx".format(java.util.Locale.US, value)
}

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
private fun fpsOverlayCornerOptions(): List<Pair<Int, String>> = listOf(
    AppPreferences.FPS_OVERLAY_CORNER_TOP_LEFT to stringResource(R.string.settings_fps_overlay_corner_top_left),
    AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT to stringResource(R.string.settings_fps_overlay_corner_top_right),
    AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_LEFT to stringResource(R.string.settings_fps_overlay_corner_bottom_left),
    AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_RIGHT to stringResource(R.string.settings_fps_overlay_corner_bottom_right)
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
private fun fpsOverlayMetricOptions(): List<Pair<Int, String>> = listOf(
    PerformanceOverlayMetrics.FPS to stringResource(R.string.settings_fps_metric_fps),
    PerformanceOverlayMetrics.VPS to stringResource(R.string.settings_fps_metric_vps),
    PerformanceOverlayMetrics.SPEED to stringResource(R.string.settings_fps_metric_speed),
    PerformanceOverlayMetrics.TARGET to stringResource(R.string.settings_fps_metric_target),
    PerformanceOverlayMetrics.RENDERER to stringResource(R.string.settings_fps_metric_renderer),
    PerformanceOverlayMetrics.AUDIO to stringResource(R.string.settings_fps_metric_audio),
    PerformanceOverlayMetrics.VRAM to stringResource(R.string.settings_fps_metric_vram),
    PerformanceOverlayMetrics.FRAME_TIME to stringResource(R.string.settings_fps_metric_frame_time),
    PerformanceOverlayMetrics.QUEUE to stringResource(R.string.settings_fps_metric_queue),
    PerformanceOverlayMetrics.RESOLUTION to stringResource(R.string.settings_fps_metric_resolution),
    PerformanceOverlayMetrics.HOST_CPU to stringResource(R.string.settings_fps_metric_host_cpu),
    PerformanceOverlayMetrics.HOST_GPU to stringResource(R.string.settings_fps_metric_host_gpu),
    PerformanceOverlayMetrics.EE to stringResource(R.string.settings_fps_metric_ee),
    PerformanceOverlayMetrics.GS to stringResource(R.string.settings_fps_metric_gs),
    PerformanceOverlayMetrics.VU to stringResource(R.string.settings_fps_metric_vu),
    PerformanceOverlayMetrics.SOFTWARE_THREADS to stringResource(R.string.settings_fps_metric_software_threads)
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

@StringRes
private fun gamepadActionLabelRes(actionId: String): Int = when (actionId) {
    "cross" -> R.string.settings_gamepad_action_cross
    "circle" -> R.string.settings_gamepad_action_circle
    "square" -> R.string.settings_gamepad_action_square
    "triangle" -> R.string.settings_gamepad_action_triangle
    "l1" -> R.string.settings_gamepad_action_l1
    "r1" -> R.string.settings_gamepad_action_r1
    "l2" -> R.string.settings_gamepad_action_l2
    "r2" -> R.string.settings_gamepad_action_r2
    "l3" -> R.string.settings_gamepad_action_l3
    "r3" -> R.string.settings_gamepad_action_r3
    "select" -> R.string.settings_gamepad_action_select
    "start" -> R.string.settings_gamepad_action_start
    "pressure" -> R.string.settings_gamepad_action_pressure
    GamepadManager.ACTION_QUICK_SAVE -> R.string.emulation_quick_save
    GamepadManager.ACTION_QUICK_LOAD -> R.string.emulation_quick_load
    "dpad_up" -> R.string.settings_gamepad_action_dpad_up
    "dpad_down" -> R.string.settings_gamepad_action_dpad_down
    "dpad_left" -> R.string.settings_gamepad_action_dpad_left
    "dpad_right" -> R.string.settings_gamepad_action_dpad_right
    else -> R.string.settings_gamepad_section
}

@Composable
private fun gamepadActionLabel(actionId: String): String = when (actionId) {
    "cross" -> "\u2715"
    "circle" -> "\u25cb"
    "square" -> "\u25a1"
    "triangle" -> "\u25b3"
    "pressure" -> stringResource(R.string.settings_gamepad_action_pressure)
    else -> stringResource(gamepadActionLabelRes(actionId))
}

@Composable
private fun gamepadPlayerLabel(padIndex: Int): String {
    return stringResource(
        if (padIndex == 0) R.string.settings_gamepad_player_1 else R.string.settings_gamepad_player_2
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CheatEditorSheet(
    fileName: String,
    value: String,
    onValueChange: (String) -> Unit,
    onDismiss: () -> Unit,
    onSave: () -> Unit,
    onDelete: () -> Unit
) {
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        containerColor = MaterialTheme.colorScheme.surface,
        shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp)
    ) {
        val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding, vertical = 12.dp)
                .padding(bottom = bottomInset),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = stringResource(R.string.settings_cheats_editor_title),
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = fileName,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            OutlinedTextField(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier
                    .fillMaxWidth()
                    .fillMaxHeight(0.65f),
                textStyle = MaterialTheme.typography.bodySmall,
                label = { Text(stringResource(R.string.settings_cheats_editor_field)) }
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                TextButton(onClick = onDelete) {
                    Text(stringResource(R.string.settings_cheats_delete))
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = onDismiss) {
                        Text(stringResource(android.R.string.cancel))
                    }
                    TextButton(onClick = onSave) {
                        Text(stringResource(R.string.settings_cheats_save))
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LanguageSettingsScreen(
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 10.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val options = rememberLanguageOptions()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
            .verticalScroll(rememberScrollState())
    ) {
        ScreenTopBar(
            title = stringResource(R.string.settings_language),
            subtitle = stringResource(R.string.settings_language_screen_subtitle),
            onBackClick = onBackClick,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding, vertical = 0.dp)
                .padding(top = topInset, bottom = 10.dp),
            backContentColor = MaterialTheme.colorScheme.onSurface
        )

        Column(
            modifier = Modifier
                .padding(horizontal = ScreenHorizontalPadding, vertical = 4.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            options.forEach { option ->
                LanguageOptionCard(
                    badgeText = option.badge,
                    title = stringResource(option.titleRes),
                    subtitle = option.subtitleRes?.let { stringResource(it) },
                    selected = uiState.languageTag == option.tag,
                    onClick = { viewModel.setLanguage(option.tag) }
                )
            }
            Spacer(modifier = Modifier.height(18.dp))
        }

        Spacer(modifier = Modifier.height(bottomInset))
    }
}

@Composable
private fun LanguageOptionCard(
    badgeText: String,
    title: String,
    subtitle: String?,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(shape = RoundedCornerShape(20.dp)),
        shape = RoundedCornerShape(20.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
        },
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(42.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(
                        if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
                        else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = badgeText,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 12.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                    )
                    subtitle?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            if (selected) {
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .clip(RoundedCornerShape(999.dp))
                        .background(MaterialTheme.colorScheme.primary)
                )
            }
        }
    }
}

private data class LanguageUiOption(
    val tag: String?,
    val badge: String,
    @param:StringRes val titleRes: Int,
    @param:StringRes val subtitleRes: Int? = null
)

@Composable
private fun rememberLanguageOptions(): List<LanguageUiOption> {
    return remember {
        listOf(
            LanguageUiOption(null, "SYS", R.string.settings_language_system, R.string.settings_language_system_subtitle),
            LanguageUiOption("en", "EN", R.string.settings_language_english, R.string.settings_language_native_english),
            LanguageUiOption("uk", "UA", R.string.settings_language_ukrainian, R.string.settings_language_native_ukrainian),
            LanguageUiOption("ru", "RU", R.string.settings_language_russian, R.string.settings_language_native_russian),
            LanguageUiOption("es", "ES", R.string.settings_language_spanish, R.string.settings_language_native_spanish),
            LanguageUiOption("fr", "FR", R.string.settings_language_french, R.string.settings_language_native_french),
            LanguageUiOption("de", "DE", R.string.settings_language_german, R.string.settings_language_native_german),
            LanguageUiOption("pt", "PT", R.string.settings_language_portuguese, R.string.settings_language_native_portuguese),
            LanguageUiOption("it", "IT", R.string.settings_language_italian, R.string.settings_language_native_italian),
            LanguageUiOption("id", "ID", R.string.settings_language_indonesian, R.string.settings_language_native_indonesian),
            LanguageUiOption("hi", "HI", R.string.settings_language_hindi, R.string.settings_language_native_hindi),
            LanguageUiOption("zh", "繁", R.string.settings_language_traditional_chinese, R.string.settings_language_native_traditional_chinese),
            LanguageUiOption("ar", "AR", R.string.settings_language_arabic, R.string.settings_language_native_arabic),
            LanguageUiOption("fa", "FA", R.string.settings_language_persian, R.string.settings_language_native_persian),
            LanguageUiOption("ja", "JA", R.string.settings_language_japanese, R.string.settings_language_native_japanese),
            LanguageUiOption("ko", "KO", R.string.settings_language_korean, R.string.settings_language_native_korean),
            LanguageUiOption("tr", "TR", R.string.settings_language_turkish, R.string.settings_language_native_turkish)
        )
    }
}

@Composable
private fun languageLabel(tag: String?): String {
    return when (tag) {
        "en" -> stringResource(R.string.settings_language_english)
        "uk" -> stringResource(R.string.settings_language_ukrainian)
        "ru" -> stringResource(R.string.settings_language_russian)
        "es" -> stringResource(R.string.settings_language_spanish)
        "fr" -> stringResource(R.string.settings_language_french)
        "de" -> stringResource(R.string.settings_language_german)
        "pt" -> stringResource(R.string.settings_language_portuguese)
        "it" -> stringResource(R.string.settings_language_italian)
        "id", "id-ID", "in", "in-ID" -> stringResource(R.string.settings_language_indonesian)
        "hi" -> stringResource(R.string.settings_language_hindi)
        "zh", "zh-TW", "zh-Hant", "zh-Hant-TW" -> stringResource(R.string.settings_language_traditional_chinese)
        "ar" -> stringResource(R.string.settings_language_arabic)
        "fa", "fa-IR" -> stringResource(R.string.settings_language_persian)
        "ja" -> stringResource(R.string.settings_language_japanese)
        "ko" -> stringResource(R.string.settings_language_korean)
        "tr" -> stringResource(R.string.settings_language_turkish)
        else -> stringResource(R.string.settings_language_system)
    }
}

@Composable
private fun SettingsTab.label(): String {
    return when (this) {
        SettingsTab.General -> stringResource(R.string.settings_general_tab)
        SettingsTab.Customization -> stringResource(R.string.settings_customization_tab)
        SettingsTab.GameMenu -> stringResource(R.string.settings_game_menu_tab)
        SettingsTab.Graphics -> stringResource(R.string.settings_graphics_tab)
        SettingsTab.Audio -> stringResource(R.string.settings_audio_tab)
        SettingsTab.Controls -> stringResource(R.string.settings_controls_tab)
        SettingsTab.Emulation -> stringResource(R.string.settings_emulation_tab)
        SettingsTab.Fixes -> stringResource(R.string.settings_fixes_tab)
        SettingsTab.Network -> stringResource(R.string.settings_network_tab)
        SettingsTab.Library -> stringResource(R.string.settings_library_tab)
        SettingsTab.Pro -> stringResource(R.string.settings_pro_tab)
        SettingsTab.Updates -> stringResource(R.string.settings_updates_tab)
        SettingsTab.About -> stringResource(R.string.settings_about)
    }
}

@Composable
private fun SettingsTab.icon(): ImageVector {
    return when (this) {
        SettingsTab.General -> Icons.Rounded.Tune
        SettingsTab.Customization -> Icons.Rounded.Palette
        SettingsTab.GameMenu -> Icons.Rounded.MoreVert
        SettingsTab.Graphics -> Icons.Rounded.GraphicEq
        SettingsTab.Audio -> Icons.AutoMirrored.Rounded.VolumeUp
        SettingsTab.Controls -> Icons.Rounded.Gamepad
        SettingsTab.Emulation -> Icons.Rounded.Speed
        SettingsTab.Fixes -> Icons.Rounded.SettingsSuggest
        SettingsTab.Network -> Icons.Rounded.Link
        SettingsTab.Library -> Icons.Rounded.FolderOpen
        SettingsTab.Pro -> Icons.Rounded.Star
        SettingsTab.Updates -> Icons.Rounded.SystemUpdateAlt
        SettingsTab.About -> Icons.Rounded.Info
    }
}

private fun String.toSettingsTab(): SettingsTab {
    return when (lowercase()) {
        "graphics" -> SettingsTab.Graphics
        "customization", "customize", "appearance", "background", "font", "layout" -> SettingsTab.Customization
        "game_menu", "game-menu", "ingame", "in-game", "menu" -> SettingsTab.GameMenu
        "audio", "sound" -> SettingsTab.Audio
        "controls" -> SettingsTab.Controls
        "paths", "files", "memorycards", "memory_cards", "memory-cards", "memcards", "covers",
        "cover-art", "cover_art", "data_transfer", "transfer", "backup", "data-transfer", "library" -> SettingsTab.Library
        "performance", "jit", "speedhacks", "speed_hacks", "speed-hacks", "cheats", "cheat", "emulation" -> SettingsTab.Emulation
        "advanced", "fixes", "hacks" -> SettingsTab.Fixes
        "network", "networking", "multiplayer", "online", "dev9" -> SettingsTab.Network
        "pro", "premium", "crimson", "support" -> SettingsTab.Pro
        "updates", "update", "app_update", "app-update" -> SettingsTab.Updates
        "about" -> SettingsTab.About
        else -> SettingsTab.General
    }
}

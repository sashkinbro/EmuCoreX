package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SaveAs
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.StayPrimaryPortrait
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Vibration
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DeviceChipsetFamily
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.CheatFileEntry
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsBackupRepository
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.launch
import java.io.File

private enum class SettingsTab {
    General, Graphics, Controls, Paths, DataTransfer, Performance, SpeedHacks, Cheats, Advanced, About
}

@SuppressLint("ConfigurationScreenWidthHeight")
@OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    initialTab: String = "general",
    onBackClick: (() -> Unit)? = null,
    onOpenLanguageScreen: (() -> Unit)? = null,
    onEditControlsClick: (() -> Unit)? = null,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 10.dp
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val isWide = configuration.screenWidthDp >= 900
    val isAdaptiveLandscape = isLandscape && configuration.screenWidthDp >= 560
    val useWideLayout = isWide || isAdaptiveLandscape
    var selectedTab by remember(initialTab) { mutableStateOf(initialTab.toSettingsTab()) }
    val cheatRepository = remember(context) { CheatRepository(context) }
    var cheatEntries by remember { mutableStateOf(cheatRepository.listImportedCheatFiles()) }
    var cheatEditorGameKey by remember { mutableStateOf<String?>(null) }
    var cheatEditorFileName by remember { mutableStateOf<String?>(null) }
    var cheatEditorText by remember { mutableStateOf("") }
    var pendingGamepadActionId by remember { mutableStateOf<String?>(null) }
    var showTopBarMenu by remember { mutableStateOf(false) }
    var showResetAllSettingsDialog by remember { mutableStateOf(false) }
    val selectedTabFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }
    val scope = rememberCoroutineScope()
    val settingsTabs = remember { SettingsTab.entries.toList() }
    val pagerState = rememberPagerState(
        initialPage = initialTab.toSettingsTab().ordinal,
        pageCount = { settingsTabs.size }
    )
    val backupRepository = remember(context) {
        SettingsBackupRepository(
            context = context,
            preferences = com.sbro.emucorex.data.AppPreferences(context),
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

    val biosPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? -> uri?.let(viewModel::setBiosPath) }

    val gamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? -> uri?.let(viewModel::setGamePath) }

    val launchBiosPicker = rememberDebouncedClick(onClick = { biosPicker.launch(null) })
    val launchGamePicker = rememberDebouncedClick(onClick = { gamePicker.launch(null) })
    val openLanguageSheet = rememberDebouncedClick(onClick = { onOpenLanguageScreen?.invoke() })
    val refreshCheatEntries = remember {
        {
            cheatEntries = cheatRepository.listImportedCheatFiles()
        }
    }

    LaunchedEffect(useWideLayout, shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus) {
            selectedTabFocusRequester.requestFocus()
        }
    }
    LaunchedEffect(useWideLayout, pagerState.settledPage) {
        if (!useWideLayout) {
            val pageTab = settingsTabs.getOrNull(pagerState.settledPage) ?: return@LaunchedEffect
            if (selectedTab != pageTab) {
                selectedTab = pageTab
            }
        }
    }
    RequestFocusOnResume(
        focusRequester = selectedTabFocusRequester,
        enabled = shouldRequestGamepadFocus
    )
    DisposableEffect(pendingGamepadActionId) {
        val actionId = pendingGamepadActionId
        if (actionId != null) {
            GamepadManager.startBindingCapture { keyCode ->
                viewModel.setGamepadBinding(actionId, keyCode)
                pendingGamepadActionId = null
            }
        } else {
            GamepadManager.cancelBindingCapture()
        }
        onDispose {
            GamepadManager.cancelBindingCapture()
        }
    }

    val driverPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let {
            val path = DocumentPathResolver.resolveFilePath(context, it.toString())
            viewModel.setCustomDriverPath(path)
        }
    }
    val settingsBackupExporter = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/zip")
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val success = backupRepository.backup(uri)
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
            val success = backupRepository.restore(uri)
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
            cheatRepository.importCheatFile(gameKey, fileName, contents)
            refreshCheatEntries()
            Toast.makeText(context, cheatsImportSuccessMessage, Toast.LENGTH_SHORT).show()
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        if (useWideLayout) {
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .statusBarsPadding()
                    .navigationBarsPadding()
                    .padding(horizontal = ScreenHorizontalPadding),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                SettingsTabRail(
                    selectedTab = selectedTab,
                    onSelected = { selectedTab = it },
                    topInset = 0.dp,
                    selectedTabFocusRequester = selectedTabFocusRequester,
                    modifier = Modifier.width(if (isWide) 216.dp else 188.dp)
                )
                SettingsContent(
                    uiState = uiState,
                    selectedTab = selectedTab,
                    context = context,
                    launchBiosPicker = launchBiosPicker,
                    launchGamePicker = launchGamePicker,
                    launchDriverPicker = { driverPicker.launch(arrayOf("*/*")) },
                    launchSettingsBackupExport = { settingsBackupExporter.launch("emucorex-settings-backup.zip") },
                    launchSettingsBackupImport = { settingsBackupImporter.launch(arrayOf("application/zip", "*/*")) },
                    launchCheatImport = { cheatImporter.launch(arrayOf("*/*")) },
                    openLanguageSheet = openLanguageSheet,
                    cheatEntries = cheatEntries,
                    onOpenCheatEditor = { gameKey ->
                        cheatEditorGameKey = gameKey
                        cheatEditorFileName = cheatRepository.listImportedCheatFiles()
                            .firstOrNull { it.gameKey == gameKey }
                            ?.fileName ?: "$gameKey.pnach"
                        cheatEditorText = cheatRepository.getImportedCheatText(gameKey).orEmpty()
                    },
                    onRequestGamepadBinding = { pendingGamepadActionId = it },
                    onEditControlsClick = onEditControlsClick,
                    viewModel = viewModel,
                    topInset = 0.dp,
                    modifier = Modifier.weight(1f)
                )
            }
        } else {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .navigationBarsPadding()
            ) {
                SettingsCompactTopBar(
                    title = stringResource(R.string.settings_title),
                    subtitle = selectedTab.label(),
                    topInset = topInset,
                    onBackClick = onBackClick,
                    menuExpanded = showTopBarMenu,
                    onMenuExpandedChange = { showTopBarMenu = it },
                    onResetAllSettingsClick = {
                        showTopBarMenu = false
                        showResetAllSettingsDialog = true
                    }
                )

                SettingsTabRow(
                    selectedTab = selectedTab,
                    onSelected = { tab ->
                        if (selectedTab == tab && pagerState.settledPage == tab.ordinal) return@SettingsTabRow
                        selectedTab = tab
                        scope.launch {
                            if (pagerState.currentPage != pagerState.settledPage) {
                                pagerState.scrollToPage(pagerState.settledPage)
                            }
                            if (pagerState.settledPage != tab.ordinal) {
                                pagerState.animateScrollToPage(tab.ordinal)
                            }
                        }
                    },
                    selectedTabFocusRequester = selectedTabFocusRequester
                )

                HorizontalPager(
                    state = pagerState,
                    modifier = Modifier.weight(1f),
                    userScrollEnabled = false
                ) { page ->
                    SettingsContent(
                        uiState = uiState,
                        selectedTab = settingsTabs[page],
                        context = context,
                        launchBiosPicker = launchBiosPicker,
                        launchGamePicker = launchGamePicker,
                        launchDriverPicker = { driverPicker.launch(arrayOf("*/*")) },
                        launchSettingsBackupExport = { settingsBackupExporter.launch("emucorex-settings-backup.zip") },
                        launchSettingsBackupImport = { settingsBackupImporter.launch(arrayOf("application/zip", "*/*")) },
                        launchCheatImport = { cheatImporter.launch(arrayOf("*/*")) },
                        openLanguageSheet = openLanguageSheet,
                        cheatEntries = cheatEntries,
                        onOpenCheatEditor = { gameKey ->
                            cheatEditorGameKey = gameKey
                            cheatEditorFileName = cheatRepository.listImportedCheatFiles()
                                .firstOrNull { it.gameKey == gameKey }
                                ?.fileName ?: "$gameKey.pnach"
                            cheatEditorText = cheatRepository.getImportedCheatText(gameKey).orEmpty()
                        },
                        onRequestGamepadBinding = { pendingGamepadActionId = it },
                        onEditControlsClick = onEditControlsClick,
                        viewModel = viewModel,
                        topInset = 0.dp,
                        modifier = Modifier.fillMaxSize()
                    )
                }
            }
        }
    }

    if (cheatEditorGameKey != null) {
        CheatEditorSheet(
            fileName = cheatEditorFileName.orEmpty(),
            value = cheatEditorText,
            onValueChange = { cheatEditorText = it },
            onDismiss = {
                cheatEditorGameKey = null
                cheatEditorFileName = null
                cheatEditorText = ""
            },
            onSave = {
                cheatEditorGameKey?.let { gameKey ->
                    cheatRepository.updateImportedCheatText(gameKey, cheatEditorText)
                    refreshCheatEntries()
                    Toast.makeText(context, cheatsSavedMessage, Toast.LENGTH_SHORT).show()
                    cheatEditorGameKey = null
                    cheatEditorFileName = null
                    cheatEditorText = ""
                }
            },
            onDelete = {
                cheatEditorGameKey?.let { gameKey ->
                    cheatRepository.deleteImportedCheats(gameKey, null, null)
                    refreshCheatEntries()
                    Toast.makeText(context, cheatsDeletedMessage, Toast.LENGTH_SHORT).show()
                    cheatEditorGameKey = null
                    cheatEditorFileName = null
                    cheatEditorText = ""
                }
            }
        )
    }

    if (pendingGamepadActionId != null) {
        AlertDialog(
            onDismissRequest = { pendingGamepadActionId = null },
            title = {
                Text(stringResource(R.string.settings_gamepad_mapping_listening_title))
            },
            text = {
                Text(
                    stringResource(
                        R.string.settings_gamepad_mapping_listening_desc,
                        stringResource(gamepadActionLabelRes(pendingGamepadActionId.orEmpty()))
                    )
                )
            },
            confirmButton = {
                TextButton(onClick = { pendingGamepadActionId = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showResetAllSettingsDialog) {
        AlertDialog(
            onDismissRequest = { showResetAllSettingsDialog = false },
            title = {
                Text(stringResource(R.string.settings_reset_all_title))
            },
            text = {
                Text(stringResource(R.string.settings_reset_all_confirm))
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        showResetAllSettingsDialog = false
                        viewModel.resetAllSettings()
                    }
                ) {
                    Text(stringResource(R.string.settings_reset_all_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showResetAllSettingsDialog = false }) {
                    Text(stringResource(R.string.cancel))
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
    onResetAllSettingsClick: () -> Unit
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
                    .padding(start = 4.dp, end = 8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
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
private fun SettingsTabRail(
    selectedTab: SettingsTab,
    onSelected: (SettingsTab) -> Unit,
    topInset: androidx.compose.ui.unit.Dp,
    selectedTabFocusRequester: FocusRequester,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxHeight(),
        shape = RoundedCornerShape(30.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 6.dp
    ) {
        Column(
            modifier = Modifier
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Spacer(modifier = Modifier.height(topInset))
            Text(
                text = stringResource(R.string.settings_title),
                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(horizontal = 8.dp, vertical = 8.dp)
            )
            SettingsTab.entries.forEach { tab ->
                FilterChip(
                    modifier = if (tab == selectedTab) {
                        Modifier.focusRequester(selectedTabFocusRequester)
                    } else {
                        Modifier
                    },
                    selected = selectedTab == tab,
                    onClick = { onSelected(tab) },
                    label = { Text(text = tab.label()) },
                    leadingIcon = {
                        Icon(
                            imageVector = tab.icon(),
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                )
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
    LazyRow(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(SettingsTab.entries) { tab ->
            FilterChip(
                modifier = if (tab == selectedTab) {
                    Modifier.focusRequester(selectedTabFocusRequester)
                } else {
                    Modifier
                },
                selected = selectedTab == tab,
                onClick = { onSelected(tab) },
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

@Composable
private fun SettingsContent(
    uiState: SettingsUiState,
    selectedTab: SettingsTab,
    context: android.content.Context,
    launchBiosPicker: () -> Unit,
    launchGamePicker: () -> Unit,
    launchDriverPicker: () -> Unit,
    launchSettingsBackupExport: () -> Unit,
    launchSettingsBackupImport: () -> Unit,
    launchCheatImport: () -> Unit,
    openLanguageSheet: () -> Unit,
    cheatEntries: List<CheatFileEntry>,
    onOpenCheatEditor: (String) -> Unit,
    onRequestGamepadBinding: (String) -> Unit,
    onEditControlsClick: (() -> Unit)? = null,
    viewModel: SettingsViewModel,
    topInset: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier
) {
    val gamepadActions = remember { GamepadManager.mappableButtonActions() }
    Box(
        modifier = modifier
            .fillMaxSize()
            .imePadding()
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(top = topInset + 12.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            when (selectedTab) {
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
                            onSelected = viewModel::setThemeMode
                        )
                        ToggleItem(
                            icon = Icons.Rounded.StayPrimaryPortrait,
                            title = stringResource(R.string.settings_keep_screen_on),
                            subtitle = stringResource(R.string.settings_keep_screen_on_desc),
                            checked = uiState.keepScreenOn,
                            onCheckedChange = viewModel::setKeepScreenOn
                        )
                    }
                }

                SettingsTab.Graphics -> {
                    SettingsSection(title = stringResource(R.string.settings_graphics)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_renderer),
                            options = listOf(
                                14 to stringResource(R.string.settings_renderer_vulkan),
                                12 to stringResource(R.string.settings_renderer_opengl),
                                13 to stringResource(R.string.settings_renderer_software)
                            ),
                            selectedValue = uiState.renderer,
                            onSelect = viewModel::setRenderer
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_gpu_driver),
                            options = listOf(
                                0 to stringResource(R.string.settings_gpu_driver_system),
                                1 to stringResource(R.string.settings_gpu_driver_custom)
                            ),
                            selectedValue = uiState.gpuDriverType,
                            onSelect = viewModel::setGpuDriverType
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_upscale),
                            options = listOf(
                                1 to stringResource(R.string.settings_upscale_native),
                                2 to stringResource(R.string.settings_upscale_2x),
                                3 to stringResource(R.string.settings_upscale_3x),
                                4 to "4x",
                                6 to "6x",
                                8 to "8x"
                            ),
                            selectedValue = uiState.upscaleMultiplier,
                            onSelect = viewModel::setUpscaleMultiplier
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_aspect_ratio),
                            options = listOf(
                                1 to stringResource(R.string.settings_aspect_ratio_auto),
                                2 to stringResource(R.string.settings_aspect_ratio_43),
                                3 to stringResource(R.string.settings_aspect_ratio_169),
                                0 to stringResource(R.string.emulation_aspect_stretch)
                            ),
                            selectedValue = uiState.aspectRatio,
                            onSelect = viewModel::setAspectRatio
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_texture_filtering),
                            options = listOf(
                                0 to stringResource(R.string.settings_texture_filtering_nearest),
                                1 to stringResource(R.string.settings_texture_filtering_bilinear),
                                2 to stringResource(R.string.settings_texture_filtering_trilinear)
                            ),
                            selectedValue = uiState.textureFiltering,
                            onSelect = viewModel::setTextureFiltering
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
                            onSelect = viewModel::setAnisotropicFiltering
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_widescreen_patches),
                            subtitle = stringResource(R.string.settings_widescreen_patches_desc),
                            checked = uiState.enableWidescreenPatches,
                            onCheckedChange = viewModel::setEnableWidescreenPatches
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_no_interlacing_patches),
                            subtitle = stringResource(R.string.settings_no_interlacing_patches_desc),
                            checked = uiState.enableNoInterlacingPatches,
                            onCheckedChange = viewModel::setEnableNoInterlacingPatches
                        )
                    }
                }

                SettingsTab.Controls -> {
                    SettingsSection(title = stringResource(R.string.settings_touch_controls)) {
                        SliderItem(
                            icon = Icons.Rounded.TouchApp,
                            title = stringResource(R.string.settings_overlay_scale),
                            subtitle = "${uiState.overlayScale}%",
                            value = uiState.overlayScale.toFloat(),
                            range = 50f..150f,
                            steps = 9,
                            onValueChange = { viewModel.setOverlayScale(it.toInt()) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_overlay_opacity),
                            subtitle = "${uiState.overlayOpacity}%",
                            value = uiState.overlayOpacity.toFloat(),
                            range = 20f..100f,
                            steps = 7,
                            onValueChange = { viewModel.setOverlayOpacity(it.toInt()) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_gamepad_section)) {
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_gamepad_auto),
                            subtitle = stringResource(R.string.settings_gamepad_auto_desc),
                            checked = uiState.enableAutoGamepad,
                            onCheckedChange = viewModel::setEnableAutoGamepad
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_gamepad_hide_overlay),
                            subtitle = stringResource(R.string.settings_gamepad_hide_overlay_desc),
                            checked = uiState.hideOverlayOnGamepad,
                            onCheckedChange = viewModel::setHideOverlayOnGamepad
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_pad_vibration),
                            subtitle = stringResource(R.string.settings_pad_vibration_desc),
                            checked = uiState.padVibration,
                            onCheckedChange = viewModel::setPadVibration
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_gamepad_mapping_title)) {
                        SettingsInlineNote(
                            text = GamepadManager.firstConnectedControllerName()?.let {
                                context.getString(R.string.settings_gamepad_mapping_connected, it)
                            } ?: stringResource(R.string.settings_gamepad_mapping_disconnected)
                        )
                        for (action in gamepadActions) {
                            val assignedKeyCode = GamepadManager.resolveBindingForAction(
                                actionId = action.id,
                                customBindings = uiState.gamepadBindings
                            )
                            val isCustomBinding = uiState.gamepadBindings.containsKey(action.id)
                            GamepadBindingRow(
                                title = stringResource(gamepadActionLabelRes(action.id)),
                                value = assignedKeyCode?.let(GamepadManager::keyCodeLabel)
                                    ?: stringResource(R.string.settings_not_set),
                                autoLabel = if (isCustomBinding) null else {
                                    stringResource(R.string.settings_gamepad_mapping_auto_format)
                                },
                                onBindClick = { onRequestGamepadBinding(action.id) },
                                onClearClick = if (isCustomBinding) {
                                    { viewModel.clearGamepadBinding(action.id) }
                                } else {
                                    null
                                }
                            )
                        }
                        SettingsItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            label = stringResource(R.string.settings_gamepad_mapping_reset_title),
                            value = stringResource(R.string.settings_gamepad_mapping_reset_desc),
                            onClick = viewModel::resetGamepadBindings
                        )
                    }
                    if (onEditControlsClick != null) {
                        SettingsSection(title = stringResource(R.string.settings_edit_controls)) {
                            SettingsItem(
                                icon = Icons.Rounded.TouchApp,
                                label = stringResource(R.string.settings_edit_controls),
                                value = stringResource(R.string.settings_edit_controls_desc),
                                onClick = onEditControlsClick
                            )
                        }
                    }
                }

                SettingsTab.Paths -> {
                    val biosDisplayName = remember(uiState.biosPath, context) {
                        uiState.biosPath?.let { DocumentPathResolver.getDisplayName(context, it) }
                            ?: context.getString(R.string.settings_not_set)
                    }
                    val gameDisplayName = remember(uiState.gamePath, context) {
                        uiState.gamePath?.let { DocumentPathResolver.getDisplayName(context, it) }
                            ?: context.getString(R.string.settings_not_set)
                    }
                    val driverDisplayName = remember(uiState.customDriverPath, context) {
                        uiState.customDriverPath?.let { File(it).name }
                            ?: context.getString(R.string.settings_not_set)
                    }
                    SettingsSection(title = stringResource(R.string.settings_paths)) {
                        SettingsItem(
                            icon = Icons.Rounded.Memory,
                            label = stringResource(R.string.settings_bios_path),
                            value = biosDisplayName,
                            onClick = launchBiosPicker
                        )
                        SettingsItem(
                            icon = Icons.Rounded.FolderOpen,
                            label = stringResource(R.string.settings_game_path),
                            value = gameDisplayName,
                            onClick = launchGamePicker
                        )
                        SettingsItem(
                            icon = Icons.Rounded.Tune,
                            label = stringResource(R.string.settings_gpu_driver_path),
                            value = driverDisplayName,
                            onClick = launchDriverPicker
                        )
                        if (uiState.gpuDriverType == 0) {
                            SettingsInlineNote(
                                text = stringResource(R.string.settings_gpu_driver_path_note)
                            )
                        }
                    }
                }

                SettingsTab.DataTransfer -> {
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

                SettingsTab.Performance -> {
                    SettingsSection(title = stringResource(R.string.settings_device_profile_title)) {
                        SettingsItem(
                            icon = Icons.Rounded.Memory,
                            label = stringResource(R.string.settings_device_profile_chipset),
                            value = chipsetDisplayLabel(
                                family = uiState.deviceChipsetFamily,
                                detectedName = uiState.detectedChipsetName
                            ),
                            onClick = { }
                        )
                        SettingsItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            label = stringResource(R.string.settings_device_profile_apply),
                            value = stringResource(deviceProfileDescription(uiState.deviceChipsetFamily)),
                            onClick = viewModel::applyRecommendedDeviceProfile
                        )
                        SettingsInlineNote(
                            text = stringResource(R.string.settings_device_profile_note)
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_performance_preset)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_performance_preset),
                            options = performancePresetOptions(),
                            selectedValue = uiState.performancePreset,
                            onSelect = viewModel::applyPerformancePreset
                        )
                        SettingsInlineNote(
                            text = stringResource(performancePresetDescription(uiState.performancePreset))
                        )
                    }
                    SettingsSection(title = stringResource(R.string.emulation_performance_stats)) {
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_show_fps),
                            subtitle = stringResource(R.string.settings_show_fps_desc),
                            checked = uiState.showFps,
                            onCheckedChange = viewModel::setShowFps
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_fps_overlay_mode),
                            options = listOf(
                                FPS_OVERLAY_MODE_SIMPLE to stringResource(R.string.settings_fps_overlay_mode_simple),
                                FPS_OVERLAY_MODE_DETAILED to stringResource(R.string.settings_fps_overlay_mode_detailed)
                            ),
                            selectedValue = uiState.fpsOverlayMode,
                            onSelect = viewModel::setFpsOverlayMode
                        )
                    }
                }

                SettingsTab.SpeedHacks -> {
                    SettingsSection(title = stringResource(R.string.settings_speed_hacks)) {
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_frame_limiter),
                            subtitle = stringResource(R.string.settings_frame_limiter_desc),
                            checked = uiState.frameLimitEnabled,
                            onCheckedChange = viewModel::setFrameLimitEnabled
                        )
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
                            onValueChange = { viewModel.setTargetFps(it.toInt()) }
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
                            onSelect = viewModel::setEeCycleRate
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
                            onSelect = viewModel::setEeCycleSkip
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_mtvu),
                            subtitle = stringResource(R.string.settings_mtvu_desc),
                            checked = uiState.enableMtvu,
                            onCheckedChange = viewModel::setEnableMtvu
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_fast_cdvd),
                            subtitle = stringResource(R.string.settings_fast_cdvd_desc),
                            checked = uiState.enableFastCdvd,
                            onCheckedChange = viewModel::setEnableFastCdvd
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_hw_download_mode),
                            options = hwDownloadModeOptions(),
                            selectedValue = uiState.hwDownloadMode,
                            onSelect = viewModel::setHwDownloadMode
                        )
                        SettingsInlineNote(
                            text = stringResource(R.string.settings_hw_download_mode_desc)
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_blending_accuracy),
                            options = blendingAccuracyOptions(),
                            selectedValue = uiState.blendingAccuracy,
                            onSelect = viewModel::setBlendingAccuracy
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_texture_preloading),
                            options = texturePreloadingOptions(),
                            selectedValue = uiState.texturePreloading,
                            onSelect = viewModel::setTexturePreloading
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_bilinear_filtering),
                            options = bilinearFilteringOptions(),
                            selectedValue = uiState.textureFiltering,
                            onSelect = viewModel::setTextureFiltering
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_trilinear_filtering),
                            options = trilinearFilteringOptions(),
                            selectedValue = uiState.trilinearFiltering,
                            onSelect = viewModel::setTrilinearFiltering
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_anisotropic_filtering),
                            options = anisotropicFilteringOptions(),
                            selectedValue = uiState.anisotropicFiltering,
                            onSelect = viewModel::setAnisotropicFiltering
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_hw_mipmapping),
                            subtitle = stringResource(R.string.settings_hw_mipmapping_desc),
                            checked = uiState.enableHwMipmapping,
                            onCheckedChange = viewModel::setEnableHwMipmapping
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_frame_control)) {
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
                            onSelect = viewModel::setFrameSkip
                        )
                    }
                }

                SettingsTab.Cheats -> {
                    SettingsSection(title = stringResource(R.string.settings_cheats_tab)) {
                        ToggleItem(
                            icon = Icons.Rounded.Star,
                            title = stringResource(R.string.settings_enable_cheats),
                            subtitle = stringResource(R.string.settings_enable_cheats_desc),
                            checked = uiState.enableCheats,
                            onCheckedChange = viewModel::setEnableCheats
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
                                SettingsItem(
                                    icon = Icons.Rounded.SaveAs,
                                    label = entry.displayName,
                                    value = context.getString(
                                        R.string.settings_cheats_file_summary,
                                        entry.fileName,
                                        entry.blockCount
                                    ),
                                    onClick = { onOpenCheatEditor(entry.gameKey) }
                                )
                            }
                        }
                    }
                }

                SettingsTab.Advanced -> {
                    SettingsSection(title = stringResource(R.string.settings_hardware_fixes)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_cpu_sprite_render_size),
                            options = cpuSpriteRenderSizeOptions(),
                            selectedValue = uiState.cpuSpriteRenderSize,
                            onSelect = viewModel::setCpuSpriteRenderSize
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_cpu_sprite_render_level),
                            options = cpuSpriteRenderLevelOptions(),
                            selectedValue = uiState.cpuSpriteRenderLevel,
                            onSelect = viewModel::setCpuSpriteRenderLevel
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_software_clut_render),
                            options = softwareClutRenderOptions(),
                            selectedValue = uiState.softwareClutRender,
                            onSelect = viewModel::setSoftwareClutRender
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_gpu_target_clut),
                            options = gpuTargetClutOptions(),
                            selectedValue = uiState.gpuTargetClutMode,
                            onSelect = viewModel::setGpuTargetClutMode
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_auto_flush_hardware),
                            options = autoFlushHardwareOptions(),
                            selectedValue = uiState.autoFlushHardware,
                            onSelect = viewModel::setAutoFlushHardware
                        )
                        SliderItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            title = stringResource(R.string.settings_skip_draw_start),
                            subtitle = uiState.skipDrawStart.toString(),
                            value = uiState.skipDrawStart.toFloat(),
                            range = 0f..100f,
                            steps = 99,
                            onValueChange = { viewModel.setSkipDrawStart(it.toInt()) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.SettingsSuggest,
                            title = stringResource(R.string.settings_skip_draw_end),
                            subtitle = uiState.skipDrawEnd.toString(),
                            value = uiState.skipDrawEnd.toFloat(),
                            range = 0f..100f,
                            steps = 99,
                            onValueChange = { viewModel.setSkipDrawEnd(it.toInt()) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_cpu_framebuffer_conversion),
                            subtitle = stringResource(R.string.settings_cpu_framebuffer_conversion_desc),
                            checked = uiState.cpuFramebufferConversion,
                            onCheckedChange = viewModel::setCpuFramebufferConversion
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_depth_conversion),
                            subtitle = stringResource(R.string.settings_disable_depth_conversion_desc),
                            checked = uiState.disableDepthConversion,
                            onCheckedChange = viewModel::setDisableDepthConversion
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_safe_features),
                            subtitle = stringResource(R.string.settings_disable_safe_features_desc),
                            checked = uiState.disableSafeFeatures,
                            onCheckedChange = viewModel::setDisableSafeFeatures
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_render_fixes),
                            subtitle = stringResource(R.string.settings_disable_render_fixes_desc),
                            checked = uiState.disableRenderFixes,
                            onCheckedChange = viewModel::setDisableRenderFixes
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_preload_frame_data),
                            subtitle = stringResource(R.string.settings_preload_frame_data_desc),
                            checked = uiState.preloadFrameData,
                            onCheckedChange = viewModel::setPreloadFrameData
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_disable_partial_invalidation),
                            subtitle = stringResource(R.string.settings_disable_partial_invalidation_desc),
                            checked = uiState.disablePartialInvalidation,
                            onCheckedChange = viewModel::setDisablePartialInvalidation
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_texture_inside_rt),
                            options = textureInsideRtOptions(),
                            selectedValue = uiState.textureInsideRt,
                            onSelect = viewModel::setTextureInsideRt
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_read_targets_on_close),
                            subtitle = stringResource(R.string.settings_read_targets_on_close_desc),
                            checked = uiState.readTargetsOnClose,
                            onCheckedChange = viewModel::setReadTargetsOnClose
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_estimate_texture_region),
                            subtitle = stringResource(R.string.settings_estimate_texture_region_desc),
                            checked = uiState.estimateTextureRegion,
                            onCheckedChange = viewModel::setEstimateTextureRegion
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_gpu_palette_conversion),
                            subtitle = stringResource(R.string.settings_gpu_palette_conversion_desc),
                            checked = uiState.gpuPaletteConversion,
                            onCheckedChange = viewModel::setGpuPaletteConversion
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_upscaling_fixes)) {
                        ChoiceSection(
                            title = stringResource(R.string.settings_half_pixel_offset),
                            options = halfPixelOffsetOptions(),
                            selectedValue = uiState.halfPixelOffset,
                            onSelect = viewModel::setHalfPixelOffset
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_native_scaling),
                            options = nativeScalingOptions(),
                            selectedValue = uiState.nativeScaling,
                            onSelect = viewModel::setNativeScaling
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_round_sprite),
                            options = roundSpriteOptions(),
                            selectedValue = uiState.roundSprite,
                            onSelect = viewModel::setRoundSprite
                        )
                        ChoiceSection(
                            title = stringResource(R.string.settings_bilinear_upscale),
                            options = bilinearUpscaleOptions(),
                            selectedValue = uiState.bilinearUpscale,
                            onSelect = viewModel::setBilinearUpscale
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_texture_offset_x),
                            subtitle = uiState.textureOffsetX.toString(),
                            value = uiState.textureOffsetX.toFloat(),
                            range = -512f..512f,
                            steps = 1023,
                            onValueChange = { viewModel.setTextureOffsetX(it.toInt()) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_texture_offset_y),
                            subtitle = uiState.textureOffsetY.toString(),
                            value = uiState.textureOffsetY.toFloat(),
                            range = -512f..512f,
                            steps = 1023,
                            onValueChange = { viewModel.setTextureOffsetY(it.toInt()) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_align_sprite),
                            subtitle = stringResource(R.string.settings_align_sprite_desc),
                            checked = uiState.alignSprite,
                            onCheckedChange = viewModel::setAlignSprite
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_merge_sprite),
                            subtitle = stringResource(R.string.settings_merge_sprite_desc),
                            checked = uiState.mergeSprite,
                            onCheckedChange = viewModel::setMergeSprite
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_force_even_sprite_position),
                            subtitle = stringResource(R.string.settings_force_even_sprite_position_desc),
                            checked = uiState.forceEvenSpritePosition,
                            onCheckedChange = viewModel::setForceEvenSpritePosition
                        )
                        ToggleItem(
                            icon = Icons.Rounded.GraphicEq,
                            title = stringResource(R.string.settings_native_palette_draw),
                            subtitle = stringResource(R.string.settings_native_palette_draw_desc),
                            checked = uiState.nativePaletteDraw,
                            onCheckedChange = viewModel::setNativePaletteDraw
                        )
                    }
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
                    }
                }
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
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding, vertical = 4.dp),
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

@Composable
private fun ThemeSelector(
    selected: ThemeMode,
    onSelected: (ThemeMode) -> Unit
) {
    ChoiceSection(
        title = stringResource(R.string.settings_theme),
        options = listOf(
            0 to stringResource(R.string.settings_theme_system),
            1 to stringResource(R.string.settings_theme_light),
            2 to stringResource(R.string.settings_theme_dark)
        ),
        selectedValue = when (selected) {
            ThemeMode.SYSTEM -> 0
            ThemeMode.LIGHT -> 1
            ThemeMode.DARK -> 2
        },
        onSelect = { value ->
            onSelected(
                when (value) {
                    1 -> ThemeMode.LIGHT
                    2 -> ThemeMode.DARK
                    else -> ThemeMode.SYSTEM
                }
            )
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
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding),
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
    onClick: () -> Unit
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
                Text(
                    text = label,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = value,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
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
    onCheckedChange: (Boolean) -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
        onClick = { onCheckedChange(!checked) }
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
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
                modifier = Modifier.padding(end = 2.dp)
            )
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
    onValueChange: (Float) -> Unit
) {
    var sliderValue by remember { mutableFloatStateOf(value) }

    LaunchedEffect(value) {
        sliderValue = value
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
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
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Medium
                )
            }
        }
        Slider(
            value = sliderValue,
            onValueChange = { sliderValue = it },
            onValueChangeFinished = { onValueChange(sliderValue) },
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

@Composable
private fun ChoiceSection(
    title: String,
    options: List<Pair<Int, String>>,
    selectedValue: Int,
    onSelect: (Int) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.padding(horizontal = 16.dp)
        )
        LazyRow(
            modifier = Modifier
                .fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(options) { (value, label) ->
                FilterChip(
                    selected = selectedValue == value,
                    onClick = { onSelect(value) },
                    label = { Text(text = label) }
                )
            }
        }
    }
}

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
private fun performancePresetOptions(): List<Pair<Int, String>> = listOf(
    PerformancePresets.CUSTOM to stringResource(R.string.settings_performance_preset_custom),
    PerformancePresets.BATTERY to stringResource(R.string.settings_performance_preset_battery),
    PerformancePresets.BALANCED to stringResource(R.string.settings_performance_preset_balanced),
    PerformancePresets.PERFORMANCE to stringResource(R.string.settings_performance_preset_performance),
    PerformancePresets.AGGRESSIVE to stringResource(R.string.settings_performance_preset_aggressive)
)

@Composable
private fun hwDownloadModeOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_hw_download_mode_accurate),
    1 to stringResource(R.string.settings_hw_download_mode_no_readbacks),
    2 to stringResource(R.string.settings_hw_download_mode_unsynchronized),
    3 to stringResource(R.string.settings_hw_download_mode_disabled)
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
    0 to stringResource(R.string.settings_trilinear_filtering_auto),
    1 to stringResource(R.string.settings_trilinear_filtering_off),
    2 to stringResource(R.string.settings_trilinear_filtering_ps2),
    3 to stringResource(R.string.settings_trilinear_filtering_forced)
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
    2 to stringResource(R.string.settings_native_scaling_aggressive)
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
private fun performancePresetDescription(preset: Int): Int = when (preset) {
    PerformancePresets.BATTERY -> R.string.settings_performance_preset_battery_desc
    PerformancePresets.BALANCED -> R.string.settings_performance_preset_balanced_desc
    PerformancePresets.PERFORMANCE -> R.string.settings_performance_preset_performance_desc
    PerformancePresets.AGGRESSIVE -> R.string.settings_performance_preset_aggressive_desc
    else -> R.string.settings_performance_preset_custom_desc
}

@Composable
private fun chipsetDisplayLabel(
    family: DeviceChipsetFamily,
    detectedName: String
): String {
    val familyLabel = when (family) {
        DeviceChipsetFamily.MEDIATEK -> stringResource(R.string.settings_device_profile_family_mediatek)
        DeviceChipsetFamily.SNAPDRAGON -> stringResource(R.string.settings_device_profile_family_snapdragon)
        DeviceChipsetFamily.EXYNOS -> stringResource(R.string.settings_device_profile_family_exynos)
        DeviceChipsetFamily.TENSOR -> stringResource(R.string.settings_device_profile_family_tensor)
        DeviceChipsetFamily.GENERIC -> stringResource(R.string.settings_device_profile_family_generic)
    }
    return if (detectedName.isBlank()) familyLabel else "$familyLabel • $detectedName"
}

@StringRes
private fun deviceProfileDescription(family: DeviceChipsetFamily): Int = when (family) {
    DeviceChipsetFamily.MEDIATEK -> R.string.settings_device_profile_desc_mediatek
    DeviceChipsetFamily.SNAPDRAGON -> R.string.settings_device_profile_desc_snapdragon
    DeviceChipsetFamily.EXYNOS -> R.string.settings_device_profile_desc_generic
    DeviceChipsetFamily.TENSOR -> R.string.settings_device_profile_desc_generic
    DeviceChipsetFamily.GENERIC -> R.string.settings_device_profile_desc_generic
}

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
    "dpad_up" -> R.string.settings_gamepad_action_dpad_up
    "dpad_down" -> R.string.settings_gamepad_action_dpad_down
    "dpad_left" -> R.string.settings_gamepad_action_dpad_left
    "dpad_right" -> R.string.settings_gamepad_action_dpad_right
    else -> R.string.settings_gamepad_section
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
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding, vertical = 12.dp)
                .navigationBarsPadding(),
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
    val options = rememberLanguageOptions()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
            .verticalScroll(rememberScrollState())
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding, vertical = 0.dp)
                .padding(top = topInset, bottom = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            NavigationBackButton(
                onClick = onBackClick,
                contentColor = MaterialTheme.colorScheme.onSurface
            )
            Column(modifier = Modifier.padding(start = 8.dp)) {
                Text(
                    text = stringResource(R.string.settings_language),
                    style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onBackground
                )
                Text(
                    text = stringResource(R.string.settings_language_screen_subtitle),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

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
    @StringRes val titleRes: Int,
    @StringRes val subtitleRes: Int? = null
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
            LanguageUiOption("zh", "繁", R.string.settings_language_traditional_chinese, R.string.settings_language_native_traditional_chinese)
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
        "zh", "zh-TW", "zh-Hant", "zh-Hant-TW" -> stringResource(R.string.settings_language_traditional_chinese)
        else -> stringResource(R.string.settings_language_system)
    }
}

@Composable
private fun SettingsTab.label(): String {
    return when (this) {
        SettingsTab.General -> stringResource(R.string.settings_general_tab)
        SettingsTab.Graphics -> stringResource(R.string.settings_graphics_tab)
        SettingsTab.Controls -> stringResource(R.string.settings_controls_tab)
        SettingsTab.Paths -> stringResource(R.string.settings_paths_tab)
        SettingsTab.DataTransfer -> stringResource(R.string.settings_data_transfer_tab)
        SettingsTab.Performance -> stringResource(R.string.settings_performance_tab)
        SettingsTab.SpeedHacks -> stringResource(R.string.settings_speedhacks_tab)
        SettingsTab.Cheats -> stringResource(R.string.settings_cheats_tab)
        SettingsTab.Advanced -> stringResource(R.string.settings_advanced_tab)
        SettingsTab.About -> stringResource(R.string.settings_about)
    }
}

@Composable
private fun SettingsTab.icon(): ImageVector {
    return when (this) {
        SettingsTab.General -> Icons.Rounded.Tune
        SettingsTab.Graphics -> Icons.Rounded.GraphicEq
        SettingsTab.Controls -> Icons.Rounded.Gamepad
        SettingsTab.Paths -> Icons.Rounded.FolderOpen
        SettingsTab.DataTransfer -> Icons.Rounded.SaveAs
        SettingsTab.Performance -> Icons.Rounded.Speed
        SettingsTab.SpeedHacks -> Icons.Rounded.Speed
        SettingsTab.Cheats -> Icons.Rounded.Star
        SettingsTab.Advanced -> Icons.Rounded.SettingsSuggest
        SettingsTab.About -> Icons.Rounded.Info
    }
}

private fun String.toSettingsTab(): SettingsTab {
    return when (lowercase()) {
        "graphics" -> SettingsTab.Graphics
        "controls" -> SettingsTab.Controls
        "paths", "files" -> SettingsTab.Paths
        "data_transfer", "transfer", "backup", "data-transfer" -> SettingsTab.DataTransfer
        "performance" -> SettingsTab.Performance
        "speedhacks", "speed_hacks", "speed-hacks" -> SettingsTab.SpeedHacks
        "cheats", "cheat" -> SettingsTab.Cheats
        "advanced" -> SettingsTab.Advanced
        "about" -> SettingsTab.About
        else -> SettingsTab.General
    }
}

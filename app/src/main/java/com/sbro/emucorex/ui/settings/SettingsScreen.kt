package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SaveAs
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.StayPrimaryPortrait
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Vibration
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.input.key.onPreviewKeyEvent
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
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.core.buildUpscaleOptions
import com.sbro.emucorex.core.upscaleMultiplierValue
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.CheatFileEntry
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.CoverArtRepository
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.data.OverlayLayoutSnapshot
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsBackupRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.launch
import java.io.File

private enum class SettingsTab {
    General, Graphics, Controls, Paths, MemoryCards, Covers, DataTransfer, Performance, SpeedHacks, Cheats, Advanced, About
}

@SuppressLint("ConfigurationScreenWidthHeight")
@OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    initialTab: String = "general",
    onBackClick: (() -> Unit)? = null,
    onOpenLanguageScreen: (() -> Unit)? = null,
    onOpenMemoryCardManager: (() -> Unit)? = null,
    onEditControlsClick: (() -> Unit)? = null,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 10.dp
    var selectedTab by remember(initialTab) { mutableStateOf(initialTab.toSettingsTab()) }
    val cheatRepository = remember(context) { CheatRepository(context) }
    var cheatEntries by remember { mutableStateOf(cheatRepository.listImportedCheatFiles()) }
    var cheatEditorGameKey by remember { mutableStateOf<String?>(null) }
    var cheatEditorFileName by remember { mutableStateOf<String?>(null) }
    var cheatEditorText by remember { mutableStateOf("") }
    var pendingGamepadActionId by remember { mutableStateOf<String?>(null) }
    var showTopBarMenu by remember { mutableStateOf(false) }
    var showResetAllSettingsDialog by remember { mutableStateOf(false) }
    var showCoverUrlDialog by remember { mutableStateOf(false) }
    var showBiosDialog by remember { mutableStateOf(false) }
    var pendingCoverUrl by remember { mutableStateOf("") }
    var searchEnabled by remember { mutableStateOf(false) }
    var searchQuery by remember { mutableStateOf("") }
    val selectedTabFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }
    val scope = rememberCoroutineScope()
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
    val coverUrlCopiedMessage = stringResource(R.string.settings_cover_download_url_copied)
    val coverUrlInvalidMessage = stringResource(R.string.settings_cover_download_url_invalid)
    val notSetLabel = stringResource(R.string.settings_not_set)
    val settingsScrollState = rememberScrollState()

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

    val biosPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? -> uri?.let(viewModel::setBiosPath) }

    val gamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? -> uri?.let(viewModel::setGamePath) }

    val launchBiosPicker = rememberDebouncedClick(onClick = { biosPicker.launch(arrayOf("*/*")) })
    val openBiosDialog = rememberDebouncedClick(onClick = { showBiosDialog = true })
    val launchGamePicker = rememberDebouncedClick(onClick = { gamePicker.launch(null) })
    val openLanguageSheet = rememberDebouncedClick(onClick = { onOpenLanguageScreen?.invoke() })
    val refreshCheatEntries = remember {
        {
            cheatEntries = cheatRepository.listImportedCheatFiles()
        }
    }

    LaunchedEffect(shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus) {
            selectedTabFocusRequester.requestFocus()
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
        Column(
            modifier = Modifier
                .fillMaxSize()
                .navigationBarsPadding()
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
                    showResetAllSettingsDialog = true
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
                onOpenCoverUrlEditor = {
                    pendingCoverUrl = uiState.coverDownloadBaseUrl.orEmpty()
                    showCoverUrlDialog = true
                },
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
                searchQuery = searchQuery,
                onSearchResultSelected = { tab ->
                    selectedTab = tab
                    searchEnabled = false
                    searchQuery = ""
                },
                onOpenMemoryCardManager = onOpenMemoryCardManager,
                onEditControlsClick = onEditControlsClick,
                viewModel = viewModel,
                topInset = 0.dp,
                modifier = Modifier
                    .fillMaxWidth()
            )
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
        val dialogFocusRequester = remember { FocusRequester() }

        LaunchedEffect(Unit) {
            dialogFocusRequester.requestFocus()
        }

        AlertDialog(
            onDismissRequest = { pendingGamepadActionId = null },
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
                        R.string.settings_gamepad_mapping_listening_desc,
                        gamepadActionLabel(pendingGamepadActionId.orEmpty())
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

    if (showBiosDialog) {
        val biosDisplayName = uiState.biosPath?.let { DocumentPathResolver.getDisplayName(context, it) }
            ?: stringResource(R.string.settings_not_set)
        AlertDialog(
            onDismissRequest = { showBiosDialog = false },
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
                        showBiosDialog = false
                        launchBiosPicker()
                    }
                ) {
                    Text(stringResource(R.string.settings_bios_picker_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showBiosDialog = false }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }

    if (showCoverUrlDialog) {
        val coverUrlFocusRequester = remember { FocusRequester() }
        val exampleBundle = remember {
            "${CoverArtRepository.DEFAULT_COVER_BASE_URL} ${CoverArtRepository.DEFAULT_COVER_3D_BASE_URL}"
        }
        LaunchedEffect(showCoverUrlDialog) {
            if (showCoverUrlDialog) {
                coverUrlFocusRequester.requestFocus()
            }
        }
        AlertDialog(
            onDismissRequest = { showCoverUrlDialog = false },
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
                        value = pendingCoverUrl,
                        onValueChange = { pendingCoverUrl = it },
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
                                    pendingCoverUrl = exampleBundle
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
                        val parts = pendingCoverUrl.trim()
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
                        showCoverUrlDialog = false
                    }
                ) {
                    Text(stringResource(R.string.save))
                }
            },
            dismissButton = {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(
                        onClick = {
                            pendingCoverUrl = ""
                            viewModel.setCoverDownloadBaseUrl(null)
                            showCoverUrlDialog = false
                        }
                    ) {
                        Text(stringResource(R.string.settings_cover_download_url_use_default))
                    }
                    TextButton(onClick = { showCoverUrlDialog = false }) {
                        Text(stringResource(R.string.cancel))
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
                    .padding(start = 4.dp, end = 8.dp)
            ) {
                if (searchEnabled) {
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = onSearchQueryChange,
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp),
                        placeholder = { Text(stringResource(R.string.settings_search_placeholder)) }
                    )
                } else {
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
private fun SettingsTabRail(
    selectedTab: SettingsTab,
    onSelected: (SettingsTab) -> Unit,
    topInset: androidx.compose.ui.unit.Dp,
    selectedTabFocusRequester: FocusRequester,
    searchEnabled: Boolean,
    searchQuery: String,
    onSearchEnabledChange: (Boolean) -> Unit,
    onSearchQueryChange: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    val tabs = remember { SettingsTab.entries.toList() }
    Surface(
        modifier = modifier.fillMaxHeight(),
        shape = RoundedCornerShape(30.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 6.dp
    ) {
        LazyColumn(
            modifier = Modifier
                .fillMaxHeight()
                .padding(horizontal = 16.dp),
            contentPadding = PaddingValues(top = 16.dp, bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            item(key = "topInset") {
                Spacer(modifier = Modifier.height(topInset))
            }
            item(key = "title") {
                if (searchEnabled) {
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = onSearchQueryChange,
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 4.dp, vertical = 8.dp),
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp),
                        placeholder = { Text(stringResource(R.string.settings_search_placeholder)) },
                        trailingIcon = {
                            IconButton(onClick = { onSearchEnabledChange(false) }) {
                                Icon(
                                    imageVector = Icons.Rounded.Close,
                                    contentDescription = stringResource(R.string.settings_search)
                                )
                            }
                        }
                    )
                } else {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 8.dp, vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = stringResource(R.string.settings_title),
                            style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.weight(1f)
                        )
                        IconButton(onClick = { onSearchEnabledChange(true) }) {
                            Icon(
                                imageVector = Icons.Rounded.Search,
                                contentDescription = stringResource(R.string.settings_search)
                            )
                        }
                    }
                }
            }
            items(items = tabs, key = { it.name }) { tab ->
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
    val tabs = remember { SettingsTab.entries.toList() }
    LazyRow(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(items = tabs, key = { it.name }) { tab ->
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
    searchQuery: String,
    context: android.content.Context,
    launchBiosPicker: () -> Unit,
    launchGamePicker: () -> Unit,
    onOpenCoverUrlEditor: () -> Unit,
    launchDriverPicker: () -> Unit,
    launchSettingsBackupExport: () -> Unit,
    launchSettingsBackupImport: () -> Unit,
    launchCheatImport: () -> Unit,
    openLanguageSheet: () -> Unit,
    cheatEntries: List<CheatFileEntry>,
    onOpenCheatEditor: (String) -> Unit,
    onRequestGamepadBinding: (String) -> Unit,
    onSearchResultSelected: (SettingsTab) -> Unit,
    onOpenMemoryCardManager: (() -> Unit)? = null,
    onEditControlsClick: (() -> Unit)? = null,
    viewModel: SettingsViewModel,
    topInset: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier
) {
    val gamepadActions = remember { GamepadManager.mappableButtonActions() }
    val defaults = remember { SettingsSnapshot() }
    val overlayDefaults = remember { OverlayLayoutSnapshot() }
    val searchEntries = rememberSettingsSearchEntries()
    val notSetLabel = stringResource(R.string.settings_not_set)
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
                            onSelected = viewModel::setThemeMode
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

                SettingsTab.Graphics -> {
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
                        ChoiceSection(
                            title = stringResource(R.string.settings_gpu_driver),
                            options = listOf(
                                0 to stringResource(R.string.settings_gpu_driver_system),
                                1 to stringResource(R.string.settings_gpu_driver_custom)
                            ),
                            selectedValue = uiState.gpuDriverType,
                            onSelect = viewModel::setGpuDriverType,
                            helpText = stringResource(R.string.settings_help_gpu_driver),
                            onResetToDefault = { viewModel.setGpuDriverType(defaults.gpuDriverType) }
                        )
                        if (uiState.gpuDriverType == 1) {
                            val driverDisplayName = remember(uiState.customDriverPath, notSetLabel) {
                                uiState.customDriverPath?.let { File(it).name }
                                    ?: notSetLabel
                            }
                            SettingsItem(
                                icon = Icons.Rounded.Tune,
                                label = stringResource(R.string.settings_gpu_driver_path),
                                value = driverDisplayName,
                                onClick = launchDriverPicker
                            )
                        }
                        ChoiceSection(
                            title = stringResource(R.string.settings_upscale),
                            options = buildUpscaleOptions(stringResource(R.string.settings_upscale_native)),
                            selectedValue = upscaleMultiplierValue(uiState.upscaleMultiplier),
                            onSelect = { viewModel.setUpscaleMultiplier(it.toFloat()) },
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
                            title = stringResource(R.string.settings_texture_filtering),
                            options = listOf(
                                0 to stringResource(R.string.settings_texture_filtering_nearest),
                                1 to stringResource(R.string.settings_texture_filtering_bilinear),
                                2 to stringResource(R.string.settings_texture_filtering_trilinear)
                            ),
                            selectedValue = uiState.textureFiltering,
                            onSelect = viewModel::setTextureFiltering,
                            helpText = stringResource(R.string.settings_help_texture_filtering),
                            onResetToDefault = { viewModel.setTextureFiltering(defaults.textureFiltering) }
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
                            onValueChange = { viewModel.setOverlayScale(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_overlay_scale),
                            onResetToDefault = { viewModel.setOverlayScale(overlayDefaults.overlayScale) }
                        )
                        SliderItem(
                            icon = Icons.Rounded.Visibility,
                            title = stringResource(R.string.settings_overlay_opacity),
                            subtitle = "${uiState.overlayOpacity}%",
                            value = uiState.overlayOpacity.toFloat(),
                            range = 20f..100f,
                            steps = 7,
                            onValueChange = { viewModel.setOverlayOpacity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_overlay_opacity),
                            onResetToDefault = { viewModel.setOverlayOpacity(overlayDefaults.overlayOpacity) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_gamepad_section)) {
                        ToggleItem(
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.settings_gamepad_auto),
                            subtitle = stringResource(R.string.settings_gamepad_auto_desc),
                            checked = uiState.enableAutoGamepad,
                            onCheckedChange = viewModel::setEnableAutoGamepad,
                            helpText = stringResource(R.string.settings_help_auto_gamepad),
                            onResetToDefault = { viewModel.setEnableAutoGamepad(defaults.enableAutoGamepad) }
                        )
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
                            icon = Icons.Rounded.Vibration,
                            title = stringResource(R.string.settings_pad_vibration),
                            subtitle = stringResource(R.string.settings_pad_vibration_desc),
                            checked = uiState.padVibration,
                            onCheckedChange = viewModel::setPadVibration,
                            helpText = stringResource(R.string.settings_help_pad_vibration),
                            onResetToDefault = { viewModel.setPadVibration(defaults.padVibration) }
                        )
                    }
                    SettingsSection(title = stringResource(R.string.settings_gamepad_mapping_title)) {
                        val connectedControllerName = GamepadManager.firstConnectedControllerName()
                        SettingsInlineNote(
                            text = connectedControllerName?.let {
                                stringResource(R.string.settings_gamepad_mapping_connected, it)
                            } ?: stringResource(R.string.settings_gamepad_mapping_disconnected)
                        )
                        for (action in gamepadActions) {
                            val assignedKeyCode = GamepadManager.resolveBindingForAction(
                                actionId = action.id,
                                customBindings = uiState.gamepadBindings
                            )
                            val isCustomBinding = uiState.gamepadBindings.containsKey(action.id)
                            GamepadBindingRow(
                                title = gamepadActionLabel(action.id),
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
                }

                SettingsTab.Paths -> {
                    val biosDisplayName = remember(uiState.biosPath, context, notSetLabel) {
                        uiState.biosPath?.let { DocumentPathResolver.getDisplayName(context, it) }
                            ?: notSetLabel
                    }
                    val gameDisplayName = remember(uiState.gamePath, context, notSetLabel) {
                        uiState.gamePath?.let { DocumentPathResolver.getDisplayName(context, it) }
                            ?: notSetLabel
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
                    }
                }

                SettingsTab.MemoryCards -> {
                    val repository = remember(context) {
                        MemoryCardRepository(context, AppPreferences(context))
                    }
                    var memoryCardCount by remember { mutableStateOf(0) }
                    var slot1Name by remember { mutableStateOf<String?>(null) }
                    var slot2Name by remember { mutableStateOf<String?>(null) }

                    LaunchedEffect(repository) {
                        val assignments = repository.ensureDefaultCardsAssigned()
                        val cards = repository.listCards()
                        memoryCardCount = cards.size
                        slot1Name = assignments.slot1
                        slot2Name = assignments.slot2
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
                }

                SettingsTab.Covers -> {
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
                            onResetToDefault = { viewModel.setCoverArtStyle(AppPreferences.COVER_ART_STYLE_DISABLED) }
                        )
                        SettingsItem(
                            icon = Icons.Rounded.Link,
                            label = stringResource(R.string.settings_cover_download_url),
                            value = coverUrlDisplay,
                            onClick = onOpenCoverUrlEditor,
                            helpText = stringResource(R.string.settings_help_cover_download_url)
                        )
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
                    }
                }

                SettingsTab.SpeedHacks -> {
                    SettingsSection(title = stringResource(R.string.settings_speed_hacks)) {
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
                            title = stringResource(R.string.settings_skip_duplicate_frames),
                            subtitle = stringResource(R.string.settings_skip_duplicate_frames_desc),
                            checked = uiState.skipDuplicateFrames,
                            onCheckedChange = viewModel::setSkipDuplicateFrames,
                            helpText = stringResource(R.string.settings_help_skip_duplicate_frames),
                            onResetToDefault = { viewModel.setSkipDuplicateFrames(defaults.skipDuplicateFrames) }
                        )
                        ToggleItem(
                            icon = Icons.Rounded.Speed,
                            title = stringResource(R.string.settings_mtvu),
                            subtitle = stringResource(R.string.settings_mtvu_desc),
                            checked = uiState.enableMtvu,
                            onCheckedChange = viewModel::setEnableMtvu,
                            helpText = stringResource(R.string.settings_help_mtvu),
                            onResetToDefault = { viewModel.setEnableMtvu(defaults.enableMtvu) }
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
                            onResetToDefault = { viewModel.setBlendingAccuracy(defaults.blendingAccuracy) }
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
                        ChoiceSection(
                            title = stringResource(R.string.settings_anisotropic_filtering),
                            options = anisotropicFilteringOptions(),
                            selectedValue = uiState.anisotropicFiltering,
                            onSelect = viewModel::setAnisotropicFiltering,
                            helpText = stringResource(R.string.settings_help_anisotropic_filtering),
                            onResetToDefault = { viewModel.setAnisotropicFiltering(defaults.anisotropicFiltering) }
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
                            onSelect = viewModel::setFrameSkip,
                            helpText = stringResource(R.string.settings_help_frame_skip),
                            onResetToDefault = { viewModel.setFrameSkip(defaults.frameSkip) }
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

                SettingsTab.Advanced -> {
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

private fun normalizeSettingsSearchToken(value: String): String {
    return value
        .lowercase()
        .replace(Regex("[^\\p{L}\\p{N}]+"), " ")
        .trim()
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
        onResetToDefault = { onSelected(ThemeMode.SYSTEM) },
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
    return listOf(
        entry(SettingsTab.General, R.string.settings_language),
        entry(SettingsTab.General, R.string.settings_theme),
        entry(SettingsTab.General, R.string.settings_keep_screen_on),
        entry(SettingsTab.General, R.string.settings_show_recent_games),
        entry(SettingsTab.General, R.string.settings_show_home_search),
        entry(SettingsTab.General, R.string.settings_prefer_english_game_titles),
        entry(SettingsTab.Graphics, R.string.settings_renderer),
        entry(SettingsTab.Graphics, R.string.settings_gpu_driver),
        entry(SettingsTab.Graphics, R.string.settings_upscale),
        entry(SettingsTab.Graphics, R.string.settings_aspect_ratio),
        entry(SettingsTab.Graphics, R.string.settings_texture_filtering),
        entry(SettingsTab.Graphics, R.string.settings_bilinear_filtering),
        entry(SettingsTab.Graphics, R.string.settings_trilinear_filtering),
        entry(SettingsTab.Graphics, R.string.settings_anisotropic_filtering),
        entry(SettingsTab.Graphics, R.string.settings_fxaa),
        entry(SettingsTab.Graphics, R.string.settings_cas),
        entry(SettingsTab.Graphics, R.string.settings_widescreen_patches),
        entry(SettingsTab.Graphics, R.string.settings_no_interlacing_patches),
        entry(SettingsTab.Controls, R.string.settings_overlay_scale),
        entry(SettingsTab.Controls, R.string.settings_overlay_opacity),
        entry(SettingsTab.Controls, R.string.settings_gamepad_auto),
        entry(SettingsTab.Controls, R.string.settings_gamepad_hide_overlay),
        entry(SettingsTab.Controls, R.string.settings_pad_vibration),
        entry(SettingsTab.Paths, R.string.settings_bios_path),
        entry(SettingsTab.Paths, R.string.settings_game_path),
        entry(SettingsTab.MemoryCards, R.string.settings_memory_cards_tab),
        entry(SettingsTab.Graphics, R.string.settings_gpu_driver_path),
        entry(SettingsTab.Covers, R.string.settings_cover_art_style),
        entry(SettingsTab.Covers, R.string.settings_cover_download_url),
        entry(SettingsTab.DataTransfer, R.string.settings_backup_export_title),
        entry(SettingsTab.DataTransfer, R.string.settings_backup_restore_title),
        entry(SettingsTab.Performance, R.string.settings_show_fps),
        entry(SettingsTab.Performance, R.string.settings_fps_overlay_mode),
        entry(SettingsTab.Performance, R.string.settings_fps_overlay_position),
        entry(SettingsTab.SpeedHacks, R.string.settings_frame_limiter),
        entry(SettingsTab.SpeedHacks, R.string.settings_target_fps),
        entry(SettingsTab.SpeedHacks, R.string.settings_ee_cycle_rate),
        entry(SettingsTab.SpeedHacks, R.string.settings_ee_cycle_skip),
        entry(SettingsTab.SpeedHacks, R.string.settings_mtvu),
        entry(SettingsTab.SpeedHacks, R.string.settings_fast_cdvd),
        entry(SettingsTab.Cheats, R.string.settings_enable_cheats),
        entry(SettingsTab.Advanced, R.string.settings_hw_download_mode),
        entry(SettingsTab.Advanced, R.string.settings_blending_accuracy),
        entry(SettingsTab.Advanced, R.string.settings_texture_preloading),
        entry(SettingsTab.Advanced, R.string.settings_hw_mipmapping)
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
    onResetToDefault: (() -> Unit)? = null
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
                onClick = { onCheckedChange(!checked) },
                onLongClick = onResetToDefault
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
    onValueChange: (Float) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    var sliderValue by remember { mutableFloatStateOf(value) }
    val interactionSource = remember { MutableInteractionSource() }

    LaunchedEffect(value) {
        sliderValue = value
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = {},
                onLongClick = onResetToDefault
            )
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
    onSelect: (Int) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 16.dp)
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = {},
                    onLongClick = onResetToDefault
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

private fun resolveManualTargetFps(currentTargetFps: Int, defaultTargetFps: Int): Int {
    return when {
        currentTargetFps > 0 -> currentTargetFps
        defaultTargetFps > 0 -> defaultTargetFps
        else -> 60
    }
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
private fun anisotropicFilteringOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_aniso_off),
    2 to "2x",
    4 to "4x",
    8 to "8x",
    16 to "16x"
)

@Composable
private fun casModeOptions(): List<Pair<Int, String>> = listOf(
    0 to stringResource(R.string.settings_cas_mode_off),
    1 to stringResource(R.string.settings_cas_mode_sharpen_only),
    2 to stringResource(R.string.settings_cas_mode_sharpen_resize)
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

@Composable
private fun gamepadActionLabel(actionId: String): String = when (actionId) {
    "cross" -> "\u2715"
    "circle" -> "\u25cb"
    "square" -> "\u25a1"
    "triangle" -> "\u25b3"
    else -> stringResource(gamepadActionLabelRes(actionId))
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
        SettingsTab.MemoryCards -> stringResource(R.string.settings_memory_cards_tab)
        SettingsTab.Covers -> stringResource(R.string.settings_covers_tab)
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
        SettingsTab.MemoryCards -> Icons.Rounded.Memory
        SettingsTab.Covers -> Icons.Rounded.Link
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
        "memorycards", "memory_cards", "memory-cards", "memcards" -> SettingsTab.MemoryCards
        "covers", "cover-art", "cover_art" -> SettingsTab.Covers
        "data_transfer", "transfer", "backup", "data-transfer" -> SettingsTab.DataTransfer
        "performance" -> SettingsTab.Performance
        "speedhacks", "speed_hacks", "speed-hacks" -> SettingsTab.SpeedHacks
        "cheats", "cheat" -> SettingsTab.Cheats
        "advanced" -> SettingsTab.Advanced
        "about" -> SettingsTab.About
        else -> SettingsTab.General
    }
}

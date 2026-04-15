package com.sbro.emucorex.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.Save
import com.sbro.emucorex.core.EmulatorBridge
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.AlertDialog
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.sbro.emucorex.R
import com.sbro.emucorex.core.buildUpscaleOptions
import com.sbro.emucorex.core.formatUpscaleLabel
import com.sbro.emucorex.core.upscaleMultiplierValue
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.SettingHelpButton
import org.json.JSONObject
import java.text.DateFormat

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun PerGameSettingsManagerScreen(
    onBackClick: () -> Unit
) {
    val context = LocalContext.current
    val repository = remember(context) { PerGameSettingsRepository(context) }
    var profiles by remember { mutableStateOf(repository.getAll()) }
    var editingProfile by remember { mutableStateOf<PerGameSettings?>(null) }
    var pendingDeleteProfile by remember { mutableStateOf<PerGameSettings?>(null) }
    var showResetAllDialog by remember { mutableStateOf(false) }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 10.dp
    val exportSuccess = stringResource(R.string.game_settings_manager_export_success)
    val exportFailure = stringResource(R.string.game_settings_manager_export_failure)
    val importSuccess = stringResource(R.string.game_settings_manager_import_success)
    val importFailure = stringResource(R.string.game_settings_manager_import_failure)

    fun refreshProfiles() {
        profiles = repository.getAll()
    }

    fun toast(message: String) {
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
    }

    val exportLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/json")
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        val success = runCatching {
            context.contentResolver.openOutputStream(uri)?.use { output ->
                output.write(repository.exportJson().toString(2).toByteArray())
            } != null
        }.getOrDefault(false)
        toast(if (success) exportSuccess else exportFailure)
    }

    val importLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        val success = runCatching {
            val json = context.contentResolver.openInputStream(uri)?.use { input ->
                JSONObject(input.readBytes().decodeToString())
            } ?: return@runCatching false
            repository.importJson(json)
            refreshProfiles()
            true
        }.getOrDefault(false)
        toast(if (success) importSuccess else importFailure)
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(
            start = 18.dp,
            end = 18.dp,
            top = topInset + 18.dp,
            bottom = 28.dp
        ),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    NavigationBackButton(onClick = onBackClick)
                    Spacer(modifier = Modifier.width(14.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = stringResource(R.string.game_settings_manager_title),
                            style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onBackground
                        )
                    }
                }
            }

            item {
                Surface(
                    shape = RoundedCornerShape(28.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
                ) {
                    Text(
                        text = stringResource(R.string.game_settings_manager_subtitle),
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 20.dp, vertical = 18.dp),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center
                    )
                }
            }

            item {
                Surface(
                    shape = RoundedCornerShape(28.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.24f)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(18.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.game_settings_manager_tools_title),
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = stringResource(R.string.game_settings_manager_tools_desc),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            ManagerActionButton(
                                modifier = Modifier.weight(1f),
                                icon = Icons.Rounded.Save,
                                title = stringResource(R.string.game_settings_manager_export_title),
                                onClick = { exportLauncher.launch("emucorex-game-settings.json") }
                            )
                            ManagerActionButton(
                                modifier = Modifier.weight(1f),
                                icon = Icons.Rounded.FolderOpen,
                                title = stringResource(R.string.game_settings_manager_import_title),
                                onClick = { importLauncher.launch(arrayOf("application/json", "*/*")) }
                            )
                        }
                        ManagerActionButton(
                            modifier = Modifier.fillMaxWidth(),
                            icon = Icons.Rounded.Restore,
                            title = stringResource(R.string.game_settings_manager_reset_all_title),
                            onClick = { showResetAllDialog = true },
                            destructive = true
                        )
                    }
                }
            }

            if (profiles.isEmpty()) {
                item {
                    Surface(
                        shape = RoundedCornerShape(28.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
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
            } else {
                items(profiles, key = { it.gameKey }) { profile ->
                    GameSettingsProfileCard(
                        profile = profile,
                        onEdit = { editingProfile = profile },
                        onReset = {
                            repository.delete(profile.gameKey)
                            refreshProfiles()
                        },
                        onDelete = { pendingDeleteProfile = profile }
                    )
                }
            }
    }

    if (editingProfile != null) {
        GameSettingsEditorDialog(
            profile = editingProfile!!,
            onDismiss = { editingProfile = null },
            onSave = { updated ->
                repository.save(updated)
                refreshProfiles()
                editingProfile = null
            }
        )
    }

    if (pendingDeleteProfile != null) {
        AlertDialog(
            onDismissRequest = { pendingDeleteProfile = null },
            title = { Text(stringResource(R.string.game_settings_manager_delete_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.game_settings_manager_delete_desc,
                        pendingDeleteProfile!!.gameTitle
                    )
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        repository.delete(pendingDeleteProfile!!.gameKey)
                        pendingDeleteProfile = null
                        refreshProfiles()
                    }
                ) {
                    Text(stringResource(R.string.delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingDeleteProfile = null }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }

    if (showResetAllDialog) {
        AlertDialog(
            onDismissRequest = { showResetAllDialog = false },
            title = { Text(stringResource(R.string.game_settings_manager_reset_all_title)) },
            text = { Text(stringResource(R.string.game_settings_manager_reset_all_desc)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        repository.deleteAll()
                        refreshProfiles()
                        showResetAllDialog = false
                    }
                ) {
                    Text(stringResource(R.string.game_settings_manager_reset_all_confirm))
                }
            },
            dismissButton = {
                TextButton(onClick = { showResetAllDialog = false }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }
}

@Composable
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
            onDismiss()
        }
    )
}

@Composable
private fun GameSettingsProfileCard(
    profile: PerGameSettings,
    onEdit: () -> Unit,
    onReset: () -> Unit,
    onDelete: () -> Unit
) {
    val dateText = remember(profile.updatedAt) {
        DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(profile.updatedAt)
    }
    Surface(
        shape = RoundedCornerShape(26.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
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
    var draft by remember(profile) { mutableStateOf(profile) }
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
    Dialog(onDismissRequest = onDismiss) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .imePadding()
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(max = 720.dp),
                shape = RoundedCornerShape(30.dp),
                color = MaterialTheme.colorScheme.surface
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier.padding(start = 20.dp, end = 20.dp, top = 20.dp, bottom = 14.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Text(
                            text = draft.gameTitle,
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = stringResource(R.string.game_settings_manager_editor_desc),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Column(
                        modifier = Modifier
                            .weight(1f, fill = true)
                            .verticalScroll(rememberScrollState())
                            .padding(start = 20.dp, end = 20.dp, bottom = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        EditorSection(title = stringResource(R.string.game_settings_manager_section_profile)) {
                            SelectionRow(
                                title = stringResource(R.string.settings_renderer),
                                options = listOf(
                                    EmulatorBridge.AUTO_RENDERER to stringResource(R.string.settings_renderer_auto),
                                    12 to stringResource(R.string.settings_renderer_opengl),
                                    14 to stringResource(R.string.settings_renderer_vulkan),
                                    13 to stringResource(R.string.settings_renderer_software)
                                ),
                                selectedValue = draft.renderer,
                                onSelected = { draft = draft.copy(renderer = it) },
                                helpText = stringResource(R.string.settings_help_renderer),
                                onResetToDefault = { draft = draft.copy(renderer = defaultProfile.renderer) }
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
                            SelectionRow(
                                title = stringResource(R.string.settings_upscale),
                                options = buildUpscaleOptions(nativeUpscaleLabel),
                                selectedValue = upscaleMultiplierValue(draft.upscaleMultiplier),
                                onSelected = { draft = draft.copy(upscaleMultiplier = it.toFloat()) },
                                helpText = stringResource(R.string.settings_help_upscale),
                                onResetToDefault = { draft = draft.copy(upscaleMultiplier = defaultProfile.upscaleMultiplier) }
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
                            ToggleRow(
                                title = stringResource(R.string.settings_enable_cheats),
                                checked = draft.enableCheats,
                                onCheckedChange = { draft = draft.copy(enableCheats = it) },
                                helpText = stringResource(R.string.settings_help_cheats),
                                onResetToDefault = { draft = draft.copy(enableCheats = defaultProfile.enableCheats) }
                            )
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
                    }
                    EditorSection(title = stringResource(R.string.game_settings_manager_section_graphics)) {
                        SelectionRow(
                            title = stringResource(R.string.settings_frame_skip),
                            options = (0..3).map { it to it.toString() },
                                selectedValue = draft.frameSkip,
                                onSelected = { draft = draft.copy(frameSkip = it) },
                                helpText = stringResource(R.string.settings_help_frame_skip),
                                onResetToDefault = { draft = draft.copy(frameSkip = defaultProfile.frameSkip) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_bilinear_filtering),
                                options = listOf(
                                    0 to stringResource(R.string.settings_bilinear_filtering_nearest),
                                    1 to stringResource(R.string.settings_bilinear_filtering_ps2),
                                    2 to stringResource(R.string.settings_bilinear_filtering_forced),
                                    3 to stringResource(R.string.settings_bilinear_filtering_no_sprite)
                                ),
                                selectedValue = draft.textureFiltering,
                                onSelected = { draft = draft.copy(textureFiltering = it) },
                                helpText = stringResource(R.string.settings_help_bilinear_filtering),
                                onResetToDefault = { draft = draft.copy(textureFiltering = defaultProfile.textureFiltering) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_trilinear_filtering),
                                options = listOf(
                                    0 to stringResource(R.string.settings_trilinear_filtering_auto),
                                    1 to stringResource(R.string.settings_trilinear_filtering_off),
                                    2 to stringResource(R.string.settings_trilinear_filtering_ps2),
                                    3 to stringResource(R.string.settings_trilinear_filtering_forced)
                                ),
                                selectedValue = draft.trilinearFiltering,
                                onSelected = { draft = draft.copy(trilinearFiltering = it) },
                                helpText = stringResource(R.string.settings_help_trilinear_filtering),
                                onResetToDefault = { draft = draft.copy(trilinearFiltering = defaultProfile.trilinearFiltering) }
                            )
                            SelectionRow(
                                title = stringResource(R.string.settings_hw_download_mode),
                                options = listOf(
                                    0 to stringResource(R.string.settings_hw_download_mode_disabled),
                                    1 to stringResource(R.string.settings_hw_download_mode_accurate),
                                    2 to stringResource(R.string.settings_hw_download_mode_unsynchronized)
                            ),
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
                    }
                }
                HorizontalDivider(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f))
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 20.dp, vertical = 16.dp),
                        horizontalArrangement = Arrangement.End
                    ) {
                        TextButton(onClick = onDismiss) {
                            Text(stringResource(R.string.cancel))
                        }
                        TextButton(onClick = { onSave(draft) }) {
                            Text(stringResource(R.string.save))
                        }
                    }
                }
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
            shape = RoundedCornerShape(22.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
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
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier.combinedClickable(
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
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
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

@Composable
private fun ToggleRow(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = { onCheckedChange(!checked) },
                onLongClick = onResetToDefault
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
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange
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
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = {},
                    onLongClick = onResetToDefault
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
                text = valueLabel,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = range,
            steps = steps
        )
    }
}

@Composable
private fun ManagerActionButton(
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
            MaterialTheme.colorScheme.surface.copy(alpha = 0.72f)
        },
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
            MaterialTheme.colorScheme.surface.copy(alpha = 0.72f)
        }
    ) {
        IconButton(
            modifier = Modifier.fillMaxWidth(),
            onClick = onClick
        ) {
            Icon(
                imageVector = icon,
                contentDescription = title,
                tint = if (destructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
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

private fun rendererLabel(renderer: Int): Int = when (renderer) {
    EmulatorBridge.AUTO_RENDERER -> R.string.settings_renderer_auto
    12 -> R.string.settings_renderer_opengl
    13 -> R.string.settings_renderer_software
    else -> R.string.settings_renderer_vulkan
}

private fun resolveManualTargetFps(currentTargetFps: Int, defaultTargetFps: Int): Int {
    return when {
        currentTargetFps > 0 -> currentTargetFps
        defaultTargetFps > 0 -> defaultTargetFps
        else -> 60
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
        enableMtvu = enableMtvu,
        enableFastCdvd = enableFastCdvd,
        enableCheats = enableCheats,
        hwDownloadMode = hwDownloadMode,
        eeCycleRate = eeCycleRate,
        eeCycleSkip = eeCycleSkip,
        frameSkip = frameSkip,
        skipDuplicateFrames = skipDuplicateFrames,
        frameLimitEnabled = frameLimitEnabled,
        targetFps = targetFps,
        textureFiltering = textureFiltering,
        trilinearFiltering = trilinearFiltering,
        blendingAccuracy = blendingAccuracy,
        texturePreloading = texturePreloading,
        enableFxaa = enableFxaa,
        casMode = casMode,
        casSharpness = casSharpness,
        anisotropicFiltering = anisotropicFiltering,
        enableHwMipmapping = enableHwMipmapping,
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

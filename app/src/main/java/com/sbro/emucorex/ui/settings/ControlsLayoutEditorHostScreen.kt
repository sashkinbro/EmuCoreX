package com.sbro.emucorex.ui.settings

import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.data.TouchControlsLayoutProfile
import com.sbro.emucorex.data.saveTouchControlsLayout
import com.sbro.emucorex.data.toTouchControlsLayoutProfile
import com.sbro.emucorex.data.withCustomTouchControls
import com.sbro.emucorex.data.withTouchControlsLayout
import com.sbro.emucorex.data.withoutTouchControlsLayout
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

@Composable
fun ControlsLayoutEditorHostScreen(
    gamePath: String? = null,
    gameTitle: String? = null,
    gameSerial: String? = null,
    onBackClick: () -> Unit
) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val repository = remember(context) { PerGameSettingsRepository(context) }
    val scope = rememberCoroutineScope()
    val saveMutex = remember { Mutex() }
    val normalizedGamePath = gamePath?.takeIf { it.isNotBlank() }
    val unknownGameTitle = stringResource(R.string.save_manager_unknown_game)
    val resolvedGameTitle = remember(normalizedGamePath, gameTitle, unknownGameTitle) {
        gameTitle?.takeIf { it.isNotBlank() }
            ?: normalizedGamePath?.let {
                DocumentPathResolver.getDisplayName(context, it).substringBeforeLast('.')
            }
            ?: unknownGameTitle
    }
    var editorState by remember(normalizedGamePath) { mutableStateOf<ControlsEditorState?>(null) }
    var hasGameOverride by remember(normalizedGamePath) { mutableStateOf(false) }
    var persistedCustomControls by remember(normalizedGamePath) {
        mutableStateOf<CustomTouchControlLibrary?>(null)
    }
    var isFinishing by remember { mutableStateOf(false) }
    var isResetting by remember { mutableStateOf(false) }
    val saveFailureMessage = stringResource(R.string.controls_editor_save_failed)

    suspend fun loadState(): Pair<ControlsEditorState, Boolean> {
        preferences.migrateOverlayLayoutIfNeeded()
        val globalLayout = preferences.overlayLayoutSnapshot.first()
        val settings = preferences.settingsSnapshot.first()
        val perGame = normalizedGamePath?.let { path ->
            withContext(Dispatchers.IO) { repository.get(path) }
        }
        val layout = perGame?.touchControlsLayout ?: globalLayout.toTouchControlsLayoutProfile()
        val customControls = perGame?.customTouchControls
            ?: preferences.customTouchControls.first()
        return layout.toControlsEditorState(
            visualStyle = perGame?.touchControlVisualStyle ?: settings.touchControlVisualStyle,
            pressEffect = perGame?.touchControlPressEffect ?: settings.touchControlPressEffect,
            overlayScale = globalLayout.overlayScale,
            customControls = customControls
        ) to (perGame?.touchControlsLayout != null || perGame?.customTouchControls != null)
    }

    suspend fun persistState(state: ControlsEditorState) {
        val layout = state.toTouchControlsLayoutProfile()
        val customControls = state.customControls.sanitized()
        if (normalizedGamePath == null) {
            preferences.saveTouchControlsLayout(layout)
            if (customControls != persistedCustomControls) {
                preferences.setCustomTouchControls(customControls)
                persistedCustomControls = customControls
            }
        } else {
            withContext(Dispatchers.IO) {
                val existing = repository.get(normalizedGamePath)
                repository.save(
                    existing
                        .withTouchControlsLayout(
                            gameKey = normalizedGamePath,
                            gameTitle = resolvedGameTitle,
                            gameSerial = gameSerial,
                            layout = layout
                        )
                        .withCustomTouchControls(
                            gameKey = normalizedGamePath,
                            gameTitle = resolvedGameTitle,
                            gameSerial = gameSerial,
                            library = customControls
                        )
                )
            }
            persistedCustomControls = customControls
        }
    }

    fun updateState(transform: (ControlsEditorState) -> ControlsEditorState) {
        if (isResetting || isFinishing) return
        val updated = editorState?.let(transform) ?: return
        editorState = updated
        if (normalizedGamePath != null) hasGameOverride = true
        scope.launch {
            runCatching {
                saveMutex.withLock { persistState(updated) }
            }
        }
    }

    fun currentLayout(
        state: ControlsEditorState,
        controlId: String
    ): OverlayControlLayout {
        val defaults = AppPreferences.defaultOverlayControlLayouts(state.stickScale)
        return state.controlLayouts[controlId]
            ?: defaults[controlId]
            ?: OverlayControlLayout(scale = if (controlId.contains("stick")) state.stickScale else 100)
    }

    fun finishEditor() {
        if (isFinishing || isResetting) return
        if (normalizedGamePath != null && !hasGameOverride) {
            onBackClick()
            return
        }
        val current = editorState ?: return
        isFinishing = true
        scope.launch {
            val saved = runCatching {
                saveMutex.withLock { persistState(current) }
            }.isSuccess
            isFinishing = false
            if (saved) {
                onBackClick()
            } else {
                Toast.makeText(context, saveFailureMessage, Toast.LENGTH_SHORT).show()
            }
        }
    }

    LaunchedEffect(normalizedGamePath) {
        val loaded = runCatching { loadState() }.getOrElse {
            val settings = runCatching { preferences.settingsSnapshot.first() }
                .getOrDefault(SettingsSnapshot())
            ControlsEditorState(
                touchControlVisualStyle = settings.touchControlVisualStyle,
                touchControlPressEffect = settings.touchControlPressEffect
            ) to false
        }
        editorState = loaded.first
        hasGameOverride = loaded.second
        persistedCustomControls = loaded.first.customControls
    }

    val state = editorState
    if (state == null) {
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

    val subtitle = if (normalizedGamePath == null) {
        stringResource(R.string.controls_editor_global_scope)
    } else if (hasGameOverride) {
        stringResource(R.string.controls_editor_game_scope, resolvedGameTitle)
    } else {
        stringResource(R.string.controls_editor_game_inherited, resolvedGameTitle)
    }

    ControlsEditorScreen(
        state = state,
        subtitle = subtitle,
        onBackClick = ::finishEditor,
        onUpdateControlOffset = { controlId, offset ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                layouts[controlId] = currentLayout(current, controlId).copy(offset = offset)
                current.copy(controlLayouts = layouts)
            }
        },
        onUpdateControlOffsets = { offsets ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                offsets.forEach { (controlId, offset) ->
                    layouts[controlId] = currentLayout(current, controlId).copy(offset = offset)
                }
                current.copy(controlLayouts = layouts)
            }
        },
        onUpdateControlScale = { controlId, scale ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                layouts[controlId] = currentLayout(current, controlId).copy(
                    scale = scale.coerceIn(
                        AppPreferences.OVERLAY_CONTROL_SCALE_MIN,
                        AppPreferences.OVERLAY_CONTROL_SCALE_MAX
                    )
                )
                current.copy(controlLayouts = layouts)
            }
        },
        onUpdateControlWidthScale = { controlId, widthScale ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                layouts[controlId] = currentLayout(current, controlId).copy(widthScale = widthScale.coerceIn(100, 240))
                current.copy(controlLayouts = layouts)
            }
        },
        onUpdateControlOpacity = { controlId, opacity ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                layouts[controlId] = currentLayout(current, controlId).copy(
                    opacity = opacity.coerceIn(
                        AppPreferences.OVERLAY_CONTROL_OPACITY_MIN,
                        AppPreferences.OVERLAY_CONTROL_OPACITY_MAX
                    )
                )
                current.copy(controlLayouts = layouts)
            }
        },
        onUpdateControlSecondaryAction = { controlId, secondaryActionId ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                layouts[controlId] = currentLayout(current, controlId).copy(
                    secondaryActionId = secondaryActionId
                )
                current.copy(controlLayouts = layouts)
            }
        },
        onSetControlVisible = { controlId, visible ->
            updateState { current ->
                val layouts = current.controlLayouts.toMutableMap()
                layouts[controlId] = currentLayout(current, controlId).copy(visible = visible)
                current.copy(controlLayouts = layouts)
            }
        },
        onSetStickSurfaceMode = { controlId, enabled ->
            if (controlId == "left_stick" || controlId == "right_stick") {
                updateState { current ->
                    val layouts = current.controlLayouts.toMutableMap()
                    layouts[controlId] = currentLayout(current, controlId).copy(surfaceOnly = enabled)
                    current.copy(controlLayouts = layouts)
                }
            }
        },
        onResetLayout = {
            if (isResetting || isFinishing) return@ControlsEditorScreen
            isResetting = true
            scope.launch {
                val reset = runCatching {
                    saveMutex.withLock {
                        if (normalizedGamePath == null) {
                            preferences.resetControlsLayout()
                        } else {
                            withContext(Dispatchers.IO) {
                                repository.get(normalizedGamePath)?.let { existing ->
                                    existing.withoutTouchControlsLayout()?.let(repository::save)
                                        ?: repository.delete(normalizedGamePath)
                                }
                            }
                        }
                        loadState()
                    }
                }.getOrNull()
                if (reset != null) {
                    editorState = reset.first
                    hasGameOverride = reset.second
                    persistedCustomControls = reset.first.customControls
                } else {
                    Toast.makeText(context, saveFailureMessage, Toast.LENGTH_SHORT).show()
                }
                isResetting = false
            }
        },
        onCustomControlsChange = { library ->
            updateState { current -> current.copy(customControls = library) }
        }
    )
}

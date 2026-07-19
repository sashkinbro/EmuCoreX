package com.sbro.emucorex.ui.emulation

import android.annotation.SuppressLint
import android.content.pm.ActivityInfo
import android.graphics.PixelFormat
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ExitToApp
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.Fullscreen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.LockOpen
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.zIndex
import androidx.compose.ui.window.Dialog
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.AndroidTouchHaptics
import com.sbro.emucorex.core.AndroidGyroscopeInput
import com.sbro.emucorex.core.AndroidTouchHaptics.ButtonPhase
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.buildUpscaleOptions
import com.sbro.emucorex.core.upscaleKeyToMultiplier
import com.sbro.emucorex.core.upscaleMultiplierValue
import com.sbro.emucorex.core.utils.RetroAchievementsLiveStateManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.OverlayLayoutSnapshot
import com.sbro.emucorex.data.PerformanceOverlayMetrics
import com.sbro.emucorex.data.RetroAchievementEntry
import com.sbro.emucorex.data.RetroAchievementGameData
import com.sbro.emucorex.data.RetroAchievementsRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.GameMenuTabId
import com.sbro.emucorex.data.GameMenuSectionId
import com.sbro.emucorex.data.GameMenuLayoutStyle
import com.sbro.emucorex.data.gameMenuSectionsForTab
import com.sbro.emucorex.ui.common.BitmapPathImage
import com.sbro.emucorex.ui.common.ProvideGamepadMenuAction
import com.sbro.emucorex.ui.common.ProvideGamepadShoulderActions
import com.sbro.emucorex.ui.common.ProvideGamepadUiNavigation
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.OverlayDpadDirection
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.VectorDpadCluster
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.buildOverlayCanvasLayout
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.settings.ControlsEditorScreen
import com.sbro.emucorex.ui.settings.toControlsEditorState
import com.sbro.emucorex.ui.theme.GradientEnd
import com.sbro.emucorex.ui.theme.GradientStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date
import kotlin.math.roundToInt
import kotlin.time.Duration.Companion.milliseconds

private object PadKey {
    const val UP = 19
    const val RIGHT = 22
    const val DOWN = 20
    const val LEFT = 21
    const val TRIANGLE = 100
    const val CIRCLE = 97
    const val CROSS = 96
    const val SQUARE = 99
    const val SELECT = 109
    const val START = 108
    const val L1 = 102
    const val L2 = 104
    const val R1 = 103
    const val R2 = 105
    const val L3 = 106
    const val R3 = 107
    const val LEFT_STICK_UP = 110
    const val LEFT_STICK_RIGHT = 111
    const val LEFT_STICK_DOWN = 112
    const val LEFT_STICK_LEFT = 113
    const val RIGHT_STICK_UP = 120
    const val RIGHT_STICK_RIGHT = 121
    const val RIGHT_STICK_DOWN = 122
    const val RIGHT_STICK_LEFT = 123
    const val PRESSURE = 124
}

private const val TRANSPORT_HOLD_DELAY_MS = 360L

private enum class EmulationMenuTab {
    Session,
    Controls,
    Emulation,
    Graphics,
    Fixes,
    Achievements
}

private data class OverlayAchievementsContentState(
    val isLoading: Boolean = false,
    val gameData: RetroAchievementGameData? = null
)

private data class TouchButtonSpec(
    val id: String,
    val drawableRes: Int,
    val width: Dp,
    val height: Dp,
    val x: Dp,
    val y: Dp,
    val shape: androidx.compose.ui.graphics.Shape,
    val opacity: Float = 1f,
    val onPressChange: ((Boolean) -> Unit)? = null,
    val onClick: (() -> Unit)? = null,
    val tapToHold: Boolean = false,
    val longPressDelayMs: Long = 0L,
    val onLongPressChange: ((Boolean) -> Unit)? = null
)

private data class TouchButtonLayoutKey(
    val id: String,
    val width: Dp,
    val height: Dp,
    val x: Dp,
    val y: Dp,
    val tapToHold: Boolean,
    val hasLongPressAction: Boolean
)

private data class LiveSelectionOption(
    val value: Int,
    val label: String? = null,
    val icon: ImageVector? = null,
    val contentDescription: String = label.orEmpty()
)

@Composable
private fun fpsOverlayCornerLiveOptions(): List<LiveSelectionOption> = listOf(
    LiveSelectionOption(AppPreferences.FPS_OVERLAY_CORNER_TOP_LEFT, stringResource(R.string.settings_fps_overlay_corner_top_left)),
    LiveSelectionOption(AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT, stringResource(R.string.settings_fps_overlay_corner_top_right)),
    LiveSelectionOption(AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_LEFT, stringResource(R.string.settings_fps_overlay_corner_bottom_left)),
    LiveSelectionOption(AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_RIGHT, stringResource(R.string.settings_fps_overlay_corner_bottom_right))
)

@Composable
private fun fpsOverlayMetricLiveOptions(): List<Pair<Int, String>> = listOf(
    PerformanceOverlayMetrics.FPS to stringResource(R.string.settings_fps_metric_fps),
    PerformanceOverlayMetrics.VPS to stringResource(R.string.settings_fps_metric_vps),
    PerformanceOverlayMetrics.SPEED to stringResource(R.string.settings_fps_metric_speed),
    PerformanceOverlayMetrics.TARGET to stringResource(R.string.settings_fps_metric_target),
    PerformanceOverlayMetrics.RENDERER to stringResource(R.string.settings_fps_metric_renderer),
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
private fun eeCycleRateLiveOptions(): List<LiveSelectionOption> = listOf(
    LiveSelectionOption(-3, "50%"),
    LiveSelectionOption(-2, "60%"),
    LiveSelectionOption(-1, "75%"),
    LiveSelectionOption(0, "100%"),
    LiveSelectionOption(1, "130%"),
    LiveSelectionOption(2, "180%"),
    LiveSelectionOption(3, "300%")
)

@Composable
private fun eeCycleSkipLiveOptions(): List<LiveSelectionOption> = listOf(
    LiveSelectionOption(0, stringResource(R.string.settings_ee_cycle_disabled)),
    LiveSelectionOption(1, stringResource(R.string.settings_ee_cycle_mild)),
    LiveSelectionOption(2, stringResource(R.string.settings_ee_cycle_moderate)),
    LiveSelectionOption(3, stringResource(R.string.settings_ee_cycle_maximum))
)

private fun Int.toOverlayAlignment(): Alignment = when (this) {
    AppPreferences.FPS_OVERLAY_CORNER_TOP_LEFT -> Alignment.TopStart
    AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_LEFT -> Alignment.BottomStart
    AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_RIGHT -> Alignment.BottomEnd
    else -> Alignment.TopEnd
}

private fun Int.isTopOverlayCorner(): Boolean {
    return this == AppPreferences.FPS_OVERLAY_CORNER_TOP_LEFT ||
        this == AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT
}

private fun Int.isBottomOverlayCorner(): Boolean {
    return this == AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_LEFT ||
        this == AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_RIGHT
}

private fun Int.isRightOverlayCorner(): Boolean {
    return this == AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT ||
        this == AppPreferences.FPS_OVERLAY_CORNER_BOTTOM_RIGHT
}

private fun resolveManualTargetFps(currentTargetFps: Int, defaultTargetFps: Int): Int {
    return when {
        currentTargetFps > 0 -> currentTargetFps
        defaultTargetFps > 0 -> defaultTargetFps
        else -> 60
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun EmulationScreen(
    gamePath: String? = null,
    bootToBios: Boolean = false,
    bootSmokeProbe: Boolean = false,
    saveSlot: Int? = null,
    autotestMode: Boolean = false,
    enableEeRecompilerOverride: Boolean? = null,
    enableIopRecompilerOverride: Boolean? = null,
    enableVu0RecompilerOverride: Boolean? = null,
    enableVu1RecompilerOverride: Boolean? = null,
    enableFastmemOverride: Boolean? = null,
    enableMtvuOverride: Boolean? = null,
    rendererOverride: Int? = null,
    gsDumpFrames: Int? = null,
    gsDumpDelayMs: Int? = null,
    restoredAfterProcessDeath: Boolean = false,
    onExit: (activePlayTimeMs: Long) -> Unit,
    viewModel: EmulationViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val retroAchievementsState by RetroAchievementsLiveStateManager.state.collectAsState()
    val retroAchievementsNotification = retroAchievementsState.notification
    val context = LocalContext.current
    val activity = context as? android.app.Activity
    val originalRequestedOrientation = remember(activity) {
        activity?.requestedOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }
    val restoreHostUi = remember(activity, originalRequestedOrientation) {
        {
            activity?.requestedOrientation = originalRequestedOrientation
            activity?.window?.let { window ->
                WindowCompat.getInsetsController(window, window.decorView)
                    .show(WindowInsetsCompat.Type.systemBars())
            }
            Unit
        }
    }
    val currentOnExit by rememberUpdatedState(onExit)
    val lifecycleOwner = LocalLifecycleOwner.current
    val preferences = remember(context) { AppPreferences(context) }
    val globalDefaults by preferences.settingsSnapshot.collectAsState(initial = SettingsSnapshot())
    val overlayDefaults by preferences.overlayLayoutSnapshot.collectAsState(initial = OverlayLayoutSnapshot())
    val gamepadBindingsByPad by preferences.gamepadBindingsByPad.collectAsState(initial = emptyMap())
    val gamepadActions = remember { GamepadManager.mappableButtonActions() }
    val scope = rememberCoroutineScope()
    val rootCutoutPadding = WindowInsets.displayCutout.asPaddingValues()
    val rootNavPadding = WindowInsets.navigationBars.asPaddingValues()
    val overlayHorizontalSafeInset = maxOf(
        rootCutoutPadding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr),
        rootCutoutPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr),
        rootNavPadding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr),
        rootNavPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr)
    )
    val overlayTopSafeInset = maxOf(
        rootCutoutPadding.calculateTopPadding(),
        rootNavPadding.calculateTopPadding()
    )
    val overlayBottomSafeInset = maxOf(
        rootCutoutPadding.calculateBottomPadding(),
        rootNavPadding.calculateBottomPadding()
    )

    LocalConfiguration.current
    val view = LocalView.current
    var showExitDialog by remember { mutableStateOf(false) }
    var exitDispatched by remember { mutableStateOf(false) }
    var showQuickSaveDialog by remember { mutableStateOf(false) }
    var showQuickLoadDialog by remember { mutableStateOf(false) }
    var showAutoSaveLoadDialog by remember { mutableStateOf(false) }
    var showCheatsDialog by remember { mutableStateOf(false) }
    var showControlsEditor by remember { mutableStateOf(false) }
    var showGamepadMappingDialog by remember { mutableStateOf(false) }
    var pendingGamepadActionId by remember { mutableStateOf<String?>(null) }
    var pendingGamepadPadIndex by rememberSaveable { mutableIntStateOf(0) }
    var selectedGamepadPadIndex by rememberSaveable { mutableIntStateOf(0) }
    var showOverlayShortcut by remember { mutableStateOf(false) }
    var resumeAfterEditor by remember { mutableStateOf(false) }
    var lastTapTimestamp by remember { mutableLongStateOf(0L) }
    var lastTapX by remember { mutableFloatStateOf(0f) }
    var lastTapY by remember { mutableFloatStateOf(0f) }
    val connectedGamepadCount by GamepadManager.connectedGamepadCountState.collectAsState()
    val gamepadConnected = connectedGamepadCount > 0
    val touchPadIndex = GamepadManager.resolveTouchPadIndex()
    val overlayPadIndex = touchPadIndex ?: 0
    val currentOverlayPadIndex by rememberUpdatedState(overlayPadIndex)
    val currentActivePlayTimeMs by rememberUpdatedState(uiState.activePlayTimeMs)
    val gyroController = remember(context) {
        AndroidGyroscopeInput(context) { emittedMode, x, y ->
            val targetRightStick = emittedMode == AppPreferences.GYRO_MODE_AIM
            updateAnalogStick(
                x = x,
                y = y,
                upKey = if (targetRightStick) PadKey.RIGHT_STICK_UP else PadKey.LEFT_STICK_UP,
                rightKey = if (targetRightStick) PadKey.RIGHT_STICK_RIGHT else PadKey.LEFT_STICK_RIGHT,
                downKey = if (targetRightStick) PadKey.RIGHT_STICK_DOWN else PadKey.LEFT_STICK_DOWN,
                leftKey = if (targetRightStick) PadKey.RIGHT_STICK_LEFT else PadKey.LEFT_STICK_LEFT,
                onPadInput = { key, range, pressed -> viewModel.onPadInput(currentOverlayPadIndex, key, range, pressed) }
            )
        }
    }
    DisposableEffect(
        lifecycleOwner,
        uiState.isRunning,
        uiState.gyroMode,
        uiState.gyroSensitivity,
        uiState.gyroSmoothing,
        uiState.gyroInvertX,
        uiState.gyroInvertY
    ) {
        fun startGyro() {
            if (uiState.isRunning && uiState.gyroMode != AppPreferences.GYRO_MODE_OFF) {
                gyroController.start(
                    mode = uiState.gyroMode,
                    sensitivityPercent = uiState.gyroSensitivity,
                    smoothingPercent = uiState.gyroSmoothing,
                    invertX = uiState.gyroInvertX,
                    invertY = uiState.gyroInvertY
                )
            }
        }
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> startGyro()
                Lifecycle.Event.ON_PAUSE, Lifecycle.Event.ON_STOP -> gyroController.stop()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) startGyro()
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            gyroController.stop()
        }
    }
    var showGamepadIndicator by remember { mutableStateOf(gamepadConnected) }

    val shouldShowOverlay = uiState.controlsVisible && (
        touchPadIndex != null || !gamepadConnected || !uiState.hideOverlayOnGamepad
    )

    val toggleMenuClick = rememberDebouncedClick(onClick = { viewModel.toggleMenu() })
    val toggleControlsClick = rememberDebouncedClick(onClick = { viewModel.toggleControlsVisibility() })
    val togglePauseClick = rememberDebouncedClick(onClick = { viewModel.togglePause() })
    val requestQuickSaveClick = rememberDebouncedClick(onClick = {
        if (uiState.confirmSaveLoadActions) {
            showQuickSaveDialog = true
        } else {
            viewModel.quickSave()
        }
    })
    val requestQuickLoadClick = rememberDebouncedClick(onClick = {
        if (uiState.confirmSaveLoadActions) {
            showQuickLoadDialog = true
        } else {
            viewModel.quickLoad()
        }
    })
    val requestAutoSaveLoadClick = rememberDebouncedClick(onClick = { showAutoSaveLoadDialog = true })
    val confirmQuickSaveClick = rememberDebouncedClick(onClick = {
        showQuickSaveDialog = false
        viewModel.quickSave()
    })
    val confirmQuickLoadClick = rememberDebouncedClick(onClick = {
        showQuickLoadDialog = false
        viewModel.quickLoad()
    })
    val confirmAutoSaveLoadClick = rememberDebouncedClick(onClick = {
        showAutoSaveLoadDialog = false
        viewModel.loadAutoSave()
    })
    val dismissQuickSaveClick = rememberDebouncedClick(onClick = { showQuickSaveDialog = false })
    val dismissQuickLoadClick = rememberDebouncedClick(onClick = { showQuickLoadDialog = false })
    val dismissAutoSaveLoadClick = rememberDebouncedClick(onClick = { showAutoSaveLoadDialog = false })
    val requestExitClick = rememberDebouncedClick(onClick = { showExitDialog = true })
    val confirmExitClick = rememberDebouncedClick(onClick = {
        if (exitDispatched) return@rememberDebouncedClick
        exitDispatched = true
        showExitDialog = false
        val completedActivePlayTimeMs = currentActivePlayTimeMs
        viewModel.stopEmulation(onExit = {
            completeEmulationExit(
                activePlayTimeMs = completedActivePlayTimeMs,
                restoreHostUi = restoreHostUi,
                navigateFromEmulation = currentOnExit
            )
        })
    })
    val dismissExitClick = rememberDebouncedClick(onClick = { showExitDialog = false })
    val dismissCheatsDialog: () -> Unit = { showCheatsDialog = false }
    val dismissCheatsDialogClick = rememberDebouncedClick(onClick = dismissCheatsDialog)
    val dismissGamepadMappingDialog: () -> Unit = { showGamepadMappingDialog = false }
    val dismissGamepadMappingDialogClick = rememberDebouncedClick(onClick = dismissGamepadMappingDialog)
    val sessionRestoredUnavailableMessage = stringResource(R.string.emulation_session_restored_unavailable)
    val gamepadUiActive = uiState.showMenu ||
        showControlsEditor ||
        showExitDialog ||
        showQuickSaveDialog ||
        showQuickLoadDialog ||
        showAutoSaveLoadDialog ||
        showCheatsDialog ||
        showGamepadMappingDialog ||
        pendingGamepadActionId != null

    BackHandler(enabled = true) {
        when (resolveEmulationBackAction(uiState.backButtonExitsGame)) {
            EmulationBackAction.OpenGameMenu -> toggleMenuClick()
            EmulationBackAction.RequestExit -> requestExitClick()
        }
    }

    ProvideGamepadMenuAction(onMenu = toggleMenuClick)
    ProvideGamepadUiNavigation(
        enabled = gamepadUiActive,
        onBack = {
            when {
                pendingGamepadActionId != null -> {
                    pendingGamepadActionId = null
                    true
                }
                showGamepadMappingDialog -> {
                    dismissGamepadMappingDialog()
                    true
                }
                showCheatsDialog -> {
                    dismissCheatsDialog()
                    true
                }
                showQuickLoadDialog -> {
                    showQuickLoadDialog = false
                    true
                }
                showAutoSaveLoadDialog -> {
                    showAutoSaveLoadDialog = false
                    true
                }
                showQuickSaveDialog -> {
                    showQuickSaveDialog = false
                    true
                }
                showExitDialog -> {
                    showExitDialog = false
                    true
                }
                showControlsEditor -> {
                    showControlsEditor = false
                    true
                }
                uiState.showMenu -> {
                    toggleMenuClick()
                    true
                }
                else -> false
            }
        }
    )

    LaunchedEffect(restoredAfterProcessDeath) {
        if (!restoredAfterProcessDeath) return@LaunchedEffect
        Toast.makeText(
            context,
            sessionRestoredUnavailableMessage,
            Toast.LENGTH_LONG
        ).show()
        if (!exitDispatched) {
            exitDispatched = true
            completeEmulationExit(
                activePlayTimeMs = 0L,
                restoreHostUi = restoreHostUi,
                navigateFromEmulation = currentOnExit
            )
        }
    }

    DisposableEffect(activity, originalRequestedOrientation) {
        activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        activity?.window?.let { window ->
            val controller = WindowCompat.getInsetsController(window, window.decorView)
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            controller.hide(WindowInsetsCompat.Type.systemBars())
        }
        onDispose {
            restoreHostUi()
        }
    }

    DisposableEffect(lifecycleOwner, context, showControlsEditor, uiState.showMenu) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START,
                Lifecycle.Event.ON_RESUME -> {
                    if (!showControlsEditor && !uiState.showMenu) {
                        viewModel.onHostForegrounded()
                    }
                }

                Lifecycle.Event.ON_PAUSE,
                Lifecycle.Event.ON_STOP -> {
                    val activity = context as? android.app.Activity
                    if (showControlsEditor || activity?.isChangingConfigurations == true) {
                        return@LifecycleEventObserver
                    }
                    viewModel.onHostBackgrounded()
                }

                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }

    LaunchedEffect(
        gamePath,
        bootToBios,
        bootSmokeProbe,
        autotestMode,
        enableEeRecompilerOverride,
        enableIopRecompilerOverride,
        enableVu0RecompilerOverride,
        enableVu1RecompilerOverride,
        enableFastmemOverride,
        enableMtvuOverride,
        rendererOverride,
        gsDumpFrames,
        gsDumpDelayMs,
        restoredAfterProcessDeath
    ) {
        if (restoredAfterProcessDeath) return@LaunchedEffect
        viewModel.startEmulation(
            path = gamePath,
            slotToLoad = saveSlot,
            bootToBios = bootToBios,
            bootSmokeProbe = bootSmokeProbe,
            autotestMode = autotestMode,
            enableEeRecompilerOverride = enableEeRecompilerOverride,
            enableIopRecompilerOverride = enableIopRecompilerOverride,
            enableVu0RecompilerOverride = enableVu0RecompilerOverride,
            enableVu1RecompilerOverride = enableVu1RecompilerOverride,
            enableFastmemOverride = enableFastmemOverride,
            enableMtvuOverride = enableMtvuOverride,
            rendererOverride = rendererOverride,
            gsDumpFrames = gsDumpFrames,
            gsDumpDelayMs = gsDumpDelayMs
        )
        RetroAchievementsLiveStateManager.refreshFromNative()
    }

    LaunchedEffect(
        uiState.isRunning,
        gamePath,
        retroAchievementsState.enabled,
        retroAchievementsState.user?.username
    ) {
        if (!uiState.isRunning || gamePath.isNullOrBlank() || !retroAchievementsState.enabled) {
            return@LaunchedEffect
        }
        delay(800.milliseconds)
        RetroAchievementsLiveStateManager.refreshFromNative()
    }

    LaunchedEffect(gamepadConnected, uiState.showMenu) {
        if (gamepadConnected && !uiState.showMenu) {
            showGamepadIndicator = true
            delay(5000.milliseconds)
            showGamepadIndicator = false
        } else {
            showGamepadIndicator = false
        }
    }

    LaunchedEffect(showOverlayShortcut, uiState.showMenu) {
        if (uiState.showMenu) {
            showOverlayShortcut = false
        } else if (showOverlayShortcut) {
            delay(2200.milliseconds)
            showOverlayShortcut = false
        }
    }

    LaunchedEffect(retroAchievementsNotification?.id) {
        val notification = retroAchievementsNotification ?: return@LaunchedEffect
        delay((if (notification.kind == "mastery") 6500 else 4300).milliseconds)
        RetroAchievementsLiveStateManager.dismissNotification(notification.id)
    }

    LaunchedEffect(showControlsEditor) {
        if (showControlsEditor && !uiState.isPaused) {
            resumeAfterEditor = true
            viewModel.togglePause()
        } else if (!showControlsEditor && resumeAfterEditor && uiState.isPaused) {
            resumeAfterEditor = false
            viewModel.togglePause()
        }
    }

    DisposableEffect(pendingGamepadActionId) {
        val actionId = pendingGamepadActionId
        if (actionId != null) {
            GamepadManager.startBindingCapture(pendingGamepadPadIndex) { keyCode ->
                scope.launch {
                    preferences.setGamepadBinding(pendingGamepadPadIndex, actionId, keyCode)
                }
                pendingGamepadActionId = null
            }
        }
        onDispose {
            GamepadManager.cancelBindingCapture()
        }
    }

    LaunchedEffect(uiState.confirmSaveLoadActions) {
        GamepadManager.gamepadShortcutActions.collect { action ->
            when (action.actionId) {
                GamepadManager.ACTION_QUICK_SAVE -> {
                    if (uiState.confirmSaveLoadActions) {
                        showQuickSaveDialog = true
                    } else {
                        viewModel.quickSave()
                    }
                }
                GamepadManager.ACTION_QUICK_LOAD -> {
                    if (uiState.confirmSaveLoadActions) {
                        showQuickLoadDialog = true
                    } else {
                        viewModel.quickLoad()
                    }
                }
            }
        }
    }

    LaunchedEffect(gamepadUiActive) {
        GamepadManager.setEmulationInputEnabled(!gamepadUiActive)
    }

    DisposableEffect(touchPadIndex, shouldShowOverlay, uiState.showMenu, showControlsEditor) {
        val activeTouchPadIndex = touchPadIndex?.takeIf {
            shouldShowOverlay && !uiState.showMenu && !showControlsEditor
        }
        onDispose {
            activeTouchPadIndex?.let(EmulatorBridge::resetPadState)
        }
    }

    DisposableEffect(Unit) {
        onDispose {
            GamepadManager.setEmulationInputEnabled(false)
            viewModel.stopEmulation()
        }
    }

    DisposableEffect(uiState.keepScreenOn) {
        val previousValue = view.keepScreenOn
        view.keepScreenOn = uiState.keepScreenOn
        onDispose {
            view.keepScreenOn = previousValue
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        // Game surface
        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    isClickable = false
                    isFocusable = false
                    isFocusableInTouchMode = false
                    setZOrderOnTop(false)
                    setZOrderMediaOverlay(false)
                    holder.setFormat(PixelFormat.OPAQUE)
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            try { EmulatorBridge.onSurfaceCreated() } catch (_: Exception) { }
                        }

                        override fun surfaceChanged(
                            holder: SurfaceHolder,
                            format: Int,
                            width: Int,
                            height: Int
                        ) {
                            try { EmulatorBridge.onSurfaceChanged(holder.surface, width, height) } catch (_: Exception) { }
                        }

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            try { EmulatorBridge.onSurfaceDestroyed() } catch (_: Exception) { }
                        }
                    })
                }
            },
            modifier = Modifier
                .fillMaxSize()
                .pointerInteropFilter { event ->
                    if (!showControlsEditor && !uiState.showMenu && event.actionMasked == MotionEvent.ACTION_DOWN) {
                        val timestamp = event.eventTime
                        val dx = event.x - lastTapX
                        val dy = event.y - lastTapY
                        val distanceSquared = (dx * dx) + (dy * dy)
                        val isDoubleTap = timestamp - lastTapTimestamp in 40L..300L && distanceSquared <= 14400f
                        if (isDoubleTap) {
                            showOverlayShortcut = true
                            lastTapTimestamp = 0L
                        } else {
                            lastTapTimestamp = timestamp
                            lastTapX = event.x
                            lastTapY = event.y
                        }
                    }
                    false
                }
        )

        if (!showControlsEditor) {
        // Top systemic overlay (Gamepad, Achievements)
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 16.dp, start = overlayHorizontalSafeInset, end = overlayHorizontalSafeInset),
            contentAlignment = Alignment.TopCenter
        ) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Gamepad indicator
                AnimatedVisibility(
                    visible = gamepadConnected && !uiState.showMenu && showGamepadIndicator,
                    enter = fadeIn(tween(300)),
                    exit = fadeOut(tween(300))
                ) {
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(12.dp))
                            .background(Color(0xFF50D9A0).copy(alpha = 0.20f))
                            .padding(horizontal = 9.dp, vertical = 9.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Gamepad,
                            contentDescription = stringResource(R.string.gamepad_connected),
                            tint = Color(0xFF50D9A0),
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }

            }
        }

        // Native PCSX2 OSD will handle drawing performance stats natively
        // Toast notifications
        AnimatedVisibility(
            visible = showOverlayShortcut && !uiState.showMenu,
            enter = fadeIn(tween(160)) + scaleIn(initialScale = 0.88f, animationSpec = tween(180)),
            exit = fadeOut(tween(160)) + scaleOut(targetScale = 0.88f, animationSpec = tween(160)),
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = overlayTopSafeInset + 12.dp)
        ) {
            Surface(
                onClick = {
                    showOverlayShortcut = false
                    toggleMenuClick()
                },
                shape = RoundedCornerShape(18.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f),
                tonalElevation = 6.dp,
                shadowElevation = 10.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.22f))
            ) {
                Box(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Menu,
                        contentDescription = stringResource(R.string.emulation_settings),
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(20.dp)
                    )
                }
            }
        }

        // Toast notifications
        AnimatedVisibility(
            visible = uiState.toastMessage != null,
            enter = fadeIn(tween(150)) + scaleIn(initialScale = 0.85f, animationSpec = tween(150)),
            exit = fadeOut(tween(150)) + scaleOut(targetScale = 0.85f, animationSpec = tween(150)),
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 48.dp, start = overlayHorizontalSafeInset, end = overlayHorizontalSafeInset)
        ) {
            val message = when (uiState.toastMessage) {
                "saved" -> stringResource(R.string.emulation_saved)
                "loaded" -> stringResource(R.string.emulation_loaded)
                "load_failed" -> stringResource(R.string.emulation_load_failed)
                "hardcore_blocked" -> stringResource(R.string.emulation_hardcore_action_blocked)
                "bios_missing" -> stringResource(R.string.emulation_bios_missing)
                "launch_failed" -> stringResource(R.string.emulation_launch_failed)
                "launch_path_error" -> stringResource(R.string.emulation_launch_path_error)
                else -> ""
            }
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(14.dp))
                    .background(
                        Brush.horizontalGradient(
                            colors = listOf(
                                GradientStart.copy(alpha = 0.9f),
                                GradientEnd.copy(alpha = 0.9f)
                            )
                        )
                    )
                    .padding(horizontal = 24.dp, vertical = 12.dp)
            ) {
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = Color.White
                )
            }
        }

        val raNotificationAlignment = if (
            uiState.showFps &&
            uiState.fpsOverlayCorner == AppPreferences.FPS_OVERLAY_CORNER_TOP_LEFT
        ) {
            Alignment.TopEnd
        } else {
            Alignment.TopStart
        }
        AnimatedVisibility(
            visible = retroAchievementsNotification != null && !uiState.showMenu && !showControlsEditor,
            enter = fadeIn(tween(180)) + scaleIn(initialScale = 0.92f, animationSpec = tween(180)),
            exit = fadeOut(tween(160)) + scaleOut(targetScale = 0.92f, animationSpec = tween(160)),
            modifier = Modifier
                .align(raNotificationAlignment)
                .padding(
                    start = if (raNotificationAlignment == Alignment.TopStart) overlayHorizontalSafeInset + 12.dp else 0.dp,
                    top = overlayTopSafeInset + 12.dp,
                    end = if (raNotificationAlignment == Alignment.TopEnd) overlayHorizontalSafeInset + 12.dp else 0.dp
                )
                .zIndex(35f)
        ) {
            retroAchievementsNotification?.let { notification ->
                RetroAchievementsNotificationToast(
                    notification = notification,
                    isRightAligned = raNotificationAlignment == Alignment.TopEnd,
                    onDismiss = { RetroAchievementsLiveStateManager.dismissNotification(notification.id) }
                )
            }
        }

        // Full screen loading overlay specifically for loading save state
        AnimatedVisibility(
            visible = (uiState.isActionInProgress && uiState.actionLabel == "loading") || uiState.statusMessage == "status_loading_state",
            enter = fadeIn(tween(200)),
            exit = fadeOut(tween(200)),
            modifier = Modifier.fillMaxSize()
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.85f))
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                        onClick = {} 
                    ),
                contentAlignment = Alignment.Center
            ) {
                Column(
                    modifier = Modifier
                        .clip(RoundedCornerShape(24.dp))
                        .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.95f))
                        .padding(horizontal = 32.dp, vertical = 24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    androidx.compose.material3.CircularProgressIndicator(
                        modifier = Modifier.size(48.dp),
                        color = MaterialTheme.colorScheme.primary,
                        strokeWidth = 4.dp
                    )
                    Text(
                        text = stringResource(R.string.emulation_status_loading_state),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
        }

        AnimatedVisibility(
            visible = uiState.statusMessage != null && !uiState.isActionInProgress && uiState.statusMessage != "status_loading_state",
            enter = fadeIn(tween(250)) + scaleIn(initialScale = 0.9f, animationSpec = tween(250)),
            exit = fadeOut(tween(200)) + scaleOut(targetScale = 0.9f, animationSpec = tween(200)),
            modifier = Modifier
                .align(Alignment.Center)
        ) {
            val statusText = when (uiState.statusMessage) {
                "status_preparing" -> stringResource(R.string.emulation_status_preparing)
                "status_checking_bios" -> stringResource(R.string.emulation_status_checking_bios)
                "status_loading_game" -> stringResource(R.string.emulation_status_loading_game)
                "status_starting_core" -> stringResource(R.string.emulation_status_starting_core)
                "status_waiting_vm" -> stringResource(R.string.emulation_status_waiting_vm)
                "status_running" -> stringResource(R.string.emulation_status_running)
                "status_applying_config" -> stringResource(R.string.emulation_status_applying_config)
                "status_saving" -> stringResource(R.string.emulation_status_saving)
                "status_loading_state" -> stringResource(R.string.emulation_status_loading_state)
                else -> ""
            }
            val isRunningStatus = uiState.statusMessage == "status_running"
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(18.dp))
                    .background(
                        Brush.horizontalGradient(
                            colors = if (isRunningStatus) listOf(
                                Color(0xFF50D9A0).copy(alpha = 0.85f),
                                Color(0xFF3ABB88).copy(alpha = 0.85f)
                            ) else listOf(
                                Color.Black.copy(alpha = 0.7f),
                                Color.Black.copy(alpha = 0.55f)
                            )
                        )
                    )
                    .border(
                        width = 1.dp,
                        color = if (isRunningStatus) Color(0xFF50D9A0).copy(alpha = 0.4f)
                                else Color.White.copy(alpha = 0.12f),
                        shape = RoundedCornerShape(18.dp)
                    )
                    .padding(horizontal = 28.dp, vertical = 14.dp),
                contentAlignment = Alignment.Center
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    if (!isRunningStatus) {
                        androidx.compose.material3.CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            color = Color.White.copy(alpha = 0.85f),
                            strokeWidth = 2.dp
                        )
                    }
                    Text(
                        text = statusText,
                        style = MaterialTheme.typography.bodyMedium.copy(
                            fontWeight = FontWeight.SemiBold,
                            letterSpacing = 0.3.sp
                        ),
                        color = Color.White
                    )
                }
            }
        }

        val showPerformanceHud = uiState.showFps &&
            !uiState.showMenu &&
            !showControlsEditor &&
            (uiState.fpsOverlayMode == FPS_OVERLAY_MODE_SIMPLE || uiState.performanceOverlayText.isNotBlank())
        AnimatedVisibility(
            visible = showPerformanceHud,
            enter = fadeIn(tween(160)),
            exit = fadeOut(tween(160)),
            modifier = Modifier
                .align(uiState.fpsOverlayCorner.toOverlayAlignment())
                .padding(
                    start = if (uiState.fpsOverlayCorner.isRightOverlayCorner()) 0.dp else overlayHorizontalSafeInset + 12.dp,
                    top = if (uiState.fpsOverlayCorner.isTopOverlayCorner()) overlayTopSafeInset + 12.dp else 0.dp,
                    end = if (uiState.fpsOverlayCorner.isRightOverlayCorner()) overlayHorizontalSafeInset + 12.dp else 0.dp,
                    bottom = if (uiState.fpsOverlayCorner.isBottomOverlayCorner()) overlayBottomSafeInset + 12.dp else 0.dp
                )
                .zIndex(30f)
        ) {
            if (uiState.fpsOverlayMode == FPS_OVERLAY_MODE_SIMPLE) {
                SimpleFpsCounter(
                    fps = uiState.fps,
                    fontScale = uiState.fpsOverlayScale / 100f
                )
            } else {
                SystemPerformanceHud(
                    speedPercent = uiState.speedPercent,
                    text = uiState.performanceOverlayText,
                    fixedHeaderLine = uiState.performanceOverlayHeader,
                    isRightCorner = uiState.fpsOverlayCorner.isRightOverlayCorner(),
                    fontScale = uiState.fpsOverlayScale / 100f,
                    metricsMask = uiState.fpsOverlayMetrics
                )
            }
        }

        AnimatedVisibility(
            visible = uiState.transportMode != EmulationTransportMode.None && !uiState.showMenu && !showControlsEditor,
            enter = fadeIn(tween(120)) + scaleIn(initialScale = 0.94f, animationSpec = tween(120)),
            exit = fadeOut(tween(120)) + scaleOut(targetScale = 0.94f, animationSpec = tween(120)),
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = overlayTopSafeInset + 14.dp)
                .zIndex(32f)
        ) {
            TransportStatusOverlay(uiState.transportMode)
        }

        // On-screen controls
        if (shouldShowOverlay && !uiState.showMenu && !showControlsEditor) {
            val scaleFactor = uiState.overlayScale / 100f
            val alpha = uiState.overlayOpacity / 100f
            OnScreenControls(
                modifier = Modifier
                    .fillMaxSize()
                    .zIndex(20f)
                    .graphicsLayer(alpha = alpha),
                scaleFactor = scaleFactor,
                stickScaleFactor = uiState.stickScale / 100f,
                leftStickSensitivity = uiState.leftStickSensitivity / 100f,
                rightStickSensitivity = uiState.rightStickSensitivity / 100f,
                invertLeftStick = uiState.invertLeftStick,
                invertRightStick = uiState.invertRightStick,
                invertLeftStickHorizontal = uiState.invertLeftStickHorizontal,
                invertRightStickHorizontal = uiState.invertRightStickHorizontal,
                rightStickUpToR2 = uiState.gamepadRightStickUpToR2,
                rightStickDownToL2 = uiState.gamepadRightStickDownToL2,
                touchHaptics = uiState.touchHaptics,
                touchHapticsPreset = uiState.touchHapticsPreset,
                touchHapticsStrength = uiState.touchHapticsStrength,
                visualStyle = uiState.touchControlVisualStyle,
                pressEffect = uiState.touchControlPressEffect,
                dpadOffset = uiState.dpadOffset,
                lstickOffset = uiState.lstickOffset,
                rstickOffset = uiState.rstickOffset,
                actionOffset = uiState.actionOffset,
                lbtnOffset = uiState.lbtnOffset,
                rbtnOffset = uiState.rbtnOffset,
                centerOffset = uiState.centerOffset,
                controlLayouts = uiState.controlLayouts,
                racingMode = uiState.racingMode,
                onToggleLeftInputMode = viewModel::toggleLeftInputMode,
                onFastForwardHoldChange = viewModel::setFastForwardHeld,
                onPadInput = { keyCode, range, pressed ->
                    viewModel.onPadInput(overlayPadIndex, keyCode, range, pressed)
                }
            )
        }
        }

        if (showControlsEditor) {
            ControlsEditorScreen(
                state = uiState.toControlsEditorState(),
                onBackClick = { showControlsEditor = false },
                manageActivityOrientation = false,
                overlayHorizontalSafeInset = overlayHorizontalSafeInset,
                overlayTopSafeInset = overlayTopSafeInset,
                overlayBottomSafeInset = overlayBottomSafeInset,
                onUpdateControlOffset = viewModel::updateTouchControlOffset,
                onUpdateControlOffsets = viewModel::updateTouchControlOffsets,
                onUpdateControlScale = viewModel::updateTouchControlScale,
                onUpdateControlWidthScale = viewModel::updateTouchControlWidthScale,
                onUpdateControlOpacity = viewModel::updateTouchControlOpacity,
                onToggleLeftInputMode = viewModel::toggleLeftInputMode,
                onSetControlVisible = viewModel::setTouchControlVisible,
                onSetStickSurfaceMode = viewModel::setTouchStickSurfaceMode,
                onResetLayout = viewModel::resetTouchControlsLayout
            )
        }

        // Sidebar Menu
        AnimatedVisibility(
            visible = uiState.showMenu && !showControlsEditor,
            enter = androidx.compose.animation.slideInHorizontally(initialOffsetX = { fullWidth -> fullWidth }) + fadeIn(tween(300)),
            exit = androidx.compose.animation.slideOutHorizontally(targetOffsetX = { fullWidth -> fullWidth }) + fadeOut(tween(250)),
            modifier = Modifier.fillMaxHeight()
        ) {
            Box(
                modifier = Modifier.fillMaxSize()
            ) {
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null,
                            onClick = toggleMenuClick
                        )
                )

                EmulationSidebarMenu(
                    uiState = uiState,
                    gamepadConnected = gamepadConnected,
                    currentGamePath = gamePath,
                    retroState = retroAchievementsState,
                    globalDefaults = globalDefaults,
                    overlayDefaults = overlayDefaults,
                    onClose = toggleMenuClick,
                    onPauseToggle = togglePauseClick,
                    onQuickSave = requestQuickSaveClick,
                    onQuickLoad = requestQuickLoadClick,
                    onLoadAutoSave = requestAutoSaveLoadClick,
                    onSetAutoSaveEnabled = { viewModel.setAutoSaveEnabled(it) },
                    onSetAutoSaveIntervalMinutes = { viewModel.setAutoSaveIntervalMinutes(it) },
                    onSetAutoSaveOnExit = { viewModel.setAutoSaveOnExit(it) },
                    onSetAutoLoadOnStart = { viewModel.setAutoLoadOnStart(it) },
                    onSaveGameSettingsProfile = { viewModel.saveCurrentGameSettingsProfile() },
                    onResetGameSettingsProfile = { viewModel.resetCurrentGameSettingsProfile() },
                    onNextSlot = { viewModel.setSlot(uiState.currentSlot + 1) },
                    onPrevSlot = { viewModel.setSlot(uiState.currentSlot - 1) },
                    onToggleFps = { viewModel.toggleFpsVisibility() },
                    onSetFpsOverlayMode = { viewModel.setFpsOverlayMode(it) },
                    onSetFpsOverlayCorner = { viewModel.setFpsOverlayCorner(it) },
                    onSetFpsOverlayScale = { viewModel.setFpsOverlayScale(it) },
                    onSetFpsOverlayMetrics = { viewModel.setFpsOverlayMetrics(it) },
                    onSetOverlayScale = { viewModel.setOverlayScale(it) },
                    onSetOverlayOpacity = { viewModel.setOverlayOpacity(it) },
                    onSetHideOverlayOnGamepad = { viewModel.setHideOverlayOnGamepad(it) },
                    onSetCompactControls = { viewModel.setCompactControls(it) },
                    onSetKeepScreenOn = { viewModel.setKeepScreenOn(it) },
                    onSetStickScale = { viewModel.setStickScale(it) },
                    onSetLeftStickSensitivity = { viewModel.setLeftStickSensitivity(it) },
                    onSetRightStickSensitivity = { viewModel.setRightStickSensitivity(it) },
                    onSetInvertLeftStick = { viewModel.setInvertLeftStick(it) },
                    onSetInvertRightStick = { viewModel.setInvertRightStick(it) },
                    onSetInvertLeftStickHorizontal = { viewModel.setInvertLeftStickHorizontal(it) },
                    onSetInvertRightStickHorizontal = { viewModel.setInvertRightStickHorizontal(it) },
                    onSetGamepadStickDeadzone = { viewModel.setGamepadStickDeadzone(it) },
                    onSetGamepadLeftStickSensitivity = { viewModel.setGamepadLeftStickSensitivity(it) },
                    onSetGamepadRightStickSensitivity = { viewModel.setGamepadRightStickSensitivity(it) },
                    onSetGamepadRightStickUpToR2 = { viewModel.setGamepadRightStickUpToR2(it) },
                    onSetGamepadRightStickDownToL2 = { viewModel.setGamepadRightStickDownToL2(it) },
                    onToggleControls = toggleControlsClick,
                    onOpenControlsEditor = { showControlsEditor = true },
                    onOpenGamepadMapping = { showGamepadMappingDialog = true },
                    onSetRenderer = { viewModel.setRenderer(it) },
                    onSetUpscale = { viewModel.setUpscale(it) },
                    onSetAspectRatio = { viewModel.setAspectRatio(it) },
                    onSetMtvu = { viewModel.setMtvu(it) },
                    onSetThreadPinning = { viewModel.setThreadPinning(it) },
                    onSetFastCdvd = { viewModel.setFastCdvd(it) },
                    onSetEnableCheats = { viewModel.setEnableCheats(it) },
                    onOpenCheats = { showCheatsDialog = true },
                    onSetHwDownloadMode = { viewModel.setHwDownloadMode(it) },
                    onSetEeCycleRate = { viewModel.setEeCycleRate(it) },
                    onSetEeCycleSkip = { viewModel.setEeCycleSkip(it) },
                    onSetFrameSkip = { viewModel.setFrameSkip(it) },
                    onSetSkipDuplicateFrames = { viewModel.setSkipDuplicateFrames(it) },
                    onSetFrameLimitEnabled = { viewModel.setFrameLimitEnabled(it) },
                    onSetTargetFps = { viewModel.setTargetFps(it) },
                    onSetTextureFiltering = { viewModel.setTextureFiltering(it) },
                    onSetTrilinearFiltering = { viewModel.setTrilinearFiltering(it) },
                    onSetBlendingAccuracy = { viewModel.setBlendingAccuracy(it) },
                    onSetTexturePreloading = { viewModel.setTexturePreloading(it) },
                    onSetEnableFxaa = { viewModel.setEnableFxaa(it) },
                    onSetCasMode = { viewModel.setCasMode(it) },
                    onSetSgsrMode = { viewModel.setSgsrMode(it) },
                    onSetCasSharpness = { viewModel.setCasSharpness(it) },
                    onSetTvShader = { viewModel.setTvShader(it) },
                    onSetShadeBoostBrightness = { viewModel.setShadeBoostBrightness(it) },
                    onSetShadeBoostContrast = { viewModel.setShadeBoostContrast(it) },
                    onSetShadeBoostSaturation = { viewModel.setShadeBoostSaturation(it) },
                    onSetShadeBoostGamma = { viewModel.setShadeBoostGamma(it) },
                    onSetEnableWidescreenPatches = { viewModel.setEnableWidescreenPatches(it) },
                    onSetEnableNoInterlacingPatches = { viewModel.setEnableNoInterlacingPatches(it) },
                    onSetAntiBlur = { viewModel.setAntiBlur(it) },
                    onSetAnisotropicFiltering = { viewModel.setAnisotropicFiltering(it) },
                    onSetEnableHwMipmapping = { viewModel.setEnableHwMipmapping(it) },
                    onSetCpuSpriteRenderSize = { viewModel.setCpuSpriteRenderSize(it) },
                    onSetCpuSpriteRenderLevel = { viewModel.setCpuSpriteRenderLevel(it) },
                    onSetSoftwareClutRender = { viewModel.setSoftwareClutRender(it) },
                    onSetGpuTargetClutMode = { viewModel.setGpuTargetClutMode(it) },
                    onSetSkipDrawStart = { viewModel.setSkipDrawStart(it) },
                    onSetSkipDrawEnd = { viewModel.setSkipDrawEnd(it) },
                    onSetAutoFlushHardware = { viewModel.setAutoFlushHardware(it) },
                    onSetCpuFramebufferConversion = { viewModel.setCpuFramebufferConversion(it) },
                    onSetDisableDepthConversion = { viewModel.setDisableDepthConversion(it) },
                    onSetDisableSafeFeatures = { viewModel.setDisableSafeFeatures(it) },
                    onSetDisableRenderFixes = { viewModel.setDisableRenderFixes(it) },
                    onSetPreloadFrameData = { viewModel.setPreloadFrameData(it) },
                    onSetDisablePartialInvalidation = { viewModel.setDisablePartialInvalidation(it) },
                    onSetTextureInsideRt = { viewModel.setTextureInsideRt(it) },
                    onSetReadTargetsOnClose = { viewModel.setReadTargetsOnClose(it) },
                    onSetEstimateTextureRegion = { viewModel.setEstimateTextureRegion(it) },
                    onSetGpuPaletteConversion = { viewModel.setGpuPaletteConversion(it) },
                    onSetHalfPixelOffset = { viewModel.setHalfPixelOffset(it) },
                    onSetNativeScaling = { viewModel.setNativeScaling(it) },
                    onSetRoundSprite = { viewModel.setRoundSprite(it) },
                    onSetBilinearUpscale = { viewModel.setBilinearUpscale(it) },
                    onSetTextureOffsetX = { viewModel.setTextureOffsetX(it) },
                    onSetTextureOffsetY = { viewModel.setTextureOffsetY(it) },
                    onSetAlignSprite = { viewModel.setAlignSprite(it) },
                    onSetMergeSprite = { viewModel.setMergeSprite(it) },
                    onSetForceEvenSpritePosition = { viewModel.setForceEvenSpritePosition(it) },
                    onSetNativePaletteDraw = { viewModel.setNativePaletteDraw(it) },
                    onToggleJitProfiler = { viewModel.toggleJitProfiler() },
                    onToggleHangTrace = { viewModel.toggleHangTrace() },
                    onExit = requestExitClick,
                    modifier = Modifier
                        .fillMaxHeight()
                        .fillMaxWidth()
                )
            }
        }
    }

    if (showExitDialog) {
        AlertDialog(
            onDismissRequest = dismissExitClick,
            containerColor = MaterialTheme.colorScheme.surface,
            shape = RoundedCornerShape(24.dp),
            title = {
                Text(
                    stringResource(R.string.emulation_exit_confirm),
                    style = MaterialTheme.typography.titleLarge
                )
            },
            text = {
                Text(
                    stringResource(R.string.emulation_exit_confirm_desc),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            },
            confirmButton = {
                TextButton(onClick = confirmExitClick) {
                    Text(
                        stringResource(R.string.yes),
                        color = MaterialTheme.colorScheme.error
                    )
                }
            },
            dismissButton = {
                TextButton(onClick = dismissExitClick) {
                    Text(stringResource(R.string.no))
                }
            }
        )
    }

    if (showQuickSaveDialog) {
        AlertDialog(
            onDismissRequest = dismissQuickSaveClick,
            containerColor = MaterialTheme.colorScheme.surface,
            shape = RoundedCornerShape(24.dp),
            title = {
                Text(
                    stringResource(R.string.emulation_quick_save_confirm_title),
                    style = MaterialTheme.typography.titleLarge
                )
            },
            text = {
                Text(
                    stringResource(R.string.emulation_quick_save_confirm_desc, uiState.currentSlot),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            },
            confirmButton = {
                TextButton(onClick = confirmQuickSaveClick) {
                    Text(stringResource(R.string.emulation_quick_save))
                }
            },
            dismissButton = {
                TextButton(onClick = dismissQuickSaveClick) {
                    Text(stringResource(R.string.no))
                }
            }
        )
    }

    if (showQuickLoadDialog) {
        AlertDialog(
            onDismissRequest = dismissQuickLoadClick,
            containerColor = MaterialTheme.colorScheme.surface,
            shape = RoundedCornerShape(24.dp),
            title = {
                Text(
                    stringResource(R.string.emulation_quick_load_confirm_title),
                    style = MaterialTheme.typography.titleLarge
                )
            },
            text = {
                Text(
                    stringResource(R.string.emulation_quick_load_confirm_desc, uiState.currentSlot),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            },
            confirmButton = {
                TextButton(onClick = confirmQuickLoadClick) {
                    Text(stringResource(R.string.emulation_quick_load))
                }
            },
            dismissButton = {
                TextButton(onClick = dismissQuickLoadClick) {
                    Text(stringResource(R.string.no))
                }
            }
        )
    }

    if (showAutoSaveLoadDialog) {
        AlertDialog(
            onDismissRequest = dismissAutoSaveLoadClick,
            containerColor = MaterialTheme.colorScheme.surface,
            shape = RoundedCornerShape(24.dp),
            title = {
                Text(
                    stringResource(R.string.emulation_quick_load_confirm_title),
                    style = MaterialTheme.typography.titleLarge
                )
            },
            text = {
                Text(
                    stringResource(R.string.emulation_quick_load_confirm_desc, 0),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            },
            confirmButton = {
                TextButton(onClick = confirmAutoSaveLoadClick) {
                    Text(stringResource(R.string.emulation_quick_load))
                }
            },
            dismissButton = {
                TextButton(onClick = dismissAutoSaveLoadClick) {
                    Text(stringResource(R.string.no))
                }
            }
        )
    }

    if (showCheatsDialog) {
        AlertDialog(
            onDismissRequest = dismissCheatsDialog,
            title = { Text(stringResource(R.string.emulation_cheats_title)) },
            text = {
                if (uiState.availableCheats.isEmpty()) {
                    Text(stringResource(R.string.emulation_cheats_empty))
                } else {
                    Column(
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        uiState.availableCheats.forEach { cheat ->
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Text(
                                    text = cheat.title,
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurface,
                                    modifier = Modifier.weight(1f)
                                )
                                Switch(
                                    checked = cheat.enabled,
                                    onCheckedChange = { viewModel.setCheatEnabled(cheat.id, it) }
                                )
                            }
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = dismissCheatsDialogClick) {
                    Text(stringResource(android.R.string.ok))
                }
            }
        )
    }

    if (showGamepadMappingDialog) {
        Dialog(onDismissRequest = dismissGamepadMappingDialog) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 560.dp),
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 8.dp,
                shadowElevation = 16.dp
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    val connectedControllerName = GamepadManager.connectedControllerName(selectedGamepadPadIndex)
                    val selectedBindings = gamepadBindingsByPad[selectedGamepadPadIndex].orEmpty()
                    Column(
                        modifier = Modifier.padding(start = 18.dp, end = 18.dp, top = 18.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.settings_gamepad_mapping_title),
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            listOf(0, 1).forEach { padIndex ->
                                FilterChip(
                                    selected = selectedGamepadPadIndex == padIndex,
                                    onClick = { selectedGamepadPadIndex = padIndex },
                                    label = { Text(gamepadPlayerLabel(padIndex)) }
                                )
                            }
                        }
                        Text(
                            text = connectedControllerName?.let {
                                stringResource(
                                    R.string.settings_gamepad_mapping_player_connected,
                                    gamepadPlayerLabel(selectedGamepadPadIndex),
                                    it
                                )
                            } ?: stringResource(
                                R.string.settings_gamepad_mapping_player_disconnected,
                                gamepadPlayerLabel(selectedGamepadPadIndex)
                            ),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(max = 420.dp)
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = 18.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        gamepadActions.forEach { action ->
                            val assignedKeyCode = GamepadManager.resolveBindingForAction(
                                actionId = action.id,
                                customBindings = selectedBindings
                            )
                            val isCustomBinding = selectedBindings.containsKey(action.id)
                            EmulationGamepadBindingRow(
                                title = gamepadActionLabel(action.id),
                                value = assignedKeyCode?.let(GamepadManager::keyCodeLabel)
                                    ?: stringResource(R.string.settings_not_set),
                                autoLabel = if (isCustomBinding || action.defaultKeyCodes.isEmpty()) {
                                    null
                                } else {
                                    stringResource(R.string.settings_gamepad_mapping_auto_format)
                                },
                                onBindClick = {
                                    pendingGamepadPadIndex = selectedGamepadPadIndex
                                    pendingGamepadActionId = action.id
                                },
                                onClearClick = if (isCustomBinding) {
                                    {
                                        scope.launch {
                                            preferences.clearGamepadBinding(selectedGamepadPadIndex, action.id)
                                        }
                                    }
                                } else {
                                    null
                                }
                            )
                        }
                        Box(modifier = Modifier.padding(bottom = 4.dp)) {
                            MenuButton(
                                icon = Icons.Rounded.SettingsSuggest,
                                text = stringResource(R.string.settings_gamepad_mapping_reset_title),
                                onClick = {
                                    scope.launch {
                                        preferences.resetGamepadBindingsForPad(selectedGamepadPadIndex)
                                    }
                                },
                                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
                            )
                        }
                    }
                    HorizontalDivider(
                        modifier = Modifier.padding(horizontal = 18.dp),
                        thickness = 1.dp,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
                    )
                    Column(
                        modifier = Modifier.padding(start = 18.dp, end = 18.dp, bottom = 18.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            TextButton(onClick = dismissGamepadMappingDialogClick) {
                                Text(stringResource(android.R.string.ok))
                            }
                        }
                    }
                }
            }
        }
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
            title = { Text(stringResource(R.string.settings_gamepad_mapping_listening_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.settings_gamepad_mapping_listening_player_desc,
                        gamepadPlayerLabel(pendingGamepadPadIndex),
                        gamepadActionLabel(pendingGamepadActionId.orEmpty())
                    )
                )
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { pendingGamepadActionId = null }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }
}

private fun GameMenuTabId.toEmulationMenuTab(): EmulationMenuTab = when (this) {
    GameMenuTabId.SESSION -> EmulationMenuTab.Session
    GameMenuTabId.CONTROLS -> EmulationMenuTab.Controls
    GameMenuTabId.EMULATION -> EmulationMenuTab.Emulation
    GameMenuTabId.GRAPHICS -> EmulationMenuTab.Graphics
    GameMenuTabId.FIXES -> EmulationMenuTab.Fixes
    GameMenuTabId.ACHIEVEMENTS -> EmulationMenuTab.Achievements
}

@Composable
private fun TransportStatusOverlay(mode: EmulationTransportMode) {
    if (mode == EmulationTransportMode.None) return
    Surface(
        shape = RoundedCornerShape(8.dp),
        color = Color(0xDD10131A),
        tonalElevation = 8.dp,
        shadowElevation = 10.dp,
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.22f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                text = ">>",
                color = Color.White,
                style = MaterialTheme.typography.titleLarge.copy(
                    fontWeight = FontWeight.Black,
                    fontFamily = FontFamily.Monospace,
                    letterSpacing = 0.sp
                )
            )
            Text(
                text = stringResource(R.string.emulation_transport_fast_forward),
                color = Color.White.copy(alpha = 0.9f),
                style = MaterialTheme.typography.labelLarge.copy(
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 0.sp
                )
            )
        }
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun OnScreenControls(
    modifier: Modifier = Modifier,
    scaleFactor: Float,
    stickScaleFactor: Float = 1.0f,
    leftStickSensitivity: Float = 1.0f,
    rightStickSensitivity: Float = 1.0f,
    invertLeftStick: Boolean = false,
    invertRightStick: Boolean = false,
    invertLeftStickHorizontal: Boolean = false,
    invertRightStickHorizontal: Boolean = false,
    rightStickUpToR2: Boolean = false,
    rightStickDownToL2: Boolean = false,
    touchHaptics: Boolean = false,
    touchHapticsPreset: Int = AppPreferences.DEFAULT_TOUCH_HAPTICS_PRESET,
    touchHapticsStrength: Int = AppPreferences.DEFAULT_TOUCH_HAPTICS_STRENGTH,
    visualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    pressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    dpadOffset: Pair<Float, Float>,
    lstickOffset: Pair<Float, Float>,
    rstickOffset: Pair<Float, Float>,
    actionOffset: Pair<Float, Float>,
    lbtnOffset: Pair<Float, Float>,
    rbtnOffset: Pair<Float, Float>,
    centerOffset: Pair<Float, Float>,
    controlLayouts: Map<String, OverlayControlLayout>,
    racingMode: Boolean,
    onToggleLeftInputMode: () -> Unit,
    onFastForwardHoldChange: (Boolean) -> Unit,
    onPadInput: (Int, Int, Boolean) -> Unit
) {
    val density = LocalDensity.current
    val cutoutPadding = WindowInsets.displayCutout.asPaddingValues()
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues()
    val safeLeft = maxOf(cutoutPadding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr), navBarPadding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr))
    val safeRight = maxOf(cutoutPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr), navBarPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr))
    val safeTop = maxOf(cutoutPadding.calculateTopPadding(), navBarPadding.calculateTopPadding())
    val safeBottom = maxOf(cutoutPadding.calculateBottomPadding(), navBarPadding.calculateBottomPadding())
    val safeHorizontalInset = maxOf(safeLeft, safeRight)
    val context = LocalContext.current
    val hapticView = LocalView.current
    val currentOnPadInput by rememberUpdatedState(onPadInput)
    var touchL2Pressed by remember { mutableStateOf(false) }
    var touchR2Pressed by remember { mutableStateOf(false) }

    fun performTouchHaptic(phase: ButtonPhase) {
        if (touchHaptics) {
            AndroidTouchHaptics.playButton(
                context = context,
                view = hapticView,
                strengthPercent = touchHapticsStrength,
                preset = touchHapticsPreset,
                phase = phase
            )
        }
    }

    fun dispatchTouchAnalogDirection(key: Int, pressed: Boolean) {
        val range = if (pressed) 255 else 0
        currentOnPadInput(key, range, pressed)
    }

    fun dispatchTouchL2() {
        if (rightStickDownToL2) {
            currentOnPadInput(PadKey.L2, 0, false)
            dispatchTouchAnalogDirection(PadKey.RIGHT_STICK_DOWN, touchL2Pressed)
        } else {
            currentOnPadInput(PadKey.RIGHT_STICK_DOWN, 0, false)
            dispatchTouchAnalogDirection(PadKey.L2, touchL2Pressed)
        }
    }

    fun dispatchTouchR2() {
        if (rightStickUpToR2) {
            currentOnPadInput(PadKey.R2, 0, false)
            dispatchTouchAnalogDirection(PadKey.RIGHT_STICK_UP, touchR2Pressed)
        } else {
            currentOnPadInput(PadKey.RIGHT_STICK_UP, 0, false)
            dispatchTouchAnalogDirection(PadKey.R2, touchR2Pressed)
        }
    }

    LaunchedEffect(rightStickUpToR2) {
        currentOnPadInput(PadKey.R2, 0, false)
        currentOnPadInput(PadKey.RIGHT_STICK_UP, 0, false)
        dispatchTouchR2()
    }

    LaunchedEffect(rightStickDownToL2) {
        currentOnPadInput(PadKey.L2, 0, false)
        currentOnPadInput(PadKey.RIGHT_STICK_DOWN, 0, false)
        dispatchTouchL2()
    }

    fun buttonPressHandler(id: String): ((Boolean) -> Unit)? = when (id) {
        "l2" -> { pressed ->
            touchL2Pressed = pressed
            dispatchTouchL2()
        }
        "l1" -> { pressed -> onPadInput(PadKey.L1, 0, pressed) }
        "r2" -> { pressed ->
            touchR2Pressed = pressed
            dispatchTouchR2()
        }
        "r1" -> { pressed -> onPadInput(PadKey.R1, 0, pressed) }
        "dpad_up" -> { pressed -> onPadInput(PadKey.UP, 0, pressed) }
        "dpad_down" -> { pressed -> onPadInput(PadKey.DOWN, 0, pressed) }
        "dpad_left" -> { pressed -> onPadInput(PadKey.LEFT, 0, pressed) }
        "dpad_right" -> { pressed -> onPadInput(PadKey.RIGHT, 0, pressed) }
        "triangle" -> { pressed -> onPadInput(PadKey.TRIANGLE, 0, pressed) }
        "cross" -> { pressed -> onPadInput(PadKey.CROSS, 0, pressed) }
        "square" -> { pressed -> onPadInput(PadKey.SQUARE, 0, pressed) }
        "circle" -> { pressed -> onPadInput(PadKey.CIRCLE, 0, pressed) }
        "select" -> { pressed -> onPadInput(PadKey.SELECT, 0, pressed) }
        "start" -> { pressed -> onPadInput(PadKey.START, 0, pressed) }
        "pressure" -> { pressed -> onPadInput(PadKey.PRESSURE, 0, pressed) }
        "l3" -> { pressed -> onPadInput(PadKey.L3, 0, pressed) }
        "r3" -> { pressed -> onPadInput(PadKey.R3, 0, pressed) }
        else -> null
    }

    fun isRacingTapToHoldButton(id: String): Boolean = id in setOf(
        "l2", "l1", "r2", "r1",
        "triangle", "cross", "square", "circle"
    )

    BoxWithConstraints(modifier = modifier) {
        val layout = buildOverlayCanvasLayout(
            canvasWidth = maxWidth,
            canvasHeight = maxHeight,
            density = density,
            scaleFactor = scaleFactor,
            stickScaleFactor = stickScaleFactor,
            dpadOffset = dpadOffset,
            lstickOffset = lstickOffset,
            rstickOffset = rstickOffset,
            actionOffset = actionOffset,
            lbtnOffset = lbtnOffset,
            rbtnOffset = rbtnOffset,
            centerOffset = centerOffset,
            controlLayouts = controlLayouts,
            safeHorizontalInset = safeHorizontalInset,
            safeTopInset = safeTop,
            safeBottomInset = safeBottom
        )
        var extraDpadDirections by remember { mutableStateOf(emptySet<OverlayDpadDirection>()) }
        val currentExtraDpadDirections by rememberUpdatedState(extraDpadDirections)

        fun dpadKeyFor(direction: OverlayDpadDirection): Int = when (direction) {
            OverlayDpadDirection.Up -> PadKey.UP
            OverlayDpadDirection.Down -> PadKey.DOWN
            OverlayDpadDirection.Left -> PadKey.LEFT
            OverlayDpadDirection.Right -> PadKey.RIGHT
        }

        fun updateExtraDpadDirections(next: Set<OverlayDpadDirection>) {
            val released = extraDpadDirections - next
            val pressed = next - extraDpadDirections
            released.forEach { direction -> currentOnPadInput(dpadKeyFor(direction), 0, false) }
            if (pressed.isNotEmpty()) {
                performTouchHaptic(ButtonPhase.PRESS)
            } else if (released.isNotEmpty()) {
                performTouchHaptic(ButtonPhase.RELEASE)
            }
            pressed.forEach { direction -> currentOnPadInput(dpadKeyFor(direction), 0, true) }
            extraDpadDirections = next
        }

        DisposableEffect(Unit) {
            onDispose {
                currentExtraDpadDirections.forEach { direction ->
                    currentOnPadInput(dpadKeyFor(direction), 0, false)
                }
            }
        }

        fun runtimeSpecs(specs: List<com.sbro.emucorex.ui.common.OverlayCanvasButtonSpec>): List<TouchButtonSpec> {
            return specs.filter { it.visible }.map { spec ->
                TouchButtonSpec(
                    id = spec.id,
                    drawableRes = spec.drawableRes,
                    width = spec.width,
                    height = spec.height,
                    x = spec.x,
                    y = spec.y,
                    shape = spec.shape,
                    opacity = spec.opacity / 100f,
                    onPressChange = buttonPressHandler(spec.id),
                    onClick = if (spec.id == "left_input_toggle") onToggleLeftInputMode else null,
                    tapToHold = racingMode && isRacingTapToHoldButton(spec.id),
                    longPressDelayMs = if (spec.id == "start") TRANSPORT_HOLD_DELAY_MS else 0L,
                    onLongPressChange = if (spec.id == "start") onFastForwardHoldChange else null
                )
            }
        }

        fun stickIsSurfaceOnly(stick: com.sbro.emucorex.ui.common.OverlayCanvasStickSpec): Boolean {
            return controlLayouts[stick.id]?.surfaceOnly == true
        }

        fun stickPanelWidth(stick: com.sbro.emucorex.ui.common.OverlayCanvasStickSpec): Dp {
            return if (stickIsSurfaceOnly(stick)) stick.size * (stick.widthScale / 100f) else stick.size
        }

        fun stickPanelX(stick: com.sbro.emucorex.ui.common.OverlayCanvasStickSpec): Dp {
            val width = stickPanelWidth(stick)
            return if (stickIsSurfaceOnly(stick)) stick.x - ((width - stick.size) / 2f) else stick.x
        }

        val rightStickTriggerVisualY = (
            (if (rightStickDownToL2 && touchL2Pressed) 1f else 0f) -
                (if (rightStickUpToR2 && touchR2Pressed) 1f else 0f)
            ).coerceIn(-1f, 1f)

        val leftShoulderSpecs = runtimeSpecs(layout.leftShoulders)
        if (leftShoulderSpecs.isNotEmpty()) {
            TouchButtonGroup(specs = leftShoulderSpecs, visualStyle = visualStyle, pressEffect = pressEffect, onTouchHaptic = ::performTouchHaptic)
        }

        val rightShoulderSpecs = runtimeSpecs(layout.rightShoulders)
        if (rightShoulderSpecs.isNotEmpty()) {
            TouchButtonGroup(specs = rightShoulderSpecs, visualStyle = visualStyle, pressEffect = pressEffect, onTouchHaptic = ::performTouchHaptic)
        }

        val dpadSpecs = runtimeSpecs(layout.dpadButtons)
        if (dpadSpecs.isNotEmpty()) {
            TouchButtonGroup(specs = dpadSpecs, visualStyle = visualStyle, pressEffect = pressEffect, onTouchHaptic = ::performTouchHaptic)
        }

        layout.dpadCluster?.takeIf { it.visible }?.let { cluster ->
            VectorDpadCluster(
                size = cluster.size,
                alpha = cluster.opacity / 100f,
                visualStyle = visualStyle,
                pressEffect = pressEffect,
                onDirectionsChange = ::updateExtraDpadDirections,
                modifier = Modifier.offset {
                    IntOffset(cluster.x.roundToPx(), cluster.y.roundToPx())
                }
            )
        }

        layout.leftStick?.takeIf { it.visible }?.let { stick ->
            val panelWidth = stickPanelWidth(stick)
            VectorAnalogStick(
                analogSize = stick.size,
                analogWidth = panelWidth,
                analogHeight = stick.size,
                alpha = stick.opacity / 100f,
                surfaceOnly = stickIsSurfaceOnly(stick),
                visualStyle = visualStyle,
                pressEffect = pressEffect,
                onValueChange = { x, y ->
                    updateAnalogStick(
                        x = if (invertLeftStickHorizontal) -x else x,
                        y = if (invertLeftStick) -y else y,
                        sensitivity = leftStickSensitivity,
                        upKey = PadKey.LEFT_STICK_UP,
                        rightKey = PadKey.LEFT_STICK_RIGHT,
                        downKey = PadKey.LEFT_STICK_DOWN,
                        leftKey = PadKey.LEFT_STICK_LEFT,
                        onPadInput = onPadInput
                    )
                },
                modifier = Modifier.offset {
                    IntOffset(stickPanelX(stick).roundToPx(), stick.y.roundToPx())
                }
            )
        }

        val actionSpecs = runtimeSpecs(layout.actionButtons)
        if (actionSpecs.isNotEmpty()) {
            TouchButtonGroup(specs = actionSpecs, visualStyle = visualStyle, pressEffect = pressEffect, onTouchHaptic = ::performTouchHaptic)
        }

        layout.rightStick?.takeIf { it.visible }?.let { stick ->
            val panelWidth = stickPanelWidth(stick)
            VectorAnalogStick(
                analogSize = stick.size,
                analogWidth = panelWidth,
                analogHeight = stick.size,
                alpha = stick.opacity / 100f,
                surfaceOnly = stickIsSurfaceOnly(stick),
                visualStyle = visualStyle,
                pressEffect = pressEffect,
                visualY = rightStickTriggerVisualY,
                onValueChange = { x, y ->
                    updateRightAnalogStick(
                        x = if (invertRightStickHorizontal) -x else x,
                        y = if (invertRightStick) -y else y,
                        sensitivity = rightStickSensitivity,
                        onPadInput = onPadInput
                    )
                },
                modifier = Modifier.offset {
                    IntOffset(stickPanelX(stick).roundToPx(), stick.y.roundToPx())
                }
            )
        }

        val centerSpecs = runtimeSpecs(layout.centerButtons)
        if (centerSpecs.isNotEmpty()) {
            TouchButtonGroup(specs = centerSpecs, visualStyle = visualStyle, pressEffect = pressEffect, onTouchHaptic = ::performTouchHaptic)
        }
    }
}

@Composable
private fun RetroAchievementsNotificationToast(
    notification: com.sbro.emucorex.core.utils.RetroAchievementsNotification,
    isRightAligned: Boolean,
    onDismiss: () -> Unit
) {
    val accent = when (notification.kind) {
        "unlock", "mastery" -> Color(0xFFFFD35A)
        "leaderboard" -> Color(0xFF86D7FF)
        "hardcore" -> Color(0xFFFF7A7A)
        "error" -> Color(0xFFFF6B83)
        else -> Color(0xFF7DDFA8)
    }

    Surface(
        onClick = onDismiss,
        shape = RoundedCornerShape(16.dp),
        color = Color(0xE614151A),
        tonalElevation = 6.dp,
        shadowElevation = 10.dp,
        border = BorderStroke(1.dp, accent.copy(alpha = 0.42f)),
        modifier = Modifier.widthIn(min = 260.dp, max = 420.dp)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            BitmapPathImage(
                imagePath = notification.imagePath,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier
                    .size(42.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(accent.copy(alpha = 0.16f)),
                fallback = {
                    Icon(
                        imageVector = Icons.Rounded.Star,
                        contentDescription = null,
                        tint = accent,
                        modifier = Modifier.size(24.dp)
                    )
                }
            )

            Column(
                modifier = Modifier.weight(1f),
                horizontalAlignment = if (isRightAligned) Alignment.End else Alignment.Start,
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = notification.title,
                    color = Color.White,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = if (isRightAligned) TextAlign.End else TextAlign.Start,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Bold)
                )
                if (notification.message.isNotBlank()) {
                    Text(
                        text = notification.message,
                        color = Color.White.copy(alpha = 0.78f),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                        textAlign = if (isRightAligned) TextAlign.End else TextAlign.Start,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
}

@Composable
private fun TouchButtonGroup(
    specs: List<TouchButtonSpec>,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    onTouchHaptic: (ButtonPhase) -> Unit,
    modifier: Modifier = Modifier
) {
    val density = LocalDensity.current
    val coroutineScope = rememberCoroutineScope()
    val activeTargets = remember { mutableStateMapOf<Int, String>() }
    val downTargets = remember { mutableMapOf<Int, String?>() }
    val latchedTargets = remember { mutableStateMapOf<String, Boolean>() }
    val longPressJobs = remember { mutableMapOf<Int, Job>() }
    val longPressActiveTargets = remember { mutableStateMapOf<Int, String>() }
    val specById = specs.associateBy { it.id }
    val currentSpecById by rememberUpdatedState(specById)
    val layoutKey = specs.map { spec ->
        TouchButtonLayoutKey(
            id = spec.id,
            width = spec.width,
            height = spec.height,
            x = spec.x,
            y = spec.y,
            tapToHold = spec.tapToHold,
            hasLongPressAction = spec.onLongPressChange != null && spec.longPressDelayMs > 0L
        )
    }
    val bounds = remember(layoutKey, density) {
        with(density) {
            val rects = specs.associate { spec ->
                spec.id to Rect(
                    left = spec.x.toPx(),
                    top = spec.y.toPx(),
                    right = spec.x.toPx() + spec.width.toPx(),
                    bottom = spec.y.toPx() + spec.height.toPx()
                )
            }
            val left = rects.values.minOfOrNull { it.left } ?: 0f
            val top = rects.values.minOfOrNull { it.top } ?: 0f
            val right = rects.values.maxOfOrNull { it.right } ?: 0f
            val bottom = rects.values.maxOfOrNull { it.bottom } ?: 0f
            Triple(rects, Rect(left = left, top = top, right = right, bottom = bottom), Unit)
        }
    }
    val rects = bounds.first
    val groupRect = bounds.second
    val groupWidth = with(density) { (groupRect.right - groupRect.left).coerceAtLeast(0f).toDp() }
    val groupHeight = with(density) { (groupRect.bottom - groupRect.top).coerceAtLeast(0f).toDp() }

    fun hitTarget(x: Float, y: Float): String? =
        specs.lastOrNull { spec -> rects.getValue(spec.id).contains(Offset(x, y)) }?.id

    fun TouchButtonSpec.hasLongPressAction(): Boolean {
        return onLongPressChange != null && longPressDelayMs > 0L
    }

    fun TouchButtonSpec.hasTapToHoldAction(): Boolean {
        return tapToHold && onPressChange != null
    }

    fun toggleLatchedTarget(spec: TouchButtonSpec) {
        val press = spec.onPressChange ?: return
        if (latchedTargets.remove(spec.id) == true) {
            press(false)
        } else {
            latchedTargets[spec.id] = true
            press(true)
        }
    }

    fun cancelLongPress(pointerId: Int) {
        longPressJobs.remove(pointerId)?.cancel()
        val activeTarget = longPressActiveTargets.remove(pointerId)
        if (activeTarget != null && !longPressActiveTargets.containsValue(activeTarget)) {
            specById[activeTarget]?.onLongPressChange?.invoke(false)
        }
    }

    fun startLongPress(pointerId: Int, targetId: String) {
        val spec = specById[targetId] ?: return
        if (!spec.hasLongPressAction()) return
        longPressJobs.remove(pointerId)?.cancel()
        longPressJobs[pointerId] = coroutineScope.launch {
            delay(spec.longPressDelayMs.milliseconds)
            if (activeTargets[pointerId] != targetId) return@launch
            val wasAlreadyActive = longPressActiveTargets.containsValue(targetId)
            longPressActiveTargets[pointerId] = targetId
            if (!wasAlreadyActive) {
                spec.onLongPressChange?.invoke(true)
            }
        }
    }

    fun sendShortTap(spec: TouchButtonSpec) {
        val press = spec.onPressChange ?: return
        coroutineScope.launch {
            press(true)
            delay(70.milliseconds)
            press(false)
        }
    }

    fun updatePointerTarget(pointerId: Int, newTarget: String?, emitReleaseHaptic: Boolean = true) {
        val oldTarget = activeTargets[pointerId]
        if (oldTarget == newTarget) return

        if (oldTarget != null) {
            activeTargets.remove(pointerId)
            val oldSpec = specById[oldTarget]
            if (oldSpec?.hasLongPressAction() == true) {
                cancelLongPress(pointerId)
            } else if (oldSpec?.hasTapToHoldAction() == true) {
                // Tap-to-hold buttons are toggled on release, not while the pointer is moving.
            } else if (!activeTargets.containsValue(oldTarget)) {
                oldSpec?.onPressChange?.invoke(false)
            }
            if (newTarget == null && emitReleaseHaptic && !activeTargets.containsValue(oldTarget)) {
                onTouchHaptic(ButtonPhase.RELEASE)
            }
        }

        if (newTarget != null) {
            val alreadyActive = activeTargets.containsValue(newTarget)
            activeTargets[pointerId] = newTarget
            val newSpec = specById[newTarget]
            if (!alreadyActive) {
                onTouchHaptic(ButtonPhase.PRESS)
            }
            if (newSpec?.hasLongPressAction() == true) {
                startLongPress(pointerId, newTarget)
            } else if (newSpec?.hasTapToHoldAction() == true) {
                // Tap-to-hold buttons are toggled on release, not on pointer entry.
            } else if (!alreadyActive) {
                newSpec?.onPressChange?.invoke(true)
            }
        }
    }

    DisposableEffect(layoutKey) {
        onDispose {
            longPressJobs.values.forEach { it.cancel() }
            longPressActiveTargets.values.toSet().forEach { targetId ->
                currentSpecById[targetId]?.onLongPressChange?.invoke(false)
            }
            latchedTargets.keys.toList().forEach { targetId ->
                currentSpecById[targetId]?.onPressChange?.invoke(false)
            }
            activeTargets.values.toSet().forEach { targetId ->
                val spec = currentSpecById[targetId]
                if (spec?.hasLongPressAction() != true && spec?.hasTapToHoldAction() != true) {
                    spec?.onPressChange?.invoke(false)
                }
            }
            activeTargets.clear()
            downTargets.clear()
            latchedTargets.clear()
            longPressJobs.clear()
            longPressActiveTargets.clear()
        }
    }

    Box(
        modifier = modifier
            .offset {
                IntOffset(groupRect.left.roundToInt(), groupRect.top.roundToInt())
            }
            .size(groupWidth, groupHeight)
            .pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        val index = event.actionIndex
                        val pointerId = event.getPointerId(index)
                        val target = hitTarget(
                            event.getX(index) + groupRect.left,
                            event.getY(index) + groupRect.top
                        )
                        downTargets[pointerId] = target
                        updatePointerTarget(pointerId, target)
                        target != null
                    }

                    MotionEvent.ACTION_MOVE -> {
                        var handled = false
                        for (index in 0 until event.pointerCount) {
                            val pointerId = event.getPointerId(index)
                            val currentTarget = activeTargets[pointerId]
                            val target = hitTarget(
                                event.getX(index) + groupRect.left,
                                event.getY(index) + groupRect.top
                            )
                            updatePointerTarget(pointerId, target)
                            handled = handled || target != null || currentTarget != null
                        }
                        handled
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        val index = event.actionIndex
                        val pointerId = event.getPointerId(index)
                        val downTarget = downTargets.remove(pointerId)
                        val upTarget = hitTarget(
                            event.getX(index) + groupRect.left,
                            event.getY(index) + groupRect.top
                        )
                        val downSpec = downTarget?.let { specById[it] }
                        val consumedByLongPress = downTarget != null && longPressActiveTargets[pointerId] == downTarget
                        updatePointerTarget(pointerId, null)
                        if (downTarget != null && downTarget == upTarget) {
                            if (downSpec?.hasLongPressAction() == true) {
                                if (!consumedByLongPress) {
                                    sendShortTap(downSpec)
                                }
                            } else if (downSpec?.hasTapToHoldAction() == true) {
                                toggleLatchedTarget(downSpec)
                            } else {
                                downSpec?.onClick?.invoke()
                            }
                        }
                        downTarget != null || upTarget != null
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        val activePointerIds = activeTargets.keys.toList()
                        activePointerIds.forEach { updatePointerTarget(it, null, emitReleaseHaptic = false) }
                        downTargets.clear()
                        true
                    }

                    else -> false
                }
            }
    ) {
        specs.forEach { spec ->
            VectorOverlayButton(
                drawableRes = spec.drawableRes,
                width = spec.width,
                height = spec.height,
                shape = spec.shape,
                alpha = spec.opacity,
                interactive = false,
                pressed = activeTargets.containsValue(spec.id) || latchedTargets[spec.id] == true,
                visualStyle = visualStyle,
                pressEffect = pressEffect,
                modifier = Modifier.offset {
                    IntOffset(
                        (spec.x.roundToPx() - groupRect.left.roundToInt()),
                        (spec.y.roundToPx() - groupRect.top.roundToInt())
                    )
                }
            )
        }
    }
}

private fun updateAnalogStick(
    x: Float, y: Float,
    sensitivity: Float = 1f,
    upKey: Int, rightKey: Int, downKey: Int, leftKey: Int,
    onPadInput: (Int, Int, Boolean) -> Unit
) {
    val scaledX = (x * sensitivity.coerceIn(0.5f, 2f)).coerceIn(-1f, 1f)
    val scaledY = (y * sensitivity.coerceIn(0.5f, 2f)).coerceIn(-1f, 1f)
    val right = (scaledX.coerceAtLeast(0f) * 255f).roundToInt()
    val left = ((-scaledX).coerceAtLeast(0f) * 255f).roundToInt()
    val down = (scaledY.coerceAtLeast(0f) * 255f).roundToInt()
    val up = ((-scaledY).coerceAtLeast(0f) * 255f).roundToInt()
    onPadInput(upKey, up, up > 0)
    onPadInput(rightKey, right, right > 0)
    onPadInput(downKey, down, down > 0)
    onPadInput(leftKey, left, left > 0)
}

private fun updateRightAnalogStick(
    x: Float,
    y: Float,
    sensitivity: Float = 1f,
    onPadInput: (Int, Int, Boolean) -> Unit
) {
    updateAnalogStick(
        x = x,
        y = y,
        sensitivity = sensitivity,
        upKey = PadKey.RIGHT_STICK_UP,
        rightKey = PadKey.RIGHT_STICK_RIGHT,
        downKey = PadKey.RIGHT_STICK_DOWN,
        leftKey = PadKey.RIGHT_STICK_LEFT,
        onPadInput = onPadInput
    )
}

private fun formatSaveTimestamp(timestamp: Long): String {
    if (timestamp <= 0L) return ""
    return DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(timestamp))
}

private fun formatPlayTime(milliseconds: Long): String {
    val totalSeconds = (milliseconds / 1000L).coerceAtLeast(0L)
    val hours = totalSeconds / 3600L
    val minutes = (totalSeconds % 3600L) / 60L
    val seconds = totalSeconds % 60L
    return if (hours > 0L) {
        "%d:%02d:%02d".format(hours, minutes, seconds)
    } else {
        "%02d:%02d".format(minutes, seconds)
    }
}

@Composable
private fun EmulationSidebarMenu(
    uiState: EmulationUiState,
    gamepadConnected: Boolean,
    currentGamePath: String?,
    retroState: com.sbro.emucorex.core.utils.RetroAchievementsLiveUiState,
    globalDefaults: SettingsSnapshot,
    overlayDefaults: OverlayLayoutSnapshot,
    onClose: () -> Unit,
    onPauseToggle: () -> Unit,
    onQuickSave: () -> Unit,
    onQuickLoad: () -> Unit,
    onLoadAutoSave: () -> Unit,
    onSetAutoSaveEnabled: (Boolean) -> Unit,
    onSetAutoSaveIntervalMinutes: (Int) -> Unit,
    onSetAutoSaveOnExit: (Boolean) -> Unit,
    onSetAutoLoadOnStart: (Boolean) -> Unit,
    onSaveGameSettingsProfile: () -> Unit,
    onResetGameSettingsProfile: () -> Unit,
    onNextSlot: () -> Unit,
    onPrevSlot: () -> Unit,
    onToggleFps: () -> Unit,
    onSetFpsOverlayMode: (Int) -> Unit,
    onSetFpsOverlayCorner: (Int) -> Unit,
    onSetFpsOverlayScale: (Int) -> Unit,
    onSetFpsOverlayMetrics: (Int) -> Unit,
    onSetOverlayScale: (Int) -> Unit,
    onSetOverlayOpacity: (Int) -> Unit,
    onSetHideOverlayOnGamepad: (Boolean) -> Unit,
    onSetCompactControls: (Boolean) -> Unit,
    onSetKeepScreenOn: (Boolean) -> Unit,
    onSetStickScale: (Int) -> Unit,
    onSetLeftStickSensitivity: (Int) -> Unit,
    onSetRightStickSensitivity: (Int) -> Unit,
    onSetInvertLeftStick: (Boolean) -> Unit,
    onSetInvertRightStick: (Boolean) -> Unit,
    onSetInvertLeftStickHorizontal: (Boolean) -> Unit,
    onSetInvertRightStickHorizontal: (Boolean) -> Unit,
    onSetGamepadStickDeadzone: (Int) -> Unit,
    onSetGamepadLeftStickSensitivity: (Int) -> Unit,
    onSetGamepadRightStickSensitivity: (Int) -> Unit,
    onSetGamepadRightStickUpToR2: (Boolean) -> Unit,
    onSetGamepadRightStickDownToL2: (Boolean) -> Unit,
    onToggleControls: () -> Unit,
    onOpenControlsEditor: () -> Unit,
    onOpenGamepadMapping: () -> Unit,
    onSetRenderer: (Int) -> Unit,
    onSetUpscale: (Float) -> Unit,
    onSetAspectRatio: (Int) -> Unit,
    onSetMtvu: (Boolean) -> Unit,
    onSetThreadPinning: (Boolean) -> Unit,
    onSetFastCdvd: (Boolean) -> Unit,
    onSetEnableCheats: (Boolean) -> Unit,
    onOpenCheats: () -> Unit,
    onSetHwDownloadMode: (Int) -> Unit,
    onSetEeCycleRate: (Int) -> Unit,
    onSetEeCycleSkip: (Int) -> Unit,
    onSetFrameSkip: (Int) -> Unit,
    onSetSkipDuplicateFrames: (Boolean) -> Unit,
    onSetFrameLimitEnabled: (Boolean) -> Unit,
    onSetTargetFps: (Int) -> Unit,
    onSetTextureFiltering: (Int) -> Unit,
    onSetTrilinearFiltering: (Int) -> Unit,
    onSetBlendingAccuracy: (Int) -> Unit,
    onSetTexturePreloading: (Int) -> Unit,
    onSetEnableFxaa: (Boolean) -> Unit,
    onSetCasMode: (Int) -> Unit,
    onSetSgsrMode: (Int) -> Unit,
    onSetCasSharpness: (Int) -> Unit,
    onSetTvShader: (Int) -> Unit,
    onSetShadeBoostBrightness: (Int) -> Unit,
    onSetShadeBoostContrast: (Int) -> Unit,
    onSetShadeBoostSaturation: (Int) -> Unit,
    onSetShadeBoostGamma: (Int) -> Unit,
    onSetEnableWidescreenPatches: (Boolean) -> Unit,
    onSetEnableNoInterlacingPatches: (Boolean) -> Unit,
    onSetAntiBlur: (Boolean) -> Unit,
    onSetAnisotropicFiltering: (Int) -> Unit,
    onSetEnableHwMipmapping: (Boolean) -> Unit,
    onSetCpuSpriteRenderSize: (Int) -> Unit,
    onSetCpuSpriteRenderLevel: (Int) -> Unit,
    onSetSoftwareClutRender: (Int) -> Unit,
    onSetGpuTargetClutMode: (Int) -> Unit,
    onSetSkipDrawStart: (Int) -> Unit,
    onSetSkipDrawEnd: (Int) -> Unit,
    onSetAutoFlushHardware: (Int) -> Unit,
    onSetCpuFramebufferConversion: (Boolean) -> Unit,
    onSetDisableDepthConversion: (Boolean) -> Unit,
    onSetDisableSafeFeatures: (Boolean) -> Unit,
    onSetDisableRenderFixes: (Boolean) -> Unit,
    onSetPreloadFrameData: (Boolean) -> Unit,
    onSetDisablePartialInvalidation: (Boolean) -> Unit,
    onSetTextureInsideRt: (Int) -> Unit,
    onSetReadTargetsOnClose: (Boolean) -> Unit,
    onSetEstimateTextureRegion: (Boolean) -> Unit,
    onSetGpuPaletteConversion: (Boolean) -> Unit,
    onSetHalfPixelOffset: (Int) -> Unit,
    onSetNativeScaling: (Int) -> Unit,
    onSetRoundSprite: (Int) -> Unit,
    onSetBilinearUpscale: (Int) -> Unit,
    onSetTextureOffsetX: (Int) -> Unit,
    onSetTextureOffsetY: (Int) -> Unit,
    onSetAlignSprite: (Boolean) -> Unit,
    onSetMergeSprite: (Boolean) -> Unit,
    onSetForceEvenSpritePosition: (Boolean) -> Unit,
    onSetNativePaletteDraw: (Boolean) -> Unit,
    onToggleJitProfiler: () -> Unit,
    onToggleHangTrace: () -> Unit,
    onExit: () -> Unit,
    modifier: Modifier = Modifier
) {
    val sectionTitleColor = MaterialTheme.colorScheme.primary
    16.dp
    val sectionSpacing = 16.dp
    val sectionLabelInset = 4.dp
    val sectionLabelTopPadding = 8.dp
    val navPadding = WindowInsets.navigationBars.asPaddingValues()
    val animatedRightInset by animateDpAsState(
        targetValue = navPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr),
        animationSpec = tween(durationMillis = 220),
        label = "emulation_menu_right_inset"
    )
    val animatedBottomInset by animateDpAsState(
        targetValue = navPadding.calculateBottomPadding(),
        animationSpec = tween(durationMillis = 220),
        label = "emulation_menu_bottom_inset"
    )
    val drawerBottomPadding = when {
        !uiState.controlsVisible -> 0.dp
        uiState.compactControls -> 28.dp
        else -> 36.dp
    }
    var selectedMenuTabName by rememberSaveable { mutableStateOf(EmulationMenuTab.Session.name) }
    val selectedMenuTab = remember(selectedMenuTabName) { EmulationMenuTab.valueOf(selectedMenuTabName) }
    val firstMenuFocusRequester = remember { FocusRequester() }
    val railFocusRequesters = remember {
        EmulationMenuTab.entries.associateWith { FocusRequester() }
    }
    var autoSaveIntervalText by remember(uiState.autoSaveIntervalMinutes) {
        mutableStateOf(uiState.autoSaveIntervalMinutes.toString())
    }
    val sessionScrollState = rememberScrollState()
    val controlsScrollState = rememberScrollState()
    val emulationScrollState = rememberScrollState()
    val graphicsScrollState = rememberScrollState()
    val fixesScrollState = rememberScrollState()
    val achievementsScrollState = rememberScrollState()
    val selectedTabScrollState = when (selectedMenuTab) {
        EmulationMenuTab.Session -> sessionScrollState
        EmulationMenuTab.Controls -> controlsScrollState
        EmulationMenuTab.Emulation -> emulationScrollState
        EmulationMenuTab.Graphics -> graphicsScrollState
        EmulationMenuTab.Fixes -> fixesScrollState
        EmulationMenuTab.Achievements -> achievementsScrollState
    }
    LaunchedEffect(selectedMenuTab) {
        railFocusRequesters[selectedMenuTab]?.requestFocus()
    }
    val menuTabs = remember(uiState.gameMenuTabOrder, uiState.hiddenGameMenuTabs) {
        uiState.gameMenuTabOrder
            .filterNot(uiState.hiddenGameMenuTabs::contains)
            .map(GameMenuTabId::toEmulationMenuTab)
            .ifEmpty { listOf(EmulationMenuTab.Session) }
    }
    LaunchedEffect(menuTabs, selectedMenuTab) {
        if (selectedMenuTab !in menuTabs) {
            selectedMenuTabName = menuTabs.first().name
        }
    }
    fun selectRelativeMenuTab(offset: Int) {
        val currentIndex = menuTabs.indexOf(selectedMenuTab).coerceAtLeast(0)
        selectedMenuTabName = menuTabs[(currentIndex + offset + menuTabs.size) % menuTabs.size].name
    }
    ProvideGamepadShoulderActions(
        onPrevious = { selectRelativeMenuTab(-1) },
        onNext = { selectRelativeMenuTab(1) }
    )

    val menuContent: @Composable ColumnScope.() -> Unit = {
                Surface(
                    shape = RoundedCornerShape(22.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.30f),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                Brush.horizontalGradient(
                                    listOf(
                                        GradientStart.copy(alpha = 0.14f),
                                        GradientEnd.copy(alpha = 0.08f)
                                    )
                                )
                            )
                            .padding(horizontal = 18.dp, vertical = 18.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = uiState.currentGameTitle.ifBlank { stringResource(R.string.emulation_sidebar_title) },
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                text = uiState.currentGameSubtitle.ifBlank { stringResource(R.string.emulation_menu_subtitle) },
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f)
                            )
                            Surface(
                                shape = RoundedCornerShape(999.dp),
                                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f),
                                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.18f))
                            ) {
                                Text(
                                    text = stringResource(
                                        R.string.emulation_play_time_badge,
                                        formatPlayTime(uiState.activePlayTimeMs)
                                    ),
                                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                                    color = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp)
                                )
                            }
                        }
                    }
                }

                when (selectedMenuTab) {
                    EmulationMenuTab.Session -> {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Box(modifier = Modifier.weight(1f)) {
                                MenuButton(
                                    modifier = Modifier.focusRequester(firstMenuFocusRequester),
                                    icon = if (uiState.isPaused) Icons.Rounded.PlayArrow else Icons.Rounded.Pause,
                                    text = stringResource(if (uiState.isPaused) R.string.emulation_resume else R.string.emulation_pause),
                                    onClick = onPauseToggle,
                                    enabled = !uiState.isActionInProgress,
                                    containerColor = if (!uiState.isPaused) {
                                        MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.46f)
                                    } else {
                                        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                                    }
                                )
                            }
                            Box(modifier = Modifier.weight(1f)) {
                                MenuButton(
                                    icon = Icons.AutoMirrored.Rounded.ExitToApp,
                                    text = stringResource(R.string.emulation_exit),
                                    onClick = onExit,
                                    enabled = !uiState.isActionInProgress,
                                    isDestructive = true,
                                    containerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.28f)
                                )
                            }
                        }

                        val visibleSessionSections = gameMenuSectionsForTab(
                            GameMenuTabId.SESSION,
                            uiState.gameMenuSectionOrder
                        )
                            .filterNot(uiState.hiddenGameMenuSections::contains)
                        visibleSessionSections.forEachIndexed { index, section ->
                        val includeFollowingAutoSave = section == GameMenuSectionId.SAVE_STATES &&
                            visibleSessionSections.getOrNull(index + 1) == GameMenuSectionId.AUTO_SAVE
                        val mergedIntoPreviousSaveStates = section == GameMenuSectionId.AUTO_SAVE &&
                            visibleSessionSections.getOrNull(index - 1) == GameMenuSectionId.SAVE_STATES
                        if (!mergedIntoPreviousSaveStates &&
                            (section == GameMenuSectionId.SAVE_STATES || section == GameMenuSectionId.AUTO_SAVE)
                        ) {
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f)
                        ) {
                            Column(
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                if (section == GameMenuSectionId.SAVE_STATES) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                            text = stringResource(R.string.detail_save_states),
                                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                            color = MaterialTheme.colorScheme.onSurface
                                        )
                                        Text(
                                            text = if (uiState.currentSlotLastModified > 0L) {
                                                stringResource(
                                                    R.string.emulation_save_slot_saved_at,
                                                    formatSaveTimestamp(uiState.currentSlotLastModified)
                                                )
                                            } else {
                                                stringResource(R.string.emulation_save_slot_empty)
                                            },
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                    androidx.compose.material3.IconButton(
                                        onClick = onPrevSlot,
                                        enabled = uiState.currentSlot > 1,
                                        colors = androidx.compose.material3.IconButtonDefaults.iconButtonColors(
                                            contentColor = MaterialTheme.colorScheme.primary
                                        )
                                    ) {
                                        Icon(Icons.AutoMirrored.Rounded.KeyboardArrowLeft, null)
                                    }
                                    Text(
                                        text = "${uiState.currentSlot}",
                                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.ExtraBold),
                                        color = MaterialTheme.colorScheme.primary,
                                        modifier = Modifier.padding(horizontal = 4.dp)
                                    )
                                    androidx.compose.material3.IconButton(
                                        onClick = onNextSlot,
                                        enabled = uiState.currentSlot < 10,
                                        colors = androidx.compose.material3.IconButtonDefaults.iconButtonColors(
                                            contentColor = MaterialTheme.colorScheme.primary
                                        )
                                    ) {
                                        Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, null)
                                    }
                                }

                                }

                                if (includeFollowingAutoSave) {
                                    HorizontalDivider(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f))
                                }

                                if (section == GameMenuSectionId.AUTO_SAVE || includeFollowingAutoSave) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                            text = stringResource(R.string.emulation_auto_save_title),
                                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                            color = MaterialTheme.colorScheme.onSurface
                                        )
                                        Text(
                                            text = if (uiState.autoSaveLastModified > 0L) {
                                                formatSaveTimestamp(uiState.autoSaveLastModified)
                                            } else {
                                                stringResource(R.string.emulation_auto_save_empty)
                                            },
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                    Switch(
                                        checked = uiState.autoSaveEnabled,
                                        onCheckedChange = onSetAutoSaveEnabled,
                                        enabled = !uiState.isActionInProgress
                                    )
                                }

                                AnimatedVisibility(visible = uiState.autoSaveEnabled) {
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Text(
                                            text = stringResource(R.string.emulation_auto_save_interval_label),
                                            style = MaterialTheme.typography.bodyMedium,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                        OutlinedTextField(
                                            value = autoSaveIntervalText,
                                            onValueChange = { raw ->
                                                val filtered = raw.filter(Char::isDigit).take(3)
                                                autoSaveIntervalText = filtered
                                                filtered.toIntOrNull()?.takeIf { it >= 1 }?.let { minutes ->
                                                    onSetAutoSaveIntervalMinutes(minutes)
                                                }
                                            },
                                            enabled = !uiState.isActionInProgress,
                                            singleLine = true,
                                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                                            shape = RoundedCornerShape(16.dp),
                                            textStyle = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                                            modifier = Modifier
                                                .width(108.dp)
                                                .height(48.dp),
                                            suffix = {
                                                Text(stringResource(R.string.emulation_auto_save_interval_suffix))
                                            }
                                        )
                                        Spacer(modifier = Modifier.weight(1f))
                                        CompactIconActionButton(
                                            icon = Icons.Rounded.Restore,
                                            contentDescription = stringResource(R.string.emulation_quick_load_desc),
                                            onClick = onLoadAutoSave,
                                            enabled = !uiState.isActionInProgress && uiState.autoSaveLastModified > 0L,
                                            showProgress = uiState.actionLabel == "loading"
                                        )
                                    }
                                }
                                }
                            }
                        }

                        }

                        if (section == GameMenuSectionId.QUICK_ACTIONS) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Box(modifier = Modifier.weight(1f)) {
                                QuickIconActionButton(
                                    icon = Icons.Rounded.Save,
                                    contentDescription = stringResource(R.string.emulation_quick_save_desc),
                                    onClick = onQuickSave,
                                    enabled = !uiState.isActionInProgress,
                                    showProgress = uiState.actionLabel == "saving",
                                    containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f)
                                )
                            }
                            Box(modifier = Modifier.weight(1f)) {
                                QuickIconActionButton(
                                    icon = Icons.Rounded.Restore,
                                    contentDescription = stringResource(R.string.emulation_quick_load_desc),
                                    onClick = onQuickLoad,
                                    enabled = !uiState.isActionInProgress,
                                    showProgress = uiState.actionLabel == "loading",
                                    containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f)
                                )
                            }
                        }

                        }

                        if (section == GameMenuSectionId.SESSION_DEBUG_TOOLS && uiState.showDebugOptions) {
                            Surface(
                                modifier = Modifier.fillMaxWidth(),
                                shape = RoundedCornerShape(16.dp),
                                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f)
                            ) {
                                Column(
                                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                    ) {
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(
                                                text = stringResource(R.string.jit_profiler_title),
                                                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                                color = MaterialTheme.colorScheme.onSurface
                                            )
                                            Text(
                                                text = stringResource(R.string.jit_profiler_desc),
                                                style = MaterialTheme.typography.bodySmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                        Switch(
                                            checked = uiState.isJitProfilerActive,
                                            onCheckedChange = { onToggleJitProfiler() },
                                            enabled = !uiState.isActionInProgress
                                        )
                                    }
                                }
                            }

                            Surface(
                                modifier = Modifier.fillMaxWidth(),
                                shape = RoundedCornerShape(16.dp),
                                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f)
                            ) {
                                Column(
                                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                    ) {
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(
                                                text = stringResource(R.string.hang_trace_title),
                                                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                                color = MaterialTheme.colorScheme.onSurface
                                            )
                                            Text(
                                                text = stringResource(R.string.hang_trace_desc),
                                                style = MaterialTheme.typography.bodySmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                        Switch(
                                            checked = uiState.isHangTraceActive,
                                            onCheckedChange = { onToggleHangTrace() },
                                            enabled = !uiState.isActionInProgress
                                        )
                                    }
                                }
                            }
                        }

                        if (section == GameMenuSectionId.AUTOMATION) {
                        SettingsToggle(
                            title = stringResource(R.string.emulation_auto_save_on_exit),
                            checked = uiState.autoSaveOnExit,
                            enabled = !uiState.isActionInProgress,
                            onCheckedChange = onSetAutoSaveOnExit,
                            helpText = stringResource(R.string.emulation_auto_save_on_exit_desc)
                        )

                        SettingsToggle(
                            title = stringResource(R.string.emulation_auto_load_on_start),
                            checked = uiState.autoLoadOnStart,
                            enabled = !uiState.isActionInProgress,
                            onCheckedChange = onSetAutoLoadOnStart,
                            helpText = stringResource(R.string.emulation_auto_load_on_start_desc)
                        )

                        }

                        if (section == GameMenuSectionId.GAME_PROFILE) {
                        SidebarSectionTitle(
                            text = stringResource(R.string.game_settings_overlay_section).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(18.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
                        ) {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Text(
                                    text = stringResource(
                                        if (uiState.gameSettingsProfileActive) {
                                            R.string.game_settings_overlay_profile_active
                                        } else {
                                            R.string.game_settings_overlay_profile_inactive
                                        }
                                    ),
                                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                                Text(
                                    text = stringResource(R.string.game_settings_overlay_profile_note),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Box(modifier = Modifier.weight(1f)) {
                                MenuButton(
                                    icon = Icons.Rounded.Save,
                                    text = stringResource(R.string.game_settings_overlay_save),
                                    onClick = onSaveGameSettingsProfile,
                                    enabled = !uiState.isActionInProgress,
                                    containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.34f)
                                )
                            }
                            Box(modifier = Modifier.weight(1f)) {
                                MenuButton(
                                    icon = Icons.Rounded.Restore,
                                    text = stringResource(R.string.game_settings_overlay_reset),
                                    onClick = onResetGameSettingsProfile,
                                    enabled = !uiState.isActionInProgress && uiState.gameSettingsProfileActive,
                                    containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
                                )
                            }
                        }

                        }
                            }

                    }

                    EmulationMenuTab.Controls -> {
                        gameMenuSectionsForTab(GameMenuTabId.CONTROLS, uiState.gameMenuSectionOrder)
                            .filterNot(uiState.hiddenGameMenuSections::contains)
                            .forEach { section ->
                                when (section) {
                                    GameMenuSectionId.CONTROLS_GENERAL -> {
                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_controls_tab).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        MenuButton(
                            icon = if (uiState.controlsVisible) Icons.Rounded.VisibilityOff else Icons.Rounded.Visibility,
                            text = stringResource(if (uiState.controlsVisible) R.string.emulation_hide_controls else R.string.emulation_show_controls),
                            onClick = onToggleControls,
                            containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.34f)
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_gamepad_hide_overlay),
                            checked = uiState.hideOverlayOnGamepad,
                            onCheckedChange = onSetHideOverlayOnGamepad,
                            helpText = stringResource(R.string.settings_help_hide_overlay_on_gamepad),
                            onResetToDefault = { onSetHideOverlayOnGamepad(overlayDefaults.hideOverlayOnGamepad) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_compact_controls),
                            checked = uiState.compactControls,
                            onCheckedChange = onSetCompactControls,
                            helpText = stringResource(R.string.settings_help_compact_controls),
                            onResetToDefault = { onSetCompactControls(globalDefaults.compactControls) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_keep_screen_on),
                            checked = uiState.keepScreenOn,
                            onCheckedChange = onSetKeepScreenOn,
                            helpText = stringResource(R.string.settings_help_keep_screen_on),
                            onResetToDefault = { onSetKeepScreenOn(globalDefaults.keepScreenOn) }
                        )

                                    }

                                    GameMenuSectionId.CONTROLS_TOUCH -> {

                        OverlaySubsectionLabel(text = stringResource(R.string.settings_touch_controls_section))

                        LiveSliderRow(
                            title = stringResource(R.string.settings_overlay_scale),
                            valueLabelForValue = { "$it%" },
                            value = uiState.overlayScale.toFloat(),
                            range = 50f..150f,
                            steps = 99,
                            onValueChange = { onSetOverlayScale(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_overlay_scale),
                            onResetToDefault = { onSetOverlayScale(overlayDefaults.overlayScale) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_overlay_opacity),
                            valueLabelForValue = { "$it%" },
                            value = uiState.overlayOpacity.toFloat(),
                            range = 20f..100f,
                            steps = 79,
                            onValueChange = { onSetOverlayOpacity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_overlay_opacity),
                            onResetToDefault = { onSetOverlayOpacity(overlayDefaults.overlayOpacity) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.emulation_stick_scale),
                            valueLabelForValue = { "$it%" },
                            value = uiState.stickScale.toFloat(),
                            range = AppPreferences.OVERLAY_CONTROL_SCALE_MIN.toFloat()..
                                AppPreferences.OVERLAY_CONTROL_SCALE_MAX.toFloat(),
                            steps = AppPreferences.OVERLAY_CONTROL_SCALE_MAX -
                                AppPreferences.OVERLAY_CONTROL_SCALE_MIN - 1,
                            onValueChange = { onSetStickScale(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_stick_scale),
                            onResetToDefault = { onSetStickScale(overlayDefaults.stickScale) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_left_stick_sensitivity),
                            valueLabelForValue = { "$it%" },
                            value = uiState.leftStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 149,
                            onValueChange = { onSetLeftStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_left_stick_sensitivity),
                            onResetToDefault = { onSetLeftStickSensitivity(AppPreferences.DEFAULT_STICK_SENSITIVITY) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_right_stick_sensitivity),
                            valueLabelForValue = { "$it%" },
                            value = uiState.rightStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 149,
                            onValueChange = { onSetRightStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_right_stick_sensitivity),
                            onResetToDefault = { onSetRightStickSensitivity(AppPreferences.DEFAULT_STICK_SENSITIVITY) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_invert_left_stick),
                            checked = uiState.invertLeftStick,
                            onCheckedChange = onSetInvertLeftStick,
                            helpText = stringResource(R.string.settings_help_invert_left_stick),
                            onResetToDefault = { onSetInvertLeftStick(false) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_invert_left_stick_horizontal),
                            checked = uiState.invertLeftStickHorizontal,
                            onCheckedChange = onSetInvertLeftStickHorizontal,
                            helpText = stringResource(R.string.settings_help_invert_left_stick_horizontal),
                            onResetToDefault = { onSetInvertLeftStickHorizontal(false) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_invert_right_stick),
                            checked = uiState.invertRightStick,
                            onCheckedChange = onSetInvertRightStick,
                            helpText = stringResource(R.string.settings_help_invert_right_stick),
                            onResetToDefault = { onSetInvertRightStick(false) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_invert_right_stick_horizontal),
                            checked = uiState.invertRightStickHorizontal,
                            onCheckedChange = onSetInvertRightStickHorizontal,
                            helpText = stringResource(R.string.settings_help_invert_right_stick_horizontal),
                            onResetToDefault = { onSetInvertRightStickHorizontal(false) }
                        )

                        MenuButton(
                            icon = Icons.Rounded.TouchApp,
                            text = stringResource(R.string.settings_edit_controls),
                            onClick = onOpenControlsEditor,
                            containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.28f)
                        )

                                    }

                                    GameMenuSectionId.CONTROLS_GAMEPAD -> {

                        OverlaySubsectionLabel(text = stringResource(R.string.settings_gamepad_controls_section))

                        if (!gamepadConnected) {
                            Text(
                                text = stringResource(R.string.settings_gamepad_controls_disconnected_hint),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }

                        LiveSliderRow(
                            title = stringResource(R.string.settings_gamepad_stick_deadzone),
                            valueLabelForValue = { "$it%" },
                            value = uiState.gamepadStickDeadzone.toFloat(),
                            range = 0f..35f,
                            steps = 34,
                            enabled = gamepadConnected,
                            onValueChange = { onSetGamepadStickDeadzone(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_gamepad_stick_deadzone),
                            onResetToDefault = { onSetGamepadStickDeadzone(AppPreferences.DEFAULT_GAMEPAD_STICK_DEADZONE) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_gamepad_left_stick_sensitivity),
                            valueLabelForValue = { "$it%" },
                            value = uiState.gamepadLeftStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 149,
                            enabled = gamepadConnected,
                            onValueChange = { onSetGamepadLeftStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_gamepad_left_stick_sensitivity),
                            onResetToDefault = { onSetGamepadLeftStickSensitivity(AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_gamepad_right_stick_sensitivity),
                            valueLabelForValue = { "$it%" },
                            value = uiState.gamepadRightStickSensitivity.toFloat(),
                            range = 50f..200f,
                            steps = 149,
                            enabled = gamepadConnected,
                            onValueChange = { onSetGamepadRightStickSensitivity(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_gamepad_right_stick_sensitivity),
                            onResetToDefault = { onSetGamepadRightStickSensitivity(AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_gamepad_right_stick_up_to_r2),
                            checked = uiState.gamepadRightStickUpToR2,
                            onCheckedChange = onSetGamepadRightStickUpToR2,
                            helpText = stringResource(R.string.settings_help_gamepad_right_stick_up_to_r2),
                            onResetToDefault = { onSetGamepadRightStickUpToR2(globalDefaults.gamepadRightStickUpToR2) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_gamepad_right_stick_down_to_l2),
                            checked = uiState.gamepadRightStickDownToL2,
                            onCheckedChange = onSetGamepadRightStickDownToL2,
                            helpText = stringResource(R.string.settings_help_gamepad_right_stick_down_to_l2),
                            onResetToDefault = { onSetGamepadRightStickDownToL2(globalDefaults.gamepadRightStickDownToL2) }
                        )

                        MenuButton(
                            icon = Icons.Rounded.Gamepad,
                            text = stringResource(R.string.settings_gamepad_mapping_title),
                            onClick = onOpenGamepadMapping,
                            enabled = gamepadConnected,
                            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.40f)
                        )
                                    }

                                    else -> Unit
                                }
                            }
                    }

                    EmulationMenuTab.Emulation -> {
                        gameMenuSectionsForTab(GameMenuTabId.EMULATION, uiState.gameMenuSectionOrder)
                            .filterNot(uiState.hiddenGameMenuSections::contains)
                            .forEach { section ->
                                when (section) {
                                    GameMenuSectionId.EMULATION_PERFORMANCE -> {
                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_emulation_tab).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        SettingsToggle(
                            title = stringResource(R.string.emulation_performance_stats),
                            checked = uiState.showFps,
                            onCheckedChange = { onToggleFps() },
                            helpText = stringResource(R.string.settings_help_show_fps),
                            onResetToDefault = {
                                if (uiState.showFps != globalDefaults.showFps) {
                                    onToggleFps()
                                }
                            }
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_fps_overlay_mode),
                            options = listOf(
                                LiveSelectionOption(FPS_OVERLAY_MODE_SIMPLE, stringResource(R.string.settings_fps_overlay_mode_simple)),
                                LiveSelectionOption(FPS_OVERLAY_MODE_DETAILED, stringResource(R.string.settings_fps_overlay_mode_detailed))
                            ),
                            currentValue = uiState.fpsOverlayMode,
                            onValueChange = onSetFpsOverlayMode,
                            helpText = stringResource(R.string.settings_help_fps_overlay_mode),
                            onResetToDefault = { onSetFpsOverlayMode(globalDefaults.fpsOverlayMode) }
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_fps_overlay_position),
                            options = fpsOverlayCornerLiveOptions(),
                            currentValue = uiState.fpsOverlayCorner,
                            onValueChange = onSetFpsOverlayCorner,
                            helpText = stringResource(R.string.settings_help_fps_overlay_position),
                            onResetToDefault = { onSetFpsOverlayCorner(globalDefaults.fpsOverlayCorner) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_fps_overlay_scale),
                            valueLabelResId = R.string.settings_fps_overlay_scale_value,
                            valueLabelForValue = { scale -> "$scale%" },
                            value = uiState.fpsOverlayScale.toFloat(),
                            range = AppPreferences.MIN_FPS_OVERLAY_SCALE.toFloat()..AppPreferences.MAX_FPS_OVERLAY_SCALE.toFloat(),
                            steps = 24,
                            onValueChange = { onSetFpsOverlayScale(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_fps_overlay_scale),
                            onResetToDefault = { onSetFpsOverlayScale(globalDefaults.fpsOverlayScale) }
                        )

                        if (uiState.fpsOverlayMode == FPS_OVERLAY_MODE_DETAILED) {
                            LiveBitmaskChoiceRow(
                                title = stringResource(R.string.settings_fps_overlay_metrics),
                                options = fpsOverlayMetricLiveOptions(),
                                selectedMask = uiState.fpsOverlayMetrics,
                                onToggle = { metric -> onSetFpsOverlayMetrics(uiState.fpsOverlayMetrics xor metric) },
                                helpText = stringResource(R.string.settings_help_fps_overlay_metrics),
                                onResetToDefault = { onSetFpsOverlayMetrics(globalDefaults.fpsOverlayMetrics) }
                            )
                        }

                                    }

                                    GameMenuSectionId.EMULATION_SPEED -> {

                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_speed_hacks).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_ee_cycle_rate),
                            options = eeCycleRateLiveOptions(),
                            currentValue = uiState.eeCycleRate,
                            onValueChange = onSetEeCycleRate,
                            helpText = stringResource(R.string.settings_help_ee_cycle_rate),
                            onResetToDefault = { onSetEeCycleRate(globalDefaults.eeCycleRate) }
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_ee_cycle_skip),
                            options = eeCycleSkipLiveOptions(),
                            currentValue = uiState.eeCycleSkip,
                            onValueChange = onSetEeCycleSkip,
                            helpText = stringResource(R.string.settings_help_ee_cycle_skip),
                            onResetToDefault = { onSetEeCycleSkip(globalDefaults.eeCycleSkip) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_frame_limiter),
                            checked = uiState.frameLimitEnabled,
                            onCheckedChange = onSetFrameLimitEnabled,
                            helpText = stringResource(R.string.settings_help_frame_limiter),
                            onResetToDefault = { onSetFrameLimitEnabled(globalDefaults.frameLimitEnabled) }
                        )
                        LiveSelectionRow(
                            title = stringResource(R.string.settings_target_fps_mode),
                            options = listOf(
                                LiveSelectionOption(0, stringResource(R.string.settings_target_fps_auto)),
                                LiveSelectionOption(1, stringResource(R.string.settings_target_fps_manual))
                            ),
                            currentValue = if (uiState.targetFps <= 0) 0 else 1,
                            onValueChange = { mode ->
                                onSetTargetFps(
                                    if (mode == 0) 0 else resolveManualTargetFps(uiState.targetFps, globalDefaults.targetFps)
                                )
                            },
                            allowWrap = false,
                            helpText = stringResource(R.string.settings_help_target_fps),
                            onResetToDefault = { onSetTargetFps(globalDefaults.targetFps) }
                        )
                        if (uiState.targetFps > 0) {
                            LiveSliderRow(
                                title = stringResource(R.string.settings_target_fps),
                                valueLabelResId = R.string.settings_target_fps_desc,
                                valueLabelForValue = { fps -> fps.toString() },
                                value = uiState.targetFps.toFloat(),
                                range = 20f..120f,
                                steps = 99,
                                onValueChange = { onSetTargetFps(it.toInt()) },
                                helpText = stringResource(R.string.settings_help_target_fps),
                                onResetToDefault = { onSetTargetFps(globalDefaults.targetFps) }
                            )
                        }

                        SettingsToggle(
                            title = stringResource(R.string.settings_mtvu),
                            checked = uiState.enableMtvu,
                            onCheckedChange = onSetMtvu,
                            helpText = stringResource(R.string.settings_help_mtvu),
                            onResetToDefault = { onSetMtvu(globalDefaults.enableMtvu) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_thread_pinning),
                            checked = uiState.enableThreadPinning,
                            onCheckedChange = onSetThreadPinning,
                            helpText = stringResource(R.string.settings_help_thread_pinning),
                            onResetToDefault = { onSetThreadPinning(globalDefaults.enableThreadPinning) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_fast_cdvd),
                            checked = uiState.enableFastCdvd,
                            onCheckedChange = onSetFastCdvd,
                            helpText = stringResource(R.string.settings_help_fast_cdvd),
                            onResetToDefault = { onSetFastCdvd(globalDefaults.enableFastCdvd) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_skip_duplicate_frames),
                            checked = uiState.skipDuplicateFrames,
                            onCheckedChange = onSetSkipDuplicateFrames,
                            helpText = stringResource(R.string.settings_help_skip_duplicate_frames),
                            onResetToDefault = { onSetSkipDuplicateFrames(globalDefaults.skipDuplicateFrames) }
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_frame_skip),
                            options = listOf(
                                LiveSelectionOption(0, stringResource(R.string.settings_frame_skip_off)),
                                LiveSelectionOption(1, "1/2"),
                                LiveSelectionOption(2, "1/4")
                            ),
                            currentValue = uiState.frameSkip,
                            onValueChange = onSetFrameSkip,
                            helpText = stringResource(R.string.settings_help_frame_skip),
                            onResetToDefault = { onSetFrameSkip(globalDefaults.frameSkip) }
                        )

                                    }

                                    GameMenuSectionId.EMULATION_CHEATS -> {

                        SettingsToggle(
                            title = stringResource(R.string.settings_enable_cheats),
                            checked = uiState.enableCheats,
                            onCheckedChange = onSetEnableCheats,
                            helpText = stringResource(R.string.settings_help_cheats),
                            onResetToDefault = { onSetEnableCheats(globalDefaults.enableCheats) }
                        )

                        if (uiState.availableCheats.isNotEmpty()) {
                            MenuButton(
                                icon = Icons.Rounded.Star,
                                text = stringResource(R.string.emulation_cheats_open_button),
                                onClick = onOpenCheats,
                                containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f),
                                enabled = true
                            )
                        }
                                    }

                                    else -> Unit
                                }
                            }
                    }

                    EmulationMenuTab.Graphics -> {
                        gameMenuSectionsForTab(GameMenuTabId.GRAPHICS, uiState.gameMenuSectionOrder)
                            .filterNot(uiState.hiddenGameMenuSections::contains)
                            .forEach { section ->
                                when (section) {
                                    GameMenuSectionId.GRAPHICS_DISPLAY -> {
                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_graphics_tab).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_renderer),
                            options = listOf(
                                LiveSelectionOption(12, stringResource(R.string.settings_renderer_opengl)),
                                LiveSelectionOption(14, stringResource(R.string.settings_renderer_vulkan)),
                                LiveSelectionOption(13, stringResource(R.string.settings_renderer_software))
                            ),
                            currentValue = uiState.renderer,
                            onValueChange = onSetRenderer,
                            allowWrap = false,
                            helpText = stringResource(R.string.settings_help_renderer),
                            onResetToDefault = { onSetRenderer(globalDefaults.renderer) }
                        )

                        val maxUpscaleMultiplier = remember(uiState.renderer) {
                            EmulatorBridge.getMaxUpscaleMultiplier(uiState.renderer)
                        }
                        val nativeUpscaleLabel = stringResource(R.string.settings_upscale_native)
                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_upscale),
                            options = buildUpscaleOptions(nativeUpscaleLabel, maxUpscaleMultiplier),
                            currentValue = upscaleMultiplierValue(uiState.upscale),
                            onValueChange = { onSetUpscale(upscaleKeyToMultiplier(it)) },
                            helpText = stringResource(R.string.settings_help_upscale),
                            onResetToDefault = { onSetUpscale(globalDefaults.upscaleMultiplier) }
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_aspect_ratio).replace(":", ""),
                            options = listOf(
                                LiveSelectionOption(1, stringResource(R.string.emulation_aspect_auto)),
                                LiveSelectionOption(2, "4:3"),
                                LiveSelectionOption(3, "16:9"),
                                LiveSelectionOption(4, stringResource(R.string.settings_aspect_ratio_107)),
                                LiveSelectionOption(
                                    value = 0,
                                    icon = Icons.Rounded.Fullscreen,
                                    contentDescription = stringResource(R.string.emulation_aspect_stretch)
                                )
                            ),
                            currentValue = uiState.aspectRatio,
                            onValueChange = onSetAspectRatio,
                            allowWrap = false,
                            helpText = stringResource(R.string.settings_help_aspect_ratio),
                            onResetToDefault = { onSetAspectRatio(globalDefaults.aspectRatio) }
                        )

                                    }

                                    GameMenuSectionId.GRAPHICS_RENDERING -> {

                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_rendering_section).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_hw_download_mode),
                            options = listOf(
                                LiveSelectionOption(0, stringResource(R.string.settings_hw_download_mode_accurate)),
                                LiveSelectionOption(1, stringResource(R.string.settings_hw_download_mode_force_full)),
                                LiveSelectionOption(2, stringResource(R.string.settings_hw_download_mode_no_readbacks)),
                                LiveSelectionOption(3, stringResource(R.string.settings_hw_download_mode_unsynchronized)),
                                LiveSelectionOption(4, stringResource(R.string.settings_hw_download_mode_disabled))
                            ),
                            currentValue = uiState.hwDownloadMode,
                            onValueChange = onSetHwDownloadMode,
                            helpText = stringResource(R.string.settings_help_hw_download_mode),
                            onResetToDefault = { onSetHwDownloadMode(globalDefaults.hwDownloadMode) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_blending_accuracy),
                            options = listOf(
                                0 to stringResource(R.string.settings_blending_accuracy_minimum),
                                1 to stringResource(R.string.settings_blending_accuracy_basic),
                                2 to stringResource(R.string.settings_blending_accuracy_medium),
                                3 to stringResource(R.string.settings_blending_accuracy_high),
                                4 to stringResource(R.string.settings_blending_accuracy_full),
                                5 to stringResource(R.string.settings_blending_accuracy_maximum)
                            ),
                            currentValue = uiState.blendingAccuracy,
                            onValueChange = onSetBlendingAccuracy,
                            helpText = stringResource(R.string.settings_help_blending_accuracy),
                            onResetToDefault = { onSetBlendingAccuracy(globalDefaults.blendingAccuracy) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_texture_preloading),
                            options = listOf(
                                0 to stringResource(R.string.settings_texture_preloading_none),
                                1 to stringResource(R.string.settings_texture_preloading_partial),
                                2 to stringResource(R.string.settings_texture_preloading_full)
                            ),
                            currentValue = uiState.texturePreloading,
                            onValueChange = onSetTexturePreloading,
                            helpText = stringResource(R.string.settings_help_texture_preloading),
                            onResetToDefault = { onSetTexturePreloading(globalDefaults.texturePreloading) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_bilinear_filtering),
                            options = listOf(
                                0 to stringResource(R.string.settings_bilinear_filtering_nearest),
                                1 to stringResource(R.string.settings_bilinear_filtering_forced),
                                2 to stringResource(R.string.settings_bilinear_filtering_ps2),
                                3 to stringResource(R.string.settings_bilinear_filtering_no_sprite)
                            ),
                            currentValue = uiState.textureFiltering,
                            onValueChange = onSetTextureFiltering,
                            helpText = stringResource(R.string.settings_help_bilinear_filtering),
                            onResetToDefault = { onSetTextureFiltering(globalDefaults.textureFiltering) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_trilinear_filtering),
                            options = listOf(
                                -1 to stringResource(R.string.settings_trilinear_filtering_auto),
                                0 to stringResource(R.string.settings_trilinear_filtering_off),
                                1 to stringResource(R.string.settings_trilinear_filtering_ps2),
                                2 to stringResource(R.string.settings_trilinear_filtering_forced)
                            ),
                            currentValue = uiState.trilinearFiltering,
                            onValueChange = onSetTrilinearFiltering,
                            helpText = stringResource(R.string.settings_help_trilinear_filtering),
                            onResetToDefault = { onSetTrilinearFiltering(globalDefaults.trilinearFiltering) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_anisotropic_filtering),
                            options = listOf(
                                0 to stringResource(R.string.settings_aniso_off),
                                2 to "2x",
                                4 to "4x",
                                8 to "8x",
                                16 to "16x"
                            ),
                            currentValue = uiState.anisotropicFiltering,
                            onValueChange = onSetAnisotropicFiltering,
                            helpText = stringResource(R.string.settings_help_anisotropic_filtering),
                            onResetToDefault = { onSetAnisotropicFiltering(globalDefaults.anisotropicFiltering) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_fxaa),
                            checked = uiState.enableFxaa,
                            onCheckedChange = onSetEnableFxaa,
                            helpText = stringResource(R.string.settings_help_fxaa),
                            onResetToDefault = { onSetEnableFxaa(globalDefaults.enableFxaa) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_sgsr),
                            options = listOf(
                                0 to stringResource(R.string.settings_sgsr_off),
                                1 to stringResource(R.string.settings_sgsr_quality),
                                2 to stringResource(R.string.settings_sgsr_balanced),
                                3 to stringResource(R.string.settings_sgsr_performance)
                            ),
                            currentValue = uiState.sgsrMode,
                            onValueChange = onSetSgsrMode,
                            helpText = stringResource(R.string.settings_help_sgsr),
                            onResetToDefault = { onSetSgsrMode(globalDefaults.sgsrMode) }
                        )

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_cas),
                            options = listOf(
                                0 to stringResource(R.string.settings_cas_mode_off),
                                1 to stringResource(R.string.settings_cas_mode_sharpen_only),
                                2 to stringResource(R.string.settings_cas_mode_sharpen_resize)
                            ),
                            currentValue = uiState.casMode,
                            onValueChange = onSetCasMode,
                            helpText = stringResource(R.string.settings_help_cas),
                            onResetToDefault = { onSetCasMode(globalDefaults.casMode) }
                        )

                        if (uiState.casMode != 0) {
                            LiveSliderRow(
                                title = stringResource(R.string.settings_cas_sharpness),
                                valueLabelForValue = { "$it%" },
                                value = uiState.casSharpness.toFloat(),
                                range = 0f..100f,
                                steps = 99,
                                onValueChange = { onSetCasSharpness(it.toInt()) },
                                helpText = stringResource(R.string.settings_help_cas_sharpness),
                                onResetToDefault = { onSetCasSharpness(globalDefaults.casSharpness) }
                            )
                        }

                        LiveChipsSelectionRow(
                            title = stringResource(R.string.settings_tv_shader),
                            options = listOf(
                                0 to stringResource(R.string.settings_tv_shader_none),
                                1 to stringResource(R.string.settings_tv_shader_scanline),
                                2 to stringResource(R.string.settings_tv_shader_diagonal),
                                3 to stringResource(R.string.settings_tv_shader_triangular),
                                4 to stringResource(R.string.settings_tv_shader_wave),
                                5 to stringResource(R.string.settings_tv_shader_lottes_crt),
                                6 to stringResource(R.string.settings_tv_shader_4x_rgss),
                                7 to stringResource(R.string.settings_tv_shader_nx_agss)
                            ),
                            currentValue = uiState.tvShader,
                            onValueChange = onSetTvShader,
                            helpText = stringResource(R.string.settings_help_tv_shader),
                            onResetToDefault = { onSetTvShader(globalDefaults.tvShader) }
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_hw_mipmapping),
                            checked = uiState.enableHwMipmapping,
                            onCheckedChange = onSetEnableHwMipmapping,
                            helpText = stringResource(R.string.settings_help_hw_mipmapping),
                            onResetToDefault = { onSetEnableHwMipmapping(globalDefaults.enableHwMipmapping) }
                        )

                                    }

                                    GameMenuSectionId.GRAPHICS_SCREEN -> {

                        SidebarSectionTitle(
                            text = stringResource(R.string.emulation_screen_tab).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(18.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
                        ) {
                            Text(
                                text = stringResource(R.string.screen_settings_menu_desc),
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }

                        LiveSliderRow(
                            title = stringResource(R.string.settings_shadeboost_brightness),
                            valueLabelForValue = { it.toString() },
                            value = uiState.shadeBoostBrightness.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { onSetShadeBoostBrightness(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_brightness),
                            onResetToDefault = { onSetShadeBoostBrightness(globalDefaults.shadeBoostBrightness) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_shadeboost_contrast),
                            valueLabelForValue = { it.toString() },
                            value = uiState.shadeBoostContrast.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { onSetShadeBoostContrast(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_contrast),
                            onResetToDefault = { onSetShadeBoostContrast(globalDefaults.shadeBoostContrast) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_shadeboost_saturation),
                            valueLabelForValue = { it.toString() },
                            value = uiState.shadeBoostSaturation.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { onSetShadeBoostSaturation(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_saturation),
                            onResetToDefault = { onSetShadeBoostSaturation(globalDefaults.shadeBoostSaturation) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_shadeboost_gamma),
                            valueLabelForValue = { it.toString() },
                            value = uiState.shadeBoostGamma.toFloat(),
                            range = 1f..100f,
                            steps = 98,
                            onValueChange = { onSetShadeBoostGamma(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_shadeboost_gamma),
                            onResetToDefault = { onSetShadeBoostGamma(globalDefaults.shadeBoostGamma) }
                        )
                                    }

                                    else -> Unit
                                }
                            }
                    }

                    EmulationMenuTab.Fixes -> {
                        gameMenuSectionsForTab(GameMenuTabId.FIXES, uiState.gameMenuSectionOrder)
                            .filterNot(uiState.hiddenGameMenuSections::contains)
                            .forEach { section ->
                                when (section) {
                                    GameMenuSectionId.FIXES_PATCHES -> {
                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_patches_section).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_widescreen_patches),
                            checked = uiState.widescreenPatches,
                            onCheckedChange = onSetEnableWidescreenPatches,
                            helpText = stringResource(R.string.settings_help_widescreen_patches),
                            onResetToDefault = { onSetEnableWidescreenPatches(globalDefaults.enableWidescreenPatches) }
                        )
                        SettingsToggle(
                            title = stringResource(R.string.settings_no_interlacing_patches),
                            checked = uiState.noInterlacingPatches,
                            onCheckedChange = onSetEnableNoInterlacingPatches,
                            helpText = stringResource(R.string.settings_help_no_interlacing_patches),
                            onResetToDefault = { onSetEnableNoInterlacingPatches(globalDefaults.enableNoInterlacingPatches) }
                        )
                        SettingsToggle(
                            title = stringResource(R.string.settings_anti_blur),
                            checked = uiState.antiBlur,
                            onCheckedChange = onSetAntiBlur,
                            helpText = stringResource(R.string.settings_help_anti_blur),
                            onResetToDefault = { onSetAntiBlur(globalDefaults.antiBlur) }
                        )

                                    }

                                    GameMenuSectionId.FIXES_HARDWARE -> {

                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_hardware_fixes).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_cpu_sprite_render_size),
                    options = (0..10).map { value ->
                        value to if (value == 0) {
                            stringResource(R.string.settings_disabled_short)
                        } else {
                            value.toString()
                        }
                    },
                    currentValue = uiState.cpuSpriteRenderSize,
                    onValueChange = onSetCpuSpriteRenderSize,
                    helpText = stringResource(R.string.settings_help_cpu_sprite_render_size),
                    onResetToDefault = { onSetCpuSpriteRenderSize(globalDefaults.cpuSpriteRenderSize) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_cpu_sprite_render_level),
                    options = listOf(
                        0 to stringResource(R.string.settings_cpu_sprite_render_level_sprites),
                        1 to stringResource(R.string.settings_cpu_sprite_render_level_triangles),
                        2 to stringResource(R.string.settings_cpu_sprite_render_level_blended)
                    ),
                    currentValue = uiState.cpuSpriteRenderLevel,
                    onValueChange = onSetCpuSpriteRenderLevel,
                    helpText = stringResource(R.string.settings_help_cpu_sprite_render_level),
                    onResetToDefault = { onSetCpuSpriteRenderLevel(globalDefaults.cpuSpriteRenderLevel) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_software_clut_render),
                    options = listOf(
                        0 to stringResource(R.string.settings_disabled_short),
                        1 to stringResource(R.string.settings_normal_short),
                        2 to stringResource(R.string.settings_aggressive_short)
                    ),
                    currentValue = uiState.softwareClutRender,
                    onValueChange = onSetSoftwareClutRender,
                    helpText = stringResource(R.string.settings_help_software_clut_render),
                    onResetToDefault = { onSetSoftwareClutRender(globalDefaults.softwareClutRender) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_gpu_target_clut),
                    options = listOf(
                        0 to stringResource(R.string.settings_hw_download_mode_disabled),
                        1 to stringResource(R.string.settings_gpu_target_clut_exact),
                        2 to stringResource(R.string.settings_gpu_target_clut_inside)
                    ),
                    currentValue = uiState.gpuTargetClutMode,
                    onValueChange = onSetGpuTargetClutMode,
                    helpText = stringResource(R.string.settings_help_gpu_target_clut),
                    onResetToDefault = { onSetGpuTargetClutMode(globalDefaults.gpuTargetClutMode) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_auto_flush_hardware),
                    options = listOf(
                        0 to stringResource(R.string.settings_hw_download_mode_disabled),
                        1 to stringResource(R.string.settings_auto_flush_sprites),
                        2 to stringResource(R.string.settings_auto_flush_all)
                    ),
                    currentValue = uiState.autoFlushHardware,
                    onValueChange = onSetAutoFlushHardware,
                    helpText = stringResource(R.string.settings_help_auto_flush_hardware),
                    onResetToDefault = { onSetAutoFlushHardware(globalDefaults.autoFlushHardware) }
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_skip_draw_start),
                    valueLabelForValue = { it.toString() },
                    value = uiState.skipDrawStart.toFloat(),
                    range = 0f..100f,
                    steps = 99,
                    onValueChange = { onSetSkipDrawStart(it.toInt()) },
                    helpText = stringResource(R.string.settings_help_skip_draw_start),
                    onResetToDefault = { onSetSkipDrawStart(globalDefaults.skipDrawStart) }
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_skip_draw_end),
                    valueLabelForValue = { it.toString() },
                    value = uiState.skipDrawEnd.toFloat(),
                    range = 0f..100f,
                    steps = 99,
                    onValueChange = { onSetSkipDrawEnd(it.toInt()) },
                    helpText = stringResource(R.string.settings_help_skip_draw_end),
                    onResetToDefault = { onSetSkipDrawEnd(globalDefaults.skipDrawEnd) }
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_cpu_framebuffer_conversion),
                    checked = uiState.cpuFramebufferConversion,
                    onCheckedChange = onSetCpuFramebufferConversion,
                    helpText = stringResource(R.string.settings_help_cpu_framebuffer_conversion),
                    onResetToDefault = { onSetCpuFramebufferConversion(globalDefaults.cpuFramebufferConversion) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_depth_conversion),
                    checked = uiState.disableDepthConversion,
                    onCheckedChange = onSetDisableDepthConversion,
                    helpText = stringResource(R.string.settings_help_disable_depth_conversion),
                    onResetToDefault = { onSetDisableDepthConversion(globalDefaults.disableDepthConversion) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_safe_features),
                    checked = uiState.disableSafeFeatures,
                    onCheckedChange = onSetDisableSafeFeatures,
                    helpText = stringResource(R.string.settings_help_disable_safe_features),
                    onResetToDefault = { onSetDisableSafeFeatures(globalDefaults.disableSafeFeatures) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_render_fixes),
                    checked = uiState.disableRenderFixes,
                    onCheckedChange = onSetDisableRenderFixes,
                    helpText = stringResource(R.string.settings_help_disable_render_fixes),
                    onResetToDefault = { onSetDisableRenderFixes(globalDefaults.disableRenderFixes) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_preload_frame_data),
                    checked = uiState.preloadFrameData,
                    onCheckedChange = onSetPreloadFrameData,
                    helpText = stringResource(R.string.settings_help_preload_frame_data),
                    onResetToDefault = { onSetPreloadFrameData(globalDefaults.preloadFrameData) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_partial_invalidation),
                    checked = uiState.disablePartialInvalidation,
                    onCheckedChange = onSetDisablePartialInvalidation,
                    helpText = stringResource(R.string.settings_help_disable_partial_invalidation),
                    onResetToDefault = { onSetDisablePartialInvalidation(globalDefaults.disablePartialInvalidation) }
                )
                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_texture_inside_rt),
                    options = listOf(
                        0 to stringResource(R.string.settings_hw_download_mode_disabled),
                        1 to stringResource(R.string.settings_texture_inside_rt_inside),
                        2 to stringResource(R.string.settings_texture_inside_rt_merge)
                    ),
                    currentValue = uiState.textureInsideRt,
                    onValueChange = onSetTextureInsideRt,
                    helpText = stringResource(R.string.settings_help_texture_inside_rt),
                    onResetToDefault = { onSetTextureInsideRt(globalDefaults.textureInsideRt) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_read_targets_on_close),
                    checked = uiState.readTargetsOnClose,
                    onCheckedChange = onSetReadTargetsOnClose,
                    helpText = stringResource(R.string.settings_help_read_targets_on_close),
                    onResetToDefault = { onSetReadTargetsOnClose(globalDefaults.readTargetsOnClose) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_estimate_texture_region),
                    checked = uiState.estimateTextureRegion,
                    onCheckedChange = onSetEstimateTextureRegion,
                    helpText = stringResource(R.string.settings_help_estimate_texture_region),
                    onResetToDefault = { onSetEstimateTextureRegion(globalDefaults.estimateTextureRegion) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_gpu_palette_conversion),
                    checked = uiState.gpuPaletteConversion,
                    onCheckedChange = onSetGpuPaletteConversion,
                    helpText = stringResource(R.string.settings_help_gpu_palette_conversion),
                    onResetToDefault = { onSetGpuPaletteConversion(globalDefaults.gpuPaletteConversion) }
                )

                                    }

                                    GameMenuSectionId.FIXES_UPSCALING -> {

                SidebarSectionTitle(
                    text = stringResource(R.string.settings_upscaling_fixes).uppercase(),
                    color = sectionTitleColor,
                    topPadding = sectionLabelTopPadding,
                    horizontalInset = sectionLabelInset
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_half_pixel_offset),
                    options = listOf(
                        0 to stringResource(R.string.settings_half_pixel_off),
                        1 to stringResource(R.string.settings_half_pixel_normal),
                        2 to stringResource(R.string.settings_half_pixel_special),
                        3 to stringResource(R.string.settings_half_pixel_special_aggressive),
                        4 to stringResource(R.string.settings_half_pixel_native),
                        5 to stringResource(R.string.settings_half_pixel_native_tex)
                    ),
                    currentValue = uiState.halfPixelOffset,
                    onValueChange = onSetHalfPixelOffset,
                    helpText = stringResource(R.string.settings_help_half_pixel_offset),
                    onResetToDefault = { onSetHalfPixelOffset(globalDefaults.halfPixelOffset) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_native_scaling),
                    options = listOf(
                        0 to stringResource(R.string.settings_native_scaling_off),
                        1 to stringResource(R.string.settings_native_scaling_normal),
                        2 to stringResource(R.string.settings_native_scaling_aggressive),
                        3 to stringResource(R.string.settings_native_scaling_normal_maintain_upscale),
                        4 to stringResource(R.string.settings_native_scaling_aggressive_maintain_upscale)
                    ),
                    currentValue = uiState.nativeScaling,
                    onValueChange = onSetNativeScaling,
                    helpText = stringResource(R.string.settings_help_native_scaling),
                    onResetToDefault = { onSetNativeScaling(globalDefaults.nativeScaling) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_round_sprite),
                    options = listOf(
                        0 to stringResource(R.string.settings_half_pixel_off),
                        1 to stringResource(R.string.settings_round_sprite_half),
                        2 to stringResource(R.string.settings_round_sprite_full)
                    ),
                    currentValue = uiState.roundSprite,
                    onValueChange = onSetRoundSprite,
                    helpText = stringResource(R.string.settings_help_round_sprite),
                    onResetToDefault = { onSetRoundSprite(globalDefaults.roundSprite) }
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_bilinear_upscale),
                    options = listOf(
                        0 to stringResource(R.string.settings_trilinear_filtering_auto),
                        1 to stringResource(R.string.settings_bilinear_upscale_force_bilinear),
                        2 to stringResource(R.string.settings_bilinear_upscale_force_nearest)
                    ),
                    currentValue = uiState.bilinearUpscale,
                    onValueChange = onSetBilinearUpscale,
                    helpText = stringResource(R.string.settings_help_bilinear_upscale),
                    onResetToDefault = { onSetBilinearUpscale(globalDefaults.bilinearUpscale) }
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_texture_offset_x),
                    valueLabelForValue = { it.toString() },
                    value = uiState.textureOffsetX.toFloat(),
                    range = -512f..512f,
                    steps = 1023,
                    onValueChange = { onSetTextureOffsetX(it.toInt()) },
                    helpText = stringResource(R.string.settings_help_texture_offset_x),
                    onResetToDefault = { onSetTextureOffsetX(globalDefaults.textureOffsetX) }
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_texture_offset_y),
                    valueLabelForValue = { it.toString() },
                    value = uiState.textureOffsetY.toFloat(),
                    range = -512f..512f,
                    steps = 1023,
                    onValueChange = { onSetTextureOffsetY(it.toInt()) },
                    helpText = stringResource(R.string.settings_help_texture_offset_y),
                    onResetToDefault = { onSetTextureOffsetY(globalDefaults.textureOffsetY) }
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_align_sprite),
                    checked = uiState.alignSprite,
                    onCheckedChange = onSetAlignSprite,
                    helpText = stringResource(R.string.settings_help_align_sprite),
                    onResetToDefault = { onSetAlignSprite(globalDefaults.alignSprite) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_merge_sprite),
                    checked = uiState.mergeSprite,
                    onCheckedChange = onSetMergeSprite,
                    helpText = stringResource(R.string.settings_help_merge_sprite),
                    onResetToDefault = { onSetMergeSprite(globalDefaults.mergeSprite) }
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_force_even_sprite_position),
                    checked = uiState.forceEvenSpritePosition,
                    onCheckedChange = onSetForceEvenSpritePosition,
                    helpText = stringResource(R.string.settings_help_force_even_sprite_position),
                    onResetToDefault = { onSetForceEvenSpritePosition(globalDefaults.forceEvenSpritePosition) }
                )
                        SettingsToggle(
                            title = stringResource(R.string.settings_native_palette_draw),
                            checked = uiState.nativePaletteDraw,
                            onCheckedChange = onSetNativePaletteDraw,
                            helpText = stringResource(R.string.settings_help_native_palette_draw),
                            onResetToDefault = { onSetNativePaletteDraw(globalDefaults.nativePaletteDraw) }
                        )
                                    }

                                    else -> Unit
                                }
                            }
                    }

                    EmulationMenuTab.Achievements -> {
                        if (GameMenuSectionId.ACHIEVEMENTS_PROGRESS !in uiState.hiddenGameMenuSections) {
                            OverlayAchievementsPane(
                                gamePath = currentGamePath,
                                currentGameTitle = uiState.currentGameTitle,
                                retroState = retroState
                            )
                        }
                    }
                }
    }

    GameMenuLayoutHost(
        style = uiState.gameMenuLayoutStyle,
        modifier = modifier,
        menuTabs = menuTabs,
        selectedMenuTab = selectedMenuTab,
        selectedTabScrollState = selectedTabScrollState,
        railFocusRequesters = railFocusRequesters,
        contentBottomPadding = drawerBottomPadding + animatedBottomInset,
        rightInset = animatedRightInset,
        sectionSpacing = sectionSpacing,
        onSelectTab = { selectedMenuTabName = it.name },
        onClose = onClose,
        content = menuContent
    )
}

@Composable
private fun GameMenuLayoutHost(
    style: GameMenuLayoutStyle,
    modifier: Modifier,
    menuTabs: List<EmulationMenuTab>,
    selectedMenuTab: EmulationMenuTab,
    selectedTabScrollState: ScrollState,
    railFocusRequesters: Map<EmulationMenuTab, FocusRequester>,
    contentBottomPadding: Dp,
    rightInset: Dp,
    sectionSpacing: Dp,
    onSelectTab: (EmulationMenuTab) -> Unit,
    onClose: () -> Unit,
    content: @Composable ColumnScope.() -> Unit
) {
    val safeModifier = modifier
        .padding(WindowInsets.displayCutout.asPaddingValues())
        .padding(WindowInsets.statusBars.asPaddingValues())

    when (style) {
        GameMenuLayoutStyle.SIDEBAR -> {
            Row(
                modifier = safeModifier.padding(end = 16.dp + rightInset, top = 16.dp, bottom = 16.dp),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.Top
            ) {
                GameMenuContentSurface(
                    modifier = Modifier
                        .fillMaxHeight()
                        .widthIn(min = 300.dp, max = 420.dp),
                    shape = RoundedCornerShape(28.dp),
                    scrollState = selectedTabScrollState,
                    bottomPadding = contentBottomPadding,
                    sectionSpacing = sectionSpacing,
                    content = content
                )
                Spacer(modifier = Modifier.width(12.dp))
                GameMenuVerticalNavigation(
                    modifier = Modifier
                        .fillMaxHeight()
                        .width(74.dp),
                    menuTabs = menuTabs,
                    selectedMenuTab = selectedMenuTab,
                    railFocusRequesters = railFocusRequesters,
                    showLabels = false,
                    onSelectTab = onSelectTab,
                    onClose = onClose
                )
            }
        }

        GameMenuLayoutStyle.DASHBOARD -> {
            Box(
                modifier = safeModifier
                    .fillMaxSize()
                    .padding(end = rightInset, top = 16.dp, bottom = 16.dp),
                contentAlignment = Alignment.Center
            ) {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth(0.94f)
                        .fillMaxHeight(),
                    shape = RoundedCornerShape(24.dp),
                    color = MaterialTheme.colorScheme.surface.copy(alpha = 0.98f),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
                    shadowElevation = 12.dp
                ) {
                    Row(modifier = Modifier.fillMaxSize()) {
                        GameMenuVerticalNavigation(
                            modifier = Modifier
                                .fillMaxHeight()
                                .width(196.dp),
                            menuTabs = menuTabs,
                            selectedMenuTab = selectedMenuTab,
                            railFocusRequesters = railFocusRequesters,
                            showLabels = true,
                            onSelectTab = onSelectTab,
                            onClose = onClose
                        )
                        GameMenuContentColumn(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxHeight(),
                            scrollState = selectedTabScrollState,
                            bottomPadding = 18.dp,
                            sectionSpacing = sectionSpacing,
                            content = content
                        )
                    }
                }
            }
        }

        GameMenuLayoutStyle.COMMAND_CENTER -> {
            Box(
                modifier = safeModifier
                    .fillMaxSize()
                    .padding(end = rightInset, top = 16.dp, bottom = 16.dp),
                contentAlignment = Alignment.BottomCenter
            ) {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth(0.96f)
                        .fillMaxHeight(),
                    shape = RoundedCornerShape(topStart = 30.dp, topEnd = 30.dp, bottomStart = 18.dp, bottomEnd = 18.dp),
                    color = MaterialTheme.colorScheme.surface.copy(alpha = 0.98f),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.50f)),
                    shadowElevation = 14.dp
                ) {
                    Column(modifier = Modifier.fillMaxSize()) {
                        GameMenuHorizontalNavigation(
                            menuTabs = menuTabs,
                            selectedMenuTab = selectedMenuTab,
                            railFocusRequesters = railFocusRequesters,
                            iconOnly = false,
                            onSelectTab = onSelectTab,
                            onClose = onClose
                        )
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                        GameMenuContentColumn(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxWidth(),
                            scrollState = selectedTabScrollState,
                            bottomPadding = 18.dp,
                            sectionSpacing = sectionSpacing,
                            content = content
                        )
                    }
                }
            }
        }

        GameMenuLayoutStyle.COMPACT -> {
            Box(
                modifier = safeModifier
                    .fillMaxSize()
                    .padding(end = 12.dp + rightInset, top = 16.dp, bottom = 16.dp),
                contentAlignment = Alignment.CenterEnd
            ) {
                Surface(
                    modifier = Modifier
                        .fillMaxHeight()
                        .widthIn(min = 300.dp, max = 352.dp),
                    shape = RoundedCornerShape(18.dp),
                    color = MaterialTheme.colorScheme.surface.copy(alpha = 0.95f),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)),
                    shadowElevation = 10.dp
                ) {
                    Column(modifier = Modifier.fillMaxSize()) {
                        GameMenuHorizontalNavigation(
                            menuTabs = menuTabs,
                            selectedMenuTab = selectedMenuTab,
                            railFocusRequesters = railFocusRequesters,
                            iconOnly = true,
                            pinClose = true,
                            onSelectTab = onSelectTab,
                            onClose = onClose
                        )
                        GameMenuContentColumn(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxWidth(),
                            scrollState = selectedTabScrollState,
                            bottomPadding = contentBottomPadding,
                            sectionSpacing = 10.dp,
                            horizontalPadding = 12.dp,
                            verticalPadding = 12.dp,
                            content = content
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun GameMenuContentSurface(
    modifier: Modifier,
    shape: RoundedCornerShape,
    scrollState: ScrollState,
    bottomPadding: Dp,
    sectionSpacing: Dp,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = modifier,
        shape = shape,
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f))
    ) {
        GameMenuContentColumn(
            scrollState = scrollState,
            bottomPadding = bottomPadding,
            sectionSpacing = sectionSpacing,
            content = content
        )
    }
}

@Composable
private fun GameMenuContentColumn(
    modifier: Modifier = Modifier,
    scrollState: ScrollState,
    bottomPadding: Dp,
    sectionSpacing: Dp,
    horizontalPadding: Dp = 18.dp,
    verticalPadding: Dp = 18.dp,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(horizontal = horizontalPadding, vertical = verticalPadding)
            .padding(bottom = bottomPadding),
        verticalArrangement = Arrangement.spacedBy(sectionSpacing),
        content = content
    )
}

@Composable
private fun GameMenuVerticalNavigation(
    modifier: Modifier,
    menuTabs: List<EmulationMenuTab>,
    selectedMenuTab: EmulationMenuTab,
    railFocusRequesters: Map<EmulationMenuTab, FocusRequester>,
    showLabels: Boolean,
    onSelectTab: (EmulationMenuTab) -> Unit,
    onClose: () -> Unit
) {
    Surface(
        modifier = modifier,
        shape = if (showLabels) RoundedCornerShape(topStart = 24.dp, bottomStart = 24.dp) else RoundedCornerShape(24.dp),
        color = if (showLabels) {
            MaterialTheme.colorScheme.surfaceContainerLow
        } else {
            MaterialTheme.colorScheme.surface.copy(alpha = 0.72f)
        },
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.48f))
    ) {
        val railScrollState = rememberScrollState()
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(railScrollState)
                .padding(vertical = 16.dp, horizontal = if (showLabels) 12.dp else 10.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            menuTabs.forEach { tab ->
                if (showLabels) {
                    GameMenuNavigationTile(
                        modifier = Modifier.focusRequester(railFocusRequesters.getValue(tab)),
                        tab = tab,
                        selected = selectedMenuTab == tab,
                        iconOnly = false,
                        onClick = { onSelectTab(tab) }
                    )
                } else {
                    EmulationMenuRailButton(
                        modifier = Modifier.focusRequester(railFocusRequesters.getValue(tab)),
                        icon = gameMenuTabIcon(tab),
                        contentDescription = gameMenuTabLabel(tab),
                        selected = selectedMenuTab == tab,
                        onClick = { onSelectTab(tab) }
                    )
                }
            }
            Spacer(modifier = Modifier.weight(1f))
            if (showLabels) {
                GameMenuNavigationTile(
                    icon = Icons.AutoMirrored.Rounded.ExitToApp,
                    label = stringResource(R.string.emulation_close_menu),
                    selected = false,
                    destructive = true,
                    iconOnly = false,
                    onClick = onClose
                )
            } else {
                EmulationMenuRailButton(
                    icon = Icons.AutoMirrored.Rounded.ExitToApp,
                    contentDescription = stringResource(R.string.emulation_close_menu),
                    selected = false,
                    onClick = onClose,
                    isDestructive = true
                )
            }
        }
    }
}

@Composable
private fun GameMenuHorizontalNavigation(
    menuTabs: List<EmulationMenuTab>,
    selectedMenuTab: EmulationMenuTab,
    railFocusRequesters: Map<EmulationMenuTab, FocusRequester>,
    iconOnly: Boolean,
    pinClose: Boolean = false,
    onSelectTab: (EmulationMenuTab) -> Unit,
    onClose: () -> Unit
) {
    if (pinClose) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            LazyRow(
                modifier = Modifier.weight(1f),
                contentPadding = PaddingValues(start = 12.dp, end = 8.dp, top = 10.dp, bottom = 10.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                items(menuTabs, key = { it.name }) { tab ->
                    GameMenuNavigationTile(
                        modifier = Modifier.focusRequester(railFocusRequesters.getValue(tab)),
                        tab = tab,
                        selected = selectedMenuTab == tab,
                        iconOnly = iconOnly,
                        fillWidth = false,
                        onClick = { onSelectTab(tab) }
                    )
                }
            }
            GameMenuNavigationTile(
                modifier = Modifier.padding(end = 12.dp),
                icon = Icons.AutoMirrored.Rounded.ExitToApp,
                label = stringResource(R.string.emulation_close_menu),
                selected = false,
                destructive = true,
                iconOnly = iconOnly,
                fillWidth = false,
                onClick = onClose
            )
        }
        return
    }

    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        items(menuTabs, key = { it.name }) { tab ->
            GameMenuNavigationTile(
                modifier = Modifier.focusRequester(railFocusRequesters.getValue(tab)),
                tab = tab,
                selected = selectedMenuTab == tab,
                iconOnly = iconOnly,
                fillWidth = false,
                onClick = { onSelectTab(tab) }
            )
        }
        item(key = "close") {
            GameMenuNavigationTile(
                icon = Icons.AutoMirrored.Rounded.ExitToApp,
                label = stringResource(R.string.emulation_close_menu),
                selected = false,
                destructive = true,
                iconOnly = iconOnly,
                fillWidth = false,
                onClick = onClose
            )
        }
    }
}

@Composable
private fun GameMenuNavigationTile(
    modifier: Modifier = Modifier,
    tab: EmulationMenuTab? = null,
    icon: ImageVector? = null,
    label: String? = null,
    selected: Boolean,
    destructive: Boolean = false,
    iconOnly: Boolean,
    fillWidth: Boolean = !iconOnly,
    onClick: () -> Unit
) {
    val resolvedIcon = icon ?: tab?.let(::gameMenuTabIcon) ?: Icons.Rounded.Menu
    val resolvedLabel = label ?: tab?.let { gameMenuTabLabel(it) }.orEmpty()
    val shape = RoundedCornerShape(if (iconOnly) 14.dp else 16.dp)
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    Surface(
        modifier = modifier
            .then(if (fillWidth) Modifier.fillMaxWidth() else Modifier)
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick)
            .focusable(interactionSource = interactionSource),
        shape = shape,
        color = when {
            destructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.12f)
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.26f)
        },
        border = BorderStroke(
            if (isFocused) 2.dp else 1.dp,
            when {
                isFocused -> MaterialTheme.colorScheme.primary
                destructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.24f)
                selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.30f)
                else -> MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            }
        )
    ) {
        Row(
            modifier = Modifier.padding(
                horizontal = if (iconOnly) 13.dp else 14.dp,
                vertical = if (iconOnly) 12.dp else 13.dp
            ),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = resolvedIcon,
                contentDescription = resolvedLabel,
                tint = when {
                    destructive -> MaterialTheme.colorScheme.error
                    selected -> MaterialTheme.colorScheme.primary
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                },
                modifier = Modifier.size(21.dp)
            )
            if (!iconOnly) {
                Text(
                    text = resolvedLabel,
                    style = MaterialTheme.typography.labelLarge,
                    color = if (destructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

private fun gameMenuTabIcon(tab: EmulationMenuTab): ImageVector = when (tab) {
    EmulationMenuTab.Session -> Icons.Rounded.Menu
    EmulationMenuTab.Controls -> Icons.Rounded.Gamepad
    EmulationMenuTab.Emulation -> Icons.Rounded.SettingsSuggest
    EmulationMenuTab.Graphics -> Icons.Rounded.Fullscreen
    EmulationMenuTab.Fixes -> Icons.Rounded.Star
    EmulationMenuTab.Achievements -> Icons.Rounded.LockOpen
}

@Composable
private fun gameMenuTabLabel(tab: EmulationMenuTab): String = when (tab) {
    EmulationMenuTab.Session -> stringResource(R.string.emulation_session_tab)
    EmulationMenuTab.Controls -> stringResource(R.string.settings_controls_tab)
    EmulationMenuTab.Emulation -> stringResource(R.string.settings_emulation_tab)
    EmulationMenuTab.Graphics -> stringResource(R.string.settings_graphics_tab)
    EmulationMenuTab.Fixes -> stringResource(R.string.settings_fixes_tab)
    EmulationMenuTab.Achievements -> stringResource(R.string.emulation_achievements_tab)
}

@Composable
private fun OverlayAchievementsPane(
    gamePath: String?,
    currentGameTitle: String,
    retroState: com.sbro.emucorex.core.utils.RetroAchievementsLiveUiState
) {
    val context = LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val cachedActiveGameData = remember(
        gamePath,
        currentGameTitle,
        retroState.enabled,
        retroState.user?.username,
        retroState.game?.gameId
    ) {
        repository.peekCachedOverlayGameData(
            gamePath = gamePath,
            gameTitle = retroState.game?.title ?: currentGameTitle,
            gameId = retroState.game?.gameId
        )
    }
    val contentState by produceState(
        initialValue = OverlayAchievementsContentState(
            isLoading = cachedActiveGameData == null && gamePath != null && retroState.enabled && retroState.user != null,
            gameData = cachedActiveGameData
        ),
        gamePath,
        currentGameTitle,
        retroState.enabled,
        retroState.user?.username,
        retroState.game?.gameId
    ) {
        if (gamePath.isNullOrBlank() || !retroState.enabled || retroState.user == null) {
            value = OverlayAchievementsContentState(isLoading = false, gameData = null)
            return@produceState
        }
        val cachedData = repository.peekCachedOverlayGameData(
            gamePath = gamePath,
            gameTitle = retroState.game?.title ?: currentGameTitle,
            gameId = retroState.game?.gameId
        )
        value = if (cachedData != null) {
            OverlayAchievementsContentState(isLoading = false, gameData = cachedData)
        } else {
            OverlayAchievementsContentState(isLoading = true, gameData = null)
        }
        value = withContext(Dispatchers.IO) {
            val loadedData = loadOverlayRetroAchievementsGameDataWithFallback(
                repository = repository,
                gamePath = gamePath,
                gameTitle = currentGameTitle,
                allowRetry = cachedData == null
            )
                ?: cachedData
            repository.cacheOverlayGameData(
                gamePath = gamePath,
                gameTitle = retroState.game?.title ?: currentGameTitle,
                data = loadedData
            )
            OverlayAchievementsContentState(
                isLoading = false,
                gameData = loadedData
            )
        }
    }

    SidebarSectionTitle(
        text = stringResource(R.string.emulation_achievements_tab).uppercase(),
        color = MaterialTheme.colorScheme.primary,
        topPadding = 8.dp,
        horizontalInset = 4.dp
    )

    when {
        gamePath.isNullOrBlank() -> {
            OverlayAchievementsNotice(stringResource(R.string.emulation_achievements_bios_unavailable))
        }

        !retroState.isSupported -> {
            OverlayAchievementsNotice(stringResource(R.string.emulation_achievements_not_supported))
        }

        !retroState.enabled -> {
            OverlayAchievementsNotice(stringResource(R.string.settings_ra_empty_disabled))
        }

        retroState.user == null -> {
            OverlayAchievementsNotice(stringResource(R.string.achievements_login_to_sync))
        }

        contentState.isLoading -> {
            OverlayAchievementsNotice(stringResource(R.string.achievements_loading))
        }

        contentState.gameData == null && retroState.game == null -> {
            OverlayAchievementsNotice(stringResource(R.string.emulation_achievements_unavailable))
        }

        else -> {
            val gameData = contentState.gameData
            val liveGame = retroState.game
            val subtitle = liveGame?.richPresence?.takeIf { it.isNotBlank() }
                ?: gameData?.title
                ?: currentGameTitle
            var showAllAchievements by remember(gameData?.gameId) { mutableStateOf(false) }
            val sortedAchievements = remember(gameData) {
                gameData?.achievements
                    ?.sortedWith(
                        compareByDescending<RetroAchievementEntry> { it.isEarned }
                            .thenBy { it.title.lowercase() }
                    )
                    .orEmpty()
            }
            val visibleAchievements = remember(sortedAchievements, showAllAchievements) {
                if (showAllAchievements) sortedAchievements else sortedAchievements.take(8)
            }
            val headerTitle = liveGame?.title?.ifBlank { null }
                ?: gameData?.title?.ifBlank { null }
                ?: currentGameTitle.ifBlank { stringResource(R.string.emulation_achievements_tab) }
            val earnedCount = liveGame?.earnedAchievements ?: gameData?.earnedCount ?: 0
            val totalCount = liveGame?.totalAchievements ?: gameData?.totalCount ?: 0
            val earnedPoints = liveGame?.earnedPoints ?: gameData?.earnedPoints ?: 0
            val totalPoints = liveGame?.totalPoints ?: gameData?.totalPoints ?: 0

            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Text(
                        text = headerTitle,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    subtitle.takeIf { it.isNotBlank() && it != headerTitle }?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        OverlayAchievementsMetricCard(
                            modifier = Modifier.weight(1f),
                            value = "$earnedCount/$totalCount",
                            label = stringResource(R.string.settings_ra_achievements_label)
                        )
                        OverlayAchievementsMetricCard(
                            modifier = Modifier.weight(1f),
                            value = "$earnedPoints/$totalPoints",
                            label = stringResource(R.string.settings_ra_points_label)
                        )
                    }
                    if (visibleAchievements.isEmpty()) {
                        OverlayAchievementsNotice(stringResource(R.string.achievements_game_empty))
                    } else {
                        visibleAchievements.forEach { achievement ->
                            OverlayAchievementRow(achievement = achievement)
                        }
                        val hiddenCount = sortedAchievements.size - visibleAchievements.size
                        if (hiddenCount > 0) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = stringResource(R.string.emulation_achievements_more, hiddenCount),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.weight(1f)
                                )
                                TextButton(onClick = { showAllAchievements = true }) {
                                    Text(text = stringResource(R.string.emulation_achievements_show_all))
                                }
                            }
                        } else if (showAllAchievements && sortedAchievements.size > 8) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.End
                            ) {
                                TextButton(onClick = { showAllAchievements = false }) {
                                    Text(text = stringResource(R.string.emulation_achievements_show_less))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

private suspend fun loadActiveRetroAchievementsGameDataWithRetry(
    repository: RetroAchievementsRepository,
    allowRetry: Boolean
): RetroAchievementGameData? {
    val attempts = if (allowRetry) 8 else 1
    repeat(attempts) { attempt ->
        val data = runCatching { repository.loadActiveGameData() }.getOrNull()
        if (data != null && (data.achievements.isNotEmpty() || data.totalCount > 0)) {
            return data
        }
        if (attempt < attempts - 1) {
            delay(500.milliseconds)
            RetroAchievementsLiveStateManager.refreshFromNative()
        }
    }
    return null
}

private suspend fun loadOverlayRetroAchievementsGameDataWithFallback(
    repository: RetroAchievementsRepository,
    gamePath: String,
    gameTitle: String,
    allowRetry: Boolean
): RetroAchievementGameData? {
    val activeData = loadActiveRetroAchievementsGameDataWithRetry(repository, allowRetry)
    if (activeData != null && (activeData.achievements.isNotEmpty() || activeData.totalCount > 0)) {
        return activeData
    }

    return runCatching { repository.loadGameData(gamePath) }.getOrNull()
        ?: gameTitle.takeIf { it.isNotBlank() }?.let { title ->
            runCatching { repository.loadGameData(title) }.getOrNull()
        }
}

@Composable
private fun OverlayAchievementsMetricCard(
    modifier: Modifier = Modifier,
    value: String,
    label: String
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.82f)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = value,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = label,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun OverlayAchievementsNotice(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.36f)
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun OverlayAchievementRow(achievement: RetroAchievementEntry) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.80f)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 10.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            BitmapPathImage(
                imagePath = if (achievement.isEarned) {
                    achievement.badgeUrl ?: achievement.badgeLockedUrl
                } else {
                    achievement.badgeLockedUrl ?: achievement.badgeUrl
                },
                contentDescription = achievement.title,
                modifier = Modifier
                    .size(42.dp)
                    .clip(RoundedCornerShape(12.dp)),
                fallback = {
                    Icon(
                        imageVector = Icons.Rounded.Star,
                        contentDescription = null,
                        tint = if (achievement.isEarned) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            )
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = achievement.title,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = achievement.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (achievement.hasMeasuredProgress) {
                    Text(
                        text = stringResource(
                            R.string.achievements_measured_progress,
                            achievement.measuredProgress.orEmpty()
                        ),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            Column(
                horizontalAlignment = Alignment.End,
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = achievement.points.toString(),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.primary
                )
                Text(
                    text = stringResource(
                        when {
                            achievement.isPrimed -> R.string.achievements_status_primed
                            achievement.isEarned -> R.string.achievements_status_earned
                            else -> R.string.achievements_status_locked
                        }
                    ),
                    style = MaterialTheme.typography.labelSmall,
                    color = if (achievement.isEarned) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun EmulationMenuRailButton(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    contentDescription: String,
    selected: Boolean,
    onClick: () -> Unit,
    isDestructive: Boolean = false
) {
    val shape = RoundedCornerShape(18.dp)
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    val iconScale by animateFloatAsState(
        targetValue = if (selected && !isDestructive) 1.08f else 1f,
        animationSpec = tween(durationMillis = 220),
        label = "emulation_menu_rail_icon_scale"
    )
    val border = BorderStroke(
        if (isFocused) 2.dp else 1.dp,
        when {
            isFocused -> MaterialTheme.colorScheme.primary.copy(alpha = 0.95f)
            isDestructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.22f)
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.30f)
            else -> MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.52f)
        }
    )
    Surface(
        modifier = modifier
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
            .focusable(interactionSource = interactionSource),
        shape = shape,
        color = when {
            isDestructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.12f)
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
        },
        border = border
    ) {
        Box(
            modifier = Modifier
                .size(52.dp),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = contentDescription,
                tint = when {
                    isDestructive -> MaterialTheme.colorScheme.error
                    selected -> MaterialTheme.colorScheme.primary
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                },
                modifier = Modifier
                    .size(22.dp)
                    .graphicsLayer(
                        scaleX = iconScale,
                        scaleY = iconScale
                    )
            )
        }
    }
}

@Composable
private fun EmulationGamepadBindingRow(
    title: String,
    value: String,
    autoLabel: String?,
    onBindClick: () -> Unit,
    onClearClick: (() -> Unit)?
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
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
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)),
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

@androidx.annotation.StringRes
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
    else -> stringResource(gamepadActionLabelRes(actionId))
}

@Composable
private fun gamepadPlayerLabel(padIndex: Int): String {
    return stringResource(
        if (padIndex == 0) R.string.settings_gamepad_player_1 else R.string.settings_gamepad_player_2
    )
}

@Composable
private fun SettingsToggle(
    title: String,
    checked: Boolean,
    enabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    val shape = RoundedCornerShape(16.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = enabled,
                onClick = { onCheckedChange(!checked) },
                onLongClick = if (enabled) onResetToDefault?.let {
                    {
                        it()
                        Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                    }
                } else null
            )
            .gamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
                    modifier = Modifier.weight(1f, fill = false),
                    color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant
                )
                helpText?.let {
                    SettingHelpButton(title = title, description = it)
                }
            }
            Switch(
                checked = checked,
                onCheckedChange = null,
                enabled = enabled,
                modifier = Modifier.scale(0.85f),
                colors = androidx.compose.material3.SwitchDefaults.colors(
                    checkedThumbColor = MaterialTheme.colorScheme.primary,
                    checkedTrackColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                )
            )
        }
    }
}

@Composable
private fun SidebarSectionTitle(
    text: String,
    color: Color,
    topPadding: Dp,
    horizontalInset: Dp
) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall.copy(
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 1.sp
        ),
        color = color,
        modifier = Modifier.padding(top = topPadding, start = horizontalInset, end = horizontalInset)
    )
}

@Composable
private fun LiveSelectionRow(
    title: String,
    options: List<LiveSelectionOption>,
    currentValue: Int,
    onValueChange: (Int) -> Unit,
    allowWrap: Boolean = true,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 4.dp)
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
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        if (allowWrap) {
            FlowRow(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                options.forEach { option ->
                    LiveSelectionChip(
                        option = option,
                        selected = option.value == currentValue,
                        onClick = { onValueChange(option.value) }
                    )
                }
            }
        } else {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                options.forEach { option ->
                    val selected = option.value == currentValue
                    Surface(
                        modifier = Modifier.weight(1f),
                        onClick = { onValueChange(option.value) },
                        shape = RoundedCornerShape(12.dp),
                        color = if (selected) {
                            MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
                        } else {
                            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f)
                        },
                        border = BorderStroke(
                            1.dp,
                            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.28f)
                            else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
                        )
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 6.dp, vertical = 10.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            if (option.icon != null) {
                                Icon(
                                    imageVector = option.icon,
                                    contentDescription = option.contentDescription,
                                    tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.size(18.dp)
                                )
                            } else {
                                Text(
                                    text = option.label.orEmpty(),
                                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                                    maxLines = 1,
                                    softWrap = false,
                                    overflow = TextOverflow.Clip,
                                    color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun LiveSelectionChip(
    option: LiveSelectionOption,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    FilterChip(
        modifier = modifier,
        selected = selected,
        onClick = onClick,
        leadingIcon = option.icon?.let { icon ->
            {
                Icon(
                    imageVector = icon,
                    contentDescription = option.contentDescription,
                    modifier = Modifier.size(18.dp)
                )
            }
        },
        label = {
            Text(
                text = option.label.orEmpty(),
                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                maxLines = 1,
                softWrap = false,
                overflow = TextOverflow.Clip
            )
        }
    )
}

@Composable
private fun SimpleFpsCounter(
    fps: String,
    fontScale: Float
) {
    val safeScale = fontScale.coerceIn(0.75f, 2f)
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(999.dp))
            .background(Color.Black.copy(alpha = 0.42f))
            .border(
                width = 1.dp,
                color = Color.White.copy(alpha = 0.08f),
                shape = RoundedCornerShape(999.dp)
            )
            .padding(horizontal = 10.dp * safeScale, vertical = 5.dp * safeScale)
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = stringResource(R.string.emulation_hud_fps_label),
                style = MaterialTheme.typography.labelSmall.copy(
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 11.sp * safeScale,
                    lineHeight = 14.sp * safeScale,
                    letterSpacing = 0.2.sp
                ),
                color = Color.White.copy(alpha = 0.72f)
            )
            Text(
                text = fps,
                style = MaterialTheme.typography.labelLarge.copy(
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 14.sp * safeScale,
                    lineHeight = 18.sp * safeScale
                ),
                color = if (fps.toFloatOrNull()?.let { it >= 55f } == true) {
                    Color(0xFF50D9A0)
                } else {
                    Color(0xFFFFB85C)
                }
            )
        }
    }
}

@Composable
private fun SystemPerformanceHud(
    speedPercent: Float,
    text: String,
    fixedHeaderLine: String,
    isRightCorner: Boolean = false,
    fontScale: Float,
    metricsMask: Int
) {
    val safeScale = fontScale.coerceIn(0.75f, 2f)
    val lines = remember(text, metricsMask, fixedHeaderLine) {
        buildPerformanceOverlayLayout(text, metricsMask, fixedHeaderLine)
    }
    val mainLines = lines.mainLines
    val bottomLines = lines.bottomLines
    val textAlign = if (isRightCorner) TextAlign.End else TextAlign.Start
    Column(
        modifier = Modifier
            .widthIn(min = 190.dp, max = 330.dp * safeScale.coerceAtMost(1.75f))
            .padding(horizontal = 6.dp * safeScale, vertical = 4.dp * safeScale),
        verticalArrangement = Arrangement.spacedBy(1.dp * safeScale)
    ) {
        mainLines.forEach { line ->
            Text(
                text = buildPerformanceAnnotatedText(line, speedPercent),
                modifier = Modifier.fillMaxWidth(),
                style = MaterialTheme.typography.labelMedium.copy(
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 10.sp * safeScale,
                    lineHeight = 13.sp * safeScale,
                    letterSpacing = 0.sp,
                    shadow = Shadow(
                        color = Color.Black.copy(alpha = 1f),
                        offset = Offset(0f, 0f),
                        blurRadius = 6f * safeScale
                    )
                ),
                color = Color.White.copy(alpha = 0.97f),
                textAlign = textAlign,
                softWrap = true
            )
        }
        if (bottomLines.isNotEmpty()) {
            if (mainLines.isNotEmpty()) {
                Spacer(modifier = Modifier.height(4.dp * safeScale))
            }
            bottomLines.forEach { line ->
                Text(
                    text = buildPerformanceAnnotatedText(line, speedPercent),
                    modifier = Modifier.fillMaxWidth(),
                    style = MaterialTheme.typography.labelMedium.copy(
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Medium,
                        fontSize = 8.5.sp * safeScale,
                        lineHeight = 11.sp * safeScale,
                        letterSpacing = 0.sp,
                        shadow = Shadow(
                            color = Color.Black.copy(alpha = 1f),
                            offset = Offset(0f, 0f),
                            blurRadius = 5f * safeScale
                        )
                    ),
                    color = Color.White.copy(alpha = 0.97f),
                    textAlign = textAlign,
                    softWrap = true
                )
            }
        }
    }
}

private fun buildPerformanceAnnotatedText(text: String, speedPercent: Float): AnnotatedString {
    val speedColor = when {
        speedPercent < 90f -> Color(0xFFFF5A5A)
        speedPercent < 99f -> Color(0xFFFFC04D)
        speedPercent <= 101f -> Color(0xFF7CFF7C)
        speedPercent <= 110f -> Color(0xFF9BE870)
        else -> Color(0xFF59D2FF)
    }
    return buildAnnotatedString {
        append(text)
        addRepeatedStyle(
            text,
            listOf(
                "EmuCoreX", "FPS:", "VPS:", "Speed:", "Target:",
                "Frame:", "GS Queue:", "Res:", "CPU:", "GPU:",
                "EE:", "GS:", "VU:", "SW-", "VRAM:"
            ),
            SpanStyle(color = Color(0xFF9DD7FF))
        )
        addRepeatedStyle(
            text,
            listOf("Vulkan", "OpenGL", "Software", "Null"),
            SpanStyle(color = Color(0xFF9DD7FF))
        )
        addLineValueStyle(text, "Speed:", speedColor)
        addLineValueStyle(text, "FPS:", Color(0xFFB9F7CF), stopAt = " | ")
        addLineValueStyle(text, "VPS:", Color(0xFFB9F7CF), stopAt = " | ")
        addLineValueStyle(text, "Target:", Color(0xFFB9F7CF), stopAt = " | ")
    }
}

private fun AnnotatedString.Builder.addRepeatedStyle(
    text: String,
    tokens: List<String>,
    style: SpanStyle
) {
    tokens.forEach { token ->
        var start = text.indexOf(token)
        while (start >= 0) {
            addStyle(style, start, start + token.length)
            start = text.indexOf(token, startIndex = start + token.length)
        }
    }
}

private fun AnnotatedString.Builder.addLineValueStyle(
    text: String,
    label: String,
    color: Color,
    stopAt: String? = null
) {
    val labelStart = text.indexOf(label)
    if (labelStart < 0) return
    val valueStart = labelStart + label.length
    val lineEnd = text.indexOf('\n', startIndex = valueStart).let { if (it == -1) text.length else it }
    val valueEnd = stopAt
        ?.let { separator -> text.indexOf(separator, startIndex = valueStart).takeIf { it in 0..<lineEnd } }
        ?: lineEnd
    if (valueStart < valueEnd)
        addStyle(SpanStyle(color = color), valueStart, valueEnd)
}

@Composable
private fun LiveChipsSelectionRow(
    title: String,
    options: List<Pair<Int, String>>,
    currentValue: Int,
    onValueChange: (Int) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val context = LocalContext.current
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 4.dp)
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
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        LazyRow(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalViewportBleed(18.dp),
            contentPadding = PaddingValues(horizontal = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(options) { (value, label) ->
                FilterChip(
                    selected = currentValue == value,
                    onClick = { onValueChange(value) },
                    label = { Text(text = label) }
                )
            }
        }
    }
}

@Composable
private fun LiveBitmaskChoiceRow(
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
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 4.dp)
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
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        LazyRow(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalViewportBleed(18.dp),
            contentPadding = PaddingValues(horizontal = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(options) { (metric, label) ->
                FilterChip(
                    selected = PerformanceOverlayMetrics.isEnabled(selectedMask, metric),
                    onClick = { onToggle(metric) },
                    label = { Text(text = label) }
                )
            }
        }
    }
}

private fun Modifier.horizontalViewportBleed(inset: Dp): Modifier = layout { measurable, constraints ->
    val insetPx = inset.roundToPx()
    val expandedMaxWidth = if (constraints.hasBoundedWidth) {
        constraints.maxWidth + (insetPx * 2)
    } else {
        constraints.maxWidth
    }
    val placeable = measurable.measure(constraints.copy(maxWidth = expandedMaxWidth))
    layout(constraints.maxWidth, placeable.height) {
        placeable.placeRelative(-insetPx, 0)
    }
}

@Composable
private fun OverlaySubsectionLabel(
    text: String,
    modifier: Modifier = Modifier
) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
        color = MaterialTheme.colorScheme.primary,
        modifier = modifier.padding(top = 4.dp)
    )
}

@Composable
private fun LiveSliderRow(
    title: String,
    @androidx.annotation.StringRes valueLabelResId: Int? = null,
    valueLabelForValue: (Int) -> String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    steps: Int,
    enabled: Boolean = true,
    onValueChange: (Float) -> Unit,
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

    val displayValue = sliderValue.roundToInt()

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .then(
                    if (enabled) {
                        Modifier.combinedClickable(
                            interactionSource = interactionSource,
                            indication = null,
                            onClick = {},
                            onLongClick = onResetToDefault?.let {
                                {
                                    it()
                                    Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                                }
                            }
                        )
                    } else {
                        Modifier
                    }
                ),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                modifier = Modifier.weight(1f),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = if (enabled) {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.48f)
                    },
                    modifier = Modifier.weight(1f, fill = false)
                )
                helpText?.let {
                    SettingHelpButton(title = title, description = it)
                }
            }
            Text(
                text = valueLabelResId?.let { stringResource(it, displayValue) }
                    ?: valueLabelForValue(displayValue),
                style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                color = if (enabled) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.48f)
                }
            )
        }
        Slider(
            value = sliderValue,
            onValueChange = { sliderValue = it },
            onValueChangeFinished = {
                if (enabled) {
                    onValueChange(sliderValue)
                }
            },
            valueRange = range,
            steps = steps,
            enabled = enabled,
            colors = SliderDefaults.colors(
                thumbColor = MaterialTheme.colorScheme.primary,
                activeTrackColor = MaterialTheme.colorScheme.primary
            )
        )
    }
}

@Composable
private fun MenuButton(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    text: String,
    onClick: () -> Unit,
    isDestructive: Boolean = false,
    gradientColors: List<Color>? = null,
    enabled: Boolean = true,
    showProgress: Boolean = false,
    containerColor: Color? = null
) {
    val shape = RoundedCornerShape(16.dp)
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .clickable(
                enabled = enabled,
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
            .gamepadFocusableCard(
                enabled = enabled,
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = when {
            gradientColors != null -> Color.Transparent
            containerColor != null -> containerColor
            isDestructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.15f)
            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
        },
        border = if (gradientColors == null) BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f)) else null
    ) {
        Row(
            modifier = Modifier
                .then(
                    if (gradientColors != null) {
                        Modifier.background(
                            Brush.horizontalGradient(gradientColors.map { it.copy(alpha = 0.22f) }),
                            RoundedCornerShape(16.dp)
                        ).border(1.dp, gradientColors.first().copy(alpha = 0.3f), RoundedCornerShape(16.dp))
                    } else Modifier
                )
                .padding(horizontal = 16.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = when {
                    gradientColors != null -> gradientColors.first()
                    isDestructive -> MaterialTheme.colorScheme.error
                    else -> MaterialTheme.colorScheme.primary
                },
                modifier = Modifier.size(20.dp)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = text,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.ExtraBold),
                    color = if (isDestructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
                )
            }
            if (showProgress) {
                androidx.compose.material3.CircularProgressIndicator(
                    modifier = Modifier.size(16.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
private fun QuickIconActionButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    enabled: Boolean,
    showProgress: Boolean,
    containerColor: Color
) {
    val shape = RoundedCornerShape(18.dp)
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(
                enabled = enabled,
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
            .gamepadFocusableCard(
                enabled = enabled,
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = containerColor,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            if (showProgress) {
                androidx.compose.material3.CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
            } else {
                Icon(
                    imageVector = icon,
                    contentDescription = contentDescription,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(22.dp)
                )
            }
        }
    }
}

@Composable
private fun CompactIconActionButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    enabled: Boolean,
    showProgress: Boolean
) {
    val shape = RoundedCornerShape(16.dp)
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        modifier = Modifier
            .size(52.dp)
            .clickable(
                enabled = enabled,
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
            .gamepadFocusableCard(
                enabled = enabled,
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = if (enabled) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
        },
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Box(contentAlignment = Alignment.Center) {
            if (showProgress) {
                androidx.compose.material3.CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
            } else {
                Icon(
                    imageVector = icon,
                    contentDescription = contentDescription,
                    tint = if (enabled) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                    },
                    modifier = Modifier.size(22.dp)
                )
            }
        }
    }
}

package com.sbro.emucorex.ui.emulation

import android.annotation.SuppressLint
import android.content.pm.ActivityInfo
import android.widget.Toast
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
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
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorex.R
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.buildUpscaleOptions
import com.sbro.emucorex.core.upscaleMultiplierValue
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.OverlayLayoutSnapshot
import com.sbro.emucorex.data.RetroAchievementEntry
import com.sbro.emucorex.data.RetroAchievementGameData
import com.sbro.emucorex.data.RetroAchievementsRepository
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.ui.common.BitmapPathImage
import com.sbro.emucorex.ui.common.OverlayBottomAnchorPadding
import com.sbro.emucorex.ui.common.OverlayActionGapLandscape
import com.sbro.emucorex.ui.common.OverlayActionGapPortrait
import com.sbro.emucorex.ui.common.OverlayCenterBaseShiftX
import com.sbro.emucorex.ui.common.OverlayCenterBottomPadding
import com.sbro.emucorex.ui.common.OverlayCenterColumnGapLandscape
import com.sbro.emucorex.ui.common.OverlayCenterColumnGapPortrait
import com.sbro.emucorex.ui.common.OverlayCenterInlineGapLandscape
import com.sbro.emucorex.ui.common.OverlayCenterInlineGapPortrait
import com.sbro.emucorex.ui.common.OverlayCenterSelectOpticalNudgeX
import com.sbro.emucorex.ui.common.OverlayCenterStartOpticalNudgeX
import com.sbro.emucorex.ui.common.OverlayCenterToggleOpticalNudgeY
import com.sbro.emucorex.ui.common.OverlayCenterRowGapLandscape
import com.sbro.emucorex.ui.common.OverlayCenterRowGapPortrait
import com.sbro.emucorex.ui.common.OverlayClusterGapLandscape
import com.sbro.emucorex.ui.common.OverlayClusterGapPortrait
import com.sbro.emucorex.ui.common.OverlayRightShoulderBaseOffset
import com.sbro.emucorex.ui.common.OverlayRightShoulderGapOffset
import com.sbro.emucorex.ui.common.OverlayShoulderTopPadding
import com.sbro.emucorex.ui.common.OverlayShoulderVerticalGap
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.overlayActionOffset
import com.sbro.emucorex.ui.common.overlayClusterStep
import com.sbro.emucorex.ui.common.overlayCenterButtonOffset
import com.sbro.emucorex.ui.common.overlayInlineGroupOffset
import com.sbro.emucorex.ui.common.overlayCenterSecondRowOffset
import com.sbro.emucorex.ui.common.overlayDrawableForControl
import com.sbro.emucorex.ui.common.overlayDpadOffset
import com.sbro.emucorex.ui.settings.ControlsEditorScreen
import com.sbro.emucorex.ui.theme.AccentPrimary
import com.sbro.emucorex.ui.theme.GradientEnd
import com.sbro.emucorex.ui.theme.GradientStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

private object PadKey {
    const val Up = 19
    const val Right = 22
    const val Down = 20
    const val Left = 21
    const val Triangle = 100
    const val Circle = 97
    const val Cross = 96
    const val Square = 99
    const val Select = 109
    const val Start = 108
    const val L1 = 102
    const val L2 = 104
    const val R1 = 103
    const val R2 = 105
    const val L3 = 106
    const val R3 = 107
    const val LeftStickUp = 110
    const val LeftStickRight = 111
    const val LeftStickDown = 112
    const val LeftStickLeft = 113
    const val RightStickUp = 120
    const val RightStickRight = 121
    const val RightStickDown = 122
    const val RightStickLeft = 123
}

private enum class EmulationMenuTab {
    Session,
    Overlay,
    Basic,
    Screen,
    Advanced,
    Achievements
}

private data class OverlayAchievementsContentState(
    val isLoading: Boolean = false,
    val gameData: RetroAchievementGameData? = null
)

private const val RETRO_ACHIEVEMENTS_HUD_DURATION_MS = 6_000L

private data class TouchButtonSpec(
    val id: String,
    val drawableRes: Int,
    val width: androidx.compose.ui.unit.Dp,
    val height: androidx.compose.ui.unit.Dp,
    val x: androidx.compose.ui.unit.Dp,
    val y: androidx.compose.ui.unit.Dp,
    val shape: androidx.compose.ui.graphics.Shape,
    val onPressChange: ((Boolean) -> Unit)? = null,
    val onClick: (() -> Unit)? = null
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
    saveSlot: Int? = null,
    restoredAfterProcessDeath: Boolean = false,
    onExit: () -> Unit,
    viewModel: EmulationViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val retroAchievementsState by RetroAchievementsStateManager.state.collectAsState()
    val context = LocalContext.current
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
    var showQuickSaveDialog by remember { mutableStateOf(false) }
    var showQuickLoadDialog by remember { mutableStateOf(false) }
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
    val gamepadConnected = remember { mutableStateOf(GamepadManager.isGamepadConnected()) }
    var showGamepadIndicator by remember { mutableStateOf(gamepadConnected.value) }
    var showRetroAchievementsHud by remember { mutableStateOf(false) }

    val shouldShowOverlay = uiState.controlsVisible &&
            !(uiState.hideOverlayOnGamepad && gamepadConnected.value)

    val toggleMenuClick = rememberDebouncedClick(onClick = { viewModel.toggleMenu() })
    val toggleControlsClick = rememberDebouncedClick(onClick = { viewModel.toggleControlsVisibility() })
    val togglePauseClick = rememberDebouncedClick(onClick = { viewModel.togglePause() })
    val requestQuickSaveClick = rememberDebouncedClick(onClick = { showQuickSaveDialog = true })
    val requestQuickLoadClick = rememberDebouncedClick(onClick = { showQuickLoadDialog = true })
    val confirmQuickSaveClick = rememberDebouncedClick(onClick = {
        showQuickSaveDialog = false
        viewModel.quickSave()
    })
    val confirmQuickLoadClick = rememberDebouncedClick(onClick = {
        showQuickLoadDialog = false
        viewModel.quickLoad()
    })
    val dismissQuickSaveClick = rememberDebouncedClick(onClick = { showQuickSaveDialog = false })
    val dismissQuickLoadClick = rememberDebouncedClick(onClick = { showQuickLoadDialog = false })
    val requestExitClick = rememberDebouncedClick(onClick = { showExitDialog = true })
    val confirmExitClick = rememberDebouncedClick(onClick = {
        showExitDialog = false
        viewModel.stopEmulation(onExit = onExit)
    })
    val dismissExitClick = rememberDebouncedClick(onClick = { showExitDialog = false })

    BackHandler(enabled = true) {
        toggleMenuClick()
    }

    LaunchedEffect(restoredAfterProcessDeath) {
        if (!restoredAfterProcessDeath) return@LaunchedEffect
        Toast.makeText(
            context,
            context.getString(R.string.emulation_session_restored_unavailable),
            Toast.LENGTH_LONG
        ).show()
        onExit()
    }

    LaunchedEffect(Unit) {
        val activity = context as? android.app.Activity ?: return@LaunchedEffect
        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    }

    DisposableEffect("orientation") {
        val activity = context as? android.app.Activity
        val originalOrientation = activity?.requestedOrientation
        onDispose {
            activity?.requestedOrientation = originalOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
    }

    LaunchedEffect(Unit) {
        val activity = context as? android.app.Activity ?: return@LaunchedEffect
        val window = activity.window
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    DisposableEffect(Unit) {
        onDispose {
            val activity = context as? android.app.Activity ?: return@onDispose
            val window = activity.window
            val controller = WindowCompat.getInsetsController(window, window.decorView)
            controller.show(WindowInsetsCompat.Type.systemBars())
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

    LaunchedEffect(gamePath, bootToBios, restoredAfterProcessDeath) {
        if (restoredAfterProcessDeath) return@LaunchedEffect
        viewModel.startEmulation(gamePath, saveSlot, bootToBios)
    }

    LaunchedEffect(gamepadConnected.value, uiState.showMenu) {
        if (gamepadConnected.value && !uiState.showMenu) {
            showGamepadIndicator = true
            delay(5000)
            showGamepadIndicator = false
        } else if (!gamepadConnected.value || uiState.showMenu) {
            showGamepadIndicator = false
        }
    }

    LaunchedEffect(showOverlayShortcut, uiState.showMenu) {
        if (uiState.showMenu) {
            showOverlayShortcut = false
        } else if (showOverlayShortcut) {
            delay(2200)
            showOverlayShortcut = false
        }
    }

    LaunchedEffect(
        uiState.showMenu,
        retroAchievementsState.enabled,
        retroAchievementsState.hardcoreActive,
        retroAchievementsState.game?.gameId,
        retroAchievementsState.game?.earnedAchievements,
        retroAchievementsState.game?.totalAchievements,
        retroAchievementsState.game?.earnedPoints,
        retroAchievementsState.game?.totalPoints
    ) {
        if (uiState.showMenu || !retroAchievementsState.enabled || retroAchievementsState.game == null) {
            showRetroAchievementsHud = false
            return@LaunchedEffect
        }
        showRetroAchievementsHud = true
        delay(RETRO_ACHIEVEMENTS_HUD_DURATION_MS)
        showRetroAchievementsHud = false
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

    DisposableEffect(Unit) {
        GamepadManager.setEmulationInputEnabled(true)
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

    LaunchedEffect(Unit) {
        while (true) {
            gamepadConnected.value = GamepadManager.isGamepadConnected()
            delay(2000)
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        if (!showControlsEditor) {
        // Game surface
        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).apply {
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
                    if (!uiState.showMenu && event.actionMasked == MotionEvent.ACTION_DOWN) {
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
                    visible = gamepadConnected.value && !uiState.showMenu && showGamepadIndicator,
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

                AnimatedVisibility(
                    visible = retroAchievementsState.enabled &&
                        retroAchievementsState.game != null &&
                        showRetroAchievementsHud &&
                        !uiState.showMenu,
                    enter = fadeIn(tween(220)),
                    exit = fadeOut(tween(180))
                ) {
                    retroAchievementsState.game?.let { game ->
                        RetroAchievementsHudCard(
                            title = game.title,
                            richPresence = game.richPresence,
                            earnedAchievements = game.earnedAchievements,
                            totalAchievements = game.totalAchievements,
                            earnedPoints = game.earnedPoints,
                            totalPoints = game.totalPoints,
                            hardcoreActive = retroAchievementsState.hardcoreActive
                        )
                    }
                }

            }
        }

        AnimatedVisibility(
            visible = !uiState.showMenu && uiState.showFps,
            enter = fadeIn(tween(200)),
            exit = fadeOut(tween(200)),
            modifier = Modifier
                .align(uiState.fpsOverlayCorner.toOverlayAlignment())
                .padding(
                    top = if (uiState.fpsOverlayCorner.isTopOverlayCorner()) overlayTopSafeInset + 8.dp else 0.dp,
                    bottom = if (uiState.fpsOverlayCorner.isBottomOverlayCorner()) {
                        overlayBottomSafeInset + 8.dp + if (uiState.controlsVisible) 96.dp else 0.dp
                    } else {
                        0.dp
                    },
                    start = overlayHorizontalSafeInset + 6.dp,
                    end = overlayHorizontalSafeInset + 6.dp
                )
        ) {
            if (uiState.fpsOverlayMode == FPS_OVERLAY_MODE_SIMPLE) {
                SimpleFpsCounter(fps = uiState.fps)
            } else {
                SystemPerformanceHud(
                    alignToEnd = uiState.fpsOverlayCorner.isRightOverlayCorner(),
                    speedPercent = uiState.speedPercent,
                    text = uiState.performanceOverlayText.ifBlank {
                        "VPS: ${uiState.fps}"
                    }
                )
            }
        }

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

        // On-screen controls
        if (shouldShowOverlay && !uiState.showMenu && !showControlsEditor) {
            val scaleFactor = uiState.overlayScale / 100f
            val alpha = uiState.overlayOpacity / 100f
            OnScreenControls(
                modifier = Modifier
                    .fillMaxSize()
                    .graphicsLayer(alpha = alpha),
                scaleFactor = scaleFactor,
                stickScaleFactor = uiState.stickScale / 100f,
                stickSurfaceMode = uiState.stickSurfaceMode,
                dpadOffset = uiState.dpadOffset,
                lstickOffset = uiState.lstickOffset,
                rstickOffset = uiState.rstickOffset,
                actionOffset = uiState.actionOffset,
                lbtnOffset = uiState.lbtnOffset,
                rbtnOffset = uiState.rbtnOffset,
                centerOffset = uiState.centerOffset,
                controlLayouts = uiState.controlLayouts,
                onToggleLeftInputMode = viewModel::toggleLeftInputMode,
                onPadInput = { keyCode, range, pressed ->
                    viewModel.onPadInput(keyCode, range, pressed)
                }
            )
        }
        }

        if (showControlsEditor) {
            ControlsEditorScreen(
                state = uiState,
                onBackClick = { showControlsEditor = false },
                manageActivityOrientation = false,
                overlayHorizontalSafeInset = overlayHorizontalSafeInset,
                overlayTopSafeInset = overlayTopSafeInset,
                overlayBottomSafeInset = overlayBottomSafeInset
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
                    currentGamePath = gamePath,
                    retroState = retroAchievementsState,
                    globalDefaults = globalDefaults,
                    overlayDefaults = overlayDefaults,
                    onClose = toggleMenuClick,
                    onPauseToggle = togglePauseClick,
                    onQuickSave = requestQuickSaveClick,
                    onQuickLoad = requestQuickLoadClick,
                    onSaveGameSettingsProfile = { viewModel.saveCurrentGameSettingsProfile() },
                    onResetGameSettingsProfile = { viewModel.resetCurrentGameSettingsProfile() },
                    onNextSlot = { viewModel.setSlot(uiState.currentSlot + 1) },
                    onPrevSlot = { viewModel.setSlot(uiState.currentSlot - 1) },
                    onToggleFps = { viewModel.toggleFpsVisibility() },
                    onSetFpsOverlayMode = { viewModel.setFpsOverlayMode(it) },
                    onSetFpsOverlayCorner = { viewModel.setFpsOverlayCorner(it) },
                    onSetOverlayScale = { viewModel.setOverlayScale(it) },
                    onSetOverlayOpacity = { viewModel.setOverlayOpacity(it) },
                    onSetHideOverlayOnGamepad = { viewModel.setHideOverlayOnGamepad(it) },
                    onSetCompactControls = { viewModel.setCompactControls(it) },
                    onSetKeepScreenOn = { viewModel.setKeepScreenOn(it) },
                    onSetStickScale = { viewModel.setStickScale(it) },
                    onToggleControls = toggleControlsClick,
                    onOpenControlsEditor = { showControlsEditor = true },
                    onOpenGamepadMapping = { showGamepadMappingDialog = true },
                    onSetRenderer = { viewModel.setRenderer(it) },
                    onSetUpscale = { viewModel.setUpscale(it) },
                    onSetAspectRatio = { viewModel.setAspectRatio(it) },
                    onSetMtvu = { viewModel.setMtvu(it) },
                    onSetFastCdvd = { viewModel.setFastCdvd(it) },
                    onSetEnableCheats = { viewModel.setEnableCheats(it) },
                    onOpenCheats = { showCheatsDialog = true },
                    onSetHwDownloadMode = { viewModel.setHwDownloadMode(it) },
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
                    onSetCasSharpness = { viewModel.setCasSharpness(it) },
                    onSetShadeBoostEnabled = { viewModel.setShadeBoostEnabled(it) },
                    onSetShadeBoostBrightness = { viewModel.setShadeBoostBrightness(it) },
                    onSetShadeBoostContrast = { viewModel.setShadeBoostContrast(it) },
                    onSetShadeBoostSaturation = { viewModel.setShadeBoostSaturation(it) },
                    onSetShadeBoostGamma = { viewModel.setShadeBoostGamma(it) },
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

    if (showCheatsDialog) {
        AlertDialog(
            onDismissRequest = { showCheatsDialog = false },
            title = { Text(stringResource(R.string.emulation_cheats_title)) },
            text = {
                if (uiState.availableCheats.isEmpty()) {
                    Text(stringResource(R.string.emulation_cheats_empty))
                } else {
                    Column(
                        modifier = Modifier.verticalScroll(rememberScrollState()),
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
                TextButton(onClick = { showCheatsDialog = false }) {
                    Text(stringResource(android.R.string.ok))
                }
            }
        )
    }

    if (showGamepadMappingDialog) {
        Dialog(onDismissRequest = { showGamepadMappingDialog = false }) {
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
                                autoLabel = if (isCustomBinding) null else stringResource(R.string.settings_gamepad_mapping_auto_format),
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
                            TextButton(onClick = { showGamepadMappingDialog = false }) {
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

@Composable
private fun RetroAchievementsHudCard(
    title: String,
    richPresence: String?,
    earnedAchievements: Int,
    totalAchievements: Int,
    earnedPoints: Int,
    totalPoints: Int,
    hardcoreActive: Boolean
) {
    Column(
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF14181F).copy(alpha = 0.86f))
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Rounded.Star,
                contentDescription = null,
                tint = Color(0xFFFFD166),
                modifier = Modifier.size(14.dp)
            )
            Text(
                text = title,
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                color = Color.White,
                maxLines = 1
            )
            if (hardcoreActive) {
                Text(
                    text = stringResource(R.string.settings_ra_hardcore_badge),
                    style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                    color = Color(0xFFFFD166)
                )
            }
        }
        richPresence?.takeIf { it.isNotBlank() }?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.labelSmall,
                color = Color.White.copy(alpha = 0.78f),
                maxLines = 1
            )
        }
        Text(
            text = "$earnedAchievements/$totalAchievements ${stringResource(R.string.settings_ra_achievements_label)}  •  $earnedPoints/$totalPoints ${stringResource(R.string.settings_ra_points_label)}",
            style = MaterialTheme.typography.labelSmall,
            color = Color.White.copy(alpha = 0.88f),
            maxLines = 1
        )
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun OnScreenControls(
    modifier: Modifier = Modifier,
    scaleFactor: Float,
    stickScaleFactor: Float = 1.0f,
    stickSurfaceMode: Boolean = false,
    dpadOffset: Pair<Float, Float>,
    lstickOffset: Pair<Float, Float>,
    rstickOffset: Pair<Float, Float>,
    actionOffset: Pair<Float, Float>,
    lbtnOffset: Pair<Float, Float>,
    rbtnOffset: Pair<Float, Float>,
    centerOffset: Pair<Float, Float>,
    controlLayouts: Map<String, OverlayControlLayout>,
    onToggleLeftInputMode: () -> Unit,
    onPadInput: (Int, Int, Boolean) -> Unit
) {
    val configuration = LocalConfiguration.current
    val density = LocalDensity.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val cutoutPadding = WindowInsets.displayCutout.asPaddingValues()
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues()
    val safeLeft = maxOf(cutoutPadding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr), navBarPadding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr))
    val safeRight = maxOf(cutoutPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr), navBarPadding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr))
    val safeTop = maxOf(cutoutPadding.calculateTopPadding(), navBarPadding.calculateTopPadding())
    val safeBottom = maxOf(cutoutPadding.calculateBottomPadding(), navBarPadding.calculateBottomPadding())
    val safeHorizontalInset = maxOf(safeLeft, safeRight)

    // Scaled sizes
    val dpadSize = ((if (isLandscape) 130 else 150) * scaleFactor).dp
    val actionSize = ((if (isLandscape) 130 else 150) * scaleFactor).dp
    val analogSize = ((if (isLandscape) 120 else 140) * scaleFactor * stickScaleFactor).dp
    val shoulderW = ((if (isLandscape) 65 else 72) * scaleFactor).dp
    val shoulderH = ((if (isLandscape) 32 else 36) * scaleFactor).dp
    val centerW = ((if (isLandscape) 60 else 68) * scaleFactor).dp
    val centerH = ((if (isLandscape) 26 else 30) * scaleFactor).dp
    val wideCenterW = centerW * 1.2f
    val centerColumnGap = (if (isLandscape) OverlayCenterColumnGapLandscape else OverlayCenterColumnGapPortrait) * scaleFactor
    val centerInlineGap = (if (isLandscape) OverlayCenterInlineGapLandscape else OverlayCenterInlineGapPortrait) * scaleFactor
    val centerRowGap = (if (isLandscape) OverlayCenterRowGapLandscape else OverlayCenterRowGapPortrait) * scaleFactor
    val clusterGap = if (isLandscape) OverlayClusterGapLandscape else OverlayClusterGapPortrait
    val actionGap = if (isLandscape) OverlayActionGapLandscape else OverlayActionGapPortrait
    val dpadStep = overlayClusterStep(dpadSize / 3f, clusterGap)
    val actionButtonSize = actionSize / 3.1f
    val actionStep = overlayClusterStep(actionButtonSize, actionGap)
    val actionClusterExtent = actionStep + actionButtonSize
    val rightStickBaseX = -(actionClusterExtent + 12.dp)

    val baseEdgePad = if (isLandscape) 28.dp else 12.dp
    val baseBottomPad = if (isLandscape) 24.dp else 36.dp
    val edgePadStart = baseEdgePad + safeHorizontalInset
    val edgePadEnd = baseEdgePad + safeHorizontalInset
    val edgePadTop = maxOf(OverlayShoulderTopPadding, safeTop + 4.dp)
    val bottomPad = baseBottomPad + safeBottom
    fun layoutFor(id: String, defaultScale: Int = 100): OverlayControlLayout {
        return controlLayouts[id]
            ?: AppPreferences.defaultOverlayControlLayouts(defaultScale)[id]
            ?: OverlayControlLayout(scale = defaultScale)
    }

    Box(modifier = modifier) {
        Box(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(top = edgePadTop, start = edgePadStart)
                .offset { IntOffset(lbtnOffset.first.roundToInt(), lbtnOffset.second.roundToInt()) }
        ) {
            val l2 = layoutFor("l2")
            val l1 = layoutFor("l1")
            TouchButtonGroup(
                specs = buildList {
                    if (l2.visible) {
                        add(
                            TouchButtonSpec(
                                id = "l2",
                                drawableRes = requireNotNull(overlayDrawableForControl("l2")),
                                width = shoulderW * (l2.scale / 100f),
                                height = shoulderH * (l2.scale / 100f),
                                x = l2.offset.first.dp,
                                y = l2.offset.second.dp,
                                shape = RoundedCornerShape(10.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.L2, 0, pressed) }
                            )
                        )
                    }
                    if (l1.visible) {
                        add(
                            TouchButtonSpec(
                                id = "l1",
                                drawableRes = requireNotNull(overlayDrawableForControl("l1")),
                                width = shoulderW * (l1.scale / 100f),
                                height = shoulderH * (l1.scale / 100f),
                                x = l1.offset.first.dp,
                                y = OverlayShoulderVerticalGap + l1.offset.second.dp,
                                shape = RoundedCornerShape(10.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.L1, 0, pressed) }
                            )
                        )
                    }
                }
            )
        }

        Box(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = edgePadTop, end = edgePadEnd)
                .offset { IntOffset(rbtnOffset.first.roundToInt(), rbtnOffset.second.roundToInt()) }
        ) {
            val r1 = layoutFor("r1")
            val r2 = layoutFor("r2")
            TouchButtonGroup(
                specs = buildList {
                    if (r2.visible) {
                        add(
                            TouchButtonSpec(
                                id = "r2",
                                drawableRes = requireNotNull(overlayDrawableForControl("r2")),
                                width = shoulderW * (r2.scale / 100f),
                                height = shoulderH * (r2.scale / 100f),
                                x = OverlayRightShoulderBaseOffset + r2.offset.first.dp,
                                y = r2.offset.second.dp,
                                shape = RoundedCornerShape(10.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.R2, 0, pressed) }
                            )
                        )
                    }
                    if (r1.visible) {
                        add(
                            TouchButtonSpec(
                                id = "r1",
                                drawableRes = requireNotNull(overlayDrawableForControl("r1")),
                                width = shoulderW * (r1.scale / 100f),
                                height = shoulderH * (r1.scale / 100f),
                                x = OverlayRightShoulderBaseOffset + r1.offset.first.dp,
                                y = OverlayRightShoulderGapOffset + r1.offset.second.dp,
                                shape = RoundedCornerShape(10.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.R1, 0, pressed) }
                            )
                        )
                    }
                }
            )
        }

        // Left side — D-Pad + Left Stick
        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = edgePadStart, bottom = bottomPad)
        ) {
            DPadCluster(
                onPadInput = onPadInput,
                clusterSize = dpadSize,
                controlLayouts = controlLayouts,
                modifier = Modifier
                    .offset { IntOffset(dpadOffset.first.roundToInt(), dpadOffset.second.roundToInt()) }
            )
            val leftStick = layoutFor("left_stick", (stickScaleFactor * 100f).roundToInt())
            if (leftStick.visible) {
                val leftStickSize = analogSize * (leftStick.scale / (stickScaleFactor * 100f).coerceAtLeast(1f))
                val leftStickBase = (dpadSize - leftStickSize) / 2f
                VectorAnalogStick(
                    analogSize = leftStickSize,
                    surfaceOnly = stickSurfaceMode,
                    onValueChange = { x, y ->
                        updateAnalogStick(
                            x = x,
                            y = y,
                            upKey = PadKey.LeftStickUp,
                            rightKey = PadKey.LeftStickRight,
                            downKey = PadKey.LeftStickDown,
                            leftKey = PadKey.LeftStickLeft,
                            onPadInput = onPadInput
                        )
                    },
                    modifier = Modifier
                        .offset {
                            IntOffset(
                                leftStickBase.roundToPx() + lstickOffset.first.roundToInt() + leftStick.offset.first.roundToInt(),
                                leftStickBase.roundToPx() + lstickOffset.second.roundToInt() + leftStick.offset.second.roundToInt()
                            )
                        }
                )
            }
        }

        // Right side — Actions + Right Stick
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = edgePadEnd, bottom = bottomPad)
        ) {
            ActionCluster(
                onPadInput = onPadInput,
                clusterSize = actionSize,
                controlLayouts = controlLayouts,
                modifier = Modifier
                    .offset { IntOffset(actionOffset.first.roundToInt(), actionOffset.second.roundToInt()) }
            )
            val rightStick = layoutFor("right_stick", (stickScaleFactor * 100f).roundToInt())
            if (rightStick.visible) {
                VectorAnalogStick(
                    analogSize = analogSize * (rightStick.scale / (stickScaleFactor * 100f).coerceAtLeast(1f)),
                    surfaceOnly = stickSurfaceMode,
                    onValueChange = { x, y ->
                        updateAnalogStick(
                            x = x,
                            y = y,
                            upKey = PadKey.RightStickUp,
                            rightKey = PadKey.RightStickRight,
                            downKey = PadKey.RightStickDown,
                            leftKey = PadKey.RightStickLeft,
                            onPadInput = onPadInput
                        )
                    },
                    modifier = Modifier
                        .offset {
                            IntOffset(
                                rightStickBaseX.roundToPx() + rstickOffset.first.roundToInt() + rightStick.offset.first.roundToInt(),
                                rstickOffset.second.roundToInt() + rightStick.offset.second.roundToInt()
                            )
                        }
                )
            }
        }

            val select = layoutFor("select")
            val toggle = layoutFor("left_input_toggle")
            val start = layoutFor("start")
            val l3 = layoutFor("l3")
            val r3 = layoutFor("r3")
            val l3Width = centerW * (l3.scale / 100f)
            val r3Width = centerW * (r3.scale / 100f)
            val selectWidth = wideCenterW * (select.scale / 100f)
            val toggleSize = centerH * (toggle.scale / 100f)
            val startWidth = wideCenterW * (start.scale / 100f)
            val centerItems = buildList {
                if (l3.visible) add("l3" to l3Width)
                if (select.visible) add("select" to selectWidth)
                if (toggle.visible) add("left_input_toggle" to toggleSize)
                if (start.visible) add("start" to startWidth)
                if (r3.visible) add("r3" to r3Width)
            }
            val centerInlineWidths = centerItems.map { it.second }
            fun centerInlineX(id: String): Dp {
                val index = centerItems.indexOfFirst { it.first == id }
                return if (index >= 0) {
                    overlayInlineGroupOffset(centerInlineWidths, centerInlineGap, index)
                } else {
                    0.dp
                }
            }
            val l3X = centerInlineX("l3") + l3.offset.first.dp
            val selectX = centerInlineX("select") + OverlayCenterSelectOpticalNudgeX + select.offset.first.dp
            val toggleX = centerInlineX("left_input_toggle") + toggle.offset.first.dp
            val startX = centerInlineX("start") + OverlayCenterStartOpticalNudgeX + start.offset.first.dp
            val r3X = centerInlineX("r3") + r3.offset.first.dp
            val centerBounds = buildList {
                if (select.visible) add(selectX to selectWidth)
                if (toggle.visible) add(toggleX to toggleSize)
                if (start.visible) add(startX to startWidth)
                if (l3.visible) add(l3X to l3Width)
                if (r3.visible) add(r3X to r3Width)
            }
            val centerVisualShift = if (centerBounds.isNotEmpty()) {
                val left = centerBounds.minOf { it.first }
                -left
            } else {
                0.dp
            }

        Box(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = bottomPad + (OverlayCenterBottomPadding - OverlayBottomAnchorPadding))
                .offset {
                    IntOffset(
                        OverlayCenterBaseShiftX.roundToPx() + centerOffset.first.roundToInt() + centerVisualShift.roundToPx(),
                        centerOffset.second.roundToInt()
                    )
                }
        ) {
            TouchButtonGroup(
                specs = buildList {
                    if (select.visible) {
                        add(
                            TouchButtonSpec(
                                id = "select",
                                drawableRes = requireNotNull(overlayDrawableForControl("select")),
                                width = selectWidth,
                                height = centerH * (select.scale / 100f),
                                x = selectX,
                                y = select.offset.second.dp,
                                shape = RoundedCornerShape(8.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.Select, 0, pressed) }
                            )
                        )
                    }
                    if (toggle.visible) {
                        add(
                            TouchButtonSpec(
                                id = "left_input_toggle",
                                drawableRes = requireNotNull(overlayDrawableForControl("left_input_toggle")),
                                width = toggleSize,
                                height = toggleSize,
                                x = toggleX,
                                y = OverlayCenterToggleOpticalNudgeY + toggle.offset.second.dp,
                                shape = CircleShape,
                                onClick = onToggleLeftInputMode
                            )
                        )
                    }
                    if (start.visible) {
                        add(
                            TouchButtonSpec(
                                id = "start",
                                drawableRes = requireNotNull(overlayDrawableForControl("start")),
                                width = startWidth,
                                height = centerH * (start.scale / 100f),
                                x = startX,
                                y = start.offset.second.dp,
                                shape = RoundedCornerShape(8.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.Start, 0, pressed) }
                            )
                        )
                    }
                    if (l3.visible) {
                        add(
                            TouchButtonSpec(
                                id = "l3",
                                drawableRes = requireNotNull(overlayDrawableForControl("l3")),
                                width = l3Width,
                                height = centerH * (l3.scale / 100f),
                                x = l3X,
                                y = l3.offset.second.dp,
                                shape = RoundedCornerShape(8.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.L3, 0, pressed) }
                            )
                        )
                    }
                    if (r3.visible) {
                        add(
                            TouchButtonSpec(
                                id = "r3",
                                drawableRes = requireNotNull(overlayDrawableForControl("r3")),
                                width = r3Width,
                                height = centerH * (r3.scale / 100f),
                                x = r3X,
                                y = r3.offset.second.dp,
                                shape = RoundedCornerShape(8.dp),
                                onPressChange = { pressed -> onPadInput(PadKey.R3, 0, pressed) }
                            )
                        )
                    }
                }
            )
        }
    }
}

@Composable
private fun TouchButtonGroup(
    specs: List<TouchButtonSpec>,
    modifier: Modifier = Modifier
) {
    val density = LocalDensity.current
    val activeTargets = remember { mutableStateMapOf<Int, String>() }
    val downTargets = remember { mutableMapOf<Int, String?>() }
    val specById = remember(specs) { specs.associateBy { it.id } }
    val bounds = remember(specs, density) {
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

    fun updatePointerTarget(pointerId: Int, newTarget: String?) {
        val oldTarget = activeTargets[pointerId]
        if (oldTarget == newTarget) return

        if (oldTarget != null) {
            activeTargets.remove(pointerId)
            if (!activeTargets.containsValue(oldTarget)) {
                specById[oldTarget]?.onPressChange?.invoke(false)
            }
        }

        if (newTarget != null) {
            val alreadyActive = activeTargets.containsValue(newTarget)
            activeTargets[pointerId] = newTarget
            if (!alreadyActive) {
                specById[newTarget]?.onPressChange?.invoke(true)
            }
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
                        if (downTarget != null && downTarget == upTarget) {
                            specById[downTarget]?.onClick?.invoke()
                        }
                        updatePointerTarget(pointerId, null)
                        downTarget != null || upTarget != null
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        val activePointerIds = activeTargets.keys.toList()
                        activePointerIds.forEach { updatePointerTarget(it, null) }
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
                interactive = false,
                pressed = activeTargets.containsValue(spec.id),
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
    upKey: Int, rightKey: Int, downKey: Int, leftKey: Int,
    onPadInput: (Int, Int, Boolean) -> Unit
) {
    val right = (x.coerceAtLeast(0f) * 255f).roundToInt()
    val left = ((-x).coerceAtLeast(0f) * 255f).roundToInt()
    val down = (y.coerceAtLeast(0f) * 255f).roundToInt()
    val up = ((-y).coerceAtLeast(0f) * 255f).roundToInt()
    onPadInput(upKey, up, up > 0)
    onPadInput(rightKey, right, right > 0)
    onPadInput(downKey, down, down > 0)
    onPadInput(leftKey, left, left > 0)
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun DPadCluster(
    onPadInput: (Int, Int, Boolean) -> Unit,
    clusterSize: androidx.compose.ui.unit.Dp,
    controlLayouts: Map<String, OverlayControlLayout>,
    modifier: Modifier = Modifier
) {
    val btnSize = clusterSize / 3f
    val clusterGap = if (LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp) {
        OverlayClusterGapLandscape
    } else {
        OverlayClusterGapPortrait
    }
    val step = overlayClusterStep(btnSize, clusterGap)
    val clusterExtent = step + btnSize
    val centerOffset = (clusterExtent - btnSize) / 2f

    Box(modifier = modifier) {
        val defaults = AppPreferences.defaultOverlayControlLayouts()
        val up = controlLayouts["dpad_up"] ?: defaults["dpad_up"] ?: OverlayControlLayout()
        val down = controlLayouts["dpad_down"] ?: defaults["dpad_down"] ?: OverlayControlLayout()
        val left = controlLayouts["dpad_left"] ?: defaults["dpad_left"] ?: OverlayControlLayout()
        val right = controlLayouts["dpad_right"] ?: defaults["dpad_right"] ?: OverlayControlLayout()
        TouchButtonGroup(
            specs = buildList {
                if (up.visible) {
                    add(
                        TouchButtonSpec(
                            id = "dpad_up",
                            drawableRes = requireNotNull(overlayDrawableForControl("dpad_up")),
                            width = btnSize * (up.scale / 100f),
                            height = btnSize * (up.scale / 100f),
                            x = centerOffset + up.offset.first.dp,
                            y = 0.dp + up.offset.second.dp,
                            shape = RoundedCornerShape(8.dp),
                            onPressChange = { pressed -> onPadInput(PadKey.Up, 0, pressed) }
                        )
                    )
                }
                if (down.visible) {
                    add(
                        TouchButtonSpec(
                            id = "dpad_down",
                            drawableRes = requireNotNull(overlayDrawableForControl("dpad_down")),
                            width = btnSize * (down.scale / 100f),
                            height = btnSize * (down.scale / 100f),
                            x = centerOffset + down.offset.first.dp,
                            y = step + down.offset.second.dp,
                            shape = RoundedCornerShape(8.dp),
                            onPressChange = { pressed -> onPadInput(PadKey.Down, 0, pressed) }
                        )
                    )
                }
                if (left.visible) {
                    add(
                        TouchButtonSpec(
                            id = "dpad_left",
                            drawableRes = requireNotNull(overlayDrawableForControl("dpad_left")),
                            width = btnSize * (left.scale / 100f),
                            height = btnSize * (left.scale / 100f),
                            x = 0.dp + left.offset.first.dp,
                            y = centerOffset + left.offset.second.dp,
                            shape = RoundedCornerShape(8.dp),
                            onPressChange = { pressed -> onPadInput(PadKey.Left, 0, pressed) }
                        )
                    )
                }
                if (right.visible) {
                    add(
                        TouchButtonSpec(
                            id = "dpad_right",
                            drawableRes = requireNotNull(overlayDrawableForControl("dpad_right")),
                            width = btnSize * (right.scale / 100f),
                            height = btnSize * (right.scale / 100f),
                            x = step + right.offset.first.dp,
                            y = centerOffset + right.offset.second.dp,
                            shape = RoundedCornerShape(8.dp),
                            onPressChange = { pressed -> onPadInput(PadKey.Right, 0, pressed) }
                        )
                    )
                }
            }
        )
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun ActionCluster(
    onPadInput: (Int, Int, Boolean) -> Unit,
    clusterSize: androidx.compose.ui.unit.Dp,
    controlLayouts: Map<String, OverlayControlLayout>,
    modifier: Modifier = Modifier
) {
    val btnSize = clusterSize / 3.1f
    val clusterGap = if (LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp) {
        OverlayActionGapLandscape
    } else {
        OverlayActionGapPortrait
    }
    val step = overlayClusterStep(btnSize, clusterGap)
    val clusterExtent = step + btnSize
    val centerOffset = (clusterExtent - btnSize) / 2f

    Box(
        modifier = modifier
    ) {
        val triangle = controlLayouts["triangle"] ?: OverlayControlLayout()
        val cross = controlLayouts["cross"] ?: OverlayControlLayout()
        val square = controlLayouts["square"] ?: OverlayControlLayout()
        val circle = controlLayouts["circle"] ?: OverlayControlLayout()
        TouchButtonGroup(
            specs = buildList {
                if (triangle.visible) {
                    add(
                        TouchButtonSpec(
                            id = "triangle",
                            drawableRes = requireNotNull(overlayDrawableForControl("triangle")),
                            width = btnSize * (triangle.scale / 100f),
                            height = btnSize * (triangle.scale / 100f),
                            x = centerOffset + triangle.offset.first.dp,
                            y = 0.dp + triangle.offset.second.dp,
                            shape = CircleShape,
                            onPressChange = { pressed -> onPadInput(PadKey.Triangle, 0, pressed) }
                        )
                    )
                }
                if (cross.visible) {
                    add(
                        TouchButtonSpec(
                            id = "cross",
                            drawableRes = requireNotNull(overlayDrawableForControl("cross")),
                            width = btnSize * (cross.scale / 100f),
                            height = btnSize * (cross.scale / 100f),
                            x = centerOffset + cross.offset.first.dp,
                            y = step + cross.offset.second.dp,
                            shape = CircleShape,
                            onPressChange = { pressed -> onPadInput(PadKey.Cross, 0, pressed) }
                        )
                    )
                }
                if (square.visible) {
                    add(
                        TouchButtonSpec(
                            id = "square",
                            drawableRes = requireNotNull(overlayDrawableForControl("square")),
                            width = btnSize * (square.scale / 100f),
                            height = btnSize * (square.scale / 100f),
                            x = 0.dp + square.offset.first.dp,
                            y = centerOffset + square.offset.second.dp,
                            shape = CircleShape,
                            onPressChange = { pressed -> onPadInput(PadKey.Square, 0, pressed) }
                        )
                    )
                }
                if (circle.visible) {
                    add(
                        TouchButtonSpec(
                            id = "circle",
                            drawableRes = requireNotNull(overlayDrawableForControl("circle")),
                            width = btnSize * (circle.scale / 100f),
                            height = btnSize * (circle.scale / 100f),
                            x = step + circle.offset.first.dp,
                            y = centerOffset + circle.offset.second.dp,
                            shape = CircleShape,
                            onPressChange = { pressed -> onPadInput(PadKey.Circle, 0, pressed) }
                        )
                    )
                }
            }
        )
    }
}

@Composable
private fun EmulationSidebarMenu(
    uiState: EmulationUiState,
    currentGamePath: String?,
    retroState: com.sbro.emucorex.core.utils.RetroAchievementsUiState,
    globalDefaults: SettingsSnapshot,
    overlayDefaults: OverlayLayoutSnapshot,
    onClose: () -> Unit,
    onPauseToggle: () -> Unit,
    onQuickSave: () -> Unit,
    onQuickLoad: () -> Unit,
    onSaveGameSettingsProfile: () -> Unit,
    onResetGameSettingsProfile: () -> Unit,
    onNextSlot: () -> Unit,
    onPrevSlot: () -> Unit,
    onToggleFps: () -> Unit,
    onSetFpsOverlayMode: (Int) -> Unit,
    onSetFpsOverlayCorner: (Int) -> Unit,
    onSetOverlayScale: (Int) -> Unit,
    onSetOverlayOpacity: (Int) -> Unit,
    onSetHideOverlayOnGamepad: (Boolean) -> Unit,
    onSetCompactControls: (Boolean) -> Unit,
    onSetKeepScreenOn: (Boolean) -> Unit,
    onSetStickScale: (Int) -> Unit,
    onToggleControls: () -> Unit,
    onOpenControlsEditor: () -> Unit,
    onOpenGamepadMapping: () -> Unit,
    onSetRenderer: (Int) -> Unit,
    onSetUpscale: (Float) -> Unit,
    onSetAspectRatio: (Int) -> Unit,
    onSetMtvu: (Boolean) -> Unit,
    onSetFastCdvd: (Boolean) -> Unit,
    onSetEnableCheats: (Boolean) -> Unit,
    onOpenCheats: () -> Unit,
    onSetHwDownloadMode: (Int) -> Unit,
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
    onSetCasSharpness: (Int) -> Unit,
    onSetShadeBoostEnabled: (Boolean) -> Unit,
    onSetShadeBoostBrightness: (Int) -> Unit,
    onSetShadeBoostContrast: (Int) -> Unit,
    onSetShadeBoostSaturation: (Int) -> Unit,
    onSetShadeBoostGamma: (Int) -> Unit,
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
    onExit: () -> Unit,
    modifier: Modifier = Modifier
) {
    val sectionTitleColor = MaterialTheme.colorScheme.primary
    val contentPadding = 16.dp
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

    Row(
        modifier = modifier
            .padding(WindowInsets.displayCutout.asPaddingValues())
            .padding(WindowInsets.statusBars.asPaddingValues())
            .padding(end = 16.dp + animatedRightInset, top = 16.dp, bottom = 16.dp),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.Top
    ) {
        Surface(
            modifier = Modifier
                .fillMaxHeight()
                .widthIn(max = 420.dp),
            shape = RoundedCornerShape(28.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f))
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 18.dp, vertical = 18.dp)
                    .padding(bottom = drawerBottomPadding + animatedBottomInset),
                verticalArrangement = Arrangement.spacedBy(sectionSpacing)
            ) {
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
                        Text(
                            text = uiState.currentGameSubtitle.ifBlank { stringResource(R.string.emulation_menu_subtitle) },
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
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

                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f)
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = stringResource(R.string.detail_save_states),
                                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                    color = MaterialTheme.colorScheme.onSurface,
                                    modifier = Modifier.weight(1f)
                                )
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

                        SidebarSectionTitle(
                            text = stringResource(R.string.emulation_live_settings).uppercase(),
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
                    }

                    EmulationMenuTab.Overlay -> {
                        SidebarSectionTitle(
                            text = stringResource(R.string.emulation_overlay_tab).uppercase(),
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
                            range = 50f..200f,
                            steps = 149,
                            onValueChange = { onSetStickScale(it.toInt()) },
                            helpText = stringResource(R.string.settings_help_stick_scale),
                            onResetToDefault = { onSetStickScale(overlayDefaults.stickScale) }
                        )

                        MenuButton(
                            icon = Icons.Rounded.TouchApp,
                            text = stringResource(R.string.settings_edit_controls),
                            onClick = onOpenControlsEditor,
                            containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.28f)
                        )

                        MenuButton(
                            icon = Icons.Rounded.Gamepad,
                            text = stringResource(R.string.settings_gamepad_mapping_title),
                            onClick = onOpenGamepadMapping,
                            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.40f)
                        )
                    }

                    EmulationMenuTab.Basic -> {

                SidebarSectionTitle(
                    text = stringResource(R.string.emulation_live_settings).uppercase(),
                    color = sectionTitleColor,
                    topPadding = sectionLabelTopPadding,
                    horizontalInset = sectionLabelInset
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

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_upscale),
                    options = buildUpscaleOptions(stringResource(R.string.settings_upscale_native)),
                    currentValue = upscaleMultiplierValue(uiState.upscale),
                    onValueChange = { onSetUpscale(it.toFloat()) },
                    helpText = stringResource(R.string.settings_help_upscale),
                    onResetToDefault = { onSetUpscale(globalDefaults.upscaleMultiplier) }
                )

                // Aspect Ratio (Core IDs: Stretch=0, Auto=1, 4:3=2, 16:9=3)
                LiveSelectionRow(
                    title = stringResource(R.string.settings_aspect_ratio).replace(":",""),
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

                // Performance Hacks Section
                SidebarSectionTitle(
                    text = stringResource(R.string.emulation_section_performance).uppercase(),
                    color = sectionTitleColor,
                    topPadding = sectionLabelTopPadding,
                    horizontalInset = sectionLabelInset
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_mtvu),
                    checked = uiState.enableMtvu,
                    onCheckedChange = onSetMtvu,
                    helpText = stringResource(R.string.settings_help_mtvu),
                    onResetToDefault = { onSetMtvu(globalDefaults.enableMtvu) }
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

                LiveSelectionRow(
                    title = stringResource(R.string.settings_hw_download_mode),
                    options = listOf(
                        LiveSelectionOption(0, stringResource(R.string.settings_hw_download_mode_accurate)),
                        LiveSelectionOption(1, stringResource(R.string.settings_hw_download_mode_no_readbacks)),
                        LiveSelectionOption(2, stringResource(R.string.settings_hw_download_mode_unsynchronized)),
                        LiveSelectionOption(3, stringResource(R.string.settings_hw_download_mode_disabled))
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

                SidebarSectionTitle(
                    text = stringResource(R.string.emulation_section_video).uppercase(),
                    color = sectionTitleColor,
                    topPadding = sectionLabelTopPadding,
                    horizontalInset = sectionLabelInset
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
                        0 to stringResource(R.string.settings_trilinear_filtering_auto),
                        1 to stringResource(R.string.settings_trilinear_filtering_off),
                        2 to stringResource(R.string.settings_trilinear_filtering_ps2),
                        3 to stringResource(R.string.settings_trilinear_filtering_forced)
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

                SettingsToggle(
                    title = stringResource(R.string.settings_hw_mipmapping),
                    checked = uiState.enableHwMipmapping,
                    onCheckedChange = onSetEnableHwMipmapping,
                    helpText = stringResource(R.string.settings_help_hw_mipmapping),
                    onResetToDefault = { onSetEnableHwMipmapping(globalDefaults.enableHwMipmapping) }
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

                    EmulationMenuTab.Screen -> {
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

                        SettingsToggle(
                            title = stringResource(R.string.settings_shadeboost),
                            checked = uiState.shadeBoostEnabled,
                            onCheckedChange = onSetShadeBoostEnabled,
                            helpText = stringResource(R.string.settings_help_shadeboost),
                            onResetToDefault = { onSetShadeBoostEnabled(globalDefaults.shadeBoostEnabled) }
                        )

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

                    EmulationMenuTab.Advanced -> {
                        SidebarSectionTitle(
                            text = stringResource(R.string.settings_hardware_fixes).uppercase(),
                            color = sectionTitleColor,
                            topPadding = sectionLabelTopPadding,
                            horizontalInset = sectionLabelInset
                        )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_cpu_sprite_render_size),
                    options = (0..10).map { it to it.toString() },
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
                        2 to stringResource(R.string.settings_native_scaling_aggressive)
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

                    EmulationMenuTab.Achievements -> {
                        OverlayAchievementsPane(
                            gamePath = currentGamePath,
                            currentGameTitle = uiState.currentGameTitle,
                            retroState = retroState
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.width(12.dp))

        Surface(
            modifier = Modifier
                .fillMaxHeight()
                .width(74.dp),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.72f),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.48f))
        ) {
            val railScrollState = rememberScrollState()
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(railScrollState)
                    .padding(vertical = 16.dp, horizontal = 10.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Menu,
                    contentDescription = stringResource(R.string.emulation_session_tab),
                    selected = selectedMenuTab == EmulationMenuTab.Session,
                    onClick = { selectedMenuTabName = EmulationMenuTab.Session.name }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Gamepad,
                    contentDescription = stringResource(R.string.emulation_overlay_tab),
                    selected = selectedMenuTab == EmulationMenuTab.Overlay,
                    onClick = { selectedMenuTabName = EmulationMenuTab.Overlay.name }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.SettingsSuggest,
                    contentDescription = stringResource(R.string.emulation_basic_settings),
                    selected = selectedMenuTab == EmulationMenuTab.Basic,
                    onClick = { selectedMenuTabName = EmulationMenuTab.Basic.name }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Fullscreen,
                    contentDescription = stringResource(R.string.emulation_screen_tab),
                    selected = selectedMenuTab == EmulationMenuTab.Screen,
                    onClick = { selectedMenuTabName = EmulationMenuTab.Screen.name }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Star,
                    contentDescription = stringResource(R.string.emulation_advanced_settings),
                    selected = selectedMenuTab == EmulationMenuTab.Advanced,
                    onClick = { selectedMenuTabName = EmulationMenuTab.Advanced.name }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.LockOpen,
                    contentDescription = stringResource(R.string.emulation_achievements_tab),
                    selected = selectedMenuTab == EmulationMenuTab.Achievements,
                    onClick = { selectedMenuTabName = EmulationMenuTab.Achievements.name }
                )
                Spacer(modifier = Modifier.weight(1f))
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
private fun OverlayAchievementsPane(
    gamePath: String?,
    currentGameTitle: String,
    retroState: com.sbro.emucorex.core.utils.RetroAchievementsUiState
) {
    val context = LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val contentState by produceState(
        initialValue = OverlayAchievementsContentState(
            isLoading = gamePath != null && retroState.enabled && retroState.user != null
        ),
        gamePath,
        retroState.enabled,
        retroState.user?.username,
        retroState.game?.gameId
    ) {
        if (gamePath.isNullOrBlank() || !retroState.enabled || retroState.user == null) {
            value = OverlayAchievementsContentState(isLoading = false, gameData = null)
            return@produceState
        }
        value = OverlayAchievementsContentState(isLoading = true, gameData = null)
        value = withContext(Dispatchers.IO) {
            OverlayAchievementsContentState(
                isLoading = false,
                gameData = runCatching { repository.loadGameData(gamePath) }.getOrNull()
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
                    subtitle.takeIf { !it.isNullOrBlank() && it != headerTitle }?.let {
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
                    if (contentState.isLoading && visibleAchievements.isEmpty()) {
                        OverlayAchievementsNotice(stringResource(R.string.achievements_loading))
                    } else if (visibleAchievements.isEmpty()) {
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
                        if (achievement.isEarned) R.string.achievements_status_earned
                        else R.string.achievements_status_locked
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
    icon: ImageVector,
    contentDescription: String,
    selected: Boolean,
    onClick: () -> Unit,
    isDestructive: Boolean = false
) {
    val iconScale by animateFloatAsState(
        targetValue = if (selected && !isDestructive) 1.08f else 1f,
        animationSpec = tween(durationMillis = 220),
        label = "emulation_menu_rail_icon_scale"
    )
    Surface(
        onClick = onClick,
        shape = RoundedCornerShape(18.dp),
        color = when {
            isDestructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.12f)
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
        },
        border = BorderStroke(
            1.dp,
            when {
                isDestructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.22f)
                selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.30f)
                else -> MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.52f)
            }
        )
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
    onCheckedChange: (Boolean) -> Unit,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    Surface(
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = { onCheckedChange(!checked) },
                    onLongClick = onResetToDefault
                )
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
                    color = MaterialTheme.colorScheme.onSurface
                )
                helpText?.let {
                    SettingHelpButton(title = title, description = it)
                }
            }
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
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
    topPadding: androidx.compose.ui.unit.Dp,
    horizontalInset: androidx.compose.ui.unit.Dp
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
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 4.dp)
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
private fun SimpleFpsCounter(fps: String) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(999.dp))
            .background(Color.Black.copy(alpha = 0.42f))
            .border(
                width = 1.dp,
                color = Color.White.copy(alpha = 0.08f),
                shape = RoundedCornerShape(999.dp)
            )
            .padding(horizontal = 10.dp, vertical = 5.dp)
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = stringResource(R.string.emulation_hud_fps_label),
                style = MaterialTheme.typography.labelSmall.copy(
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 0.2.sp
                ),
                color = Color.White.copy(alpha = 0.72f)
            )
            Text(
                text = fps,
                style = MaterialTheme.typography.labelLarge.copy(
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
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
    alignToEnd: Boolean,
    speedPercent: Float,
    text: String
) {
    val displayText = remember(text, speedPercent) {
        buildSpeedAnnotatedText(text, speedPercent)
    }
    Text(
        text = displayText,
        modifier = Modifier,
        style = MaterialTheme.typography.labelMedium.copy(
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Medium,
            fontSize = 11.sp,
            lineHeight = 13.sp,
            letterSpacing = 0.sp,
            shadow = Shadow(
                color = Color.Black.copy(alpha = 0.9f),
                offset = Offset(1.5f, 1.5f),
                blurRadius = 2f
            )
        ),
        color = Color.White,
        textAlign = if (alignToEnd) TextAlign.End else TextAlign.Start
    )
}

private fun buildSpeedAnnotatedText(text: String, speedPercent: Float): AnnotatedString {
    val speedColor = when {
        speedPercent < 90f -> Color(0xFFFF5A5A)
        speedPercent < 99f -> Color(0xFFFFC04D)
        speedPercent <= 101f -> Color(0xFF7CFF7C)
        speedPercent <= 110f -> Color(0xFF9BE870)
        else -> Color(0xFF59D2FF)
    }

    val speedLabel = "Speed:"
    val speedStart = text.indexOf(speedLabel)
    if (speedStart < 0) return AnnotatedString(text)

    val valueStart = speedStart + speedLabel.length
    val valueEnd = text.indexOf('\n', startIndex = valueStart).let { if (it == -1) text.length else it }
    return buildAnnotatedString {
        append(text)
        addStyle(
            style = SpanStyle(color = speedColor),
            start = valueStart,
            end = valueEnd
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
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
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 4.dp)
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
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(1f)
            )
            helpText?.let {
                SettingHelpButton(title = title, description = it)
            }
        }
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            options.forEach { (value, label) ->
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
private fun LiveSliderRow(
    title: String,
    @androidx.annotation.StringRes valueLabelResId: Int? = null,
    valueLabelForValue: (Int) -> String,
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

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = {},
                    onLongClick = onResetToDefault
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
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.weight(1f, fill = false)
                )
                helpText?.let {
                    SettingHelpButton(title = title, description = it)
                }
            }
            Text(
                text = valueLabelResId?.let { stringResource(it, sliderValue.toInt()) }
                    ?: valueLabelForValue(sliderValue.toInt()),
                style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.primary
            )
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
            )
        )
    }
}

@Composable
private fun MenuButton(
    icon: ImageVector,
    text: String,
    onClick: () -> Unit,
    isDestructive: Boolean = false,
    gradientColors: List<Color>? = null,
    enabled: Boolean = true,
    showProgress: Boolean = false,
    containerColor: Color? = null
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = when {
            gradientColors != null -> Color.Transparent
            containerColor != null -> containerColor
            isDestructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.15f)
            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
        },
        onClick = onClick,
        enabled = enabled,
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
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = containerColor,
        onClick = onClick,
        enabled = enabled,
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

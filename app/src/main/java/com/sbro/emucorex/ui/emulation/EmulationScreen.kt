package com.sbro.emucorex.ui.emulation

import android.annotation.SuppressLint
import android.content.pm.ActivityInfo
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
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material.icons.rounded.Fullscreen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Upload
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.AlertDialog
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DeviceChipsetFamily
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.settings.ControlsEditorScreen
import com.sbro.emucorex.ui.theme.AccentPrimary
import com.sbro.emucorex.ui.theme.GradientEnd
import com.sbro.emucorex.ui.theme.GradientStart
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
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
    Advanced
}

private data class LiveSelectionOption(
    val value: Int,
    val label: String? = null,
    val icon: ImageVector? = null,
    val contentDescription: String = label.orEmpty()
)

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun EmulationScreen(
    gamePath: String? = null,
    bootToBios: Boolean = false,
    saveSlot: Int? = null,
    onExit: () -> Unit,
    viewModel: EmulationViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val retroAchievementsState by RetroAchievementsStateManager.state.collectAsState()
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val gamepadBindings by preferences.gamepadBindings.collectAsState(initial = emptyMap())
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
    
    val configuration = LocalConfiguration.current
    val view = LocalView.current
    var showExitDialog by remember { mutableStateOf(false) }
    var showCheatsDialog by remember { mutableStateOf(false) }
    var showControlsEditor by remember { mutableStateOf(false) }
    var showGamepadMappingDialog by remember { mutableStateOf(false) }
    var pendingGamepadActionId by remember { mutableStateOf<String?>(null) }
    var showOverlayShortcut by remember { mutableStateOf(false) }
    var lastTapTimestamp by remember { mutableLongStateOf(0L) }
    var lastTapX by remember { mutableFloatStateOf(0f) }
    var lastTapY by remember { mutableFloatStateOf(0f) }
    val gamepadConnected = remember { mutableStateOf(GamepadManager.isGamepadConnected()) }
    var showGamepadIndicator by remember { mutableStateOf(gamepadConnected.value) }

    val shouldShowOverlay = uiState.controlsVisible &&
            !(uiState.hideOverlayOnGamepad && gamepadConnected.value)

    val toggleMenuClick = rememberDebouncedClick(onClick = { viewModel.toggleMenu() })
    val toggleControlsClick = rememberDebouncedClick(onClick = { viewModel.toggleControlsVisibility() })
    val togglePauseClick = rememberDebouncedClick(onClick = { viewModel.togglePause() })
    val quickSaveClick = rememberDebouncedClick(onClick = { viewModel.quickSave() })
    val quickLoadClick = rememberDebouncedClick(onClick = { viewModel.quickLoad() })
    val requestExitClick = rememberDebouncedClick(onClick = { showExitDialog = true })
    val confirmExitClick = rememberDebouncedClick(onClick = {
        showExitDialog = false
        viewModel.stopEmulation(onExit = onExit)
    })
    val dismissExitClick = rememberDebouncedClick(onClick = { showExitDialog = false })

    BackHandler(enabled = true) {
        toggleMenuClick()
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

    LaunchedEffect(gamePath, bootToBios) {
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

    DisposableEffect(pendingGamepadActionId) {
        val actionId = pendingGamepadActionId
        if (actionId != null) {
            GamepadManager.startBindingCapture { keyCode ->
                scope.launch {
                    preferences.setGamepadBinding(actionId, keyCode)
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

        // Top systemic overlay (FPS, Gamepad, Menu)
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

                // Performance Metrics
                AnimatedVisibility(
                    visible = !uiState.showMenu && uiState.showFps,
                    enter = fadeIn(tween(200)),
                    exit = fadeOut(tween(200))
                ) {
                    if (uiState.fpsOverlayMode == FPS_OVERLAY_MODE_SIMPLE) {
                        SimpleFpsCounter(fps = uiState.fps)
                    } else {
                        PerformanceHud(
                            fps = uiState.fps,
                            speedPercent = uiState.speedPercent,
                            cpuLoad = uiState.cpuLoad,
                            gpuLoad = uiState.gpuLoad,
                            frameTime = uiState.frameTime,
                            targetFps = uiState.targetFps
                        )
                    }
                }

                AnimatedVisibility(
                    visible = retroAchievementsState.enabled &&
                        retroAchievementsState.game != null &&
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
                dpadOffset = uiState.dpadOffset,
                lstickOffset = uiState.lstickOffset,
                rstickOffset = uiState.rstickOffset,
                actionOffset = uiState.actionOffset,
                lbtnOffset = uiState.lbtnOffset,
                rbtnOffset = uiState.rbtnOffset,
                centerOffset = uiState.centerOffset,
                onPadInput = { keyCode, range, pressed ->
                    viewModel.onPadInput(keyCode, range, pressed)
                },
                onPadInputs = { indices, values ->
                    viewModel.onPadInputs(indices, values)
                }
            )
        }

        if (showControlsEditor) {
            ControlsEditorScreen(
                onBackClick = { showControlsEditor = false }
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
                    onClose = toggleMenuClick,
                    onPauseToggle = togglePauseClick,
                    onQuickSave = quickSaveClick,
                    onQuickLoad = quickLoadClick,
                    onSaveGameSettingsProfile = { viewModel.saveCurrentGameSettingsProfile() },
                    onResetGameSettingsProfile = { viewModel.resetCurrentGameSettingsProfile() },
                    onNextSlot = { viewModel.setSlot(uiState.currentSlot + 1) },
                    onPrevSlot = { viewModel.setSlot(uiState.currentSlot - 1) },
                    onToggleFps = { viewModel.toggleFpsVisibility() },
                    onSetFpsOverlayMode = { viewModel.setFpsOverlayMode(it) },
                    onSetOverlayScale = { viewModel.setOverlayScale(it) },
                    onSetOverlayOpacity = { viewModel.setOverlayOpacity(it) },
                    onSetHideOverlayOnGamepad = { viewModel.setHideOverlayOnGamepad(it) },
                    onSetCompactControls = { viewModel.setCompactControls(it) },
                    onSetKeepScreenOn = { viewModel.setKeepScreenOn(it) },
                    onSetStickScale = { viewModel.setStickScale(it) },
                    onToggleControls = toggleControlsClick,
                    onOpenControlsEditor = { showControlsEditor = true },
                    onOpenGamepadMapping = { showGamepadMappingDialog = true },
                    onSetPerformancePreset = { viewModel.applyPerformancePreset(it) },
                    onApplyRecommendedProfile = { viewModel.applyRecommendedDeviceProfile() },
                    onSetRenderer = { viewModel.setRenderer(it) },
                    onSetUpscale = { viewModel.setUpscale(it) },
                    onSetAspectRatio = { viewModel.setAspectRatio(it) },
                    onSetMtvu = { viewModel.setMtvu(it) },
                    onSetFastCdvd = { viewModel.setFastCdvd(it) },
                    onSetEnableCheats = { viewModel.setEnableCheats(it) },
                    onOpenCheats = { showCheatsDialog = true },
                    onSetHwDownloadMode = { viewModel.setHwDownloadMode(it) },
                    onSetFrameSkip = { viewModel.setFrameSkip(it) },
                    onSetFrameLimitEnabled = { viewModel.setFrameLimitEnabled(it) },
                    onSetTargetFps = { viewModel.setTargetFps(it) },
                    onSetTextureFiltering = { viewModel.setTextureFiltering(it) },
                    onSetTrilinearFiltering = { viewModel.setTrilinearFiltering(it) },
                    onSetBlendingAccuracy = { viewModel.setBlendingAccuracy(it) },
                    onSetTexturePreloading = { viewModel.setTexturePreloading(it) },
                    onSetEnableFxaa = { viewModel.setEnableFxaa(it) },
                    onSetCasMode = { viewModel.setCasMode(it) },
                    onSetCasSharpness = { viewModel.setCasSharpness(it) },
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
                    Column(
                        modifier = Modifier.padding(start = 18.dp, end = 18.dp, top = 18.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.settings_gamepad_mapping_title),
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        val connectedControllerName = GamepadManager.firstConnectedControllerName()
                        Text(
                            text = connectedControllerName?.let {
                                stringResource(R.string.settings_gamepad_mapping_connected, it)
                            } ?: stringResource(R.string.settings_gamepad_mapping_disconnected),
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
                                customBindings = gamepadBindings
                            )
                            val isCustomBinding = gamepadBindings.containsKey(action.id)
                            EmulationGamepadBindingRow(
                                title = stringResource(gamepadActionLabelRes(action.id)),
                                value = assignedKeyCode?.let(GamepadManager::keyCodeLabel)
                                    ?: stringResource(R.string.settings_not_set),
                                autoLabel = if (isCustomBinding) null else stringResource(R.string.settings_gamepad_mapping_auto_format),
                                onBindClick = { pendingGamepadActionId = action.id },
                                onClearClick = if (isCustomBinding) {
                                    {
                                        scope.launch {
                                            preferences.clearGamepadBinding(action.id)
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
                                        preferences.resetGamepadBindings()
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
        AlertDialog(
            onDismissRequest = { pendingGamepadActionId = null },
            title = { Text(stringResource(R.string.settings_gamepad_mapping_listening_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.settings_gamepad_mapping_listening_desc,
                        stringResource(gamepadActionLabelRes(pendingGamepadActionId.orEmpty()))
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
    dpadOffset: Pair<Float, Float>,
    lstickOffset: Pair<Float, Float>,
    rstickOffset: Pair<Float, Float>,
    actionOffset: Pair<Float, Float>,
    lbtnOffset: Pair<Float, Float>,
    rbtnOffset: Pair<Float, Float>,
    centerOffset: Pair<Float, Float>,
    onPadInput: (Int, Int, Boolean) -> Unit,
    onPadInputs: (IntArray, FloatArray) -> Unit
) {
    val configuration = LocalConfiguration.current
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
    val centerW = ((if (isLandscape) 64 else 72) * scaleFactor).dp
    val centerH = ((if (isLandscape) 28 else 32) * scaleFactor).dp

    val baseEdgePad = if (isLandscape) 28.dp else 12.dp
    val baseBottomPad = if (isLandscape) 24.dp else 36.dp
    val edgePadStart = baseEdgePad + safeHorizontalInset
    val edgePadEnd = baseEdgePad + safeHorizontalInset
    val edgePadTop = maxOf(8.dp, safeTop)
    val bottomPad = baseBottomPad + safeBottom

    Box(modifier = modifier) {
        Row(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(top = edgePadTop + 4.dp, start = edgePadStart)
                .offset { IntOffset(lbtnOffset.first.roundToInt(), lbtnOffset.second.roundToInt()) },
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            ShoulderButton("L2", shoulderW, shoulderH) { pressed -> onPadInput(PadKey.L2, 0, pressed) }
            ShoulderButton("L1", shoulderW, shoulderH) { pressed -> onPadInput(PadKey.L1, 0, pressed) }
        }

        Row(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = edgePadTop + 4.dp, end = edgePadEnd)
                .offset { IntOffset(rbtnOffset.first.roundToInt(), rbtnOffset.second.roundToInt()) },
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            ShoulderButton("R1", shoulderW, shoulderH) { pressed -> onPadInput(PadKey.R1, 0, pressed) }
            ShoulderButton("R2", shoulderW, shoulderH) { pressed -> onPadInput(PadKey.R2, 0, pressed) }
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
                modifier = Modifier
                    .padding(bottom = analogSize * 0.7f)
                    .offset { IntOffset(dpadOffset.first.roundToInt(), dpadOffset.second.roundToInt()) }
            )
            AnalogStick(
                analogSize = analogSize,
                onValueChange = { x, y ->
                    updateAnalogStick(
                        x = x,
                        y = y,
                        upKey = PadKey.LeftStickUp,
                        rightKey = PadKey.LeftStickRight,
                        downKey = PadKey.LeftStickDown,
                        leftKey = PadKey.LeftStickLeft,
                        onPadInputs = onPadInputs
                    )
                },
                modifier = Modifier
                    .padding(start = dpadSize * 0.9f)
                    .align(Alignment.BottomStart)
                    .offset { IntOffset(lstickOffset.first.roundToInt(), lstickOffset.second.roundToInt()) }
            )
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
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(bottom = analogSize * 0.7f)
                    .offset { IntOffset(actionOffset.first.roundToInt(), actionOffset.second.roundToInt()) }
            )
            AnalogStick(
                analogSize = analogSize,
                onValueChange = { x, y ->
                    updateAnalogStick(
                        x = x,
                        y = y,
                        upKey = PadKey.RightStickUp,
                        rightKey = PadKey.RightStickRight,
                        downKey = PadKey.RightStickDown,
                        leftKey = PadKey.RightStickLeft,
                        onPadInputs = onPadInputs
                    )
                },
                modifier = Modifier
                    .padding(end = actionSize * 0.9f)
                    .align(Alignment.BottomEnd)
                    .offset { IntOffset(rstickOffset.first.roundToInt(), rstickOffset.second.roundToInt()) }
            )
        }

        // Center buttons
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = bottomPad + 6.dp)
                .offset { IntOffset(centerOffset.first.roundToInt(), centerOffset.second.roundToInt()) },
            verticalArrangement = Arrangement.spacedBy(6.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                CenterButton("SELECT", centerW * 1.25f, centerH) { pressed ->
                    onPadInput(PadKey.Select, 0, pressed)
                }
                CenterButton("START", centerW * 1.25f, centerH) { pressed ->
                    onPadInput(PadKey.Start, 0, pressed)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                CenterButton("L3", centerW, centerH) { pressed ->
                    onPadInput(PadKey.L3, 0, pressed)
                }
                CenterButton("R3", centerW, centerH) { pressed ->
                    onPadInput(PadKey.R3, 0, pressed)
                }
            }
        }
    }
}

private fun updateAnalogStick(
    x: Float, y: Float,
    upKey: Int, rightKey: Int, downKey: Int, leftKey: Int,
    onPadInputs: (IntArray, FloatArray) -> Unit
) {
    val right = (x.coerceAtLeast(0f) * 255f).roundToInt()
    val left = ((-x).coerceAtLeast(0f) * 255f).roundToInt()
    val down = (y.coerceAtLeast(0f) * 255f).roundToInt()
    val up = ((-y).coerceAtLeast(0f) * 255f).roundToInt()
    onPadInputs(
        intArrayOf(upKey, rightKey, downKey, leftKey),
        floatArrayOf(up / 255f, right / 255f, down / 255f, left / 255f)
    )
}

@Composable
private fun DPadCluster(
    onPadInput: (Int, Int, Boolean) -> Unit,
    clusterSize: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier
) {
    val btnSize = clusterSize / 3f

    Box(modifier = modifier.size(clusterSize)) {
        DPadButton("▲", Modifier.align(Alignment.TopCenter), btnSize) { pressed -> onPadInput(PadKey.Up, 0, pressed) }
        DPadButton("▼", Modifier.align(Alignment.BottomCenter), btnSize) { pressed -> onPadInput(PadKey.Down, 0, pressed) }
        DPadButton("◀", Modifier.align(Alignment.CenterStart), btnSize) { pressed -> onPadInput(PadKey.Left, 0, pressed) }
        DPadButton("▶", Modifier.align(Alignment.CenterEnd), btnSize) { pressed -> onPadInput(PadKey.Right, 0, pressed) }
        Box(
            modifier = Modifier
                .align(Alignment.Center)
                .size(btnSize * 0.7f)
                .clip(RoundedCornerShape(6.dp))
                .background(Color.White.copy(alpha = 0.04f))
        )
    }
}

@Composable
private fun ActionCluster(
    onPadInput: (Int, Int, Boolean) -> Unit,
    clusterSize: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier
) {
    val btnSize = clusterSize / 3.2f
    val offset = btnSize / 3f
    val totalSize = clusterSize + offset * 2

    Box(
        modifier = modifier.size(totalSize),
        contentAlignment = Alignment.Center
    ) {
        Box(modifier = Modifier.size(clusterSize)) {
            ActionButton("△", Color(0xFF50D9A0), Modifier.align(Alignment.TopCenter).offset(y = -offset), btnSize) {
                pressed -> onPadInput(PadKey.Triangle, 0, pressed)
            }
            ActionButton("✕", Color(0xFF5BA8FF), Modifier.align(Alignment.BottomCenter).offset(y = offset), btnSize) {
                pressed -> onPadInput(PadKey.Cross, 0, pressed)
            }
            ActionButton("□", Color(0xFFA07BFF), Modifier.align(Alignment.CenterStart).offset(x = -offset), btnSize) {
                pressed -> onPadInput(PadKey.Square, 0, pressed)
            }
            ActionButton("○", Color(0xFFFF6B7A), Modifier.align(Alignment.CenterEnd).offset(x = offset), btnSize) {
                pressed -> onPadInput(PadKey.Circle, 0, pressed)
            }
        }
    }
}

@Composable
private fun ShoulderButton(
    label: String,
    width: androidx.compose.ui.unit.Dp,
    height: androidx.compose.ui.unit.Dp,
    onPressChange: (Boolean) -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.92f else 1f, tween(80))

    LaunchedEffect(isPressed) { onPressChange(isPressed) }

    Box(
        modifier = Modifier
            .size(width = width, height = height)
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .clip(RoundedCornerShape(10.dp))
            .background(
                Brush.verticalGradient(
                    colors = if (isPressed) listOf(Color.White.copy(0.22f), Color.White.copy(0.12f))
                    else listOf(Color.White.copy(0.12f), Color.White.copy(0.06f))
                )
            )
            .border(
                width = 1.dp,
                color = Color.White.copy(alpha = if (isPressed) 0.25f else 0.1f),
                shape = RoundedCornerShape(10.dp)
            )
            .clickable(interactionSource = interactionSource, indication = null) { },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.sp
            ),
            color = Color.White.copy(alpha = 0.85f)
        )
    }
}

@Composable
private fun AnalogStick(
    analogSize: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
    onValueChange: (Float, Float) -> Unit
) {
    var size by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var thumbOffset by remember { mutableStateOf(Offset.Zero) }
    var pointerActive by remember { mutableStateOf(false) }
    var lastSentX by remember { mutableIntStateOf(0) }
    var lastSentY by remember { mutableIntStateOf(0) }

    fun dispatchStickValue(x: Float, y: Float) {
        val quantizedX = (x * 255f).roundToInt()
        val quantizedY = (y * 255f).roundToInt()
        if (quantizedX == lastSentX && quantizedY == lastSentY) return
        lastSentX = quantizedX
        lastSentY = quantizedY
        onValueChange(quantizedX / 255f, quantizedY / 255f)
    }

    fun resetStick() {
        pointerActive = false
        thumbOffset = Offset.Zero
        dispatchStickValue(0f, 0f)
    }

    Box(
        modifier = modifier
            .size(analogSize)
            .clip(CircleShape)
            .background(Color.White.copy(alpha = 0.05f))
            .border(width = 1.5.dp, color = Color.White.copy(alpha = 0.1f), shape = CircleShape)
            .onSizeChanged { size = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .pointerInput(size) {
                detectDragGestures(
                    onDragStart = { offset ->
                        if (size.width == 0f) return@detectDragGestures
                        pointerActive = true
                        val center = Offset(size.width / 2f, size.height / 2f)
                        val maxDistance = minOf(size.width, size.height) * 0.4f
                        val deadZone = 0.12f
                        val raw = offset - center
                        val distance = raw.getDistance()
                        val clamped = if (distance > maxDistance && distance > 0f) raw * (maxDistance / distance) else raw
                        thumbOffset = clamped
                        val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (kotlin.math.abs(it) < deadZone) 0f else it }
                        val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (kotlin.math.abs(it) < deadZone) 0f else it }
                        dispatchStickValue(nx, ny)
                    },
                    onDragEnd = { resetStick() },
                    onDragCancel = { resetStick() }
                ) { change, _ ->
                    change.consume()
                    if (size.width == 0f) return@detectDragGestures
                    pointerActive = true
                    val center = Offset(size.width / 2f, size.height / 2f)
                    val maxDistance = minOf(size.width, size.height) * 0.4f
                    val deadZone = 0.12f
                    val raw = change.position - center
                    val distance = raw.getDistance()
                    val clamped = if (distance > maxDistance && distance > 0f) raw * (maxDistance / distance) else raw
                    thumbOffset = clamped
                    val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (kotlin.math.abs(it) < deadZone) 0f else it }
                    val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (kotlin.math.abs(it) < deadZone) 0f else it }
                    dispatchStickValue(nx, ny)
                }
            },
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(analogSize * 0.65f)
                .clip(CircleShape)
                .border(1.dp, Color.White.copy(alpha = 0.06f), CircleShape)
        )
        Box(
            modifier = Modifier
                .offset { IntOffset(thumbOffset.x.roundToInt(), thumbOffset.y.roundToInt()) }
                .size(analogSize * 0.32f)
                .clip(CircleShape)
                .background(
                    Brush.radialGradient(
                        colors = if (pointerActive) listOf(
                            AccentPrimary.copy(alpha = 0.9f),
                            AccentPrimary.copy(alpha = 0.5f)
                        ) else listOf(
                            Color.White.copy(alpha = 0.35f),
                            Color.White.copy(alpha = 0.15f)
                        )
                    )
                )
                .border(1.5.dp, Color.White.copy(alpha = if (pointerActive) 0.3f else 0.1f), CircleShape)
                .offset { IntOffset(thumbOffset.x.roundToInt(), thumbOffset.y.roundToInt()) }
        )
    }
}

@Composable
private fun DPadButton(
    label: String,
    modifier: Modifier = Modifier,
    buttonSize: androidx.compose.ui.unit.Dp,
    onPressChange: (Boolean) -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.88f else 1f, tween(60))

    LaunchedEffect(isPressed) { onPressChange(isPressed) }

    Box(
        modifier = modifier
            .size(buttonSize)
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .clip(RoundedCornerShape(8.dp))
            .background(
                if (isPressed) Color.White.copy(alpha = 0.2f) else Color.White.copy(alpha = 0.08f)
            )
            .border(
                1.dp,
                Color.White.copy(alpha = if (isPressed) 0.25f else 0.08f),
                RoundedCornerShape(8.dp)
            )
            .clickable(interactionSource = interactionSource, indication = null) { },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.titleSmall,
            color = Color.White.copy(alpha = 0.75f)
        )
    }
}

@Composable
private fun ActionButton(
    label: String,
    buttonColor: Color,
    modifier: Modifier = Modifier,
    buttonSize: androidx.compose.ui.unit.Dp,
    onPressChange: (Boolean) -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.85f else 1f, tween(60))

    LaunchedEffect(isPressed) { onPressChange(isPressed) }

    Box(
        modifier = modifier
            .size(buttonSize)
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .clip(CircleShape)
            .background(
                Brush.radialGradient(
                    colors = if (isPressed) listOf(
                        buttonColor.copy(alpha = 0.7f),
                        buttonColor.copy(alpha = 0.35f)
                    ) else listOf(
                        buttonColor.copy(alpha = 0.4f),
                        buttonColor.copy(alpha = 0.15f)
                    )
                )
            )
            .border(
                1.5.dp,
                buttonColor.copy(alpha = if (isPressed) 0.6f else 0.25f),
                CircleShape
            )
            .clickable(interactionSource = interactionSource, indication = null) { },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
            color = Color.White.copy(alpha = if (isPressed) 1f else 0.85f)
        )
    }
}

@Composable
private fun CenterButton(
    label: String,
    width: androidx.compose.ui.unit.Dp,
    height: androidx.compose.ui.unit.Dp,
    onPressChange: (Boolean) -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.9f else 1f, tween(60))

    LaunchedEffect(isPressed) { onPressChange(isPressed) }

    Box(
        modifier = Modifier
            .size(width = width, height = height)
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .clip(RoundedCornerShape(8.dp))
            .background(
                if (isPressed) Color.White.copy(alpha = 0.18f) else Color.White.copy(alpha = 0.07f)
            )
            .border(
                1.dp,
                Color.White.copy(alpha = if (isPressed) 0.2f else 0.06f),
                RoundedCornerShape(8.dp)
            )
            .clickable(interactionSource = interactionSource, indication = null) { },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(
                fontWeight = FontWeight.Medium,
                letterSpacing = 1.sp
            ),
            color = Color.White.copy(alpha = 0.7f)
        )
    }
}

@Composable
private fun EmulationSidebarMenu(
    uiState: EmulationUiState,
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
    onSetOverlayScale: (Int) -> Unit,
    onSetOverlayOpacity: (Int) -> Unit,
    onSetHideOverlayOnGamepad: (Boolean) -> Unit,
    onSetCompactControls: (Boolean) -> Unit,
    onSetKeepScreenOn: (Boolean) -> Unit,
    onSetStickScale: (Int) -> Unit,
    onToggleControls: () -> Unit,
    onOpenControlsEditor: () -> Unit,
    onOpenGamepadMapping: () -> Unit,
    onSetPerformancePreset: (Int) -> Unit,
    onApplyRecommendedProfile: () -> Unit,
    onSetRenderer: (Int) -> Unit,
    onSetUpscale: (Int) -> Unit,
    onSetAspectRatio: (Int) -> Unit,
    onSetMtvu: (Boolean) -> Unit,
    onSetFastCdvd: (Boolean) -> Unit,
    onSetEnableCheats: (Boolean) -> Unit,
    onOpenCheats: () -> Unit,
    onSetHwDownloadMode: (Int) -> Unit,
    onSetFrameSkip: (Int) -> Unit,
    onSetFrameLimitEnabled: (Boolean) -> Unit,
    onSetTargetFps: (Int) -> Unit,
    onSetTextureFiltering: (Int) -> Unit,
    onSetTrilinearFiltering: (Int) -> Unit,
    onSetBlendingAccuracy: (Int) -> Unit,
    onSetTexturePreloading: (Int) -> Unit,
    onSetEnableFxaa: (Boolean) -> Unit,
    onSetCasMode: (Int) -> Unit,
    onSetCasSharpness: (Int) -> Unit,
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
    var selectedMenuTab by remember { mutableStateOf(EmulationMenuTab.Session) }

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
                                    enabled = uiState.currentSlot > 0,
                                    colors = androidx.compose.material3.IconButtonDefaults.iconButtonColors(
                                        contentColor = MaterialTheme.colorScheme.primary
                                    )
                                ) {
                                    Icon(Icons.AutoMirrored.Rounded.KeyboardArrowLeft, null)
                                }
                                Text(
                                    text = "${uiState.currentSlot + 1}",
                                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.ExtraBold),
                                    color = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.padding(horizontal = 4.dp)
                                )
                                androidx.compose.material3.IconButton(
                                    onClick = onNextSlot,
                                    enabled = uiState.currentSlot < 4,
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
                                    icon = Icons.Rounded.Download,
                                    contentDescription = stringResource(R.string.emulation_quick_save_desc),
                                    onClick = onQuickSave,
                                    enabled = !uiState.isActionInProgress,
                                    showProgress = uiState.actionLabel == "saving",
                                    containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f)
                                )
                            }
                            Box(modifier = Modifier.weight(1f)) {
                                QuickIconActionButton(
                                    icon = Icons.Rounded.Upload,
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
                            onCheckedChange = { onToggleFps() }
                        )

                        LiveSelectionRow(
                            title = stringResource(R.string.settings_fps_overlay_mode),
                            options = listOf(
                                LiveSelectionOption(FPS_OVERLAY_MODE_SIMPLE, stringResource(R.string.settings_fps_overlay_mode_simple)),
                                LiveSelectionOption(FPS_OVERLAY_MODE_DETAILED, stringResource(R.string.settings_fps_overlay_mode_detailed))
                            ),
                            currentValue = uiState.fpsOverlayMode,
                            onValueChange = onSetFpsOverlayMode
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
                            onCheckedChange = onSetHideOverlayOnGamepad
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_compact_controls),
                            checked = uiState.compactControls,
                            onCheckedChange = onSetCompactControls
                        )

                        SettingsToggle(
                            title = stringResource(R.string.settings_keep_screen_on),
                            checked = uiState.keepScreenOn,
                            onCheckedChange = onSetKeepScreenOn
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_overlay_scale),
                            valueLabelForValue = { "$it%" },
                            value = uiState.overlayScale.toFloat(),
                            range = 50f..150f,
                            steps = 99,
                            onValueChange = { onSetOverlayScale(it.toInt()) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.settings_overlay_opacity),
                            valueLabelForValue = { "$it%" },
                            value = uiState.overlayOpacity.toFloat(),
                            range = 20f..100f,
                            steps = 79,
                            onValueChange = { onSetOverlayOpacity(it.toInt()) }
                        )

                        LiveSliderRow(
                            title = stringResource(R.string.emulation_stick_scale),
                            valueLabelForValue = { "$it%" },
                            value = uiState.stickScale.toFloat(),
                            range = 50f..200f,
                            steps = 149,
                            onValueChange = { onSetStickScale(it.toInt()) }
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

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_performance_preset),
                    options = performancePresetOptions(),
                    currentValue = uiState.performancePreset,
                    onValueChange = onSetPerformancePreset
                )

                Text(
                    text = stringResource(performancePresetDescription(uiState.performancePreset)),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = sectionLabelInset)
                )

                MenuButton(
                    icon = Icons.Rounded.SettingsSuggest,
                    text = stringResource(R.string.settings_device_profile_apply),
                    onClick = onApplyRecommendedProfile,
                    containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f),
                    enabled = true
                )

                Text(
                    text = stringResource(deviceProfileDescription(uiState.deviceChipsetFamily)),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = sectionLabelInset)
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_frame_limiter),
                    checked = uiState.frameLimitEnabled,
                    onCheckedChange = onSetFrameLimitEnabled
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_target_fps),
                    valueLabelResId = R.string.settings_target_fps_desc,
                    valueLabelForValue = { fps -> fps.toString() },
                    value = uiState.targetFps.toFloat(),
                    range = 20f..120f,
                    steps = 99,
                    onValueChange = { onSetTargetFps(it.toInt()) }
                )

                // Renderer Selection (IDs: GL=12, Vulkan=14, Soft=13)
                LiveSelectionRow(
                    title = stringResource(R.string.settings_renderer),
                    options = listOf(
                        LiveSelectionOption(12, stringResource(R.string.settings_renderer_opengl)),
                        LiveSelectionOption(14, stringResource(R.string.settings_renderer_vulkan)),
                        LiveSelectionOption(13, stringResource(R.string.settings_renderer_software))
                    ),
                    currentValue = uiState.renderer,
                    onValueChange = onSetRenderer,
                    allowWrap = false
                )

                // Upscale Selection ( 1 -> 1x, 2 -> 2x, 3 -> 3x)
                LiveSelectionRow(
                    title = stringResource(R.string.settings_upscale),
                    options = listOf(
                        LiveSelectionOption(1, "1x"),
                        LiveSelectionOption(2, "2x"),
                        LiveSelectionOption(3, "3x")
                    ),
                    currentValue = uiState.upscale,
                    onValueChange = onSetUpscale
                )

                // Aspect Ratio (Core IDs: Stretch=0, Auto=1, 4:3=2, 16:9=3)
                LiveSelectionRow(
                    title = stringResource(R.string.settings_aspect_ratio).replace(":",""),
                    options = listOf(
                        LiveSelectionOption(1, stringResource(R.string.emulation_aspect_auto)),
                        LiveSelectionOption(2, "4:3"),
                        LiveSelectionOption(3, "16:9"),
                        LiveSelectionOption(
                            value = 0,
                            icon = Icons.Rounded.Fullscreen,
                            contentDescription = stringResource(R.string.emulation_aspect_stretch)
                        )
                    ),
                    currentValue = uiState.aspectRatio,
                    onValueChange = onSetAspectRatio,
                    allowWrap = false
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
                    onCheckedChange = onSetMtvu
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_fast_cdvd),
                    checked = uiState.enableFastCdvd,
                    onCheckedChange = onSetFastCdvd
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_enable_cheats),
                    checked = uiState.enableCheats,
                    onCheckedChange = onSetEnableCheats
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
                    onValueChange = onSetHwDownloadMode
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
                    onValueChange = onSetBlendingAccuracy
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_texture_preloading),
                    options = listOf(
                        0 to stringResource(R.string.settings_texture_preloading_none),
                        1 to stringResource(R.string.settings_texture_preloading_partial),
                        2 to stringResource(R.string.settings_texture_preloading_full)
                    ),
                    currentValue = uiState.texturePreloading,
                    onValueChange = onSetTexturePreloading
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
                    onValueChange = onSetTextureFiltering
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
                    onValueChange = onSetTrilinearFiltering
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
                    onValueChange = onSetAnisotropicFiltering
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_fxaa),
                    checked = uiState.enableFxaa,
                    onCheckedChange = onSetEnableFxaa
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_cas),
                    options = listOf(
                        0 to stringResource(R.string.settings_cas_mode_off),
                        1 to stringResource(R.string.settings_cas_mode_sharpen_only),
                        2 to stringResource(R.string.settings_cas_mode_sharpen_resize)
                    ),
                    currentValue = uiState.casMode,
                    onValueChange = onSetCasMode
                )

                if (uiState.casMode != 0) {
                    LiveSliderRow(
                        title = stringResource(R.string.settings_cas_sharpness),
                        valueLabelForValue = { "$it%" },
                        value = uiState.casSharpness.toFloat(),
                        range = 0f..100f,
                        steps = 99,
                        onValueChange = { onSetCasSharpness(it.toInt()) }
                    )
                }

                SettingsToggle(
                    title = stringResource(R.string.settings_hw_mipmapping),
                    checked = uiState.enableHwMipmapping,
                    onCheckedChange = onSetEnableHwMipmapping
                )

                LiveSelectionRow(
                    title = stringResource(R.string.settings_frame_skip),
                    options = listOf(
                        LiveSelectionOption(0, stringResource(R.string.settings_frame_skip_off)),
                        LiveSelectionOption(1, "1/2"),
                        LiveSelectionOption(2, "1/4")
                    ),
                    currentValue = uiState.frameSkip,
                    onValueChange = onSetFrameSkip
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
                            onValueChange = onSetCpuSpriteRenderSize
                        )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_cpu_sprite_render_level),
                    options = listOf(
                        0 to stringResource(R.string.settings_cpu_sprite_render_level_sprites),
                        1 to stringResource(R.string.settings_cpu_sprite_render_level_triangles),
                        2 to stringResource(R.string.settings_cpu_sprite_render_level_blended)
                    ),
                    currentValue = uiState.cpuSpriteRenderLevel,
                    onValueChange = onSetCpuSpriteRenderLevel
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_software_clut_render),
                    options = listOf(
                        0 to stringResource(R.string.settings_disabled_short),
                        1 to stringResource(R.string.settings_normal_short),
                        2 to stringResource(R.string.settings_aggressive_short)
                    ),
                    currentValue = uiState.softwareClutRender,
                    onValueChange = onSetSoftwareClutRender
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_gpu_target_clut),
                    options = listOf(
                        0 to stringResource(R.string.settings_hw_download_mode_disabled),
                        1 to stringResource(R.string.settings_gpu_target_clut_exact),
                        2 to stringResource(R.string.settings_gpu_target_clut_inside)
                    ),
                    currentValue = uiState.gpuTargetClutMode,
                    onValueChange = onSetGpuTargetClutMode
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_auto_flush_hardware),
                    options = listOf(
                        0 to stringResource(R.string.settings_hw_download_mode_disabled),
                        1 to stringResource(R.string.settings_auto_flush_sprites),
                        2 to stringResource(R.string.settings_auto_flush_all)
                    ),
                    currentValue = uiState.autoFlushHardware,
                    onValueChange = onSetAutoFlushHardware
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_skip_draw_start),
                    valueLabelForValue = { it.toString() },
                    value = uiState.skipDrawStart.toFloat(),
                    range = 0f..100f,
                    steps = 99,
                    onValueChange = { onSetSkipDrawStart(it.toInt()) }
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_skip_draw_end),
                    valueLabelForValue = { it.toString() },
                    value = uiState.skipDrawEnd.toFloat(),
                    range = 0f..100f,
                    steps = 99,
                    onValueChange = { onSetSkipDrawEnd(it.toInt()) }
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_cpu_framebuffer_conversion),
                    checked = uiState.cpuFramebufferConversion,
                    onCheckedChange = onSetCpuFramebufferConversion
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_depth_conversion),
                    checked = uiState.disableDepthConversion,
                    onCheckedChange = onSetDisableDepthConversion
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_safe_features),
                    checked = uiState.disableSafeFeatures,
                    onCheckedChange = onSetDisableSafeFeatures
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_render_fixes),
                    checked = uiState.disableRenderFixes,
                    onCheckedChange = onSetDisableRenderFixes
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_preload_frame_data),
                    checked = uiState.preloadFrameData,
                    onCheckedChange = onSetPreloadFrameData
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_disable_partial_invalidation),
                    checked = uiState.disablePartialInvalidation,
                    onCheckedChange = onSetDisablePartialInvalidation
                )
                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_texture_inside_rt),
                    options = listOf(
                        0 to stringResource(R.string.settings_hw_download_mode_disabled),
                        1 to stringResource(R.string.settings_texture_inside_rt_inside),
                        2 to stringResource(R.string.settings_texture_inside_rt_merge)
                    ),
                    currentValue = uiState.textureInsideRt,
                    onValueChange = onSetTextureInsideRt
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_read_targets_on_close),
                    checked = uiState.readTargetsOnClose,
                    onCheckedChange = onSetReadTargetsOnClose
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_estimate_texture_region),
                    checked = uiState.estimateTextureRegion,
                    onCheckedChange = onSetEstimateTextureRegion
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_gpu_palette_conversion),
                    checked = uiState.gpuPaletteConversion,
                    onCheckedChange = onSetGpuPaletteConversion
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
                    onValueChange = onSetHalfPixelOffset
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_native_scaling),
                    options = listOf(
                        0 to stringResource(R.string.settings_native_scaling_off),
                        1 to stringResource(R.string.settings_native_scaling_normal),
                        2 to stringResource(R.string.settings_native_scaling_aggressive)
                    ),
                    currentValue = uiState.nativeScaling,
                    onValueChange = onSetNativeScaling
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_round_sprite),
                    options = listOf(
                        0 to stringResource(R.string.settings_half_pixel_off),
                        1 to stringResource(R.string.settings_round_sprite_half),
                        2 to stringResource(R.string.settings_round_sprite_full)
                    ),
                    currentValue = uiState.roundSprite,
                    onValueChange = onSetRoundSprite
                )

                LiveChipsSelectionRow(
                    title = stringResource(R.string.settings_bilinear_upscale),
                    options = listOf(
                        0 to stringResource(R.string.settings_trilinear_filtering_auto),
                        1 to stringResource(R.string.settings_bilinear_upscale_force_bilinear),
                        2 to stringResource(R.string.settings_bilinear_upscale_force_nearest)
                    ),
                    currentValue = uiState.bilinearUpscale,
                    onValueChange = onSetBilinearUpscale
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_texture_offset_x),
                    valueLabelForValue = { it.toString() },
                    value = uiState.textureOffsetX.toFloat(),
                    range = -512f..512f,
                    steps = 1023,
                    onValueChange = { onSetTextureOffsetX(it.toInt()) }
                )

                LiveSliderRow(
                    title = stringResource(R.string.settings_texture_offset_y),
                    valueLabelForValue = { it.toString() },
                    value = uiState.textureOffsetY.toFloat(),
                    range = -512f..512f,
                    steps = 1023,
                    onValueChange = { onSetTextureOffsetY(it.toInt()) }
                )

                SettingsToggle(
                    title = stringResource(R.string.settings_align_sprite),
                    checked = uiState.alignSprite,
                    onCheckedChange = onSetAlignSprite
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_merge_sprite),
                    checked = uiState.mergeSprite,
                    onCheckedChange = onSetMergeSprite
                )
                SettingsToggle(
                    title = stringResource(R.string.settings_force_even_sprite_position),
                    checked = uiState.forceEvenSpritePosition,
                    onCheckedChange = onSetForceEvenSpritePosition
                )
                        SettingsToggle(
                            title = stringResource(R.string.settings_native_palette_draw),
                            checked = uiState.nativePaletteDraw,
                            onCheckedChange = onSetNativePaletteDraw
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
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(vertical = 16.dp, horizontal = 10.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Menu,
                    contentDescription = stringResource(R.string.emulation_session_tab),
                    selected = selectedMenuTab == EmulationMenuTab.Session,
                    onClick = { selectedMenuTab = EmulationMenuTab.Session }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Gamepad,
                    contentDescription = stringResource(R.string.emulation_overlay_tab),
                    selected = selectedMenuTab == EmulationMenuTab.Overlay,
                    onClick = { selectedMenuTab = EmulationMenuTab.Overlay }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.SettingsSuggest,
                    contentDescription = stringResource(R.string.emulation_basic_settings),
                    selected = selectedMenuTab == EmulationMenuTab.Basic,
                    onClick = { selectedMenuTab = EmulationMenuTab.Basic }
                )
                EmulationMenuRailButton(
                    icon = Icons.Rounded.Star,
                    contentDescription = stringResource(R.string.emulation_advanced_settings),
                    selected = selectedMenuTab == EmulationMenuTab.Advanced,
                    onClick = { selectedMenuTab = EmulationMenuTab.Advanced }
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
private fun SettingsToggle(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Surface(
        onClick = { onCheckedChange(!checked) },
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = title, 
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium), 
                modifier = Modifier.weight(1f),
                color = MaterialTheme.colorScheme.onSurface
            )
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
    allowWrap: Boolean = true
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = title, 
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold), 
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
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
private fun PerformanceHud(
    fps: String,
    speedPercent: String,
    cpuLoad: String,
    gpuLoad: String,
    frameTime: String,
    targetFps: Int
) {
    Column(
        modifier = Modifier
            .widthIn(max = 220.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        Color.Black.copy(alpha = 0.62f),
                        Color.Black.copy(alpha = 0.46f)
                    )
                )
            )
            .border(
                width = 1.dp,
                color = Color.White.copy(alpha = 0.08f),
                shape = RoundedCornerShape(12.dp)
            )
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            PerformanceHudMetric(
                modifier = Modifier.widthIn(min = 60.dp),
                label = stringResource(R.string.emulation_hud_fps_label),
                value = fps,
                accentColor = if (fps.toFloatOrNull()?.let { it >= 55f } == true) {
                    Color(0xFF50D9A0)
                } else {
                    Color(0xFFFFB85C)
                }
            )
            PerformanceHudMetric(
                modifier = Modifier.widthIn(min = 62.dp),
                label = stringResource(R.string.emulation_hud_speed_label),
                value = "$speedPercent%"
            )
            PerformanceHudMetric(
                modifier = Modifier.widthIn(min = 58.dp),
                label = stringResource(R.string.emulation_hud_cpu_label),
                value = "$cpuLoad%"
            )
        }
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            PerformanceHudMetric(
                modifier = Modifier.widthIn(min = 58.dp),
                label = stringResource(R.string.emulation_hud_gpu_label),
                value = "$gpuLoad%"
            )
            PerformanceHudMetric(
                modifier = Modifier.widthIn(min = 74.dp),
                label = stringResource(R.string.emulation_hud_frame_time_label),
                value = "${frameTime}ms"
            )
            PerformanceHudMetric(
                modifier = Modifier.widthIn(min = 54.dp),
                label = stringResource(R.string.emulation_target_fps_badge, targetFps),
                value = targetFps.toString()
            )
        }
    }
}

@Composable
private fun PerformanceHudMetric(
    modifier: Modifier = Modifier,
    label: String,
    value: String,
    accentColor: Color = Color.White
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(9.dp))
            .background(Color.White.copy(alpha = 0.045f))
            .padding(horizontal = 7.dp, vertical = 5.dp),
        verticalArrangement = Arrangement.spacedBy(1.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(
                fontWeight = FontWeight.SemiBold,
                letterSpacing = 0.2.sp,
                fontSize = 9.sp
            ),
            color = Color.White.copy(alpha = 0.62f)
        )
        Text(
            text = value,
            style = MaterialTheme.typography.labelLarge.copy(
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 0.sp,
                fontSize = 11.sp
            ),
            color = accentColor
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LiveChipsSelectionRow(
    title: String,
    options: List<Pair<Int, String>>,
    currentValue: Int,
    onValueChange: (Int) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
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
private fun performancePresetOptions(): List<Pair<Int, String>> = listOf(
    PerformancePresets.CUSTOM to stringResource(R.string.settings_performance_preset_custom),
    PerformancePresets.BATTERY to stringResource(R.string.settings_performance_preset_battery),
    PerformancePresets.BALANCED to stringResource(R.string.settings_performance_preset_balanced),
    PerformancePresets.PERFORMANCE to stringResource(R.string.settings_performance_preset_performance),
    PerformancePresets.AGGRESSIVE to stringResource(R.string.settings_performance_preset_aggressive)
)

@androidx.annotation.StringRes
private fun performancePresetDescription(preset: Int): Int = when (preset) {
    PerformancePresets.BATTERY -> R.string.settings_performance_preset_battery_desc
    PerformancePresets.BALANCED -> R.string.settings_performance_preset_balanced_desc
    PerformancePresets.PERFORMANCE -> R.string.settings_performance_preset_performance_desc
    PerformancePresets.AGGRESSIVE -> R.string.settings_performance_preset_aggressive_desc
    else -> R.string.settings_performance_preset_custom_desc
}

@Composable
private fun LiveSliderRow(
    title: String,
    @androidx.annotation.StringRes valueLabelResId: Int? = null,
    valueLabelForValue: (Int) -> String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    steps: Int,
    onValueChange: (Float) -> Unit
) {
    var sliderValue by remember { mutableFloatStateOf(value) }

    LaunchedEffect(value) {
        sliderValue = value
    }

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 4.dp)
            )
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

@androidx.annotation.StringRes
private fun deviceProfileDescription(family: DeviceChipsetFamily): Int = when (family) {
    DeviceChipsetFamily.MEDIATEK -> R.string.settings_device_profile_desc_mediatek
    DeviceChipsetFamily.SNAPDRAGON -> R.string.settings_device_profile_desc_snapdragon
    DeviceChipsetFamily.EXYNOS -> R.string.settings_device_profile_desc_generic
    DeviceChipsetFamily.TENSOR -> R.string.settings_device_profile_desc_generic
    DeviceChipsetFamily.GENERIC -> R.string.settings_device_profile_desc_generic
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

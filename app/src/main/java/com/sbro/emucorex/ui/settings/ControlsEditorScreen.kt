package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.app.Activity
import android.content.pm.ActivityInfo
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Remove
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.common.OverlayActionGapLandscape
import com.sbro.emucorex.ui.common.OverlayActionGapPortrait
import com.sbro.emucorex.ui.common.OverlayBottomAnchorPadding
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
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.overlayActionOffset
import com.sbro.emucorex.ui.common.overlayCenterButtonOffset
import com.sbro.emucorex.ui.common.overlayInlineGroupOffset
import com.sbro.emucorex.ui.common.overlayCenterSecondRowOffset
import com.sbro.emucorex.ui.common.overlayClusterStep
import com.sbro.emucorex.ui.common.overlayDpadOffset
import com.sbro.emucorex.ui.common.overlayDrawableForControl
import kotlin.math.roundToInt

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun ControlsEditorScreen(
    state: EmulationUiState,
    onBackClick: () -> Unit,
    manageActivityOrientation: Boolean = true,
    overlayHorizontalSafeInset: Dp? = null,
    overlayTopSafeInset: Dp? = null,
    overlayBottomSafeInset: Dp? = null,
    viewModel: ControlsEditorViewModel = viewModel()
) {
    val context = LocalContext.current
    val activity = context as? Activity
    var selectedControlId by rememberSaveable { mutableStateOf<String?>(null) }
    var editorControlLayouts by remember { mutableStateOf(state.controlLayouts) }
    val defaultLayouts = remember(state.stickScale) { AppPreferences.defaultOverlayControlLayouts(state.stickScale) }
    val isShowingLeftStick = (
        editorControlLayouts["left_stick"]
            ?: defaultLayouts["left_stick"]
            ?: OverlayControlLayout(scale = state.stickScale, visible = true)
        ).visible
    val selectedLayout = selectedControlId?.let { id ->
        editorControlLayouts[id] ?: defaultLayouts[id] ?: OverlayControlLayout()
    }
    val originalOrientation = remember(activity) {
        activity?.requestedOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }

    LaunchedEffect(state.controlLayouts) {
        editorControlLayouts = state.controlLayouts
    }

    fun currentLayoutFor(id: String, defaultScale: Int = 100): OverlayControlLayout {
        return editorControlLayouts[id]
            ?: defaultLayouts[id]
            ?: AppPreferences.defaultOverlayControlLayouts(defaultScale)[id]
            ?: OverlayControlLayout(scale = defaultScale)
    }

    fun moveControlLocally(controlId: String, delta: Pair<Float, Float>) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(
                controlId,
                current.copy(offset = (current.offset.first + delta.first) to (current.offset.second + delta.second))
            )
        }
    }

    fun persistControlPosition(controlId: String) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        viewModel.updateControlOffset(controlId, current.offset)
    }

    fun setControlVisibleLocally(controlId: String, visible: Boolean) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(visible = visible))
        }
        viewModel.setControlVisible(controlId, visible)
    }

    fun setControlScaleLocally(controlId: String, scale: Int) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        val nextScale = scale.coerceIn(50, 200)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(scale = nextScale))
        }
        viewModel.updateControlScale(controlId, nextScale)
    }

    BackHandler(onBack = onBackClick)

    if (manageActivityOrientation) {
        LaunchedEffect(activity) {
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        }
        DisposableEffect(activity) {
            onDispose {
                activity?.requestedOrientation = originalOrientation
            }
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        PreviewLayout(
            state = state,
            controlLayouts = editorControlLayouts,
            selectedControlId = selectedControlId,
            onSelectControl = { selectedControlId = it },
            onMoveControlBy = ::moveControlLocally,
            onCommitControlPosition = ::persistControlPosition,
            overlayHorizontalSafeInset = overlayHorizontalSafeInset,
            overlayTopSafeInset = overlayTopSafeInset,
            overlayBottomSafeInset = overlayBottomSafeInset,
            modifier = Modifier.fillMaxSize()
        )

        Column(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 6.dp, start = 16.dp, end = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Surface(
                color = Color(0xFF2B3F93).copy(alpha = 0.88f),
                shape = RoundedCornerShape(16.dp)
            ) {
                Column(
                    modifier = Modifier.padding(horizontal = 18.dp, vertical = 10.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text(
                        text = stringResource(R.string.controls_editor_title),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = Color.White
                    )
                    selectedControlId?.let {
                        Text(
                            text = controlTitle(it),
                            style = MaterialTheme.typography.labelMedium,
                            color = Color.White.copy(alpha = 0.84f)
                        )
                    }
                }
            }

            Row(
                modifier = Modifier.padding(top = 10.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = {
                        viewModel.toggleLeftInputMode()
                        selectedControlId = if (isShowingLeftStick) "dpad_up" else "left_stick"
                    },
                    shape = RoundedCornerShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = Color.White.copy(alpha = 0.08f),
                        contentColor = Color.White
                    )
                ) {
                    Text(if (isShowingLeftStick) "D-pad" else "Stick")
                }

                OutlinedButton(
                    onClick = { viewModel.resetLayout() },
                    shape = RoundedCornerShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = Color.White.copy(alpha = 0.08f),
                        contentColor = Color.White
                    )
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                }

                OutlinedButton(
                    onClick = {
                        selectedControlId?.let { controlId ->
                            setControlVisibleLocally(controlId, !(selectedLayout?.visible ?: true))
                        }
                    },
                    enabled = selectedControlId != null,
                    shape = RoundedCornerShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = Color.White.copy(alpha = 0.08f),
                        contentColor = Color.White
                    )
                ) {
                    Icon(
                        imageVector = if (selectedLayout?.visible == false) {
                            Icons.Rounded.VisibilityOff
                        } else {
                            Icons.Rounded.Visibility
                        },
                        contentDescription = null
                    )
                }

                OutlinedButton(
                    onClick = { viewModel.setStickSurfaceMode(!state.stickSurfaceMode) },
                    shape = RoundedCornerShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (state.stickSurfaceMode) {
                            Color(0xFF3565FF).copy(alpha = 0.78f)
                        } else {
                            Color.White.copy(alpha = 0.06f)
                        },
                        contentColor = if (state.stickSurfaceMode) Color.White else Color.White.copy(alpha = 0.58f)
                    )
                ) {
                    Icon(Icons.Rounded.TouchApp, contentDescription = null)
                }

                Button(
                    onClick = onBackClick,
                    shape = RoundedCornerShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF3565FF),
                        contentColor = Color.White
                    )
                ) {
                    Text(stringResource(R.string.controls_editor_done))
                }
            }

            selectedControlId?.let { controlId ->
                val scale = selectedLayout?.scale ?: if (controlId.contains("stick")) state.stickScale else 100
                Surface(
                    modifier = Modifier.padding(top = 8.dp),
                    color = Color(0xFF111827).copy(alpha = 0.82f),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { setControlScaleLocally(controlId, scale - 10) },
                            enabled = scale > 50,
                            shape = RoundedCornerShape(14.dp),
                            contentPadding = PaddingValues(horizontal = 10.dp, vertical = 8.dp),
                            colors = ButtonDefaults.outlinedButtonColors(
                                containerColor = Color.White.copy(alpha = 0.08f),
                                contentColor = Color.White
                            )
                        ) {
                            Icon(Icons.Rounded.Remove, contentDescription = null)
                        }
                        Text(
                            text = "$scale%",
                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                            color = Color.White,
                            modifier = Modifier.padding(horizontal = 4.dp)
                        )
                        OutlinedButton(
                            onClick = { setControlScaleLocally(controlId, scale + 10) },
                            enabled = scale < 200,
                            shape = RoundedCornerShape(14.dp),
                            contentPadding = PaddingValues(horizontal = 10.dp, vertical = 8.dp),
                            colors = ButtonDefaults.outlinedButtonColors(
                                containerColor = Color.White.copy(alpha = 0.08f),
                                contentColor = Color.White
                            )
                        ) {
                            Icon(Icons.Rounded.Add, contentDescription = null)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun controlTitle(controlId: String): String = when (controlId) {
    "l2" -> "L2"
    "l1" -> "L1"
    "r2" -> "R2"
    "r1" -> "R1"
    "dpad_up" -> stringResource(R.string.settings_gamepad_action_dpad_up)
    "dpad_down" -> stringResource(R.string.settings_gamepad_action_dpad_down)
    "dpad_left" -> stringResource(R.string.settings_gamepad_action_dpad_left)
    "dpad_right" -> stringResource(R.string.settings_gamepad_action_dpad_right)
    "left_stick" -> "Left Stick"
    "triangle" -> stringResource(R.string.settings_gamepad_action_triangle)
    "square" -> stringResource(R.string.settings_gamepad_action_square)
    "circle" -> stringResource(R.string.settings_gamepad_action_circle)
    "cross" -> stringResource(R.string.settings_gamepad_action_cross)
    "right_stick" -> "Right Stick"
    "select" -> stringResource(R.string.settings_gamepad_action_select)
    "left_input_toggle" -> stringResource(R.string.settings_gamepad_action_left_input_toggle)
    "start" -> stringResource(R.string.settings_gamepad_action_start)
    "l3" -> stringResource(R.string.settings_gamepad_action_l3)
    "r3" -> stringResource(R.string.settings_gamepad_action_r3)
    else -> controlId
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun PreviewLayout(
    state: EmulationUiState,
    controlLayouts: Map<String, OverlayControlLayout>,
    selectedControlId: String?,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit,
    overlayHorizontalSafeInset: Dp? = null,
    overlayTopSafeInset: Dp? = null,
    overlayBottomSafeInset: Dp? = null,
    modifier: Modifier = Modifier
) {
    val configuration = LocalConfiguration.current
    val density = LocalDensity.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val scaleFactor = state.overlayScale / 100f
    val stickScaleFactor = state.stickScale / 100f
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
    val actionStep = overlayClusterStep(actionSize / 3.1f, actionGap)
    val actionButtonSize = actionSize / 3.1f
    val actionClusterExtent = actionStep + actionButtonSize
    val rightStickBaseX = -(actionClusterExtent + 12.dp)

    val baseEdgePad = if (isLandscape) 28.dp else 12.dp
    val baseBottomPad = if (isLandscape) 24.dp else 36.dp
    val cutoutPadding = WindowInsets.displayCutout.asPaddingValues()
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues()
    val safeHorizontalInset = overlayHorizontalSafeInset ?: maxOf(
        maxOf(
            cutoutPadding.calculateLeftPadding(LayoutDirection.Ltr),
            cutoutPadding.calculateRightPadding(LayoutDirection.Ltr)
        ),
        maxOf(
            navBarPadding.calculateLeftPadding(LayoutDirection.Ltr),
            navBarPadding.calculateRightPadding(LayoutDirection.Ltr)
        )
    )
    val safeTop = overlayTopSafeInset ?: maxOf(cutoutPadding.calculateTopPadding(), navBarPadding.calculateTopPadding())
    val safeBottom = overlayBottomSafeInset ?: maxOf(cutoutPadding.calculateBottomPadding(), navBarPadding.calculateBottomPadding())
    val edgePadStart = baseEdgePad + safeHorizontalInset
    val edgePadEnd = baseEdgePad + safeHorizontalInset
    val edgePadTop = maxOf(OverlayShoulderTopPadding, safeTop + 4.dp)
    val bottomPad = baseBottomPad + safeBottom
    fun Float.pxToDp(): Dp = with(density) { this@pxToDp.toDp() }

    fun layoutFor(id: String, defaultScale: Int = 100): OverlayControlLayout {
        return controlLayouts[id]
            ?: AppPreferences.defaultOverlayControlLayouts(defaultScale)[id]
            ?: OverlayControlLayout(scale = defaultScale)
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        Box(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(top = edgePadTop, start = edgePadStart)
                .offset { IntOffset(state.lbtnOffset.first.roundToInt(), state.lbtnOffset.second.roundToInt()) }
        ) {
            PreviewShoulder(
                id = "l2",
                layout = layoutFor("l2"),
                width = shoulderW,
                height = shoulderH,
                selected = selectedControlId == "l2",
                y = 0.dp,
                onSelectControl = onSelectControl,
                onMoveControlBy = onMoveControlBy,
                onCommitControlPosition = onCommitControlPosition
            )
            PreviewShoulder(
                id = "l1",
                layout = layoutFor("l1"),
                width = shoulderW,
                height = shoulderH,
                selected = selectedControlId == "l1",
                y = OverlayShoulderVerticalGap,
                onSelectControl = onSelectControl,
                onMoveControlBy = onMoveControlBy,
                onCommitControlPosition = onCommitControlPosition
            )
        }

        Box(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = edgePadTop, end = edgePadEnd)
                .offset { IntOffset(state.rbtnOffset.first.roundToInt(), state.rbtnOffset.second.roundToInt()) }
        ) {
            PreviewShoulder(
                id = "r2",
                layout = layoutFor("r2"),
                width = shoulderW,
                height = shoulderH,
                selected = selectedControlId == "r2",
                x = OverlayRightShoulderBaseOffset,
                y = 0.dp,
                onSelectControl = onSelectControl,
                onMoveControlBy = onMoveControlBy,
                onCommitControlPosition = onCommitControlPosition
            )
            PreviewShoulder(
                id = "r1",
                layout = layoutFor("r1"),
                width = shoulderW,
                height = shoulderH,
                selected = selectedControlId == "r1",
                x = OverlayRightShoulderBaseOffset,
                y = OverlayRightShoulderGapOffset,
                onSelectControl = onSelectControl,
                onMoveControlBy = onMoveControlBy,
                onCommitControlPosition = onCommitControlPosition
            )
        }

        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = edgePadStart, bottom = bottomPad)
        ) {
            val leftStickLayout = layoutFor("left_stick", state.stickScale)
            listOf(
                "dpad_up" to overlayDpadOffset(dpadStep, "up"),
                "dpad_down" to overlayDpadOffset(dpadStep, "down"),
                "dpad_left" to overlayDpadOffset(dpadStep, "left"),
                "dpad_right" to overlayDpadOffset(dpadStep, "right")
            ).takeIf { !leftStickLayout.visible }?.forEach { (id, baseOffset) ->
                val layout = layoutFor(id)
                PreviewButton(
                    id = id,
                    layout = layout,
                    width = dpadSize / 3f,
                    height = dpadSize / 3f,
                    shape = RoundedCornerShape(8.dp),
                    selected = selectedControlId == id,
                    x = baseOffset.first + state.dpadOffset.first.pxToDp(),
                    y = baseOffset.second + state.dpadOffset.second.pxToDp(),
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }

            val leftStickSize = analogSize * (leftStickLayout.scale / (stickScaleFactor * 100f).coerceAtLeast(1f))
            val leftStickBase = (dpadSize - leftStickSize) / 2f
            if (leftStickLayout.visible) {
                PreviewStick(
                    id = "left_stick",
                    layout = leftStickLayout,
                    selected = selectedControlId == "left_stick",
                    size = leftStickSize,
                    surfaceOnly = state.stickSurfaceMode,
                    x = leftStickBase + state.lstickOffset.first.pxToDp(),
                    y = leftStickBase + state.lstickOffset.second.pxToDp(),
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
        }

        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = edgePadEnd, bottom = bottomPad)
        ) {
            listOf(
                "triangle" to overlayActionOffset(actionStep, "triangle"),
                "square" to overlayActionOffset(actionStep, "square"),
                "circle" to overlayActionOffset(actionStep, "circle"),
                "cross" to overlayActionOffset(actionStep, "cross")
            ).forEach { (id, baseOffset) ->
                val layout = layoutFor(id)
                PreviewButton(
                    id = id,
                    layout = layout,
                    width = actionSize / 3.1f,
                    height = actionSize / 3.1f,
                    shape = CircleShape,
                    selected = selectedControlId == id,
                    x = baseOffset.first + state.actionOffset.first.pxToDp(),
                    y = baseOffset.second + state.actionOffset.second.pxToDp(),
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
        }

        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = edgePadEnd, bottom = bottomPad)
        ) {
            val rightStick = layoutFor("right_stick", state.stickScale)
            PreviewStick(
                id = "right_stick",
                layout = rightStick,
                selected = selectedControlId == "right_stick",
                size = analogSize * (rightStick.scale / (stickScaleFactor * 100f).coerceAtLeast(1f)),
                surfaceOnly = state.stickSurfaceMode,
                x = rightStickBaseX + state.rstickOffset.first.pxToDp(),
                y = state.rstickOffset.second.pxToDp(),
                onSelectControl = onSelectControl,
                onMoveControlBy = onMoveControlBy,
                onCommitControlPosition = onCommitControlPosition
            )
        }

            val selectLayout = layoutFor("select")
            val toggleLayout = layoutFor("left_input_toggle")
            val startLayout = layoutFor("start")
            val l3Layout = layoutFor("l3")
            val r3Layout = layoutFor("r3")
            val l3Width = centerW * (l3Layout.scale / 100f)
            val r3Width = centerW * (r3Layout.scale / 100f)
            val selectWidth = wideCenterW * (selectLayout.scale / 100f)
            val toggleSize = centerH * (toggleLayout.scale / 100f)
            val startWidth = wideCenterW * (startLayout.scale / 100f)
            val visibleCenterItems = buildList {
                if (l3Layout.visible) add("l3" to l3Width)
                if (selectLayout.visible) add("select" to selectWidth)
                if (toggleLayout.visible) add("left_input_toggle" to toggleSize)
                if (startLayout.visible) add("start" to startWidth)
                if (r3Layout.visible) add("r3" to r3Width)
            }
            val visibleCenterWidths = visibleCenterItems.map { it.second }
            fun visibleCenterX(id: String): Dp? {
                val index = visibleCenterItems.indexOfFirst { it.first == id }
                return if (index >= 0) overlayInlineGroupOffset(visibleCenterWidths, centerInlineGap, index) else null
            }
            val visibleCenterBounds = visibleCenterItems.mapNotNull { (id, width) ->
                visibleCenterX(id)?.let { it to (it + width) }
            }
            val visibleCenterLeft = visibleCenterBounds.minOfOrNull { it.first }
            val visibleCenterRight = visibleCenterBounds.maxOfOrNull { it.second }
            val fallbackCenterWidths = listOf(l3Width, selectWidth, toggleSize, startWidth, r3Width)
            val l3X = visibleCenterX("l3") ?: (
                visibleCenterLeft?.let { it - centerInlineGap - l3Width }
                    ?: overlayInlineGroupOffset(fallbackCenterWidths, centerInlineGap, index = 0)
                )
            val selectX = (visibleCenterX("select")
                ?: overlayInlineGroupOffset(fallbackCenterWidths, centerInlineGap, index = 1)) +
                OverlayCenterSelectOpticalNudgeX
            val toggleX = visibleCenterX("left_input_toggle")
                ?: overlayInlineGroupOffset(fallbackCenterWidths, centerInlineGap, index = 2)
            val startX = (visibleCenterX("start")
                ?: overlayInlineGroupOffset(fallbackCenterWidths, centerInlineGap, index = 3)) +
                OverlayCenterStartOpticalNudgeX
            val r3X = visibleCenterX("r3") ?: (
                visibleCenterRight?.let { it + centerInlineGap }
                    ?: overlayInlineGroupOffset(fallbackCenterWidths, centerInlineGap, index = 4)
                )
        Box(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = bottomPad + (OverlayCenterBottomPadding - OverlayBottomAnchorPadding))
                .offset {
                    IntOffset(
                        OverlayCenterBaseShiftX.roundToPx() + state.centerOffset.first.roundToInt(),
                        state.centerOffset.second.roundToInt()
                    )
                }
        ) {
            layoutFor("select").let { layout ->
                PreviewCenter(
                    id = "select",
                    layout = layout,
                    width = selectWidth,
                    height = centerH,
                    x = selectX,
                    selected = selectedControlId == "select",
                    y = 0.dp,
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
            layoutFor("left_input_toggle").let { layout ->
                PreviewCenter(
                    id = "left_input_toggle",
                    layout = layout,
                    width = toggleSize,
                    height = centerH,
                    x = toggleX,
                    selected = selectedControlId == "left_input_toggle",
                    y = OverlayCenterToggleOpticalNudgeY,
                    isToggle = true,
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
            layoutFor("start").let { layout ->
                PreviewCenter(
                    id = "start",
                    layout = layout,
                    width = startWidth,
                    height = centerH,
                    x = startX,
                    selected = selectedControlId == "start",
                    y = 0.dp,
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
            layoutFor("l3").let { layout ->
                PreviewCenter(
                    id = "l3",
                    layout = layout,
                    width = l3Width,
                    height = centerH,
                    x = l3X,
                    selected = selectedControlId == "l3",
                    y = 0.dp,
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
            layoutFor("r3").let { layout ->
                PreviewCenter(
                    id = "r3",
                    layout = layout,
                    width = r3Width,
                    height = centerH,
                    x = r3X,
                    selected = selectedControlId == "r3",
                    y = 0.dp,
                    onSelectControl = onSelectControl,
                    onMoveControlBy = onMoveControlBy,
                    onCommitControlPosition = onCommitControlPosition
                )
            }
        }
    }
}

@Composable
private fun DraggableControl(
    id: String,
    selected: Boolean,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    Box(
        modifier = modifier
            .zIndex(if (selected) 1f else 0f)
            .clickable(
                interactionSource = interactionSource,
                indication = null
            ) {
                onSelectControl(id)
            }
            .pointerInput(id) {
                detectDragGestures(
                    onDragStart = { onSelectControl(id) },
                    onDragEnd = { onCommitControlPosition(id) },
                    onDragCancel = { onCommitControlPosition(id) }
                ) { change, dragAmount ->
                    change.consume()
                    onSelectControl(id)
                    onMoveControlBy(id, dragAmount.x to dragAmount.y)
                }
            }
    ) {
        content()
    }
}

@Composable
private fun PreviewShoulder(
    id: String,
    layout: OverlayControlLayout,
    width: Dp,
    height: Dp,
    selected: Boolean,
    x: Dp = 0.dp,
    y: Dp = 0.dp,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit
) {
    DraggableControl(
        id = id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        modifier = Modifier.offset {
            IntOffset(
                x.roundToPx() + layout.offset.first.roundToInt(),
                y.roundToPx() + layout.offset.second.roundToInt()
            )
        }
    ) {
        VectorOverlayButton(
            drawableRes = requireNotNull(overlayDrawableForControl(id)),
            width = width * (layout.scale / 100f),
            height = height * (layout.scale / 100f),
            shape = RoundedCornerShape(10.dp),
            contentScale = ContentScale.FillBounds,
            alpha = if (layout.visible) 1f else 0.38f,
            selected = selected,
            interactive = false
        )
    }
}

@Composable
private fun PreviewButton(
    id: String,
    layout: OverlayControlLayout,
    width: Dp,
    height: Dp,
    shape: Shape,
    selected: Boolean,
    x: Dp,
    y: Dp,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit
) {
    DraggableControl(
        id = id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        modifier = Modifier.offset {
            IntOffset(
                x.roundToPx() + layout.offset.first.roundToInt(),
                y.roundToPx() + layout.offset.second.roundToInt()
            )
        }
    ) {
        VectorOverlayButton(
            drawableRes = requireNotNull(overlayDrawableForControl(id)),
            width = width * (layout.scale / 100f),
            height = height * (layout.scale / 100f),
            shape = shape,
            alpha = if (layout.visible) 1f else 0.38f,
            selected = selected,
            interactive = false
        )
    }
}

@Composable
private fun PreviewStick(
    id: String,
    layout: OverlayControlLayout,
    selected: Boolean,
    size: Dp,
    surfaceOnly: Boolean = false,
    x: Dp,
    y: Dp,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit
) {
    DraggableControl(
        id = id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        modifier = Modifier.offset {
            IntOffset(
                x.roundToPx() + layout.offset.first.roundToInt(),
                y.roundToPx() + layout.offset.second.roundToInt()
            )
        }
    ) {
        VectorAnalogStick(
            analogSize = size,
            alpha = if (layout.visible) 1f else 0.38f,
            selected = selected,
            surfaceOnly = surfaceOnly,
            interactive = false
        )
    }
}

@Composable
private fun PreviewCenter(
    id: String,
    layout: OverlayControlLayout,
    width: Dp,
    height: Dp,
    x: Dp,
    selected: Boolean,
    y: Dp,
    isToggle: Boolean = false,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit
) {
    val scaledWidth = width * (layout.scale / 100f)
    val scaledHeight = height * (layout.scale / 100f)
    DraggableControl(
        id = id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        modifier = Modifier.offset {
            IntOffset(
                x.roundToPx() + layout.offset.first.roundToInt(),
                y.roundToPx() + layout.offset.second.roundToInt()
            )
        }
    ) {
        VectorOverlayButton(
            drawableRes = requireNotNull(overlayDrawableForControl(id)),
            width = scaledWidth,
            height = scaledHeight,
            shape = if (isToggle) CircleShape else RoundedCornerShape(8.dp),
            alpha = if (layout.visible) 1f else 0.38f,
            selected = selected,
            interactive = false
        )
    }
}

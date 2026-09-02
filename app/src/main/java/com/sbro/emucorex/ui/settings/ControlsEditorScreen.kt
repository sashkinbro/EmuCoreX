package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.app.Activity
import android.content.pm.ActivityInfo
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
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
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.data.TouchControlsLayoutProfile
import com.sbro.emucorex.ui.common.OverlayCanvasButtonSpec
import com.sbro.emucorex.ui.common.OverlayCanvasDpadClusterSpec
import com.sbro.emucorex.ui.common.OverlayCanvasStickSpec
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.VectorDpadCluster
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.buildOverlayCanvasLayout
import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.theme.neon.neonShape

data class ControlsEditorState(
    val overlayScale: Int = 100,
    val touchControlVisualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    val touchControlPressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    val dpadOffset: Pair<Float, Float> = AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y,
    val lstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y,
    val rstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y,
    val actionOffset: Pair<Float, Float> = AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y,
    val lbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y,
    val rbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y,
    val centerOffset: Pair<Float, Float> = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
    val stickScale: Int = 100,
    val controlLayouts: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts()
)

fun EmulationUiState.toControlsEditorState(): ControlsEditorState = ControlsEditorState(
    overlayScale = overlayScale,
    touchControlVisualStyle = touchControlVisualStyle,
    touchControlPressEffect = touchControlPressEffect,
    dpadOffset = dpadOffset,
    lstickOffset = lstickOffset,
    rstickOffset = rstickOffset,
    actionOffset = actionOffset,
    lbtnOffset = lbtnOffset,
    rbtnOffset = rbtnOffset,
    centerOffset = centerOffset,
    stickScale = stickScale,
    controlLayouts = controlLayouts
)

fun TouchControlsLayoutProfile.toControlsEditorState(
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    overlayScale: Int
): ControlsEditorState = ControlsEditorState(
    overlayScale = overlayScale,
    touchControlVisualStyle = visualStyle,
    touchControlPressEffect = pressEffect,
    dpadOffset = dpadOffset,
    lstickOffset = lstickOffset,
    rstickOffset = rstickOffset,
    actionOffset = actionOffset,
    lbtnOffset = lbtnOffset,
    rbtnOffset = rbtnOffset,
    centerOffset = centerOffset,
    stickScale = stickScale,
    controlLayouts = controlLayouts
)

fun ControlsEditorState.toTouchControlsLayoutProfile(): TouchControlsLayoutProfile = TouchControlsLayoutProfile(
    dpadOffset = dpadOffset,
    lstickOffset = lstickOffset,
    rstickOffset = rstickOffset,
    actionOffset = actionOffset,
    lbtnOffset = lbtnOffset,
    rbtnOffset = rbtnOffset,
    centerOffset = centerOffset,
    stickScale = stickScale,
    controlLayouts = controlLayouts
)

private const val ControlGroupDpad = "group_dpad"
private const val ControlGroupActions = "group_actions"
private val ControlGroupIds = setOf(ControlGroupDpad, ControlGroupActions)
private val DpadControlIds = setOf("dpad_up", "dpad_down", "dpad_left", "dpad_right")
private val ActionControlIds = setOf("triangle", "circle", "cross", "square")

private data class PreviewGroupBounds(
    val x: Dp,
    val y: Dp,
    val width: Dp,
    val height: Dp
)

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun ControlsEditorScreen(
    state: ControlsEditorState,
    onBackClick: () -> Unit,
    subtitle: String? = null,
    manageActivityOrientation: Boolean = true,
    overlayLeftSafeInset: Dp? = null,
    overlayRightSafeInset: Dp? = null,
    overlayTopSafeInset: Dp? = null,
    overlayBottomSafeInset: Dp? = null,
    onUpdateControlOffset: (String, Pair<Float, Float>) -> Unit,
    onUpdateControlOffsets: (Map<String, Pair<Float, Float>>) -> Unit,
    onUpdateControlScale: (String, Int) -> Unit,
    onUpdateControlWidthScale: (String, Int) -> Unit,
    onUpdateControlOpacity: (String, Int) -> Unit,
    onToggleLeftInputMode: () -> Unit,
    onSetControlVisible: (String, Boolean) -> Unit,
    onSetStickSurfaceMode: (String, Boolean) -> Unit,
    onResetLayout: () -> Unit
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
    val selectedIsGroup = selectedControlId?.let { it in ControlGroupIds } == true
    val selectedIsStick = selectedControlId == "left_stick" || selectedControlId == "right_stick"
    val selectedStickSurfaceMode = selectedIsStick && (selectedLayout?.surfaceOnly == true)
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

    fun setControlOffsetLocally(controlId: String, offset: Pair<Float, Float>) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(offset = offset))
        }
    }

    fun persistControlPosition(controlId: String) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        onUpdateControlOffset(controlId, current.offset)
    }

    fun persistControlPositions(controlIds: List<String>) {
        val offsets = controlIds.associateWith { controlId ->
            currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100).offset
        }
        onUpdateControlOffsets(offsets)
    }

    fun setControlVisibleLocally(controlId: String, visible: Boolean) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(visible = visible))
        }
        onSetControlVisible(controlId, visible)
    }

    fun setControlScaleLocally(controlId: String, scale: Int) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        val nextScale = scale.coerceIn(
            AppPreferences.OVERLAY_CONTROL_SCALE_MIN,
            AppPreferences.OVERLAY_CONTROL_SCALE_MAX
        )
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(scale = nextScale))
        }
        onUpdateControlScale(controlId, nextScale)
    }

    fun setControlWidthScaleLocally(controlId: String, widthScale: Int) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        val nextWidthScale = widthScale.coerceIn(100, 240)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(widthScale = nextWidthScale))
        }
        onUpdateControlWidthScale(controlId, nextWidthScale)
    }

    fun setControlOpacityLocally(controlId: String, opacity: Int) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        val nextOpacity = opacity.coerceIn(
            AppPreferences.OVERLAY_CONTROL_OPACITY_MIN,
            AppPreferences.OVERLAY_CONTROL_OPACITY_MAX
        )
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(opacity = nextOpacity))
        }
        onUpdateControlOpacity(controlId, nextOpacity)
    }

    fun setStickSurfaceModeLocally(controlId: String, enabled: Boolean) {
        if (controlId != "left_stick" && controlId != "right_stick") return
        val current = currentLayoutFor(controlId, state.stickScale)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(surfaceOnly = enabled))
        }
        onSetStickSurfaceMode(controlId, enabled)
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
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black.copy(alpha = 0.34f))
        )

        PreviewLayout(
            state = state,
            controlLayouts = editorControlLayouts,
            selectedControlId = selectedControlId,
            onSelectControl = { selectedControlId = it },
            onSetControlOffset = ::setControlOffsetLocally,
            onCommitControlPosition = ::persistControlPosition,
            onCommitControlPositions = ::persistControlPositions,
            overlayLeftSafeInset = overlayLeftSafeInset,
            overlayRightSafeInset = overlayRightSafeInset,
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
                shape = neonShape(16.dp)
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
                    subtitle?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.labelMedium,
                            color = Color.White.copy(alpha = 0.84f),
                            maxLines = 1
                        )
                    }
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
                        onToggleLeftInputMode()
                        selectedControlId = if (isShowingLeftStick) "dpad_up" else "left_stick"
                    },
                    shape = neonShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = Color.White.copy(alpha = 0.08f),
                        contentColor = Color.White
                    )
                ) {
                    Text(if (isShowingLeftStick) "D-pad" else "Stick")
                }

                OutlinedButton(
                    onClick = { onResetLayout() },
                    shape = neonShape(16.dp),
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
                            if (controlId !in ControlGroupIds) {
                                setControlVisibleLocally(controlId, !(selectedLayout?.visible ?: true))
                            }
                        }
                    },
                    enabled = selectedControlId != null && !selectedIsGroup,
                    shape = neonShape(16.dp),
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
                    onClick = {
                        selectedControlId?.let { controlId ->
                            setStickSurfaceModeLocally(controlId, !selectedStickSurfaceMode)
                        }
                    },
                    enabled = selectedIsStick,
                    shape = neonShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (selectedStickSurfaceMode) {
                            Color(0xFF3565FF).copy(alpha = 0.78f)
                        } else {
                            Color.White.copy(alpha = 0.06f)
                        },
                        contentColor = if (selectedStickSurfaceMode) Color.White else Color.White.copy(alpha = 0.58f)
                    )
                ) {
                    Icon(Icons.Rounded.TouchApp, contentDescription = null)
                }

                Button(
                    onClick = onBackClick,
                    shape = neonShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF3565FF),
                        contentColor = Color.White
                    )
                ) {
                    Text(stringResource(R.string.controls_editor_done))
                }
            }

            selectedControlId?.takeUnless { it in ControlGroupIds }?.let { controlId ->
                val scale = selectedLayout?.scale ?: if (controlId.contains("stick")) state.stickScale else 100
                val isStickPanel = (controlId == "left_stick" || controlId == "right_stick") &&
                    (selectedLayout?.surfaceOnly == true)
                Surface(
                    modifier = Modifier.padding(top = 8.dp),
                    color = Color(0xFF111827).copy(alpha = 0.82f),
                    shape = neonShape(16.dp)
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { setControlScaleLocally(controlId, scale - 10) },
                            enabled = scale > AppPreferences.OVERLAY_CONTROL_SCALE_MIN,
                            shape = neonShape(14.dp),
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
                            enabled = scale < AppPreferences.OVERLAY_CONTROL_SCALE_MAX,
                            shape = neonShape(14.dp),
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

                val opacity = selectedLayout?.opacity ?: AppPreferences.OVERLAY_CONTROL_OPACITY_DEFAULT
                Surface(
                    modifier = Modifier.padding(top = 8.dp),
                    color = Color(0xFF111827).copy(alpha = 0.82f),
                    shape = neonShape(16.dp)
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { setControlOpacityLocally(controlId, opacity - 10) },
                            enabled = opacity > AppPreferences.OVERLAY_CONTROL_OPACITY_MIN,
                            shape = neonShape(14.dp),
                            contentPadding = PaddingValues(horizontal = 10.dp, vertical = 8.dp),
                            colors = ButtonDefaults.outlinedButtonColors(
                                containerColor = Color.White.copy(alpha = 0.08f),
                                contentColor = Color.White
                            )
                        ) {
                            Icon(Icons.Rounded.Remove, contentDescription = null)
                        }
                        Text(
                            text = stringResource(R.string.controls_editor_opacity_value, opacity),
                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                            color = Color.White,
                            modifier = Modifier.padding(horizontal = 4.dp)
                        )
                        OutlinedButton(
                            onClick = { setControlOpacityLocally(controlId, opacity + 10) },
                            enabled = opacity < AppPreferences.OVERLAY_CONTROL_OPACITY_MAX,
                            shape = neonShape(14.dp),
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

                if (isStickPanel) {
                    val widthScale = selectedLayout.widthScale
                    Surface(
                        modifier = Modifier.padding(top = 8.dp),
                        color = Color(0xFF111827).copy(alpha = 0.82f),
                        shape = neonShape(16.dp)
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            OutlinedButton(
                                onClick = { setControlWidthScaleLocally(controlId, widthScale - 10) },
                                enabled = widthScale > 100,
                                shape = neonShape(14.dp),
                                contentPadding = PaddingValues(horizontal = 10.dp, vertical = 8.dp),
                                colors = ButtonDefaults.outlinedButtonColors(
                                    containerColor = Color.White.copy(alpha = 0.08f),
                                    contentColor = Color.White
                                )
                            ) {
                                Icon(Icons.Rounded.Remove, contentDescription = null)
                            }
                            Text(
                                text = "W $widthScale%",
                                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                                color = Color.White,
                                modifier = Modifier.padding(horizontal = 4.dp)
                            )
                            OutlinedButton(
                                onClick = { setControlWidthScaleLocally(controlId, widthScale + 10) },
                                enabled = widthScale < 240,
                                shape = neonShape(14.dp),
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
}

@Composable
private fun controlTitle(controlId: String): String = when (controlId) {
    "l2" -> "L2"
    "l1" -> "L1"
    "r2" -> "R2"
    "r1" -> "R1"
    ControlGroupDpad -> "D-pad"
    ControlGroupActions -> "Face Buttons"
    "dpad_up" -> stringResource(R.string.settings_gamepad_action_dpad_up)
    "dpad_down" -> stringResource(R.string.settings_gamepad_action_dpad_down)
    "dpad_left" -> stringResource(R.string.settings_gamepad_action_dpad_left)
    "dpad_right" -> stringResource(R.string.settings_gamepad_action_dpad_right)
    "dpad_cluster" -> "Extra D-pad"
    "left_stick" -> "Left Stick"
    "triangle" -> stringResource(R.string.settings_gamepad_action_triangle)
    "square" -> stringResource(R.string.settings_gamepad_action_square)
    "circle" -> stringResource(R.string.settings_gamepad_action_circle)
    "cross" -> stringResource(R.string.settings_gamepad_action_cross)
    "right_stick" -> "Right Stick"
    "select" -> stringResource(R.string.settings_gamepad_action_select)
    "left_input_toggle" -> stringResource(R.string.settings_stick_toggle_button)
    "pressure" -> stringResource(R.string.settings_gamepad_action_pressure)
    "start" -> stringResource(R.string.settings_gamepad_action_start)
    "l3" -> stringResource(R.string.settings_gamepad_action_l3)
    "r3" -> stringResource(R.string.settings_gamepad_action_r3)
    else -> controlId
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun PreviewLayout(
    state: ControlsEditorState,
    controlLayouts: Map<String, OverlayControlLayout>,
    selectedControlId: String?,
    onSelectControl: (String) -> Unit,
    onSetControlOffset: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit,
    onCommitControlPositions: (List<String>) -> Unit,
    modifier: Modifier = Modifier,
    overlayLeftSafeInset: Dp? = null,
    overlayRightSafeInset: Dp? = null,
    overlayTopSafeInset: Dp? = null,
    overlayBottomSafeInset: Dp? = null
) {
    val density = LocalDensity.current
    val safeDrawingPadding = WindowInsets.safeDrawing.asPaddingValues()
    val safeLeftInset = overlayLeftSafeInset
        ?: safeDrawingPadding.calculateLeftPadding(LayoutDirection.Ltr)
    val safeRightInset = overlayRightSafeInset
        ?: safeDrawingPadding.calculateRightPadding(LayoutDirection.Ltr)
    val safeTop = overlayTopSafeInset
        ?: safeDrawingPadding.calculateTopPadding()
    val safeBottom = overlayBottomSafeInset ?: safeDrawingPadding.calculateBottomPadding()
    BoxWithConstraints(
        modifier = modifier.fillMaxSize()
    ) {
        val layout = buildOverlayCanvasLayout(
            canvasWidth = maxWidth,
            canvasHeight = maxHeight,
            density = density,
            scaleFactor = state.overlayScale / 100f,
            stickScaleFactor = state.stickScale / 100f,
            dpadOffset = state.dpadOffset,
            lstickOffset = state.lstickOffset,
            rstickOffset = state.rstickOffset,
            actionOffset = state.actionOffset,
            lbtnOffset = state.lbtnOffset,
            rbtnOffset = state.rbtnOffset,
            centerOffset = state.centerOffset,
            controlLayouts = controlLayouts,
            safeLeftInset = safeLeftInset,
            safeRightInset = safeRightInset,
            safeTopInset = safeTop,
            safeBottomInset = safeBottom,
            previewMode = true
        )

        val showLeftStick = layout.leftStick?.visible == true
        val showIndependentDpad = layout.dpadCluster?.visible == true

        fun shouldShowButton(id: String): Boolean = when (id) {
            "dpad_up", "dpad_down", "dpad_left", "dpad_right" -> !showLeftStick && !showIndependentDpad
            else -> true
        }

        fun clampOffset(
            currentOffset: Pair<Float, Float>,
            delta: Pair<Float, Float>,
            baseX: Dp,
            baseY: Dp,
            width: Dp,
            height: Dp
        ): Pair<Float, Float> {
            val baseXPx = with(density) { baseX.toPx() }
            val baseYPx = with(density) { baseY.toPx() }
            val currentX = baseXPx + currentOffset.first
            val currentY = baseYPx + currentOffset.second
            val widthPx = with(density) { width.toPx() }
            val heightPx = with(density) { height.toPx() }
            val canvasWidthPx = with(density) { maxWidth.toPx() }
            val canvasHeightPx = with(density) { maxHeight.toPx() }
            val safeLeftPx = with(density) { safeLeftInset.toPx() }
            val safeRightPx = with(density) { safeRightInset.toPx() }
            val safeTopPx = with(density) { safeTop.toPx() }
            val safeBottomPx = with(density) { safeBottom.toPx() }
            val nextX = (currentX + delta.first).coerceIn(
                safeLeftPx,
                (canvasWidthPx - safeRightPx - widthPx).coerceAtLeast(safeLeftPx)
            )
            val nextY = (currentY + delta.second).coerceIn(
                safeTopPx,
                (canvasHeightPx - safeBottomPx - heightPx).coerceAtLeast(safeTopPx)
            )
            return (nextX - baseXPx) to (nextY - baseYPx)
        }

        fun moveButton(controlId: String, spec: OverlayCanvasButtonSpec, delta: Pair<Float, Float>) {
            val current = controlLayouts[controlId] ?: OverlayControlLayout()
            onSetControlOffset(
                controlId,
                clampOffset(
                    currentOffset = current.offset,
                    delta = delta,
                    baseX = spec.baseX,
                    baseY = spec.baseY,
                    width = spec.width,
                    height = spec.height
                )
            )
        }

        fun stickSurfaceMode(controlId: String): Boolean {
            return (
                controlLayouts[controlId]
                    ?: AppPreferences.defaultOverlayControlLayouts(state.stickScale)[controlId]
                    ?: OverlayControlLayout(scale = state.stickScale)
                ).surfaceOnly
        }

        fun stickPanelWidth(spec: OverlayCanvasStickSpec): Dp {
            return if (stickSurfaceMode(spec.id)) spec.size * (spec.widthScale / 100f) else spec.size
        }

        fun stickPanelX(spec: OverlayCanvasStickSpec): Dp {
            val width = stickPanelWidth(spec)
            return if (stickSurfaceMode(spec.id)) spec.x - ((width - spec.size) / 2f) else spec.x
        }

        fun stickPanelBaseX(spec: OverlayCanvasStickSpec): Dp {
            val width = stickPanelWidth(spec)
            return if (stickSurfaceMode(spec.id)) spec.baseX - ((width - spec.size) / 2f) else spec.baseX
        }

        fun moveStick(controlId: String, spec: OverlayCanvasStickSpec, delta: Pair<Float, Float>) {
            val current = controlLayouts[controlId] ?: OverlayControlLayout(scale = state.stickScale)
            onSetControlOffset(
                controlId,
                clampOffset(
                    currentOffset = current.offset,
                    delta = delta,
                    baseX = stickPanelBaseX(spec),
                    baseY = spec.baseY,
                    width = stickPanelWidth(spec),
                    height = spec.size
                )
            )
        }

        fun moveDpadCluster(controlId: String, spec: OverlayCanvasDpadClusterSpec, delta: Pair<Float, Float>) {
            val current = controlLayouts[controlId] ?: OverlayControlLayout()
            onSetControlOffset(
                controlId,
                clampOffset(
                    currentOffset = current.offset,
                    delta = delta,
                    baseX = spec.baseX,
                    baseY = spec.baseY,
                    width = spec.size,
                    height = spec.size
                )
            )
        }

        fun buttonGroupBounds(specs: List<OverlayCanvasButtonSpec>): PreviewGroupBounds? {
            if (specs.isEmpty()) return null
            val padding = 10.dp
            val rawLeft = specs.minOf { it.x } - padding
            val rawTop = specs.minOf { it.y } - padding
            val rawRight = specs.maxOf { it.x + it.width } + padding
            val rawBottom = specs.maxOf { it.y + it.height } + padding
            val left = rawLeft.coerceAtLeast(0.dp)
            val top = rawTop.coerceAtLeast(0.dp)
            val right = rawRight.coerceAtMost(maxWidth)
            val bottom = rawBottom.coerceAtMost(maxHeight)
            return PreviewGroupBounds(
                x = left,
                y = top,
                width = (right - left).coerceAtLeast(1.dp),
                height = (bottom - top).coerceAtLeast(1.dp)
            )
        }

        fun clampGroupDelta(specs: List<OverlayCanvasButtonSpec>, delta: Pair<Float, Float>): Pair<Float, Float> {
            if (specs.isEmpty()) return 0f to 0f
            val minX = specs.minOf { with(density) { it.x.toPx() } }
            val minY = specs.minOf { with(density) { it.y.toPx() } }
            val maxX = specs.maxOf { with(density) { (it.x + it.width).toPx() } }
            val maxY = specs.maxOf { with(density) { (it.y + it.height).toPx() } }
            val canvasWidthPx = with(density) { maxWidth.toPx() }
            val canvasHeightPx = with(density) { maxHeight.toPx() }
            val dx = delta.first.coerceIn(-minX, canvasWidthPx - maxX)
            val dy = delta.second.coerceIn(-minY, canvasHeightPx - maxY)
            return dx to dy
        }

        fun moveButtonGroup(specs: List<OverlayCanvasButtonSpec>, delta: Pair<Float, Float>) {
            val clampedDelta = clampGroupDelta(specs, delta)
            specs.forEach { spec -> moveButton(spec.id, spec, clampedDelta) }
        }

        fun commitButtonGroup(specs: List<OverlayCanvasButtonSpec>) {
            onCommitControlPositions(specs.map { it.id })
        }

        val visibleButtonSpecs = layout.allButtons.filter { shouldShowButton(it.id) }
        val actionGroupSpecs = visibleButtonSpecs.filter { it.id in ActionControlIds }
        val dpadGroupSpecs = visibleButtonSpecs.filter { it.id in DpadControlIds }

        buttonGroupBounds(dpadGroupSpecs)?.let { bounds ->
            PreviewCanvasButtonGroup(
                id = ControlGroupDpad,
                bounds = bounds,
                selected = selectedControlId == ControlGroupDpad,
                onSelectControl = onSelectControl,
                onMoveGroupBy = { delta -> moveButtonGroup(dpadGroupSpecs, delta) },
                onCommitGroupPosition = { commitButtonGroup(dpadGroupSpecs) }
            )
        }

        buttonGroupBounds(actionGroupSpecs)?.let { bounds ->
            PreviewCanvasButtonGroup(
                id = ControlGroupActions,
                bounds = bounds,
                selected = selectedControlId == ControlGroupActions,
                onSelectControl = onSelectControl,
                onMoveGroupBy = { delta -> moveButtonGroup(actionGroupSpecs, delta) },
                onCommitGroupPosition = { commitButtonGroup(actionGroupSpecs) }
            )
        }

        visibleButtonSpecs.forEach { spec ->
            val baseZIndex = when (spec.id) {
                "select", "left_input_toggle", "pressure", "start", "l3", "r3" -> 3f
                else -> 1f
            }
            PreviewCanvasButton(
                spec = spec,
                visualStyle = state.touchControlVisualStyle,
                pressEffect = state.touchControlPressEffect,
                selected = selectedControlId == spec.id,
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveButton(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition,
                baseZIndex = baseZIndex
            )
        }

        layout.dpadCluster?.let { spec ->
            PreviewCanvasDpadCluster(
                spec = spec,
                visualStyle = state.touchControlVisualStyle,
                pressEffect = state.touchControlPressEffect,
                selected = selectedControlId == spec.id,
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveDpadCluster(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition
            )
        }

        layout.leftStick
            ?.takeIf { showLeftStick }
            ?.let { spec ->
            PreviewCanvasStick(
                spec = spec,
                visualStyle = state.touchControlVisualStyle,
                pressEffect = state.touchControlPressEffect,
                selected = selectedControlId == spec.id,
                surfaceOnly = stickSurfaceMode(spec.id),
                panelWidth = stickPanelWidth(spec),
                panelX = stickPanelX(spec),
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveStick(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition
            )
        }

        layout.rightStick
            ?.let { spec ->
            PreviewCanvasStick(
                spec = spec,
                visualStyle = state.touchControlVisualStyle,
                pressEffect = state.touchControlPressEffect,
                selected = selectedControlId == spec.id,
                surfaceOnly = stickSurfaceMode(spec.id),
                panelWidth = stickPanelWidth(spec),
                panelX = stickPanelX(spec),
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveStick(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition
            )
        }
    }
}

@Composable
private fun PreviewCanvasDpadCluster(
    spec: OverlayCanvasDpadClusterSpec,
    visualStyle: com.sbro.emucorex.data.TouchControlVisualStyle,
    pressEffect: com.sbro.emucorex.data.TouchControlPressEffect,
    selected: Boolean,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit
) {
    DraggableControl(
        id = spec.id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        modifier = Modifier.offset {
            IntOffset(
                spec.x.roundToPx(),
                spec.y.roundToPx()
            )
        },
        baseZIndex = 1.5f
    ) {
        VectorDpadCluster(
            size = spec.size,
            alpha = if (spec.visible) spec.opacity / 100f else 0.38f,
            selected = selected,
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect
        )
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
    baseZIndex: Float = 0f,
    selectedZBoost: Float = 10f,
    content: @Composable () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val currentOnSelectControl by rememberUpdatedState(onSelectControl)
    val currentOnMoveControlBy by rememberUpdatedState(onMoveControlBy)
    val currentOnCommitControlPosition by rememberUpdatedState(onCommitControlPosition)
    Box(
        modifier = modifier
            .zIndex(baseZIndex + if (selected) selectedZBoost else 0f)
            .clickable(
                interactionSource = interactionSource,
                indication = null
            ) {
                currentOnSelectControl(id)
            }
            .pointerInput(id) {
                detectDragGestures(
                    onDragStart = { currentOnSelectControl(id) },
                    onDragEnd = { currentOnCommitControlPosition(id) },
                    onDragCancel = { currentOnCommitControlPosition(id) }
                ) { change, dragAmount ->
                    change.consume()
                    currentOnSelectControl(id)
                    currentOnMoveControlBy(id, dragAmount.x to dragAmount.y)
                }
            }
    ) {
        content()
    }
}

@Composable
private fun PreviewCanvasButton(
    spec: OverlayCanvasButtonSpec,
    visualStyle: com.sbro.emucorex.data.TouchControlVisualStyle,
    pressEffect: com.sbro.emucorex.data.TouchControlPressEffect,
    selected: Boolean,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit,
    baseZIndex: Float = 0f
) {
    DraggableControl(
        id = spec.id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        baseZIndex = baseZIndex,
        modifier = Modifier.offset {
            IntOffset(
                spec.x.roundToPx(),
                spec.y.roundToPx()
            )
        }
    ) {
        VectorOverlayButton(
            drawableRes = spec.drawableRes,
            width = spec.width,
            height = spec.height,
            shape = spec.shape,
            alpha = if (spec.visible) spec.opacity / 100f else 0.38f,
            selected = selected,
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect
        )
    }
}

@Composable
private fun PreviewCanvasButtonGroup(
    id: String,
    bounds: PreviewGroupBounds,
    selected: Boolean,
    onSelectControl: (String) -> Unit,
    onMoveGroupBy: (Pair<Float, Float>) -> Unit,
    onCommitGroupPosition: () -> Unit
) {
    val shape = neonShape(22.dp)
    DraggableControl(
        id = id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = { _, delta -> onMoveGroupBy(delta) },
        onCommitControlPosition = { onCommitGroupPosition() },
        modifier = Modifier.offset {
            IntOffset(
                bounds.x.roundToPx(),
                bounds.y.roundToPx()
            )
        },
        baseZIndex = 0.2f,
        selectedZBoost = 0.4f
    ) {
        Box(
            modifier = Modifier
                .size(width = bounds.width, height = bounds.height)
                .clip(shape)
                .background(
                    if (selected) {
                        Color(0xFF7CC8FF).copy(alpha = 0.11f)
                    } else {
                        Color.White.copy(alpha = 0.04f)
                    }
                )
                .border(
                    width = 1.dp,
                    color = if (selected) {
                        Color(0xFF7CC8FF).copy(alpha = 0.72f)
                    } else {
                        Color.White.copy(alpha = 0.20f)
                    },
                    shape = shape
                )
        )
    }
}

@Composable
private fun PreviewCanvasStick(
    spec: OverlayCanvasStickSpec,
    visualStyle: com.sbro.emucorex.data.TouchControlVisualStyle,
    pressEffect: com.sbro.emucorex.data.TouchControlPressEffect,
    selected: Boolean,
    surfaceOnly: Boolean = false,
    panelWidth: Dp = spec.size,
    panelX: Dp = spec.x,
    onSelectControl: (String) -> Unit,
    onMoveControlBy: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit
) {
    DraggableControl(
        id = spec.id,
        selected = selected,
        onSelectControl = onSelectControl,
        onMoveControlBy = onMoveControlBy,
        onCommitControlPosition = onCommitControlPosition,
        modifier = Modifier.offset {
            IntOffset(
                panelX.roundToPx(),
                spec.y.roundToPx()
            )
        },
        baseZIndex = 2f
    ) {
        VectorAnalogStick(
            analogSize = spec.size,
            analogWidth = panelWidth,
            analogHeight = spec.size,
            alpha = if (spec.visible) spec.opacity / 100f else 0.38f,
            selected = selected,
            surfaceOnly = surfaceOnly,
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect
        )
    }
}

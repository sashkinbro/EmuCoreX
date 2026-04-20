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
import androidx.compose.foundation.layout.BoxWithConstraints
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
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.buildOverlayCanvasLayout

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

    fun setControlOffsetLocally(controlId: String, offset: Pair<Float, Float>) {
        val current = currentLayoutFor(controlId, if (controlId.contains("stick")) state.stickScale else 100)
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(offset = offset))
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
            onSetControlOffset = ::setControlOffsetLocally,
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
    onSetControlOffset: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit,
    modifier: Modifier = Modifier,
    overlayHorizontalSafeInset: Dp? = null,
    overlayTopSafeInset: Dp? = null,
    overlayBottomSafeInset: Dp? = null
) {
    val density = LocalDensity.current
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
    BoxWithConstraints(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black)
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
            safeHorizontalInset = safeHorizontalInset,
            safeTopInset = safeTop,
            safeBottomInset = safeBottom,
            previewMode = true
        )

        val showLeftStick = layout.leftStick?.visible == true

        fun shouldShowButton(id: String): Boolean = when (id) {
            "dpad_up", "dpad_down", "dpad_left", "dpad_right" -> !showLeftStick
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
            val nextX = (currentX + delta.first).coerceIn(0f, (canvasWidthPx - widthPx).coerceAtLeast(0f))
            val nextY = (currentY + delta.second).coerceIn(0f, (canvasHeightPx - heightPx).coerceAtLeast(0f))
            return (nextX - baseXPx) to (nextY - baseYPx)
        }

        fun moveButton(controlId: String, spec: com.sbro.emucorex.ui.common.OverlayCanvasButtonSpec, delta: Pair<Float, Float>) {
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

        fun moveStick(controlId: String, spec: com.sbro.emucorex.ui.common.OverlayCanvasStickSpec, delta: Pair<Float, Float>) {
            val current = controlLayouts[controlId] ?: OverlayControlLayout(scale = state.stickScale)
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

        layout.allButtons
            .filter { shouldShowButton(it.id) }
            .forEach { spec ->
            val baseZIndex = when (spec.id) {
                "select", "left_input_toggle", "start", "l3", "r3" -> 3f
                else -> 1f
            }
            PreviewCanvasButton(
                spec = spec,
                selected = selectedControlId == spec.id,
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveButton(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition,
                baseZIndex = baseZIndex
            )
        }

        layout.leftStick
            ?.takeIf { showLeftStick }
            ?.let { spec ->
            PreviewCanvasStick(
                spec = spec,
                selected = selectedControlId == spec.id,
                surfaceOnly = state.stickSurfaceMode,
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveStick(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition
            )
        }

        layout.rightStick
            ?.let { spec ->
            PreviewCanvasStick(
                spec = spec,
                selected = selectedControlId == spec.id,
                surfaceOnly = state.stickSurfaceMode,
                onSelectControl = onSelectControl,
                onMoveControlBy = { id, delta -> moveStick(id, spec, delta) },
                onCommitControlPosition = onCommitControlPosition
            )
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
    baseZIndex: Float = 0f,
    content: @Composable () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val currentOnSelectControl by rememberUpdatedState(onSelectControl)
    val currentOnMoveControlBy by rememberUpdatedState(onMoveControlBy)
    val currentOnCommitControlPosition by rememberUpdatedState(onCommitControlPosition)
    Box(
        modifier = modifier
            .zIndex(baseZIndex + if (selected) 10f else 0f)
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
    spec: com.sbro.emucorex.ui.common.OverlayCanvasButtonSpec,
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
            alpha = if (spec.visible) 1f else 0.38f,
            selected = selected,
            interactive = false
        )
    }
}

@Composable
private fun PreviewCanvasStick(
    spec: com.sbro.emucorex.ui.common.OverlayCanvasStickSpec,
    selected: Boolean,
    surfaceOnly: Boolean = false,
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
        baseZIndex = 2f
    ) {
        VectorAnalogStick(
            analogSize = spec.size,
            alpha = if (spec.visible) 1f else 0.38f,
            selected = selected,
            surfaceOnly = surfaceOnly,
            interactive = false
        )
    }
}

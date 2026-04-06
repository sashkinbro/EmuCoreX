package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.content.pm.ActivityInfo
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.ui.common.OverlayBottomAnchorPadding
import com.sbro.emucorex.ui.common.OverlayCenterBaseShiftX
import com.sbro.emucorex.ui.common.OverlayCenterBottomPadding
import com.sbro.emucorex.ui.common.OverlayCenterColumnGapLandscape
import com.sbro.emucorex.ui.common.OverlayCenterColumnGapPortrait
import com.sbro.emucorex.ui.common.OverlayCenterRowGapLandscape
import com.sbro.emucorex.ui.common.OverlayCenterRowGapPortrait
import com.sbro.emucorex.ui.common.OverlayClusterGapLandscape
import com.sbro.emucorex.ui.common.OverlayClusterGapPortrait
import com.sbro.emucorex.ui.common.OverlayLeftStickBaseOffset
import com.sbro.emucorex.ui.common.OverlayRightShoulderBaseOffset
import com.sbro.emucorex.ui.common.OverlayRightShoulderGapOffset
import com.sbro.emucorex.ui.common.OverlayRightStickBaseOffset
import com.sbro.emucorex.ui.common.OverlayShoulderTopPadding
import com.sbro.emucorex.ui.common.overlayActionOffset
import com.sbro.emucorex.ui.common.overlayCenterButtonOffset
import com.sbro.emucorex.ui.common.overlayCenterSecondRowOffset
import com.sbro.emucorex.ui.common.overlayClusterStep
import com.sbro.emucorex.ui.common.overlayDpadOffset
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

private data class EditorControlSpec(
    val id: String,
    val anchor: EditorAnchor,
    val title: String,
    val baseOffset: Pair<Dp, Dp> = 0.dp to 0.dp,
    val wide: Boolean = false,
    val shape: EditorControlShape
)

private enum class EditorAnchor {
    TopLeft, TopRight, BottomLeft, BottomRight, BottomCenter
}

private enum class EditorControlShape {
    Dpad, Action, Stick, Shoulder, Center
}

@Stable
private class EditorControlDraftState(initial: OverlayControlLayout) {
    var offsetX by mutableFloatStateOf(initial.offset.first)
    var offsetY by mutableFloatStateOf(initial.offset.second)
    var scale by mutableIntStateOf(initial.scale)
    var visible by mutableStateOf(initial.visible)

    fun updateFrom(layout: OverlayControlLayout) {
        offsetX = layout.offset.first
        offsetY = layout.offset.second
        scale = layout.scale
        visible = layout.visible
    }

    fun toLayout(): OverlayControlLayout {
        return OverlayControlLayout(
            offset = offsetX to offsetY,
            scale = scale,
            visible = visible
        )
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun ControlsEditorScreen(
    onBackClick: () -> Unit,
    manageActivityOrientation: Boolean = true,
    viewModel: ControlsEditorViewModel = viewModel()
) {
    val layoutState by viewModel.layoutState.collectAsState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val baseControlLayouts = remember(layoutState.controlLayouts) { layoutState.controlLayouts }
    val dpadUpTitle = stringResource(R.string.settings_gamepad_action_dpad_up)
    val dpadDownTitle = stringResource(R.string.settings_gamepad_action_dpad_down)
    val dpadLeftTitle = stringResource(R.string.settings_gamepad_action_dpad_left)
    val dpadRightTitle = stringResource(R.string.settings_gamepad_action_dpad_right)
    val triangleTitle = stringResource(R.string.settings_gamepad_action_triangle)
    val crossTitle = stringResource(R.string.settings_gamepad_action_cross)
    val squareTitle = stringResource(R.string.settings_gamepad_action_square)
    val circleTitle = stringResource(R.string.settings_gamepad_action_circle)
    val selectTitle = stringResource(R.string.settings_gamepad_action_select)
    val startTitle = stringResource(R.string.settings_gamepad_action_start)
    val l3Title = stringResource(R.string.settings_gamepad_action_l3)
    val r3Title = stringResource(R.string.settings_gamepad_action_r3)

    val controls = remember(
        dpadUpTitle, dpadDownTitle, dpadLeftTitle, dpadRightTitle,
        triangleTitle, crossTitle, squareTitle, circleTitle,
        selectTitle, startTitle, l3Title, r3Title
    ) {
        listOf(
            EditorControlSpec("l2", EditorAnchor.TopLeft, "L2", shape = EditorControlShape.Shoulder),
            EditorControlSpec("l1", EditorAnchor.TopLeft, "L1", baseOffset = 73.dp to 0.dp, shape = EditorControlShape.Shoulder),
            EditorControlSpec("r1", EditorAnchor.TopRight, "R1", baseOffset = OverlayRightShoulderBaseOffset to 0.dp, shape = EditorControlShape.Shoulder),
            EditorControlSpec("r2", EditorAnchor.TopRight, "R2", baseOffset = OverlayRightShoulderGapOffset to 0.dp, shape = EditorControlShape.Shoulder),
            EditorControlSpec("dpad_up", EditorAnchor.BottomLeft, dpadUpTitle, shape = EditorControlShape.Dpad),
            EditorControlSpec("dpad_down", EditorAnchor.BottomLeft, dpadDownTitle, shape = EditorControlShape.Dpad),
            EditorControlSpec("dpad_left", EditorAnchor.BottomLeft, dpadLeftTitle, shape = EditorControlShape.Dpad),
            EditorControlSpec("dpad_right", EditorAnchor.BottomLeft, dpadRightTitle, shape = EditorControlShape.Dpad),
            EditorControlSpec("left_stick", EditorAnchor.BottomLeft, "Left Stick", baseOffset = OverlayLeftStickBaseOffset to 0.dp, shape = EditorControlShape.Stick),
            EditorControlSpec("triangle", EditorAnchor.BottomRight, triangleTitle, shape = EditorControlShape.Action),
            EditorControlSpec("cross", EditorAnchor.BottomRight, crossTitle, shape = EditorControlShape.Action),
            EditorControlSpec("square", EditorAnchor.BottomRight, squareTitle, shape = EditorControlShape.Action),
            EditorControlSpec("circle", EditorAnchor.BottomRight, circleTitle, shape = EditorControlShape.Action),
            EditorControlSpec("right_stick", EditorAnchor.BottomRight, "Right Stick", baseOffset = OverlayRightStickBaseOffset to 0.dp, shape = EditorControlShape.Stick),
            EditorControlSpec("select", EditorAnchor.BottomCenter, selectTitle, wide = true, shape = EditorControlShape.Center),
            EditorControlSpec("start", EditorAnchor.BottomCenter, startTitle, wide = true, shape = EditorControlShape.Center),
            EditorControlSpec("l3", EditorAnchor.BottomCenter, l3Title, shape = EditorControlShape.Center),
            EditorControlSpec("r3", EditorAnchor.BottomCenter, r3Title, shape = EditorControlShape.Center)
        )
    }

    fun defaultLayoutFor(spec: EditorControlSpec): OverlayControlLayout {
        return baseControlLayouts[spec.id]
            ?: OverlayControlLayout(scale = if (spec.id.endsWith("stick")) layoutState.stickScale else 100)
    }

    var selectedControlId by rememberSaveable { mutableStateOf("cross") }
    val controlDraftStates = remember(controls) {
        controls.associate { spec ->
            spec.id to EditorControlDraftState(defaultLayoutFor(spec))
        }
    }
    LaunchedEffect(baseControlLayouts, layoutState.stickScale) {
        controls.forEach { spec ->
            controlDraftStates.getValue(spec.id).updateFrom(defaultLayoutFor(spec))
        }
    }
    val selectedControl = controls.firstOrNull { it.id == selectedControlId } ?: controls.first()
    val selectedLayout = controlDraftStates.getValue(selectedControl.id)

    if (manageActivityOrientation) {
        LaunchedEffect(Unit) {
            val activity = context as? android.app.Activity ?: return@LaunchedEffect
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        }

        DisposableEffect(Unit) {
            val activity = context as? android.app.Activity
            val originalOrientation = activity?.requestedOrientation
            onDispose {
                activity?.requestedOrientation = originalOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            }
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF111217))
    ) {
        val shoulderWidth = if (isLandscape) 65.dp else 72.dp
        val shoulderHeight = if (isLandscape) 32.dp else 36.dp
        val dpadButtonSize = if (isLandscape) 130.dp / 3f else 150.dp / 3f
        val actionButtonSize = if (isLandscape) 130.dp / 3.2f else 150.dp / 3.2f
        val stickSize = if (isLandscape) 120.dp else 140.dp
        val centerWidth = if (isLandscape) 64.dp else 72.dp
        val centerHeight = if (isLandscape) 28.dp else 32.dp
        val wideCenterWidth = centerWidth * 1.25f
        val centerColumnGap = if (isLandscape) OverlayCenterColumnGapLandscape else OverlayCenterColumnGapPortrait
        val centerRowGap = if (isLandscape) OverlayCenterRowGapLandscape else OverlayCenterRowGapPortrait
        val clusterGap = if (isLandscape) OverlayClusterGapLandscape else OverlayClusterGapPortrait
        val dpadStep = overlayClusterStep(dpadButtonSize, clusterGap)
        val actionStep = overlayClusterStep(actionButtonSize, clusterGap)
        val cutoutPadding = WindowInsets.displayCutout.asPaddingValues()
        val navBarPadding = WindowInsets.navigationBars.asPaddingValues()
        val safeLeft = maxOf(
            cutoutPadding.calculateLeftPadding(LayoutDirection.Ltr),
            navBarPadding.calculateLeftPadding(LayoutDirection.Ltr)
        )
        val safeRight = maxOf(
            cutoutPadding.calculateRightPadding(LayoutDirection.Ltr),
            navBarPadding.calculateRightPadding(LayoutDirection.Ltr)
        )
        val safeTop = maxOf(cutoutPadding.calculateTopPadding(), navBarPadding.calculateTopPadding())
        val safeBottom = maxOf(cutoutPadding.calculateBottomPadding(), navBarPadding.calculateBottomPadding())
        val safeHorizontalInset = maxOf(safeLeft, safeRight)
        val baseEdgePad = if (isLandscape) 28.dp else 12.dp
        val baseBottomPad = if (isLandscape) 24.dp else 36.dp
        val edgePadStart = baseEdgePad + safeHorizontalInset
        val edgePadEnd = baseEdgePad + safeHorizontalInset
        val edgePadTop = maxOf(OverlayShoulderTopPadding, safeTop + 4.dp)
        val bottomPad = baseBottomPad + safeBottom

        Column(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .statusBarsPadding()
                .displayCutoutPadding()
                .padding(top = 2.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Column(
                modifier = Modifier
                    .widthIn(min = 250.dp, max = 300.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(Color.Black.copy(alpha = 0.78f))
                    .padding(horizontal = 7.dp, vertical = 5.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Button(
                        onClick = onBackClick,
                        modifier = Modifier.height(28.dp),
                        shape = RoundedCornerShape(8.dp),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp)
                    ) {
                        Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = null, modifier = Modifier.size(14.dp))
                    }
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = selectedControl.title,
                            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
                            color = Color.White,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Button(
                        onClick = {
                            controlDraftStates.getValue(selectedControl.id).visible = !selectedLayout.visible
                        },
                        modifier = Modifier.height(28.dp),
                        shape = RoundedCornerShape(8.dp),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp)
                    ) {
                        Icon(
                            imageVector = if (selectedLayout.visible) Icons.Rounded.Visibility else Icons.Rounded.VisibilityOff,
                            contentDescription = null,
                            modifier = Modifier.size(14.dp)
                        )
                    }
                    Button(
                        onClick = {
                            scope.launch {
                                viewModel.saveLayout(
                                    buildMap {
                                        controls.forEach { spec ->
                                            put(spec.id, controlDraftStates.getValue(spec.id).toLayout())
                                        }
                                    }
                                )
                                onBackClick()
                            }
                        },
                        modifier = Modifier.height(28.dp),
                        shape = RoundedCornerShape(8.dp),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp)
                    ) {
                        Icon(Icons.Rounded.Save, contentDescription = null, modifier = Modifier.size(13.dp))
                    }
                }
                Row(
                    modifier = Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.05f))
                        .padding(horizontal = 6.dp, vertical = 2.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Text(
                        text = "${selectedLayout.scale}%",
                        style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                        color = Color.White,
                        modifier = Modifier.width(38.dp)
                    )
                    Slider(
                        value = selectedLayout.scale.toFloat(),
                        onValueChange = {
                            controlDraftStates.getValue(selectedControl.id).scale = it.roundToInt().coerceIn(50, 200)
                        },
                        valueRange = 50f..200f,
                        modifier = Modifier
                            .weight(1f)
                            .heightIn(max = 20.dp),
                        colors = SliderDefaults.colors(
                            thumbColor = MaterialTheme.colorScheme.primary,
                            activeTrackColor = MaterialTheme.colorScheme.primary
                        )
                    )
                    Button(
                        onClick = {
                            controls.forEach { spec ->
                                controlDraftStates.getValue(spec.id)
                                    .updateFrom(OverlayControlLayout(scale = if (spec.id.endsWith("stick")) layoutState.stickScale else 100))
                            }
                            viewModel.resetLayout()
                        },
                        modifier = Modifier.height(24.dp),
                        shape = RoundedCornerShape(8.dp),
                        contentPadding = PaddingValues(horizontal = 7.dp, vertical = 1.dp)
                    ) {
                        Text(stringResource(R.string.controls_editor_reset), fontSize = 10.sp, maxLines = 1)
                    }
                }
            }
        }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(start = 6.dp, end = 6.dp, top = 6.dp, bottom = 6.dp)
                .fillMaxSize()
                .clip(RoundedCornerShape(22.dp))
                .background(Color.White.copy(alpha = 0.018f))
                .border(1.dp, Color.White.copy(alpha = 0.12f), RoundedCornerShape(22.dp))
        ) {
            controls.forEach { spec ->
                key(spec.id) {
                    val baseModifier = when (spec.anchor) {
                        EditorAnchor.TopLeft -> Modifier
                            .align(Alignment.TopStart)
                            .padding(start = edgePadStart, top = edgePadTop)
                        EditorAnchor.TopRight -> Modifier
                            .align(Alignment.TopEnd)
                            .padding(end = edgePadEnd, top = edgePadTop)
                        EditorAnchor.BottomLeft -> Modifier
                            .align(Alignment.BottomStart)
                            .padding(start = edgePadStart, bottom = bottomPad)
                        EditorAnchor.BottomRight -> Modifier
                            .align(Alignment.BottomEnd)
                            .padding(end = edgePadEnd, bottom = bottomPad)
                        EditorAnchor.BottomCenter -> Modifier
                            .align(Alignment.BottomCenter)
                            .padding(bottom = bottomPad + (OverlayCenterBottomPadding - OverlayBottomAnchorPadding))
                    }
                    val resolvedBaseOffset = when (spec.id) {
                        "dpad_up" -> overlayDpadOffset(dpadStep, "up")
                        "dpad_down" -> overlayDpadOffset(dpadStep, "down")
                        "dpad_left" -> overlayDpadOffset(dpadStep, "left")
                        "dpad_right" -> overlayDpadOffset(dpadStep, "right")
                        "triangle" -> overlayActionOffset(actionStep, "triangle")
                        "cross" -> overlayActionOffset(actionStep, "cross")
                        "square" -> overlayActionOffset(actionStep, "square")
                        "circle" -> overlayActionOffset(actionStep, "circle")
                        "select" -> overlayCenterButtonOffset(
                            buttonWidth = wideCenterWidth,
                            columnWidth = wideCenterWidth,
                            gap = centerColumnGap,
                            leftColumn = true
                        ) + OverlayCenterBaseShiftX to 0.dp
                        "start" -> overlayCenterButtonOffset(
                            buttonWidth = wideCenterWidth,
                            columnWidth = wideCenterWidth,
                            gap = centerColumnGap,
                            leftColumn = false
                        ) + OverlayCenterBaseShiftX to 0.dp
                        "l3" -> overlayCenterButtonOffset(
                            buttonWidth = centerWidth,
                            columnWidth = wideCenterWidth,
                            gap = centerColumnGap,
                            leftColumn = true
                        ) + OverlayCenterBaseShiftX to
                            overlayCenterSecondRowOffset(centerHeight, centerRowGap)
                        "r3" -> overlayCenterButtonOffset(
                            buttonWidth = centerWidth,
                            columnWidth = wideCenterWidth,
                            gap = centerColumnGap,
                            leftColumn = false
                        ) + OverlayCenterBaseShiftX to
                            overlayCenterSecondRowOffset(centerHeight, centerRowGap)
                        else -> spec.baseOffset
                    }
                    EditorControlItem(
                        spec = spec,
                        controlState = controlDraftStates.getValue(spec.id),
                        modifier = baseModifier.offset(x = resolvedBaseOffset.first, y = resolvedBaseOffset.second),
                        selected = selectedControlId == spec.id,
                        shoulderWidth = shoulderWidth,
                        shoulderHeight = shoulderHeight,
                        dpadButtonSize = dpadButtonSize,
                        actionButtonSize = actionButtonSize,
                        stickSize = stickSize,
                        centerWidth = centerWidth,
                        wideCenterWidth = wideCenterWidth,
                        centerHeight = centerHeight,
                        onSelect = { selectedControlId = spec.id }
                    )
                }
            }
        }
    }
}

@Composable
private fun EditorControlItem(
    spec: EditorControlSpec,
    controlState: EditorControlDraftState,
    modifier: Modifier,
    selected: Boolean,
    shoulderWidth: Dp,
    shoulderHeight: Dp,
    dpadButtonSize: Dp,
    actionButtonSize: Dp,
    stickSize: Dp,
    centerWidth: Dp,
    wideCenterWidth: Dp,
    centerHeight: Dp,
    onSelect: () -> Unit
) {
    val dragHandleSize = when (spec.shape) {
        EditorControlShape.Stick -> 148.dp
        EditorControlShape.Action -> 96.dp
        EditorControlShape.Dpad -> 92.dp
        EditorControlShape.Shoulder -> 92.dp
        EditorControlShape.Center -> 98.dp
    }

    DraggableEditorControl(
        offset = controlState.offsetX to controlState.offsetY,
        onOffsetChange = {
            controlState.offsetX = it.first
            controlState.offsetY = it.second
        },
        modifier = modifier,
        draggable = selected,
        dragHandleSize = dragHandleSize
    ) { isDragging ->
        EditorControlPreview(
            spec = spec,
            selected = selected,
            visible = controlState.visible,
            scale = controlState.scale / 100f,
            shoulderWidth = shoulderWidth,
            shoulderHeight = shoulderHeight,
            dpadButtonSize = dpadButtonSize,
            actionButtonSize = actionButtonSize,
            stickSize = stickSize,
            centerWidth = centerWidth,
            wideCenterWidth = wideCenterWidth,
            centerHeight = centerHeight,
            clickEnabled = !isDragging,
            onClick = onSelect
        )
    }
}

@Composable
private fun DraggableEditorControl(
    offset: Pair<Float, Float>,
    onOffsetChange: (Pair<Float, Float>) -> Unit,
    modifier: Modifier = Modifier,
    draggable: Boolean,
    dragHandleSize: Dp,
    content: @Composable (Boolean) -> Unit
) {
    var dragOffsetX by remember(offset.first) { mutableFloatStateOf(offset.first) }
    var dragOffsetY by remember(offset.second) { mutableFloatStateOf(offset.second) }
    var isDragging by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .zIndex(if (selectedOrDragging(draggable, isDragging)) 2f else 0f)
            .offset { IntOffset(dragOffsetX.roundToInt(), dragOffsetY.roundToInt()) }
            .then(if (draggable) Modifier.size(dragHandleSize) else Modifier)
            .then(
                if (draggable) {
                    Modifier.pointerInput(Unit) {
                        detectDragGestures(
                            onDragStart = {
                                isDragging = true
                            },
                            onDragEnd = {
                                isDragging = false
                                onOffsetChange(Pair(dragOffsetX, dragOffsetY))
                            },
                            onDragCancel = {
                                isDragging = false
                                onOffsetChange(Pair(dragOffsetX, dragOffsetY))
                            }
                        ) { change, dragAmount ->
                            change.consume()
                            dragOffsetX += dragAmount.x
                            dragOffsetY += dragAmount.y
                        }
                    }
                } else {
                    Modifier
                }
            ),
        contentAlignment = Alignment.Center
    ) {
        content(isDragging)
    }
}

private fun selectedOrDragging(selected: Boolean, dragging: Boolean): Boolean = selected || dragging

@Composable
private fun EditorControlPreview(
    spec: EditorControlSpec,
    selected: Boolean,
    visible: Boolean,
    scale: Float,
    shoulderWidth: Dp,
    shoulderHeight: Dp,
    dpadButtonSize: Dp,
    actionButtonSize: Dp,
    stickSize: Dp,
    centerWidth: Dp,
    wideCenterWidth: Dp,
    centerHeight: Dp,
    clickEnabled: Boolean,
    onClick: () -> Unit
) {
    val borderColor = if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.75f) else Color.White.copy(alpha = 0.16f)
    when (spec.shape) {
        EditorControlShape.Stick -> {
            Box(
                modifier = Modifier
                    .size(stickSize * scale)
                    .clip(CircleShape)
                    .background(Color.White.copy(alpha = if (visible) 0.05f else 0.025f))
                    .border(1.5.dp, Color.White.copy(alpha = if (selected) 0.22f else 0.1f), CircleShape)
                    .clickable(enabled = clickEnabled, onClick = onClick),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .size(stickSize * scale * 0.65f)
                        .clip(CircleShape)
                        .border(1.dp, Color.White.copy(alpha = if (visible) 0.06f else 0.03f), CircleShape)
                )
                Box(
                    modifier = Modifier
                        .size(stickSize * scale * 0.32f)
                        .clip(CircleShape)
                        .background(Color.White.copy(alpha = if (visible) 0.35f else 0.12f))
                        .border(1.5.dp, borderColor, CircleShape)
                )
            }
        }
        EditorControlShape.Action -> {
            val actionColor = when (spec.id) {
                "triangle" -> Color(0xFF50D9A0)
                "cross" -> Color(0xFF5BA8FF)
                "square" -> Color(0xFFA07BFF)
                else -> Color(0xFFFF6B7A)
            }
            val label = when (spec.id) {
                "triangle" -> "△"
                "cross" -> "×"
                "square" -> "□"
                else -> "○"
            }
            Box(
                modifier = Modifier
                    .size(actionButtonSize * scale)
                    .clip(CircleShape)
                    .background(
                        Brush.radialGradient(
                            colors = if (visible) listOf(
                                actionColor.copy(alpha = 0.4f),
                                actionColor.copy(alpha = 0.15f)
                            ) else listOf(
                                actionColor.copy(alpha = 0.14f),
                                actionColor.copy(alpha = 0.05f)
                            )
                        )
                    )
                    .border(1.5.dp, if (selected) actionColor.copy(alpha = 0.75f) else actionColor.copy(alpha = 0.25f), CircleShape)
                    .clickable(enabled = clickEnabled, onClick = onClick),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = Color.White.copy(alpha = if (visible) 0.85f else 0.35f)
                )
            }
        }
        EditorControlShape.Dpad -> {
            val label = when {
                spec.id.endsWith("up") -> "▲"
                spec.id.endsWith("down") -> "▼"
                spec.id.endsWith("left") -> "◀"
                else -> "▶"
            }
            Box(
                modifier = Modifier
                    .size(dpadButtonSize * scale)
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color.White.copy(alpha = if (visible) 0.08f else 0.03f))
                    .border(1.dp, if (selected) borderColor else Color.White.copy(alpha = if (visible) 0.08f else 0.04f), RoundedCornerShape(8.dp))
                    .clickable(enabled = clickEnabled, onClick = onClick),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.titleSmall,
                    color = Color.White.copy(alpha = if (visible) 0.75f else 0.35f)
                )
            }
        }
        EditorControlShape.Shoulder -> {
            Box(
                modifier = Modifier
                    .size(width = shoulderWidth * scale, height = shoulderHeight * scale)
                    .clip(RoundedCornerShape(10.dp))
                    .background(
                        Brush.verticalGradient(
                            colors = if (visible) listOf(
                                Color.White.copy(alpha = 0.12f),
                                Color.White.copy(alpha = 0.06f)
                            ) else listOf(
                                Color.White.copy(alpha = 0.05f),
                                Color.White.copy(alpha = 0.025f)
                            )
                        )
                    )
                    .border(1.dp, if (selected) borderColor else Color.White.copy(alpha = if (visible) 0.1f else 0.04f), RoundedCornerShape(10.dp))
                    .clickable(enabled = clickEnabled, onClick = onClick),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = spec.title,
                    style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold, letterSpacing = 1.sp),
                    color = Color.White.copy(alpha = if (visible) 0.85f else 0.35f)
                )
            }
        }
        EditorControlShape.Center -> {
            Box(
                modifier = Modifier
                    .size(
                        width = (if (spec.wide) wideCenterWidth else centerWidth) * scale,
                        height = centerHeight * scale
                    )
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color.White.copy(alpha = if (visible) 0.07f else 0.03f))
                    .border(1.dp, if (selected) borderColor else Color.White.copy(alpha = if (visible) 0.06f else 0.03f), RoundedCornerShape(8.dp))
                    .clickable(enabled = clickEnabled, onClick = onClick),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    spec.title.uppercase(),
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontWeight = FontWeight.Medium,
                        letterSpacing = 1.sp
                    ),
                    color = Color.White.copy(alpha = if (visible) 0.7f else 0.35f)
                )
            }
        }
    }
}

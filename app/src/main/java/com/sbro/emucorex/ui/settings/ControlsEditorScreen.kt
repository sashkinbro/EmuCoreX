package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.app.Activity
import android.content.pm.ActivityInfo
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Remove
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.zIndex
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CustomTouchControl
import com.sbro.emucorex.data.CustomTouchControlContent
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.data.CustomTouchControlShape
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.data.TouchControlsLayoutProfile
import com.sbro.emucorex.ui.common.ActionSelector
import com.sbro.emucorex.ui.common.CustomControlVisual
import com.sbro.emucorex.ui.common.actionDrawableRes
import com.sbro.emucorex.ui.common.actionLabel
import com.sbro.emucorex.ui.common.composeShape
import com.sbro.emucorex.ui.common.OverlayCanvasButtonSpec
import com.sbro.emucorex.ui.common.OverlayCanvasDpadClusterSpec
import com.sbro.emucorex.ui.common.OverlayCanvasStickSpec
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.common.VectorDpadCluster
import com.sbro.emucorex.ui.common.VectorOverlayButton
import com.sbro.emucorex.ui.common.buildOverlayCanvasLayout
import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.theme.neon.neonShape
import java.util.UUID
import kotlin.math.roundToInt

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
    val controlLayouts: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts(),
    val customControls: CustomTouchControlLibrary = CustomTouchControlLibrary.Empty
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
    controlLayouts = controlLayouts,
    customControls = customTouchControls
)

fun TouchControlsLayoutProfile.toControlsEditorState(
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    overlayScale: Int,
    customControls: CustomTouchControlLibrary = CustomTouchControlLibrary.Empty
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
    controlLayouts = controlLayouts,
    customControls = customControls
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
private const val CustomControlIdPrefix = "custom:"
private const val CustomControlSizeStepDp = 4
private val ControlGroupIds = setOf(ControlGroupDpad, ControlGroupActions)
private val DpadControlIds = setOf("dpad_up", "dpad_down", "dpad_left", "dpad_right")
private val ActionControlIds = setOf("triangle", "circle", "cross", "square")

private fun customControlSelectionId(controlId: String): String =
    "$CustomControlIdPrefix$controlId"

private fun String.toCustomControlIdOrNull(): String? =
    takeIf { it.startsWith(CustomControlIdPrefix) }?.removePrefix(CustomControlIdPrefix)

private fun actionIdForControlId(controlId: String): String? = when (controlId) {
    "dpad_up" -> "up"
    "dpad_down" -> "down"
    "dpad_left" -> "left"
    "dpad_right" -> "right"
    else -> controlId.takeIf { it in CustomTouchControl.ALLOWED_ACTION_IDS }
}

private data class PreviewGroupBounds(
    val x: Dp,
    val y: Dp,
    val width: Dp,
    val height: Dp
)

private data class EditorControlGeometry(
    val positionX: Float,
    val positionY: Float,
    val widthDp: Int,
    val heightDp: Int
)

private fun CustomTouchControlLibrary.replacing(control: CustomTouchControl): CustomTouchControlLibrary =
    copy(controls = controls.map { if (it.id == control.id) control else it })

private fun CustomTouchControlLibrary.removing(controlId: String): CustomTouchControlLibrary =
    copy(controls = controls.filterNot { it.id == controlId })

private enum class ControlShapeKind { FACE, DIRECTIONAL, ROUNDED }

private data class ControlAppearance(
    val shape: CustomTouchControlShape,
    val cornerDp: Int,
    val fillColor: Int,
    val contentColor: Int,
    val borderColor: Int,
    val borderWidthDp: Float,
    val shadowElevationDp: Float,
    val opacity: Int
)

private fun ControlAppearance.applyTo(control: CustomTouchControl): CustomTouchControl = control.copy(
    shape = shape,
    cornerDp = cornerDp,
    fillColor = fillColor,
    contentColor = contentColor,
    borderColor = borderColor,
    borderWidthDp = borderWidthDp,
    shadowElevationDp = shadowElevationDp,
    opacity = opacity
)

private fun controlShapeKindForControlId(controlId: String): ControlShapeKind = when {
    controlId in ActionControlIds -> ControlShapeKind.FACE
    controlId in DpadControlIds -> ControlShapeKind.DIRECTIONAL
    else -> ControlShapeKind.ROUNDED
}

private fun controlShapeKindForAction(actionId: String): ControlShapeKind = when (actionId) {
    "triangle", "cross", "square", "circle" -> ControlShapeKind.FACE
    "up", "down", "left", "right" -> ControlShapeKind.DIRECTIONAL
    else -> ControlShapeKind.ROUNDED
}

// New controls created from the layout editor must blend with the currently selected
// touch-control visual style; buttons made in the creator keep their custom look.
private fun editorControlAppearance(
    style: TouchControlVisualStyle,
    kind: ControlShapeKind,
    scheme: ColorScheme
): ControlAppearance = when (style) {
    TouchControlVisualStyle.CLASSIC -> ControlAppearance(
        shape = if (kind == ControlShapeKind.FACE) {
            CustomTouchControlShape.CIRCLE
        } else {
            CustomTouchControlShape.ROUNDED
        },
        cornerDp = if (kind == ControlShapeKind.DIRECTIONAL) 8 else 10,
        fillColor = 0xFF121824.toInt(),
        contentColor = 0xFFFFFFFF.toInt(),
        borderColor = 0xFF6688FF.toInt(),
        borderWidthDp = 1.5f,
        shadowElevationDp = 0f,
        opacity = 90
    )
    TouchControlVisualStyle.LEGACY -> ControlAppearance(
        shape = if (kind == ControlShapeKind.FACE) {
            CustomTouchControlShape.CIRCLE
        } else {
            CustomTouchControlShape.ROUNDED
        },
        cornerDp = if (kind == ControlShapeKind.DIRECTIONAL) 12 else 16,
        fillColor = 0xFF2A2F38.toInt(),
        contentColor = 0xFFF3F5F8.toInt(),
        borderColor = 0x8AFFFFFF.toInt(),
        borderWidthDp = 1f,
        shadowElevationDp = 0f,
        opacity = 94
    )
    TouchControlVisualStyle.MODERN -> ControlAppearance(
        shape = CustomTouchControlShape.ROUNDED,
        cornerDp = when (kind) {
            ControlShapeKind.FACE -> 32
            ControlShapeKind.DIRECTIONAL -> 8
            ControlShapeKind.ROUNDED -> 10
        },
        fillColor = 0xFF141B28.toInt(),
        contentColor = 0xFFF2F6FF.toInt(),
        borderColor = scheme.primary.toArgb(),
        borderWidthDp = 1.5f,
        shadowElevationDp = 4f,
        opacity = 96
    )
    TouchControlVisualStyle.ARCADE -> ControlAppearance(
        shape = if (kind == ControlShapeKind.ROUNDED) {
            CustomTouchControlShape.ROUNDED
        } else {
            CustomTouchControlShape.CIRCLE
        },
        cornerDp = if (kind == ControlShapeKind.DIRECTIONAL) 14 else 12,
        fillColor = 0xFF3A1430.toInt(),
        contentColor = 0xFFFFFFFF.toInt(),
        borderColor = 0xFFFFE29A.toInt(),
        borderWidthDp = 2f,
        shadowElevationDp = 0f,
        opacity = 92
    )
    TouchControlVisualStyle.MINIMAL -> ControlAppearance(
        shape = if (kind == ControlShapeKind.ROUNDED) {
            CustomTouchControlShape.ROUNDED
        } else {
            CustomTouchControlShape.CIRCLE
        },
        cornerDp = if (kind == ControlShapeKind.DIRECTIONAL) 6 else 8,
        fillColor = scheme.surface.toArgb(),
        contentColor = scheme.onSurface.copy(alpha = 0.88f).toArgb(),
        borderColor = scheme.onSurface.copy(alpha = 0.52f).toArgb(),
        borderWidthDp = 1f,
        shadowElevationDp = 0f,
        opacity = 45
    )
}

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
    onUpdateControlSecondaryAction: (String, String?) -> Unit,
    onSetControlVisible: (String, Boolean) -> Unit,
    onSetStickSurfaceMode: (String, Boolean) -> Unit,
    onResetLayout: () -> Unit,
    onCustomControlsChange: (CustomTouchControlLibrary) -> Unit = {}
) {
    val context = LocalContext.current
    val resources = LocalResources.current
    val activity = context as? Activity
    var selectedControlId by rememberSaveable { mutableStateOf<String?>(null) }
    var editorControlLayouts by remember { mutableStateOf(state.controlLayouts) }
    var editorCustomControls by remember { mutableStateOf(state.customControls.sanitized()) }
    var selectedControlGeometry by remember { mutableStateOf<EditorControlGeometry?>(null) }
    var comboDialogControlId by remember { mutableStateOf<String?>(null) }
    var standardComboControlId by remember { mutableStateOf<String?>(null) }
    var showCreateComboDialog by remember { mutableStateOf(false) }
    var showControlAdjustPanel by remember { mutableStateOf(false) }
    var deleteCustomCandidate by remember { mutableStateOf<CustomTouchControl?>(null) }
    val defaultLayouts = remember(state.stickScale) { AppPreferences.defaultOverlayControlLayouts(state.stickScale) }
    val selectedLayout = selectedControlId?.let { id ->
        editorControlLayouts[id] ?: defaultLayouts[id] ?: OverlayControlLayout()
    }
    val selectedCustomControlId = selectedControlId?.toCustomControlIdOrNull()
    val selectedCustomControl = selectedCustomControlId?.let { id ->
        editorCustomControls.controls.firstOrNull { it.id == id }
    }
    val selectedIsCustom = selectedCustomControl != null
    val selectedIsGroup = selectedControlId?.let { it in ControlGroupIds } == true
    val selectedIsStick = selectedControlId == "left_stick" || selectedControlId == "right_stick"
    val selectedStickSurfaceMode = selectedIsStick && (selectedLayout?.surfaceOnly == true)
    val selectedStandardTitle = selectedControlId
        ?.takeUnless { it in ControlGroupIds || it.toCustomControlIdOrNull() != null }
        ?.let { controlTitle(it) }
        .orEmpty()
    val canDuplicateSelected = when {
        editorCustomControls.controls.size >= CustomTouchControlLibrary.MAX_CONTROLS -> false
        selectedCustomControl != null -> true
        selectedControlId != null && !selectedIsGroup ->
            actionIdForControlId(selectedControlId!!) != null && selectedControlGeometry != null
        else -> false
    }
    val copiedNameFor: (String) -> String = { name ->
        resources.getString(R.string.touch_control_creator_copy_name, name)
            .take(CustomTouchControl.MAX_NAME_LENGTH)
    }
    val editorColorScheme = MaterialTheme.colorScheme
    fun editorAppearance(kind: ControlShapeKind): ControlAppearance =
        editorControlAppearance(state.touchControlVisualStyle, kind, editorColorScheme)
    val originalOrientation = remember(activity) {
        activity?.requestedOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }

    LaunchedEffect(state.controlLayouts) {
        editorControlLayouts = state.controlLayouts
    }

    LaunchedEffect(state.customControls) {
        editorCustomControls = state.customControls.sanitized()
    }

    LaunchedEffect(selectedControlId) {
        showControlAdjustPanel = false
    }

    fun updateCustomControls(transform: (CustomTouchControlLibrary) -> CustomTouchControlLibrary) {
        val updated = transform(editorCustomControls).sanitized()
        editorCustomControls = updated
        onCustomControlsChange(updated)
    }

    fun setCustomControlLocally(control: CustomTouchControl) {
        editorCustomControls = editorCustomControls.replacing(control)
    }

    fun persistCustomControl(control: CustomTouchControl) {
        updateCustomControls { it.replacing(control) }
    }

    fun persistCustomControlById(controlId: String) {
        val current = editorCustomControls.controls.firstOrNull { it.id == controlId } ?: return
        persistCustomControl(current.copy(updatedAtMillis = System.currentTimeMillis()))
    }

    fun duplicateSelectedControl() {
        if (!canDuplicateSelected) return
        val now = System.currentTimeMillis()
        val sourceCustom = selectedCustomControl
        val duplicate = if (sourceCustom != null) {
            sourceCustom.duplicate(
                id = UUID.randomUUID().toString(),
                name = copiedNameFor(sourceCustom.name),
                nowMillis = now
            )
        } else {
            val controlId = selectedControlId ?: return
            val actionId = actionIdForControlId(controlId) ?: return
            val geometry = selectedControlGeometry ?: return
            editorAppearance(controlShapeKindForControlId(controlId)).applyTo(
                CustomTouchControl(
                    id = UUID.randomUUID().toString(),
                    name = copiedNameFor(selectedStandardTitle.ifEmpty { controlId }),
                    actionId = actionId,
                    secondaryActionId = selectedLayout?.secondaryActionId,
                    label = CustomTouchControl.defaultLabelFor(actionId),
                    positionX = (geometry.positionX + CustomTouchControl.DEFAULT_DUPLICATE_OFFSET)
                        .coerceIn(0f, 1f),
                    positionY = (geometry.positionY + CustomTouchControl.DEFAULT_DUPLICATE_OFFSET)
                        .coerceIn(0f, 1f),
                    widthDp = geometry.widthDp,
                    heightDp = geometry.heightDp,
                    createdAtMillis = now,
                    updatedAtMillis = now
                )
            ).copy(usesVectorStyle = true)
        }
        val insertIndex = sourceCustom?.let { source ->
            editorCustomControls.controls.indexOfFirst { it.id == source.id }
                .takeIf { it >= 0 }
                ?.plus(1)
        } ?: editorCustomControls.controls.size
        updateCustomControls { library ->
            val controls = library.controls.toMutableList()
            controls.add(insertIndex.coerceIn(0, controls.size), duplicate)
            library.copy(controls = controls)
        }
        selectedControlId = customControlSelectionId(duplicate.id)
        selectedControlGeometry = EditorControlGeometry(
            positionX = duplicate.positionX,
            positionY = duplicate.positionY,
            widthDp = duplicate.widthDp,
            heightDp = duplicate.heightDp
        )
    }

    fun createComboControl(actionId: String, secondaryActionId: String?) {
        if (editorCustomControls.controls.size >= CustomTouchControlLibrary.MAX_CONTROLS) return
        if (actionId !in CustomTouchControl.ALLOWED_ACTION_IDS) return
        val now = System.currentTimeMillis()
        val created = editorAppearance(controlShapeKindForAction(actionId)).applyTo(
            CustomTouchControl(
                id = UUID.randomUUID().toString(),
                name = resources.getString(
                    R.string.touch_control_creator_default_name,
                    editorCustomControls.controls.size + 1
                ).take(CustomTouchControl.MAX_NAME_LENGTH),
                actionId = actionId,
                secondaryActionId = secondaryActionId
                    ?.takeIf { it in CustomTouchControl.ALLOWED_ACTION_IDS && it != actionId },
                label = CustomTouchControl.defaultLabelFor(actionId),
                positionX = 0.5f,
                positionY = 0.5f,
                createdAtMillis = now,
                updatedAtMillis = now
            )
        ).copy(usesVectorStyle = true)
        updateCustomControls { it.copy(controls = it.controls + created) }
        selectedControlId = customControlSelectionId(created.id)
    }

    fun applyComboActions(control: CustomTouchControl, actionId: String, secondaryActionId: String?) {
        updateCustomControls { library ->
            library.replacing(
                control.copy(
                    actionId = actionId,
                    secondaryActionId = secondaryActionId?.takeUnless { it == actionId },
                    label = if (control.content == CustomTouchControlContent.SYMBOL) {
                        CustomTouchControl.defaultLabelFor(actionId)
                    } else {
                        control.label
                    },
                    updatedAtMillis = System.currentTimeMillis()
                )
            )
        }
    }

    fun deleteCustomControl(control: CustomTouchControl) {
        updateCustomControls { it.removing(control.id) }
        if (selectedControlId == customControlSelectionId(control.id)) {
            selectedControlId = null
        }
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

    fun setControlSecondaryActionLocally(controlId: String, secondaryActionId: String?) {
        val current = currentLayoutFor(
            controlId,
            if (controlId.contains("stick")) state.stickScale else 100
        )
        val sanitized = secondaryActionId
            ?.takeIf { it in CustomTouchControl.ALLOWED_ACTION_IDS }
            ?.takeUnless { it == actionIdForControlId(controlId) }
        editorControlLayouts = editorControlLayouts.toMutableMap().apply {
            put(controlId, current.copy(secondaryActionId = sanitized))
        }
        onUpdateControlSecondaryAction(controlId, sanitized)
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
            customControls = editorCustomControls.controls,
            selectedControlId = selectedControlId,
            onSelectControl = { selectedControlId = it },
            onSetControlOffset = ::setControlOffsetLocally,
            onCommitControlPosition = ::persistControlPosition,
            onCommitControlPositions = ::persistControlPositions,
            onSetCustomControlPosition = { controlId, x, y ->
                val current = editorCustomControls.controls.firstOrNull { it.id == controlId } ?: return@PreviewLayout
                setCustomControlLocally(current.copy(positionX = x, positionY = y))
            },
            onCommitCustomControlPosition = ::persistCustomControlById,
            onSelectedGeometryChange = { selectedControlGeometry = it },
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
                            text = selectedCustomControl?.name ?: controlTitle(it),
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
                        val customControl = selectedCustomControl
                        selectedControlId?.let { controlId ->
                            when {
                                customControl != null -> updateCustomControls { library ->
                                    library.replacing(
                                        customControl.copy(
                                            enabled = !customControl.enabled,
                                            updatedAtMillis = System.currentTimeMillis()
                                        )
                                    )
                                }
                                controlId !in ControlGroupIds -> {
                                    setControlVisibleLocally(controlId, !(selectedLayout?.visible ?: true))
                                }
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
                    val isVisible = selectedCustomControl?.enabled ?: (selectedLayout?.visible != false)
                    Icon(
                        imageVector = if (isVisible) {
                            Icons.Rounded.Visibility
                        } else {
                            Icons.Rounded.VisibilityOff
                        },
                        contentDescription = null
                    )
                }

                OutlinedButton(
                    onClick = { showCreateComboDialog = true },
                    enabled = editorCustomControls.controls.size < CustomTouchControlLibrary.MAX_CONTROLS,
                    shape = neonShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = Color.White.copy(alpha = 0.08f),
                        contentColor = Color.White
                    ),
                    modifier = Modifier.testTag("controls_editor_add_button")
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Add,
                        contentDescription = stringResource(R.string.touch_control_creator_create)
                    )
                }

                OutlinedButton(
                    onClick = { duplicateSelectedControl() },
                    enabled = canDuplicateSelected,
                    shape = neonShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = Color.White.copy(alpha = 0.08f),
                        contentColor = Color.White
                    ),
                    modifier = Modifier.testTag("controls_editor_duplicate")
                ) {
                    Icon(
                        imageVector = Icons.Rounded.ContentCopy,
                        contentDescription = stringResource(R.string.touch_control_creator_duplicate)
                    )
                }

                OutlinedButton(
                    onClick = {
                        showControlAdjustPanel = !showControlAdjustPanel
                        if (showControlAdjustPanel) comboDialogControlId = null
                    },
                    enabled = selectedControlId != null && !selectedIsGroup,
                    shape = neonShape(16.dp),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (showControlAdjustPanel) {
                            Color(0xFF3565FF).copy(alpha = 0.78f)
                        } else {
                            Color.White.copy(alpha = 0.08f)
                        },
                        contentColor = Color.White
                    ),
                    modifier = Modifier.testTag("controls_editor_adjust")
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Tune,
                        contentDescription = stringResource(R.string.settings_edit_controls_action)
                    )
                }

                if (selectedIsStick) {
                    OutlinedButton(
                        onClick = {
                            selectedControlId?.let { controlId ->
                                setStickSurfaceModeLocally(controlId, !selectedStickSurfaceMode)
                            }
                        },
                        shape = neonShape(16.dp),
                        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                        colors = ButtonDefaults.outlinedButtonColors(
                            containerColor = if (selectedStickSurfaceMode) {
                                Color(0xFF3565FF).copy(alpha = 0.78f)
                            } else {
                                Color.White.copy(alpha = 0.06f)
                            },
                            contentColor = if (selectedStickSurfaceMode) {
                                Color.White
                            } else {
                                Color.White.copy(alpha = 0.58f)
                            }
                        )
                    ) {
                        Icon(Icons.Rounded.TouchApp, contentDescription = null)
                    }
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
                val customControl = selectedCustomControl
                if (showControlAdjustPanel) {
                    val scale = selectedLayout?.scale
                        ?: if (controlId.contains("stick")) state.stickScale else 100
                    val isStickPanel = customControl == null &&
                        (controlId == "left_stick" || controlId == "right_stick") &&
                        (selectedLayout?.surfaceOnly == true)
                    val opacity = customControl?.opacity
                        ?: selectedLayout?.opacity
                        ?: AppPreferences.OVERLAY_CONTROL_OPACITY_DEFAULT

                    fun applyOpacity(next: Int) {
                        if (customControl != null) {
                            updateCustomControls { library ->
                                library.replacing(
                                    customControl.copy(
                                        opacity = next.coerceIn(
                                            CustomTouchControl.MIN_OPACITY,
                                            CustomTouchControl.MAX_OPACITY
                                        ),
                                        updatedAtMillis = System.currentTimeMillis()
                                    )
                                )
                            }
                        } else {
                            setControlOpacityLocally(controlId, next)
                        }
                    }

                    AdjustPanel(
                        title = customControl?.name ?: controlTitle(controlId),
                        onDismiss = { showControlAdjustPanel = false },
                        modifier = Modifier.padding(top = 8.dp)
                    ) {
                        if (customControl != null) {
                            AdjustStepper(
                                    valueText = "${customControl.widthDp}×${customControl.heightDp} dp",
                                    minusEnabled = customControl.widthDp > CustomTouchControl.MIN_SIZE_DP ||
                                        customControl.heightDp > CustomTouchControl.MIN_SIZE_DP,
                                    plusEnabled = customControl.widthDp < CustomTouchControl.MAX_SIZE_DP ||
                                        customControl.heightDp < CustomTouchControl.MAX_SIZE_DP,
                                    onMinus = {
                                        updateCustomControls { library ->
                                            library.replacing(
                                                customControl.copy(
                                                    widthDp = (customControl.widthDp - CustomControlSizeStepDp)
                                                        .coerceAtLeast(CustomTouchControl.MIN_SIZE_DP),
                                                    heightDp = (customControl.heightDp - CustomControlSizeStepDp)
                                                        .coerceAtLeast(CustomTouchControl.MIN_SIZE_DP),
                                                    updatedAtMillis = System.currentTimeMillis()
                                                )
                                            )
                                        }
                                    },
                                    onPlus = {
                                        updateCustomControls { library ->
                                            library.replacing(
                                                customControl.copy(
                                                    widthDp = (customControl.widthDp + CustomControlSizeStepDp)
                                                        .coerceAtMost(CustomTouchControl.MAX_SIZE_DP),
                                                    heightDp = (customControl.heightDp + CustomControlSizeStepDp)
                                                        .coerceAtMost(CustomTouchControl.MAX_SIZE_DP),
                                                    updatedAtMillis = System.currentTimeMillis()
                                                )
                                            )
                                        }
                                    }
                                )
                            } else {
                                AdjustStepper(
                                    valueText = "$scale%",
                                    minusEnabled = scale > AppPreferences.OVERLAY_CONTROL_SCALE_MIN,
                                    plusEnabled = scale < AppPreferences.OVERLAY_CONTROL_SCALE_MAX,
                                    onMinus = { setControlScaleLocally(controlId, scale - 10) },
                                    onPlus = { setControlScaleLocally(controlId, scale + 10) }
                                )
                            }
                            if (isStickPanel) {
                                val widthScale = selectedLayout.widthScale
                                AdjustStepper(
                                    valueText = "W $widthScale%",
                                    minusEnabled = widthScale > 100,
                                    plusEnabled = widthScale < 240,
                                    onMinus = { setControlWidthScaleLocally(controlId, widthScale - 10) },
                                    onPlus = { setControlWidthScaleLocally(controlId, widthScale + 10) }
                                )
                            }
                            AdjustStepper(
                                valueText = stringResource(
                                    R.string.controls_editor_opacity_value,
                                    opacity
                                ),
                                minusEnabled = opacity > AppPreferences.OVERLAY_CONTROL_OPACITY_MIN,
                                plusEnabled = opacity < AppPreferences.OVERLAY_CONTROL_OPACITY_MAX,
                                onMinus = { applyOpacity(opacity - 10) },
                                onPlus = { applyOpacity(opacity + 10) }
                            )
                        }
                    }

                if (!showControlAdjustPanel) {
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
                        val currentSecondaryActionId =
                            customControl?.secondaryActionId ?: selectedLayout?.secondaryActionId
                        OutlinedButton(
                            onClick = {
                                showControlAdjustPanel = false
                                if (customControl != null) {
                                    comboDialogControlId = customControl.id
                                } else {
                                    standardComboControlId = controlId
                                }
                            },
                            shape = neonShape(14.dp),
                            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
                            colors = ButtonDefaults.outlinedButtonColors(
                                containerColor = Color.White.copy(alpha = 0.08f),
                                contentColor = Color.White
                            ),
                            modifier = Modifier.testTag("controls_editor_combo")
                        ) {
                            Text(
                                if (currentSecondaryActionId != null) {
                                    stringResource(R.string.touch_control_creator_combo_action) +
                                        " · " + actionLabel(currentSecondaryActionId)
                                } else {
                                    stringResource(R.string.touch_control_creator_combo_action)
                                }
                            )
                        }
                        if (customControl != null) {
                            OutlinedButton(
                                onClick = { deleteCustomCandidate = customControl },
                                shape = neonShape(14.dp),
                                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
                                colors = ButtonDefaults.outlinedButtonColors(
                                    containerColor = Color.White.copy(alpha = 0.08f),
                                    contentColor = Color.White
                                ),
                                modifier = Modifier.testTag("controls_editor_delete_custom")
                            ) {
                                Icon(Icons.Rounded.Delete, contentDescription = null)
                            }
                        }
                    }
                }
                }
            }
        }
    }

    if (showCreateComboDialog) {
        ComboActionDialog(
            title = stringResource(R.string.touch_control_creator_create),
            initialActionId = CustomTouchControl.DEFAULT_ACTION_ID,
            initialSecondaryActionId = null,
            confirmLabel = stringResource(R.string.controls_editor_done),
            onDismiss = { showCreateComboDialog = false },
            onConfirm = { actionId, secondaryActionId ->
                createComboControl(actionId, secondaryActionId)
                showCreateComboDialog = false
            }
        )
    }

    val comboDialogControl = comboDialogControlId?.let { id ->
        editorCustomControls.controls.firstOrNull { it.id == id }
    }
    if (comboDialogControl != null) {
        ComboActionDialog(
            title = stringResource(R.string.touch_control_creator_combo_action),
            initialActionId = comboDialogControl.actionId,
            initialSecondaryActionId = comboDialogControl.secondaryActionId,
            confirmLabel = stringResource(R.string.controls_editor_done),
            onDismiss = { comboDialogControlId = null },
            onConfirm = { actionId, secondaryActionId ->
                applyComboActions(comboDialogControl, actionId, secondaryActionId)
                comboDialogControlId = null
            }
        )
    }

    val standardComboControlIdValue = standardComboControlId
    if (standardComboControlIdValue != null) {
        val standardSecondaryActionId = (
            editorControlLayouts[standardComboControlIdValue]
                ?: defaultLayouts[standardComboControlIdValue]
            )?.secondaryActionId
        ComboActionDialog(
            title = stringResource(R.string.touch_control_creator_combo_action),
            initialActionId = actionIdForControlId(standardComboControlIdValue)
                ?: CustomTouchControl.DEFAULT_ACTION_ID,
            initialSecondaryActionId = standardSecondaryActionId,
            confirmLabel = stringResource(R.string.controls_editor_done),
            primaryEditable = false,
            onDismiss = { standardComboControlId = null },
            onConfirm = { _, secondaryActionId ->
                setControlSecondaryActionLocally(standardComboControlIdValue, secondaryActionId)
                standardComboControlId = null
            }
        )
    }

    deleteCustomCandidate?.let { candidate ->
        AlertDialog(
            onDismissRequest = { deleteCustomCandidate = null },
            title = { Text(stringResource(R.string.touch_control_creator_delete_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.touch_control_creator_delete_message,
                        candidate.name
                    )
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        deleteCustomControl(candidate)
                        deleteCustomCandidate = null
                    },
                    modifier = Modifier.testTag("controls_editor_confirm_delete_custom")
                ) {
                    Text(stringResource(R.string.touch_control_creator_delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteCustomCandidate = null }) {
                    Text(stringResource(R.string.theme_manager_cancel))
                }
            }
        )
    }
}

@Composable
private fun AdjustPanel(
    title: String,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = modifier.testTag("controls_editor_adjust_panel"),
        color = Color(0xFF111827).copy(alpha = 0.92f),
        shape = neonShape(16.dp)
    ) {
        Column(
            modifier = Modifier
                .widthIn(min = 280.dp, max = 420.dp)
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = title,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = Color.White,
                    maxLines = 1
                )
                IconButton(
                    onClick = onDismiss,
                    modifier = Modifier.size(28.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Close,
                        contentDescription = stringResource(R.string.controls_editor_done),
                        tint = Color.White.copy(alpha = 0.86f)
                    )
                }
            }
            content()
        }
    }
}

@Composable
private fun AdjustStepper(
    valueText: String,
    minusEnabled: Boolean,
    plusEnabled: Boolean,
    onMinus: () -> Unit,
    onPlus: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        OutlinedButton(
            onClick = onMinus,
            enabled = minusEnabled,
            shape = neonShape(14.dp),
            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
            colors = ButtonDefaults.outlinedButtonColors(
                containerColor = Color.White.copy(alpha = 0.08f),
                contentColor = Color.White
            )
        ) {
            Icon(Icons.Rounded.Remove, contentDescription = null)
        }
        Text(
            text = valueText,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            color = Color.White,
            maxLines = 1,
            modifier = Modifier
                .weight(1f)
                .padding(horizontal = 10.dp)
        )
        OutlinedButton(
            onClick = onPlus,
            enabled = plusEnabled,
            shape = neonShape(14.dp),
            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
            colors = ButtonDefaults.outlinedButtonColors(
                containerColor = Color.White.copy(alpha = 0.08f),
                contentColor = Color.White
            )
        ) {
            Icon(Icons.Rounded.Add, contentDescription = null)
        }
    }
}

@Composable
private fun ComboActionDialog(
    title: String,
    initialActionId: String,
    initialSecondaryActionId: String?,
    confirmLabel: String,
    primaryEditable: Boolean = true,
    onDismiss: () -> Unit,
    onConfirm: (String, String?) -> Unit
) {
    var actionId by remember(initialActionId) { mutableStateOf(initialActionId) }
    var secondaryActionId by remember(initialSecondaryActionId) {
        mutableStateOf(initialSecondaryActionId)
    }
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .padding(horizontal = 24.dp, vertical = 16.dp)
                .widthIn(max = 560.dp)
                .fillMaxWidth()
                .testTag("controls_editor_combo_dialog"),
            shape = neonShape(24.dp),
            color = Color(0xFF0D1424).copy(alpha = 0.98f),
            border = BorderStroke(1.dp, Color(0xFF6688FF).copy(alpha = 0.5f)),
            shadowElevation = 16.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = title,
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = Color.White,
                        maxLines = 1
                    )
                    IconButton(onClick = onDismiss) {
                        Icon(
                            imageVector = Icons.Rounded.Close,
                            contentDescription = stringResource(R.string.theme_manager_cancel),
                            tint = Color.White.copy(alpha = 0.8f)
                        )
                    }
                }

                ComboActionPreview(
                    primaryLabel = actionLabel(actionId),
                    secondaryLabel = secondaryActionId?.let { actionLabel(it) }
                )

                ComboActionSection(
                    title = stringResource(R.string.touch_control_creator_action)
                ) {
                    if (primaryEditable) {
                        ActionSelector(
                            selectedActionId = actionId,
                            testTagPrefix = "controls_editor_combo_primary",
                            onSelect = { selected ->
                                selected?.let { action ->
                                    actionId = action
                                    if (secondaryActionId == action) secondaryActionId = null
                                }
                            }
                        )
                    } else {
                        ComboActionBadge(
                            label = actionLabel(actionId),
                            highlighted = true
                        )
                    }
                }

                ComboActionSection(
                    title = stringResource(R.string.touch_control_creator_combo_action),
                    description = stringResource(R.string.touch_control_creator_combo_action_desc)
                ) {
                    ActionSelector(
                        selectedActionId = secondaryActionId,
                        excludedActionId = actionId,
                        allowNone = true,
                        testTagPrefix = "controls_editor_combo_secondary",
                        onSelect = { secondaryActionId = it }
                    )
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(onClick = onDismiss) {
                        Text(stringResource(R.string.theme_manager_cancel))
                    }
                    Button(
                        onClick = { onConfirm(actionId, secondaryActionId) },
                        modifier = Modifier
                            .padding(start = 8.dp)
                            .testTag("controls_editor_combo_confirm"),
                        shape = neonShape(14.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFF3565FF),
                            contentColor = Color.White
                        )
                    ) {
                        Text(confirmLabel)
                    }
                }
            }
        }
    }
}

@Composable
private fun ComboActionPreview(
    primaryLabel: String,
    secondaryLabel: String?
) {
    Surface(
        color = Color.White.copy(alpha = 0.06f),
        shape = neonShape(16.dp),
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.08f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            ComboActionBadge(label = primaryLabel, highlighted = true)
            Text(
                text = "+",
                modifier = Modifier.padding(horizontal = 12.dp),
                style = MaterialTheme.typography.titleLarge,
                color = Color.White.copy(alpha = 0.6f)
            )
            if (secondaryLabel != null) {
                ComboActionBadge(label = secondaryLabel, highlighted = false)
            } else {
                Text(
                    text = "—",
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White.copy(alpha = 0.35f)
                )
            }
        }
    }
}

@Composable
private fun ComboActionBadge(
    label: String,
    highlighted: Boolean
) {
    Surface(
        color = if (highlighted) {
            Color(0xFF3565FF).copy(alpha = 0.32f)
        } else {
            Color(0xFF45E6FF).copy(alpha = 0.22f)
        },
        shape = neonShape(12.dp),
        border = BorderStroke(
            width = 1.dp,
            color = if (highlighted) {
                Color(0xFF7CC8FF).copy(alpha = 0.7f)
            } else {
                Color(0xFF45E6FF).copy(alpha = 0.6f)
            }
        )
    ) {
        Text(
            text = label,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = Color.White
        )
    }
}

@Composable
private fun ComboActionSection(
    title: String,
    description: String? = null,
    content: @Composable () -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            color = Color(0xFF9DB4FF)
        )
        description?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.bodySmall,
                color = Color.White.copy(alpha = 0.62f)
            )
        }
        content()
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
    customControls: List<CustomTouchControl>,
    selectedControlId: String?,
    onSelectControl: (String) -> Unit,
    onSetControlOffset: (String, Pair<Float, Float>) -> Unit,
    onCommitControlPosition: (String) -> Unit,
    onCommitControlPositions: (List<String>) -> Unit,
    onSetCustomControlPosition: (String, Float, Float) -> Unit,
    onCommitCustomControlPosition: (String) -> Unit,
    onSelectedGeometryChange: (EditorControlGeometry?) -> Unit,
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

        val canvasWidthPx = with(density) { maxWidth.toPx() }
        val canvasHeightPx = with(density) { maxHeight.toPx() }
        val safeLeftPx = with(density) { safeLeftInset.toPx() }
        val safeRightPx = with(density) { safeRightInset.toPx() }
        val safeTopPx = with(density) { safeTop.toPx() }
        val safeBottomPx = with(density) { safeBottom.toPx() }

        customControls.forEach { control ->
            val widthPx = with(density) { control.widthDp.dp.toPx() }
            val heightPx = with(density) { control.heightDp.dp.toPx() }
            val travelX = (canvasWidthPx - safeLeftPx - safeRightPx - widthPx).coerceAtLeast(1f)
            val travelY = (canvasHeightPx - safeTopPx - safeBottomPx - heightPx).coerceAtLeast(1f)
            val selectionId = customControlSelectionId(control.id)
            val selected = selectedControlId == selectionId
            DraggableControl(
                id = selectionId,
                selected = selected,
                onSelectControl = onSelectControl,
                onMoveControlBy = { _, delta ->
                    onSetCustomControlPosition(
                        control.id,
                        (control.positionX + delta.first / travelX).coerceIn(0f, 1f),
                        (control.positionY + delta.second / travelY).coerceIn(0f, 1f)
                    )
                },
                onCommitControlPosition = { onCommitCustomControlPosition(control.id) },
                baseZIndex = 4f,
                modifier = Modifier.offset {
                    IntOffset(
                        (safeLeftPx + travelX * control.positionX).roundToInt(),
                        (safeTopPx + travelY * control.positionY).roundToInt()
                    )
                }
            ) {
                val vectorDrawable = control.takeIf { it.usesVectorStyle }
                    ?.let { actionDrawableRes(it.actionId) }
                if (vectorDrawable != null) {
                    VectorOverlayButton(
                        drawableRes = vectorDrawable,
                        width = control.widthDp.dp,
                        height = control.heightDp.dp,
                        shape = control.composeShape(),
                        alpha = if (control.enabled) control.opacity / 100f else 0.38f,
                        selected = selected,
                        interactive = false,
                        visualStyle = state.touchControlVisualStyle,
                        pressEffect = state.touchControlPressEffect,
                        modifier = Modifier.testTag("controls_editor_custom_${control.id}")
                    )
                } else {
                    CustomControlVisual(
                        control = control,
                        pressed = selected,
                        selected = selected,
                        modifier = Modifier
                            .size(control.widthDp.dp, control.heightDp.dp)
                            .graphicsLayer(alpha = if (control.enabled) 1f else 0.38f)
                            .testTag("controls_editor_custom_${control.id}")
                    )
                }
            }
        }

        LaunchedEffect(layout, controlLayouts, customControls, selectedControlId) {
            val selection = selectedControlId
            val geometry = when {
                selection == null -> null
                else -> {
                    val customId = selection.toCustomControlIdOrNull()
                    if (customId != null) {
                        customControls.firstOrNull { it.id == customId }?.let { control ->
                            EditorControlGeometry(
                                positionX = control.positionX,
                                positionY = control.positionY,
                                widthDp = control.widthDp,
                                heightDp = control.heightDp
                            )
                        }
                    } else {
                        layout.button(selection)?.let { spec ->
                            val widthPx = with(density) { spec.width.toPx() }
                            val heightPx = with(density) { spec.height.toPx() }
                            val travelX = (canvasWidthPx - safeLeftPx - safeRightPx - widthPx)
                                .coerceAtLeast(1f)
                            val travelY = (canvasHeightPx - safeTopPx - safeBottomPx - heightPx)
                                .coerceAtLeast(1f)
                            EditorControlGeometry(
                                positionX = (
                                    (with(density) { spec.x.toPx() } - safeLeftPx) / travelX
                                    ).coerceIn(0f, 1f),
                                positionY = (
                                    (with(density) { spec.y.toPx() } - safeTopPx) / travelY
                                    ).coerceIn(0f, 1f),
                                widthDp = spec.width.value.roundToInt()
                                    .coerceIn(CustomTouchControl.MIN_SIZE_DP, CustomTouchControl.MAX_SIZE_DP),
                                heightDp = spec.height.value.roundToInt()
                                    .coerceIn(CustomTouchControl.MIN_SIZE_DP, CustomTouchControl.MAX_SIZE_DP)
                            )
                        }
                    }
                }
            }
            onSelectedGeometryChange(geometry)
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

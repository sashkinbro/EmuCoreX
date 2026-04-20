package com.sbro.emucorex.ui.common

import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout

data class OverlayCanvasButtonSpec(
    val id: String,
    val drawableRes: Int,
    val width: Dp,
    val height: Dp,
    val baseX: Dp,
    val baseY: Dp,
    val x: Dp,
    val y: Dp,
    val shape: Shape,
    val visible: Boolean
)

data class OverlayCanvasStickSpec(
    val id: String,
    val size: Dp,
    val baseX: Dp,
    val baseY: Dp,
    val x: Dp,
    val y: Dp,
    val visible: Boolean
)

data class OverlayCanvasLayout(
    val leftShoulders: List<OverlayCanvasButtonSpec>,
    val rightShoulders: List<OverlayCanvasButtonSpec>,
    val dpadButtons: List<OverlayCanvasButtonSpec>,
    val actionButtons: List<OverlayCanvasButtonSpec>,
    val centerButtons: List<OverlayCanvasButtonSpec>,
    val leftStick: OverlayCanvasStickSpec?,
    val rightStick: OverlayCanvasStickSpec?
) {
    val allButtons: List<OverlayCanvasButtonSpec> = buildList {
        addAll(leftShoulders)
        addAll(rightShoulders)
        addAll(dpadButtons)
        addAll(actionButtons)
        addAll(centerButtons)
    }

    fun button(id: String): OverlayCanvasButtonSpec? = allButtons.firstOrNull { it.id == id }

    fun stick(id: String): OverlayCanvasStickSpec? = when (id) {
        leftStick?.id -> leftStick
        rightStick?.id -> rightStick
        else -> null
    }
}

fun buildOverlayCanvasLayout(
    canvasWidth: Dp,
    canvasHeight: Dp,
    density: Density,
    scaleFactor: Float,
    stickScaleFactor: Float,
    dpadOffset: Pair<Float, Float>,
    lstickOffset: Pair<Float, Float>,
    rstickOffset: Pair<Float, Float>,
    actionOffset: Pair<Float, Float>,
    lbtnOffset: Pair<Float, Float>,
    rbtnOffset: Pair<Float, Float>,
    centerOffset: Pair<Float, Float>,
    controlLayouts: Map<String, OverlayControlLayout>,
    safeHorizontalInset: Dp,
    safeTopInset: Dp,
    safeBottomInset: Dp,
    previewMode: Boolean = false
): OverlayCanvasLayout {
    fun pxToDp(value: Float): Dp = with(density) { value.toDp() }

    val isLandscape = canvasWidth >= canvasHeight
    val dpadSize = ((if (isLandscape) 130 else 150) * scaleFactor).dp
    val actionSize = ((if (isLandscape) 130 else 150) * scaleFactor).dp
    val analogSize = ((if (isLandscape) 120 else 140) * scaleFactor * stickScaleFactor).dp
    val shoulderW = ((if (isLandscape) 65 else 72) * scaleFactor).dp
    val shoulderH = ((if (isLandscape) 32 else 36) * scaleFactor).dp
    val centerW = ((if (isLandscape) 60 else 68) * scaleFactor).dp
    val centerH = ((if (isLandscape) 26 else 30) * scaleFactor).dp
    val wideCenterW = centerW * 1.2f
    val centerInlineGap = (if (isLandscape) OverlayCenterInlineGapLandscape else OverlayCenterInlineGapPortrait) * scaleFactor
    val clusterGap = if (isLandscape) OverlayClusterGapLandscape else OverlayClusterGapPortrait
    val actionGap = if (isLandscape) OverlayActionGapLandscape else OverlayActionGapPortrait
    val dpadButtonSize = dpadSize / 3f
    val actionButtonSize = actionSize / 3.1f
    val dpadStep = overlayClusterStep(dpadButtonSize, clusterGap)
    val actionStep = overlayClusterStep(actionButtonSize, actionGap)
    val dpadClusterExtent = dpadStep + dpadButtonSize
    val actionClusterExtent = actionStep + actionButtonSize
    val dpadCenterOffset = (dpadClusterExtent - dpadButtonSize) / 2f
    val actionCenterOffset = (actionClusterExtent - actionButtonSize) / 2f
    val baseEdgePad = if (isLandscape) 28.dp else 12.dp
    val baseBottomPad = if (isLandscape) 24.dp else 36.dp
    val edgePadStart = baseEdgePad + safeHorizontalInset
    val edgePadEnd = baseEdgePad + safeHorizontalInset
    val edgePadTop = maxOf(OverlayShoulderTopPadding, safeTopInset + 4.dp)
    val bottomPad = baseBottomPad + safeBottomInset

    fun layoutFor(id: String, defaultScale: Int = 100): OverlayControlLayout {
        return controlLayouts[id]
            ?: AppPreferences.defaultOverlayControlLayouts(defaultScale)[id]
            ?: OverlayControlLayout(scale = defaultScale)
    }

    fun buttonSpec(
        id: String,
        width: Dp,
        height: Dp,
        baseX: Dp,
        baseY: Dp,
        shape: Shape,
        visible: Boolean
    ): OverlayCanvasButtonSpec {
        val layout = layoutFor(id)
        val actualWidth = width * (layout.scale / 100f)
        val actualHeight = height * (layout.scale / 100f)
        val currentBaseX = when {
            id == "r1" || id == "r2" -> baseX - (actualWidth - width)
            else -> baseX
        }
        return OverlayCanvasButtonSpec(
            id = id,
            drawableRes = requireNotNull(overlayDrawableForControl(id)),
            width = actualWidth,
            height = actualHeight,
            baseX = currentBaseX,
            baseY = baseY,
            x = currentBaseX + pxToDp(layout.offset.first),
            y = baseY + pxToDp(layout.offset.second),
            shape = shape,
            visible = visible && layout.visible
        )
    }

    val leftShoulderBaseX = edgePadStart + pxToDp(lbtnOffset.first)
    val leftShoulderBaseY = edgePadTop + pxToDp(lbtnOffset.second)
    val leftShoulders = listOf(
        buttonSpec(
            id = "l2",
            width = shoulderW,
            height = shoulderH,
            baseX = leftShoulderBaseX,
            baseY = leftShoulderBaseY,
            shape = RoundedCornerShape(10.dp),
            visible = true
        ),
        buttonSpec(
            id = "l1",
            width = shoulderW,
            height = shoulderH,
            baseX = leftShoulderBaseX,
            baseY = leftShoulderBaseY + OverlayShoulderVerticalGap,
            shape = RoundedCornerShape(10.dp),
            visible = true
        )
    )

    val rightShoulderColumnBaseX = canvasWidth - edgePadEnd + pxToDp(rbtnOffset.first) - shoulderW
    val rightShoulderBaseY = edgePadTop + pxToDp(rbtnOffset.second)
    val rightShoulders = listOf(
        buttonSpec(
            id = "r2",
            width = shoulderW,
            height = shoulderH,
            baseX = rightShoulderColumnBaseX,
            baseY = rightShoulderBaseY,
            shape = RoundedCornerShape(10.dp),
            visible = true
        ),
        buttonSpec(
            id = "r1",
            width = shoulderW,
            height = shoulderH,
            baseX = rightShoulderColumnBaseX,
            baseY = rightShoulderBaseY + OverlayRightShoulderGapOffset,
            shape = RoundedCornerShape(10.dp),
            visible = true
        )
    )

    val leftStickLayout = layoutFor("left_stick", (stickScaleFactor * 100f).toInt())
    val leftStickSize = analogSize * (leftStickLayout.scale / (stickScaleFactor * 100f).coerceAtLeast(1f))
    val leftStickBase = (dpadSize - leftStickSize) / 2f
    val leftStick = OverlayCanvasStickSpec(
        id = "left_stick",
        size = leftStickSize,
        baseX = edgePadStart + pxToDp(lstickOffset.first) + leftStickBase,
        baseY = canvasHeight - bottomPad + pxToDp(lstickOffset.second) - dpadSize + leftStickBase,
        x = edgePadStart + pxToDp(lstickOffset.first) + leftStickBase + pxToDp(leftStickLayout.offset.first),
        y = canvasHeight - bottomPad + pxToDp(lstickOffset.second) - dpadSize + leftStickBase + pxToDp(leftStickLayout.offset.second),
        visible = leftStickLayout.visible
    )

    val showDpad = !leftStickLayout.visible
    val dpadClusterLeft = edgePadStart + pxToDp(dpadOffset.first)
    val dpadClusterTop = canvasHeight - bottomPad + pxToDp(dpadOffset.second) - dpadClusterExtent
    val dpadButtons = listOf(
        buttonSpec(
            id = "dpad_up",
            width = dpadButtonSize,
            height = dpadButtonSize,
            baseX = dpadClusterLeft + dpadCenterOffset,
            baseY = dpadClusterTop,
            shape = RoundedCornerShape(8.dp),
            visible = showDpad
        ),
        buttonSpec(
            id = "dpad_down",
            width = dpadButtonSize,
            height = dpadButtonSize,
            baseX = dpadClusterLeft + dpadCenterOffset,
            baseY = dpadClusterTop + dpadStep,
            shape = RoundedCornerShape(8.dp),
            visible = showDpad
        ),
        buttonSpec(
            id = "dpad_left",
            width = dpadButtonSize,
            height = dpadButtonSize,
            baseX = dpadClusterLeft,
            baseY = dpadClusterTop + dpadCenterOffset,
            shape = RoundedCornerShape(8.dp),
            visible = showDpad
        ),
        buttonSpec(
            id = "dpad_right",
            width = dpadButtonSize,
            height = dpadButtonSize,
            baseX = dpadClusterLeft + dpadStep,
            baseY = dpadClusterTop + dpadCenterOffset,
            shape = RoundedCornerShape(8.dp),
            visible = showDpad
        )
    )

    val actionClusterLeft = canvasWidth - edgePadEnd + pxToDp(actionOffset.first) - actionClusterExtent
    val actionClusterTop = canvasHeight - bottomPad + pxToDp(actionOffset.second) - actionClusterExtent
    val actionButtons = listOf(
        buttonSpec(
            id = "triangle",
            width = actionButtonSize,
            height = actionButtonSize,
            baseX = actionClusterLeft + actionCenterOffset,
            baseY = actionClusterTop,
            shape = CircleShape,
            visible = true
        ),
        buttonSpec(
            id = "cross",
            width = actionButtonSize,
            height = actionButtonSize,
            baseX = actionClusterLeft + actionCenterOffset,
            baseY = actionClusterTop + actionStep,
            shape = CircleShape,
            visible = true
        ),
        buttonSpec(
            id = "square",
            width = actionButtonSize,
            height = actionButtonSize,
            baseX = actionClusterLeft,
            baseY = actionClusterTop + actionCenterOffset,
            shape = CircleShape,
            visible = true
        ),
        buttonSpec(
            id = "circle",
            width = actionButtonSize,
            height = actionButtonSize,
            baseX = actionClusterLeft + actionStep,
            baseY = actionClusterTop + actionCenterOffset,
            shape = CircleShape,
            visible = true
        )
    )

    val centerAnchorX = canvasWidth / 2f + OverlayCenterBaseShiftX + pxToDp(centerOffset.first)
    val centerBaseY = canvasHeight -
        (bottomPad + (OverlayCenterBottomPadding - OverlayBottomAnchorPadding)) -
        centerH +
        pxToDp(centerOffset.second)

    val l3Layout = layoutFor("l3")
    val selectLayout = layoutFor("select")
    val toggleLayout = layoutFor("left_input_toggle")
    val startLayout = layoutFor("start")
    val r3Layout = layoutFor("r3")
    val l3Width = centerW * (l3Layout.scale / 100f)
    val r3Width = centerW * (r3Layout.scale / 100f)
    val selectWidth = wideCenterW * (selectLayout.scale / 100f)
    val toggleSize = centerH * (toggleLayout.scale / 100f)
    val startWidth = wideCenterW * (startLayout.scale / 100f)
    val coreCenterItems = buildList {
        if (selectLayout.visible) add("select" to selectWidth)
        if (toggleLayout.visible) add("left_input_toggle" to toggleSize)
        if (startLayout.visible) add("start" to startWidth)
    }
    val coreCenterWidths = coreCenterItems.map { it.second }

    fun centerNudgeX(id: String): Dp = when (id) {
        "select" -> OverlayCenterSelectOpticalNudgeX
        "start" -> OverlayCenterStartOpticalNudgeX
        else -> 0.dp
    }

    fun centerNudgeY(id: String): Dp = if (id == "left_input_toggle") {
        OverlayCenterToggleOpticalNudgeY
    } else {
        0.dp
    }

    fun coreCenterBaseX(id: String): Dp? {
        val index = coreCenterItems.indexOfFirst { it.first == id }
        return if (index >= 0) {
            centerAnchorX + overlayInlineGroupOffset(coreCenterWidths, centerInlineGap, index) + centerNudgeX(id)
        } else {
            null
        }
    }

    val coreCenterBounds = coreCenterItems.mapNotNull { (id, width) ->
        coreCenterBaseX(id)?.let { it to (it + width) }
    }
    val coreCenterLeft = coreCenterBounds.minOfOrNull { it.first } ?: centerAnchorX
    val coreCenterRight = coreCenterBounds.maxOfOrNull { it.second } ?: centerAnchorX
    val fallbackCoreWidths = listOf(selectWidth, toggleSize, startWidth)

    fun fallbackCenterX(id: String): Dp {
        return when (id) {
            "l3" -> coreCenterLeft - centerInlineGap - l3Width
            "select" -> centerAnchorX + overlayInlineGroupOffset(fallbackCoreWidths, centerInlineGap, 0) + OverlayCenterSelectOpticalNudgeX
            "left_input_toggle" -> centerAnchorX + overlayInlineGroupOffset(fallbackCoreWidths, centerInlineGap, 1)
            "start" -> centerAnchorX + overlayInlineGroupOffset(fallbackCoreWidths, centerInlineGap, 2) + OverlayCenterStartOpticalNudgeX
            "r3" -> coreCenterRight + centerInlineGap
            else -> centerAnchorX
        }
    }

    fun centerButtonSpec(
        id: String,
        width: Dp,
        height: Dp,
        visible: Boolean,
        shape: Shape
    ): OverlayCanvasButtonSpec {
        val layout = layoutFor(id)
        val baseX = coreCenterBaseX(id) ?: fallbackCenterX(id)
        val baseY = centerBaseY + centerNudgeY(id)
        return OverlayCanvasButtonSpec(
            id = id,
            drawableRes = requireNotNull(overlayDrawableForControl(id)),
            width = width,
            height = height,
            baseX = baseX,
            baseY = baseY,
            x = baseX + pxToDp(layout.offset.first),
            y = baseY + pxToDp(layout.offset.second),
            shape = shape,
            visible = visible && layout.visible
        )
    }

    val centerButtons = listOf(
        centerButtonSpec(
            id = "select",
            width = selectWidth,
            height = centerH * (selectLayout.scale / 100f),
            visible = previewMode || selectLayout.visible,
            shape = RoundedCornerShape(8.dp)
        ),
        centerButtonSpec(
            id = "left_input_toggle",
            width = toggleSize,
            height = toggleSize,
            visible = previewMode || toggleLayout.visible,
            shape = CircleShape
        ),
        centerButtonSpec(
            id = "start",
            width = startWidth,
            height = centerH * (startLayout.scale / 100f),
            visible = previewMode || startLayout.visible,
            shape = RoundedCornerShape(8.dp)
        ),
        centerButtonSpec(
            id = "l3",
            width = l3Width,
            height = centerH * (l3Layout.scale / 100f),
            visible = previewMode || l3Layout.visible,
            shape = RoundedCornerShape(8.dp)
        ),
        centerButtonSpec(
            id = "r3",
            width = r3Width,
            height = centerH * (r3Layout.scale / 100f),
            visible = previewMode || r3Layout.visible,
            shape = RoundedCornerShape(8.dp)
        )
    )

    val rightStickLayout = layoutFor("right_stick", (stickScaleFactor * 100f).toInt())
    val rightStickSize = analogSize * (rightStickLayout.scale / (stickScaleFactor * 100f).coerceAtLeast(1f))
    val rightStickGap = 18.dp
    val rightStickPreferredX = actionClusterLeft - rightStickGap - rightStickSize
    val rightStickMinX = centerAnchorX + 28.dp
    val defaultRightStickBaseX = maxOf(rightStickMinX, rightStickPreferredX)
    val rightStickBaseX = defaultRightStickBaseX + pxToDp(rstickOffset.first)
    val rightStickBaseY = canvasHeight - bottomPad + pxToDp(rstickOffset.second) - rightStickSize - OverlayRightStickBaseLift
    val rightStick = OverlayCanvasStickSpec(
        id = "right_stick",
        size = rightStickSize,
        baseX = rightStickBaseX,
        baseY = rightStickBaseY,
        x = rightStickBaseX + pxToDp(rightStickLayout.offset.first),
        y = rightStickBaseY + pxToDp(rightStickLayout.offset.second),
        visible = rightStickLayout.visible
    )

    return OverlayCanvasLayout(
        leftShoulders = leftShoulders,
        rightShoulders = rightShoulders,
        dpadButtons = dpadButtons.filter { previewMode || it.visible },
        actionButtons = actionButtons,
        centerButtons = centerButtons.filter { previewMode || it.visible },
        leftStick = leftStick.takeIf { previewMode || it.visible },
        rightStick = rightStick.takeIf { previewMode || it.visible }
    )
}

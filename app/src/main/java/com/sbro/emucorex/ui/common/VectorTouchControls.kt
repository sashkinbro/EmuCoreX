package com.sbro.emucorex.ui.common

import androidx.annotation.DrawableRes
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import kotlin.math.abs
import kotlin.math.roundToInt

private val OverlaySelectedStroke = Color(0xFF7CC8FF).copy(alpha = 0.88f)
private val OverlayPreviewStroke = Color.White.copy(alpha = 0.18f)

@DrawableRes
fun overlayDrawableForControl(controlId: String): Int? = when (controlId) {
    "triangle" -> R.drawable.ic_controller_triangle_button
    "cross" -> R.drawable.ic_controller_cross_button
    "square" -> R.drawable.ic_controller_square_button
    "circle" -> R.drawable.ic_controller_circle_button
    "dpad_up" -> R.drawable.ic_controller_up_button
    "dpad_down" -> R.drawable.ic_controller_down_button
    "dpad_left" -> R.drawable.ic_controller_left_button
    "dpad_right" -> R.drawable.ic_controller_right_button
    "l1" -> R.drawable.ic_controller_l1_button
    "l2" -> R.drawable.ic_controller_l2_button
    "r1" -> R.drawable.ic_controller_r1_button
    "r2" -> R.drawable.ic_controller_r2_button
    "select" -> R.drawable.ic_controller_select_button
    "start" -> R.drawable.ic_controller_start_button
    "l3" -> R.drawable.ic_controller_l3_button
    "r3" -> R.drawable.ic_controller_r3_button
    "left_input_toggle" -> R.drawable.ic_controller_analog_button
    else -> null
}

@Composable
fun VectorOverlayButton(
    @DrawableRes drawableRes: Int,
    width: Dp,
    height: Dp,
    modifier: Modifier = Modifier,
    shape: Shape = RoundedCornerShape(10.dp),
    contentScale: ContentScale = ContentScale.Fit,
    alpha: Float = 1f,
    selected: Boolean = false,
    pressed: Boolean = false,
    interactive: Boolean = true,
    pressedScale: Float = 0.9f,
    onClick: (() -> Unit)? = null,
    onPressChange: ((Boolean) -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(
        targetValue = if ((interactive && isPressed) || pressed) pressedScale else 1f,
        animationSpec = tween(durationMillis = 80),
        label = "vector_overlay_button_scale"
    )

    LaunchedEffect(isPressed, onPressChange) {
        onPressChange?.invoke(interactive && isPressed)
    }

    val clickableModifier = if (interactive && (onPressChange != null || onClick != null)) {
        Modifier.clickable(
            interactionSource = interactionSource,
            indication = null
        ) {
            onClick?.invoke()
        }
    } else if (onClick != null) {
        Modifier.clickable(onClick = onClick)
    } else {
        Modifier
    }

    Box(
        modifier = modifier
            .size(width = width, height = height)
            .graphicsLayer(
                scaleX = scale,
                scaleY = scale,
                alpha = alpha
            )
            .clip(shape)
            .then(
                if (selected) {
                    Modifier.border(1.5.dp, OverlaySelectedStroke, shape)
                } else if (alpha < 0.65f) {
                    Modifier.border(1.dp, OverlayPreviewStroke.copy(alpha = 0.35f), shape)
                } else {
                    Modifier
                }
            )
            .then(clickableModifier),
        contentAlignment = Alignment.Center
    ) {
        Image(
            painter = painterResource(drawableRes),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = contentScale
        )
    }
}

@Composable
fun VectorAnalogStick(
    analogSize: Dp,
    modifier: Modifier = Modifier,
    alpha: Float = 1f,
    selected: Boolean = false,
    interactive: Boolean = true,
    onClick: (() -> Unit)? = null,
    onValueChange: ((Float, Float) -> Unit)? = null
) {
    var size by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var thumbOffset by remember { mutableStateOf(Offset.Zero) }
    var lastSentX by remember { mutableIntStateOf(0) }
    var lastSentY by remember { mutableIntStateOf(0) }

    fun dispatchStickValue(x: Float, y: Float) {
        val quantizedX = (x * 255f).roundToInt()
        val quantizedY = (y * 255f).roundToInt()
        if (quantizedX == lastSentX && quantizedY == lastSentY) return
        lastSentX = quantizedX
        lastSentY = quantizedY
        onValueChange?.invoke(quantizedX / 255f, quantizedY / 255f)
    }

    fun resetStick() {
        thumbOffset = Offset.Zero
        dispatchStickValue(0f, 0f)
    }

    val pointerModifier = if (interactive && onValueChange != null) {
        Modifier.pointerInput(size) {
            detectDragGestures(
                onDragStart = { offset ->
                    if (size.width == 0f) return@detectDragGestures
                    val center = Offset(size.width / 2f, size.height / 2f)
                    val maxDistance = minOf(size.width, size.height) * 0.32f
                    val deadZone = 0.12f
                    val raw = offset - center
                    val distance = raw.getDistance()
                    val clamped = if (distance > maxDistance && distance > 0f) {
                        raw * (maxDistance / distance)
                    } else {
                        raw
                    }
                    thumbOffset = clamped
                    val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < deadZone) 0f else it }
                    val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < deadZone) 0f else it }
                    dispatchStickValue(nx, ny)
                },
                onDragEnd = { resetStick() },
                onDragCancel = { resetStick() }
            ) { change, _ ->
                change.consume()
                if (size.width == 0f) return@detectDragGestures
                val center = Offset(size.width / 2f, size.height / 2f)
                val maxDistance = minOf(size.width, size.height) * 0.32f
                val deadZone = 0.12f
                val raw = change.position - center
                val distance = raw.getDistance()
                val clamped = if (distance > maxDistance && distance > 0f) {
                    raw * (maxDistance / distance)
                } else {
                    raw
                }
                thumbOffset = clamped
                val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < deadZone) 0f else it }
                val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < deadZone) 0f else it }
                dispatchStickValue(nx, ny)
            }
        }
    } else {
        Modifier
    }

    Box(
        modifier = modifier
            .size(analogSize)
            .graphicsLayer(alpha = alpha)
            .clip(CircleShape)
            .then(
                if (selected) {
                    Modifier.border(1.5.dp, OverlaySelectedStroke, CircleShape)
                } else if (alpha < 0.65f) {
                    Modifier.border(1.dp, OverlayPreviewStroke.copy(alpha = 0.35f), CircleShape)
                } else {
                    Modifier
                }
            )
            .then(
                if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier
            )
            .onSizeChanged { size = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .then(pointerModifier),
        contentAlignment = Alignment.Center
    ) {
        Image(
            painter = painterResource(R.drawable.ic_controller_analog_base),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Fit
        )
        Image(
            painter = painterResource(R.drawable.ic_controller_analog_stick),
            contentDescription = null,
            modifier = Modifier
                .size(analogSize * 0.68f)
                .offset { IntOffset(thumbOffset.x.roundToInt(), thumbOffset.y.roundToInt()) },
            contentScale = ContentScale.Fit
        )
    }
}

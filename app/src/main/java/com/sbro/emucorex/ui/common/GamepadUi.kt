package com.sbro.emucorex.ui.common

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.border
import androidx.compose.foundation.focusable
import androidx.compose.foundation.focusGroup
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.unit.dp
import androidx.compose.material3.MaterialTheme
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.LocalTvUiEnvironment

enum class GamepadFocusHighlightMode {
    ConnectedGamepadOnly,
    Always
}

fun Modifier.gamepadFocusableCard(
    enabled: Boolean = true,
    shape: Shape = RoundedCornerShape(18.dp),
    interactionSource: MutableInteractionSource? = null,
    addFocusTarget: Boolean = true,
    focusHighlightMode: GamepadFocusHighlightMode = GamepadFocusHighlightMode.ConnectedGamepadOnly
): Modifier = composed {
    val focusInteractionSource = interactionSource ?: remember { MutableInteractionSource() }
    val isFocused by focusInteractionSource.collectIsFocusedAsState()
    val connectedGamepadCount by GamepadManager.connectedGamepadCountState.collectAsState()
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled
    val shouldShowFocusHighlight = isFocused && enabled && (
        focusHighlightMode == GamepadFocusHighlightMode.Always ||
            connectedGamepadCount > 0 ||
            tvUiEnabled
    )
    val scale by animateFloatAsState(
        targetValue = if (shouldShowFocusHighlight && !tvUiEnabled) 1.02f else 1f,
        label = "gamepadFocusScale"
    )
    val focusBorder = when {
        shouldShowFocusHighlight -> BorderStroke(2.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.95f))
        else -> BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    }

    var focusedModifier = if (tvUiEnabled) {
        this.clip(shape)
    } else {
        this
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            .shadow(
                elevation = if (shouldShowFocusHighlight) 14.dp else 0.dp,
                shape = shape,
                clip = false
            )
            .clip(shape)
    }

    focusedModifier = focusedModifier.border(
        border = focusBorder,
        shape = shape
    )

    if (addFocusTarget) {
        focusedModifier.focusable(
            enabled = enabled,
            interactionSource = focusInteractionSource
        )
    } else {
        focusedModifier
    }
}

fun Modifier.tvGamepadFocusableCard(
    shape: Shape = RoundedCornerShape(18.dp),
    interactionSource: MutableInteractionSource? = null,
    addFocusTarget: Boolean = true
): Modifier = composed {
    if (LocalTvUiEnvironment.current.enabled) {
        gamepadFocusableCard(
            shape = shape,
            interactionSource = interactionSource,
            addFocusTarget = addFocusTarget,
            focusHighlightMode = GamepadFocusHighlightMode.Always
        )
    } else {
        this
    }
}

/** Keeps D-pad traversal inside a horizontal/vertical control group on TV. */
fun Modifier.tvFocusGroup(): Modifier = composed {
    if (LocalTvUiEnvironment.current.enabled) focusGroup() else this
}

@Composable
fun RequestFocusOnResume(
    focusRequester: FocusRequester,
    enabled: Boolean = true
) {
    val lifecycleOwner = LocalLifecycleOwner.current

    DisposableEffect(lifecycleOwner, focusRequester, enabled) {
        if (!enabled) {
            onDispose { }
        } else {
            val observer = LifecycleEventObserver { _, event ->
                if (event == Lifecycle.Event.ON_RESUME) {
                    focusRequester.requestFocus()
                }
            }
            lifecycleOwner.lifecycle.addObserver(observer)
            onDispose {
                lifecycleOwner.lifecycle.removeObserver(observer)
            }
        }
    }
}

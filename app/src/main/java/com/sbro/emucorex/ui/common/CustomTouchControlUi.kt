package com.sbro.emucorex.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.CustomTouchControl
import com.sbro.emucorex.data.CustomTouchControlContent
import com.sbro.emucorex.data.CustomTouchControlShape
import com.sbro.emucorex.ui.theme.neon.neonShape

val OverlayCustomControlSelectionColor = Color(0xFF7CC8FF).copy(alpha = 0.9f)

fun CustomTouchControl.composeShape(): Shape = when (shape) {
    CustomTouchControlShape.CIRCLE -> CircleShape
    CustomTouchControlShape.ROUNDED -> RoundedCornerShape(cornerDp.dp)
    CustomTouchControlShape.SQUARE -> RoundedCornerShape(0.dp)
    CustomTouchControlShape.PILL -> RoundedCornerShape(50)
}

@Composable
fun actionLabel(actionId: String): String = when (actionId) {
    "up" -> stringResource(R.string.settings_gamepad_action_dpad_up)
    "down" -> stringResource(R.string.settings_gamepad_action_dpad_down)
    "left" -> stringResource(R.string.settings_gamepad_action_dpad_left)
    "right" -> stringResource(R.string.settings_gamepad_action_dpad_right)
    "triangle" -> stringResource(R.string.settings_gamepad_action_triangle)
    "cross" -> stringResource(R.string.settings_gamepad_action_cross)
    "square" -> stringResource(R.string.settings_gamepad_action_square)
    "circle" -> stringResource(R.string.settings_gamepad_action_circle)
    "select" -> stringResource(R.string.settings_gamepad_action_select)
    "start" -> stringResource(R.string.settings_gamepad_action_start)
    "pressure" -> stringResource(R.string.settings_gamepad_action_pressure)
    "gun_trigger" -> stringResource(R.string.settings_gamepad_action_gun_trigger)
    "gun_pedal" -> stringResource(R.string.settings_gamepad_action_gun_pedal)
    "gun_reload" -> stringResource(R.string.settings_gamepad_action_gun_reload)
    "gun_recalibrate" -> stringResource(R.string.settings_gamepad_action_gun_recalibrate)
    "coin" -> stringResource(R.string.settings_gamepad_action_coin)
    "service" -> stringResource(R.string.settings_gamepad_action_service)
    else -> actionId.uppercase()
}

@Composable
fun CustomControlVisual(
    control: CustomTouchControl,
    pressed: Boolean,
    modifier: Modifier = Modifier,
    selected: Boolean = false
) {
    val shape = control.composeShape()
    val opacity = control.opacity / 100f
    val transformModifier = Modifier
        .fillMaxSize()
        .scale(if (pressed) control.pressedScalePercent / 100f else 1f)
        .rotate(control.rotationDegrees.toFloat())
    Box(modifier = modifier) {
        Surface(
            modifier = transformModifier,
            shape = shape,
            color = Color(control.fillColor).copy(alpha = Color(control.fillColor).alpha * opacity),
            contentColor = Color(control.contentColor).copy(alpha = opacity),
            shadowElevation = control.shadowElevationDp.dp,
            border = control.borderWidthDp.takeIf { it > 0f }?.let {
                BorderStroke(
                    it.dp,
                    Color(control.borderColor).copy(alpha = Color(control.borderColor).alpha * opacity)
                )
            }
        ) {
            Box(contentAlignment = Alignment.Center) {
                if (control.content != CustomTouchControlContent.NONE) {
                    Text(
                        text = if (control.content == CustomTouchControlContent.SYMBOL) {
                            CustomTouchControl.defaultLabelFor(control.actionId)
                        } else {
                            control.label
                        },
                        modifier = Modifier.scale(control.contentScalePercent / 100f),
                        color = Color(control.contentColor).copy(alpha = opacity),
                        fontWeight = FontWeight.Bold,
                        textAlign = TextAlign.Center,
                        maxLines = 1,
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }
        }
        if (selected) {
            Box(
                modifier = transformModifier.border(
                    width = 2.dp,
                    color = OverlayCustomControlSelectionColor,
                    shape = shape
                )
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ActionSelector(
    selectedActionId: String?,
    excludedActionId: String? = null,
    allowNone: Boolean = false,
    enabled: Boolean = true,
    testTagPrefix: String? = null,
    onSelect: (String?) -> Unit
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        if (allowNone) {
            FilterChip(
                selected = selectedActionId == null,
                onClick = { onSelect(null) },
                label = { Text(stringResource(R.string.touch_control_creator_none)) },
                leadingIcon = if (selectedActionId == null) {
                    { Icon(Icons.Rounded.Check, contentDescription = null) }
                } else {
                    null
                },
                enabled = enabled,
                shape = neonShape(14.dp),
                modifier = Modifier.actionChipTestTag(testTagPrefix, "none")
            )
        }
        CustomTouchControl.ALLOWED_ACTION_IDS
            .filterNot { it == excludedActionId }
            .forEach { action ->
                FilterChip(
                    selected = selectedActionId == action,
                    onClick = { onSelect(action) },
                    label = { Text(actionLabel(action)) },
                    leadingIcon = if (selectedActionId == action) {
                        { Icon(Icons.Rounded.Check, contentDescription = null) }
                    } else {
                        null
                    },
                    enabled = enabled,
                    shape = neonShape(14.dp),
                    modifier = Modifier.actionChipTestTag(testTagPrefix, action)
                )
            }
    }
}

private fun Modifier.actionChipTestTag(prefix: String?, action: String): Modifier =
    if (prefix == null) this else testTag("${prefix}_$action")

package com.sbro.emucorex.ui.settings

import android.widget.Toast
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.LocalTvUiEnvironment
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.common.premiumFilterChipColors
import com.sbro.emucorex.ui.common.tvFocusGroup
import com.sbro.emucorex.ui.common.tvGamepadFocusableCard
import com.sbro.emucorex.ui.theme.neon.neonChipShape

/**
 * Standard overlay buttons that can be switched to tap-to-hold. The ids match
 * the touch overlay specs handled in EmulationScreen; Start/Select, the sticks
 * and the D-pad stay on their normal press-and-hold behaviour.
 */
internal val STICKY_BUTTON_IDS = listOf(
    "l1", "l2", "r1", "r2",
    "triangle", "cross", "square", "circle",
    "l3", "r3", "select", "start"
)

@Composable
private fun stickyButtonLabel(id: String): String = stringResource(
    when (id) {
        "l1" -> R.string.emulation_control_l1
        "l2" -> R.string.emulation_control_l2
        "r1" -> R.string.emulation_control_r1
        "r2" -> R.string.emulation_control_r2
        "l3" -> R.string.emulation_control_l3
        "r3" -> R.string.emulation_control_r3
        "select" -> R.string.emulation_control_select
        "start" -> R.string.emulation_control_start
        "triangle" -> R.string.settings_gamepad_action_triangle
        "square" -> R.string.settings_gamepad_action_square
        "circle" -> R.string.settings_gamepad_action_circle
        else -> R.string.settings_gamepad_action_cross
    }
)

@Composable
internal fun StickyButtonsSelector(
    selected: Set<String>,
    onSelectionChange: (Set<String>) -> Unit,
    modifier: Modifier = Modifier,
    helpText: String? = null,
    onResetToDefault: (() -> Unit)? = null
) {
    val context = LocalContext.current
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled
    val titleFocusRequester = remember { FocusRequester() }
    val helpFocusRequester = remember { FocusRequester() }
    val resetToast = stringResource(R.string.settings_reset_to_default_toast)
    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = 16.dp)
                .then(
                    if (tvUiEnabled && helpText != null) {
                        Modifier
                            .focusRequester(titleFocusRequester)
                            .focusProperties { right = helpFocusRequester }
                    } else {
                        Modifier
                    }
                )
                .combinedClickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                    onClick = {},
                    onLongClick = onResetToDefault?.let { reset ->
                        {
                            reset()
                            Toast.makeText(context, resetToast, Toast.LENGTH_SHORT).show()
                        }
                    }
                ),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = stringResource(R.string.settings_sticky_buttons),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = stringResource(R.string.settings_sticky_buttons_desc),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            helpText?.let {
                SettingHelpButton(
                    title = stringResource(R.string.settings_sticky_buttons),
                    description = it,
                    focusRequester = helpFocusRequester,
                    returnFocusRequester = titleFocusRequester
                )
            }
        }
        LazyRow(
            modifier = Modifier
                .fillMaxWidth()
                .tvFocusGroup(),
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(STICKY_BUTTON_IDS) { id ->
                val chipInteractionSource = remember { MutableInteractionSource() }
                FilterChip(
                    modifier = Modifier.tvGamepadFocusableCard(
                        shape = neonChipShape(),
                        interactionSource = chipInteractionSource,
                        addFocusTarget = false
                    ),
                    shape = neonChipShape(),
                    selected = id in selected,
                    onClick = {
                        onSelectionChange(if (id in selected) selected - id else selected + id)
                    },
                    interactionSource = chipInteractionSource,
                    colors = premiumFilterChipColors(),
                    label = { Text(text = stickyButtonLabel(id)) }
                )
            }
        }
    }
}

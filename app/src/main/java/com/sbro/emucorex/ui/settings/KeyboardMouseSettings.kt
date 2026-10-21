package com.sbro.emucorex.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Keyboard
import androidx.compose.material.icons.rounded.Mouse
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.KeyboardMouseBindings
import com.sbro.emucorex.core.KeyboardMouseManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.KeyboardMouseSettings
import com.sbro.emucorex.ui.common.AppAlertDialog
import com.sbro.emucorex.ui.common.SettingsStyledDialog
import com.sbro.emucorex.ui.common.gamepadActionLabel
import com.sbro.emucorex.ui.common.premiumFilterChipColors
import com.sbro.emucorex.ui.theme.neon.neonChipShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import kotlinx.coroutines.launch

private enum class KeyboardMouseMappingSource {
    KEYBOARD,
    MOUSE
}

@Composable
fun GlobalKeyboardMouseSettingsSection(showTitle: Boolean = true) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context.applicationContext) }
    val scope = rememberCoroutineScope()
    val settings by KeyboardMouseManager.globalSettings.collectAsState()
    val connection by KeyboardMouseManager.connection.collectAsState()

    KeyboardMouseSettingsContent(
        settings = settings,
        onSettingsChange = { updated ->
            scope.launch { preferences.setKeyboardMouseSettings(updated) }
        },
        connection = connection,
        showTitle = showTitle,
        contextNote = stringResource(R.string.settings_keyboard_mouse_global_note)
    )
}

@Composable
fun PerGameKeyboardMouseSettingsSection(
    override: KeyboardMouseSettings?,
    globalSettings: KeyboardMouseSettings,
    onOverrideChange: (KeyboardMouseSettings?) -> Unit,
    showTitle: Boolean = true
) {
    val connection by KeyboardMouseManager.connection.collectAsState()
    KeyboardMouseSettingsContent(
        settings = override ?: globalSettings,
        onSettingsChange = onOverrideChange,
        connection = connection,
        showTitle = showTitle,
        contextNote = stringResource(
            if (override == null) {
                R.string.settings_keyboard_mouse_global_note
            } else {
                R.string.settings_keyboard_mouse_game_desc
            }
        ),
        onUseGlobal = if (override != null) {
            { onOverrideChange(null) }
        } else {
            null
        }
    )
}

@Composable
fun KeyboardMouseSettingsDialog(
    settings: KeyboardMouseSettings,
    onSettingsChange: (KeyboardMouseSettings) -> Unit,
    onDismiss: () -> Unit,
    onUseGlobal: (() -> Unit)? = null
) {
    val connection by KeyboardMouseManager.connection.collectAsState()
    SettingsStyledDialog(
        title = stringResource(R.string.settings_keyboard_mouse_section),
        eyebrow = stringResource(R.string.app_name),
        icon = Icons.Rounded.Keyboard,
        onDismissRequest = onDismiss,
        edgeToEdgeContent = true
    ) {
        KeyboardMouseSettingsContent(
            settings = settings,
            onSettingsChange = onSettingsChange,
            connection = connection,
            showTitle = false,
            contextNote = stringResource(R.string.settings_keyboard_mouse_game_desc),
            onUseGlobal = onUseGlobal
        )
        TextButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.close))
        }
    }
}

@Composable
private fun KeyboardMouseSettingsContent(
    settings: KeyboardMouseSettings,
    onSettingsChange: (KeyboardMouseSettings) -> Unit,
    connection: KeyboardMouseManager.ConnectionState,
    showTitle: Boolean,
    contextNote: String,
    onUseGlobal: (() -> Unit)? = null
) {
    var mappingSource by remember { mutableStateOf<KeyboardMouseMappingSource?>(null) }

    val content: @Composable () -> Unit = {
        ToggleItem(
            icon = Icons.Rounded.Keyboard,
            title = stringResource(R.string.settings_keyboard_input),
            subtitle = stringResource(R.string.settings_keyboard_input_desc),
            checked = settings.keyboardEnabled,
            onCheckedChange = { enabled ->
                onSettingsChange(settings.copy(keyboardEnabled = enabled))
            }
        )
        if (settings.keyboardEnabled) {
            ChoiceSection(
                title = stringResource(R.string.settings_keyboard_target_player),
                options = listOf(
                    0 to stringResource(R.string.settings_gamepad_player_1),
                    1 to stringResource(R.string.settings_gamepad_player_2)
                ),
                selectedValue = settings.keyboardTargetPad,
                onSelect = { padIndex ->
                    onSettingsChange(settings.copy(keyboardTargetPad = padIndex))
                }
            )
            SettingsItem(
                icon = Icons.Rounded.Keyboard,
                label = stringResource(R.string.settings_keyboard_mapping_title),
                value = stringResource(R.string.settings_keyboard_mapping_desc),
                onClick = { mappingSource = KeyboardMouseMappingSource.KEYBOARD }
            )
        }
        ToggleItem(
            icon = Icons.Rounded.Mouse,
            title = stringResource(R.string.settings_mouse_input),
            subtitle = stringResource(R.string.settings_mouse_input_desc),
            checked = settings.mouseEnabled,
            onCheckedChange = { enabled ->
                onSettingsChange(settings.copy(mouseEnabled = enabled))
            }
        )
        if (settings.mouseEnabled) {
            ChoiceSection(
                title = stringResource(R.string.settings_mouse_target_player),
                options = listOf(
                    0 to stringResource(R.string.settings_gamepad_player_1),
                    1 to stringResource(R.string.settings_gamepad_player_2)
                ),
                selectedValue = settings.mouseTargetPad,
                onSelect = { padIndex ->
                    onSettingsChange(settings.copy(mouseTargetPad = padIndex))
                }
            )
            SettingsItem(
                icon = Icons.Rounded.Mouse,
                label = stringResource(R.string.settings_mouse_mapping_title),
                value = stringResource(R.string.settings_mouse_mapping_desc),
                onClick = { mappingSource = KeyboardMouseMappingSource.MOUSE }
            )
            ToggleItem(
                icon = Icons.Rounded.Mouse,
                title = stringResource(R.string.settings_mouse_capture),
                subtitle = stringResource(R.string.settings_mouse_capture_desc),
                checked = settings.mouseCaptureCursor,
                onCheckedChange = { enabled ->
                    onSettingsChange(settings.copy(mouseCaptureCursor = enabled))
                }
            )
            ToggleItem(
                icon = Icons.Rounded.Mouse,
                title = stringResource(R.string.settings_mouse_left_handed),
                subtitle = stringResource(R.string.settings_mouse_left_handed_desc),
                checked = settings.mouseLeftHanded,
                onCheckedChange = { enabled ->
                    onSettingsChange(settings.copy(mouseLeftHanded = enabled))
                }
            )
            ChoiceSection(
                title = stringResource(R.string.settings_mouse_wheel),
                options = listOf(
                    AppPreferences.MOUSE_WHEEL_OFF to stringResource(R.string.settings_mouse_wheel_off),
                    AppPreferences.MOUSE_WHEEL_DPAD to stringResource(R.string.settings_mouse_wheel_dpad),
                    AppPreferences.MOUSE_WHEEL_TRIGGERS to stringResource(R.string.settings_mouse_wheel_triggers),
                    AppPreferences.MOUSE_WHEEL_LEFT_STICK to stringResource(R.string.settings_mouse_wheel_left_stick),
                    AppPreferences.MOUSE_WHEEL_RIGHT_STICK to stringResource(R.string.settings_mouse_wheel_right_stick),
                    AppPreferences.MOUSE_WHEEL_SHOULDERS to stringResource(R.string.settings_mouse_wheel_shoulders)
                ),
                selectedValue = settings.mouseWheelMode,
                onSelect = { mode ->
                    onSettingsChange(settings.copy(mouseWheelMode = mode))
                }
            )
            if (settings.mouseWheelMode != AppPreferences.MOUSE_WHEEL_OFF) {
                KeyboardMouseSliderItem(
                    title = stringResource(R.string.settings_mouse_wheel_pulse),
                    value = settings.mouseWheelPulseMs,
                    range = AppPreferences.MOUSE_WHEEL_PULSE_MIN_MS.toFloat()..
                        AppPreferences.MOUSE_WHEEL_PULSE_MAX_MS.toFloat(),
                    valueSuffix = " ms",
                    onValueChange = { value ->
                        onSettingsChange(settings.copy(mouseWheelPulseMs = value))
                    }
                )
            }
            ChoiceSection(
                title = stringResource(R.string.settings_mouse_aim_mode),
                options = listOf(
                    AppPreferences.MOUSE_AIM_OFF to stringResource(R.string.settings_mouse_aim_off),
                    AppPreferences.MOUSE_AIM_RIGHT_STICK to stringResource(R.string.settings_mouse_aim_right_stick),
                    AppPreferences.MOUSE_AIM_LEFT_STICK to stringResource(R.string.settings_mouse_aim_left_stick)
                ),
                selectedValue = settings.mouseAimMode,
                onSelect = { mode ->
                    onSettingsChange(settings.copy(mouseAimMode = mode))
                },
                helpText = stringResource(R.string.settings_mouse_aim_mode_desc)
            )
            if (settings.mouseAimMode != KeyboardMouseBindings.MOUSE_AIM_OFF) {
                KeyboardMouseSliderItem(
                    title = stringResource(R.string.settings_mouse_sensitivity),
                    value = settings.mouseSensitivity,
                    range = AppPreferences.MOUSE_SENSITIVITY_MIN.toFloat()..
                        AppPreferences.MOUSE_SENSITIVITY_MAX.toFloat(),
                    onValueChange = { value ->
                        onSettingsChange(settings.copy(mouseSensitivity = value))
                    }
                )
                KeyboardMouseSliderItem(
                    title = stringResource(R.string.settings_mouse_vertical_sensitivity),
                    value = settings.mouseVerticalSensitivity,
                    range = AppPreferences.MOUSE_VERTICAL_SENSITIVITY_MIN.toFloat()..
                        AppPreferences.MOUSE_VERTICAL_SENSITIVITY_MAX.toFloat(),
                    onValueChange = { value ->
                        onSettingsChange(settings.copy(mouseVerticalSensitivity = value))
                    }
                )
                KeyboardMouseSliderItem(
                    title = stringResource(R.string.settings_mouse_smoothing),
                    value = settings.mouseSmoothing,
                    range = AppPreferences.MOUSE_SMOOTHING_MIN.toFloat()..
                        AppPreferences.MOUSE_SMOOTHING_MAX.toFloat(),
                    onValueChange = { value ->
                        onSettingsChange(settings.copy(mouseSmoothing = value))
                    }
                )
                KeyboardMouseSliderItem(
                    title = stringResource(R.string.settings_mouse_deadzone),
                    value = settings.mouseAimDeadzone,
                    range = AppPreferences.MOUSE_DEADZONE_MIN.toFloat()..
                        AppPreferences.MOUSE_DEADZONE_MAX.toFloat(),
                    onValueChange = { value ->
                        onSettingsChange(settings.copy(mouseAimDeadzone = value))
                    }
                )
                ToggleItem(
                    icon = Icons.Rounded.Mouse,
                    title = stringResource(R.string.settings_mouse_invert_x),
                    subtitle = stringResource(R.string.settings_mouse_invert_x_desc),
                    checked = settings.mouseInvertX,
                    onCheckedChange = { enabled ->
                        onSettingsChange(settings.copy(mouseInvertX = enabled))
                    }
                )
                ToggleItem(
                    icon = Icons.Rounded.Mouse,
                    title = stringResource(R.string.settings_mouse_invert_y),
                    subtitle = stringResource(R.string.settings_mouse_invert_y_desc),
                    checked = settings.mouseInvertY,
                    onCheckedChange = { enabled ->
                        onSettingsChange(settings.copy(mouseInvertY = enabled))
                    }
                )
            }
        }
        if (settings.keyboardEnabled || settings.mouseEnabled) {
            SettingsInlineNote(text = contextNote)
        }
        if (connection.keyboardConnected) {
            SettingsInlineNote(text = stringResource(R.string.settings_keyboard_connected))
        }
        if (connection.mouseConnected) {
            SettingsInlineNote(text = stringResource(R.string.settings_mouse_connected))
        }
        SettingsItem(
            icon = Icons.Rounded.SettingsSuggest,
            label = stringResource(R.string.settings_keyboard_mouse_mapping_reset_title),
            value = stringResource(R.string.settings_keyboard_mouse_mapping_reset_desc),
            onClick = {
                onSettingsChange(
                    settings.copy(
                        keyboardBindingsByPad = settings.keyboardBindingsByPad - settings.keyboardTargetPad,
                        mouseBindingsByPad = settings.mouseBindingsByPad - settings.mouseTargetPad
                    )
                )
            }
        )
        onUseGlobal?.let { useGlobal ->
            SettingsItem(
                icon = Icons.Rounded.SettingsSuggest,
                label = stringResource(R.string.settings_keyboard_mouse_use_global),
                value = stringResource(R.string.settings_keyboard_mouse_use_global_desc),
                onClick = useGlobal
            )
        }
    }

    if (showTitle) {
        SettingsSection(title = stringResource(R.string.settings_keyboard_mouse_section)) {
            content()
        }
    } else {
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            content()
        }
    }

    mappingSource?.let { source ->
        KeyboardMouseMappingDialog(
            settings = settings,
            onSettingsChange = onSettingsChange,
            initialSource = source,
            onDismiss = { mappingSource = null }
        )
    }
}

@Composable
private fun KeyboardMouseMappingDialog(
    settings: KeyboardMouseSettings,
    onSettingsChange: (KeyboardMouseSettings) -> Unit,
    initialSource: KeyboardMouseMappingSource,
    onDismiss: () -> Unit
) {
    val capture by KeyboardMouseManager.bindingCapture.collectAsState()
    val actions = remember { GamepadManager.mappableButtonActions() }
    var selectedPad by rememberSaveable { mutableIntStateOf(settings.keyboardTargetPad) }
    var source by rememberSaveable { mutableIntStateOf(initialSource.ordinal) }
    val mappingSource = if (source == KeyboardMouseMappingSource.MOUSE.ordinal) {
        KeyboardMouseMappingSource.MOUSE
    } else {
        KeyboardMouseMappingSource.KEYBOARD
    }

    SettingsStyledDialog(
        title = stringResource(
            if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                R.string.settings_mouse_mapping_title
            } else {
                R.string.settings_keyboard_mapping_title
            }
        ),
        eyebrow = stringResource(R.string.app_name),
        icon = if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
            Icons.Rounded.Mouse
        } else {
            Icons.Rounded.Keyboard
        },
        onDismissRequest = onDismiss,
        edgeToEdgeContent = true
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            listOf(0, 1).forEach { padIndex ->
                FilterChip(
                    shape = neonChipShape(),
                    selected = selectedPad == padIndex,
                    onClick = { selectedPad = padIndex },
                    colors = premiumFilterChipColors(),
                    label = {
                        Text(
                            stringResource(
                                if (padIndex == 0) {
                                    R.string.settings_gamepad_player_1
                                } else {
                                    R.string.settings_gamepad_player_2
                                }
                            )
                        )
                    }
                )
            }
        }
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            FilterChip(
                shape = neonChipShape(),
                selected = mappingSource == KeyboardMouseMappingSource.KEYBOARD,
                onClick = { source = KeyboardMouseMappingSource.KEYBOARD.ordinal },
                colors = premiumFilterChipColors(),
                label = { Text(stringResource(R.string.settings_keyboard_mouse_source_keyboard)) }
            )
            FilterChip(
                shape = neonChipShape(),
                selected = mappingSource == KeyboardMouseMappingSource.MOUSE,
                onClick = { source = KeyboardMouseMappingSource.MOUSE.ordinal },
                colors = premiumFilterChipColors(),
                label = { Text(stringResource(R.string.settings_keyboard_mouse_source_mouse)) }
            )
        }
        Text(
            text = stringResource(
                if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                    R.string.settings_mouse_mapping_desc
                } else {
                    R.string.settings_keyboard_mapping_desc
                }
            ),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 16.dp)
        )
        val customBindings = if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
            settings.mouseBindingsByPad[selectedPad].orEmpty()
        } else {
            settings.keyboardBindingsByPad[selectedPad].orEmpty()
        }
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
        for (action in actions) {
            val resolved = if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                KeyboardMouseBindings.resolveMouseBinding(action.id, customBindings)
            } else {
                KeyboardMouseBindings.resolveKeyboardBinding(action.id, customBindings)
            }
            val hasCustom = customBindings.containsKey(action.id)
            val value = when {
                resolved == null -> stringResource(R.string.settings_not_set)
                mappingSource == KeyboardMouseMappingSource.MOUSE ->
                    KeyboardMouseBindings.mouseButtonLabel(resolved)
                else -> KeyboardMouseBindings.keyboardKeyLabel(resolved)
            }
            KeyboardMouseBindingRow(
                title = gamepadActionLabel(action.id),
                value = value,
                autoLabel = if (!hasCustom && resolved != null) {
                    stringResource(R.string.settings_gamepad_mapping_auto_format)
                } else {
                    null
                },
                icon = if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                    Icons.Rounded.Mouse
                } else {
                    Icons.Rounded.Keyboard
                },
                onBindClick = {
                    if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                        KeyboardMouseManager.startMouseBindingCapture(selectedPad, action.id) { button ->
                            val updated = KeyboardMouseBindings.assign(customBindings, action.id, button)
                            onSettingsChange(
                                settings.copy(
                                    mouseBindingsByPad = settings.mouseBindingsByPad +
                                        (selectedPad to updated)
                                )
                            )
                        }
                    } else {
                        KeyboardMouseManager.startKeyboardBindingCapture(selectedPad, action.id) { keyCode ->
                            val updated = KeyboardMouseBindings.assign(customBindings, action.id, keyCode)
                            onSettingsChange(
                                settings.copy(
                                    keyboardBindingsByPad = settings.keyboardBindingsByPad +
                                        (selectedPad to updated)
                                )
                            )
                        }
                    }
                },
                onClearClick = if (hasCustom || resolved != null) {
                    {
                        val updated = KeyboardMouseBindings.clear(customBindings, action.id)
                        onSettingsChange(
                            if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                                settings.copy(
                                    mouseBindingsByPad = settings.mouseBindingsByPad +
                                        (selectedPad to updated)
                                )
                            } else {
                                settings.copy(
                                    keyboardBindingsByPad = settings.keyboardBindingsByPad +
                                        (selectedPad to updated)
                                )
                            }
                        )
                    }
                } else {
                    null
                }
            )
        }
        }
        KeyboardMouseBindingRow(
            title = stringResource(R.string.settings_keyboard_mouse_mapping_reset_title),
            value = stringResource(R.string.settings_keyboard_mouse_mapping_reset_desc),
            autoLabel = null,
            icon = Icons.Rounded.SettingsSuggest,
            valueColor = MaterialTheme.colorScheme.onSurfaceVariant,
            onBindClick = {
                onSettingsChange(
                    if (mappingSource == KeyboardMouseMappingSource.MOUSE) {
                        settings.copy(mouseBindingsByPad = settings.mouseBindingsByPad - selectedPad)
                    } else {
                        settings.copy(keyboardBindingsByPad = settings.keyboardBindingsByPad - selectedPad)
                    }
                )
            },
            onClearClick = null
        )
        TextButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.close))
        }
    }

    val activeCapture = capture
    if (activeCapture != null) {
        AppAlertDialog(
            onDismissRequest = { KeyboardMouseManager.cancelBindingCapture() },
            confirmButton = {
                TextButton(onClick = { KeyboardMouseManager.cancelBindingCapture() }) {
                    Text(stringResource(R.string.close))
                }
            },
            title = {
                Text(
                    stringResource(
                        if (activeCapture.source == KeyboardMouseManager.BindingSource.MOUSE) {
                            R.string.settings_mouse_mapping_listening_title
                        } else {
                            R.string.settings_keyboard_mapping_listening_title
                        }
                    )
                )
            },
            text = {
                Text(
                    stringResource(
                        if (activeCapture.source == KeyboardMouseManager.BindingSource.MOUSE) {
                            R.string.settings_mouse_mapping_listening_desc
                        } else {
                            R.string.settings_keyboard_mapping_listening_desc
                        }
                    )
                )
            }
        )
    }
}

@Composable
private fun KeyboardMouseBindingRow(
    title: String,
    value: String,
    autoLabel: String?,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    valueColor: Color? = null,
    onBindClick: () -> Unit,
    onClearClick: (() -> Unit)?
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        shape = neonChipShape(),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
        onClick = onBindClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(neonShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = value,
                        style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                        color = valueColor ?: MaterialTheme.colorScheme.primary
                    )
                    autoLabel?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            onClearClick?.let {
                TextButton(onClick = it) {
                    Text(stringResource(R.string.settings_gamepad_mapping_clear))
                }
            }
        }
    }
}

@Composable
private fun KeyboardMouseSliderItem(
    title: String,
    value: Int,
    range: ClosedFloatingPointRange<Float>,
    valueSuffix: String = "%",
    onValueChange: (Int) -> Unit
) {
    var sliderValue by remember(value) { mutableFloatStateOf(value.toFloat()) }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = "${sliderValue.toInt()}$valueSuffix",
                style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.primary
            )
        }
        Slider(
            value = sliderValue,
            onValueChange = { sliderValue = it },
            onValueChangeFinished = { onValueChange(sliderValue.toInt()) },
            valueRange = range,
            steps = 0,
            modifier = Modifier.padding(horizontal = 16.dp),
            colors = SliderDefaults.colors(
                thumbColor = MaterialTheme.colorScheme.primary,
                activeTrackColor = MaterialTheme.colorScheme.primary
            )
        )
    }
}

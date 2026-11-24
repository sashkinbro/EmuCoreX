package com.sbro.emucorex.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Numbers
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import com.sbro.emucorex.ui.theme.style.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameSerialFormat
import com.sbro.emucorex.ui.common.AppAlertDialog

/**
 * Branded dialog for editing the serial number a game is treated as.
 *
 * Accepts `XXXX-#####` and compact `XXXX#####` PS2 serials as well as arcade `NM#####`.
 * Saving an empty field clears the override and restores the detected serial.
 */
@Composable
internal fun GameSerialDialog(
    game: GameItem,
    onDismiss: () -> Unit,
    onConfirm: (String?) -> Unit
) {
    var input by remember(game.path) { mutableStateOf(game.serial.orEmpty()) }
    val normalized = remember(input) { GameSerialFormat.normalize(input) }
    val isBlank = input.isBlank()
    val isValid = isBlank || normalized != null
    val currentSerial = game.serial?.takeIf { it.isNotBlank() }
        ?: stringResource(R.string.content_serial_unknown)

    AppAlertDialog(
        onDismissRequest = onDismiss,
        icon = {
            Icon(
                imageVector = Icons.Rounded.Numbers,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
        },
        title = { Text(stringResource(R.string.game_serial_dialog_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    text = stringResource(R.string.game_serial_dialog_message),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = stringResource(R.string.game_serial_dialog_current, currentSerial),
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary
                )
                OutlinedTextField(
                    value = input,
                    onValueChange = { input = it },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.home_game_menu_change_serial)) },
                    placeholder = { Text(stringResource(R.string.game_serial_dialog_hint)) },
                    singleLine = true,
                    isError = !isValid,
                    supportingText = {
                        Text(
                            text = stringResource(
                                if (isValid) {
                                    R.string.game_serial_dialog_clear_hint
                                } else {
                                    R.string.game_serial_dialog_invalid
                                }
                            )
                        )
                    },
                    keyboardOptions = KeyboardOptions(
                        capitalization = KeyboardCapitalization.Characters,
                        keyboardType = KeyboardType.Ascii,
                        imeAction = ImeAction.Done
                    )
                )
            }
        },
        confirmButton = {
            TextButton(
                enabled = isValid,
                onClick = { onConfirm(if (isBlank) null else normalized) }
            ) {
                Text(stringResource(R.string.game_serial_dialog_save))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.game_serial_dialog_cancel))
            }
        }
    )
}

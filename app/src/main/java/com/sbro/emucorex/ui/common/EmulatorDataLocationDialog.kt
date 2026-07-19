package com.sbro.emucorex.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.PhoneAndroid
import androidx.compose.material.icons.rounded.SdStorage
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.EmulatorDataLocation

@Composable
fun EmulatorDataLocationDialog(
    selectedLocation: EmulatorDataLocation?,
    sdCardAvailable: Boolean,
    onSelect: (EmulatorDataLocation) -> Unit,
    onDismiss: () -> Unit
) {
    SettingsStyledDialog(
        title = stringResource(R.string.emulator_data_location_title),
        eyebrow = stringResource(R.string.settings_emulator_data_path),
        icon = Icons.Rounded.SdStorage,
        onDismissRequest = onDismiss,
    ) {
        Text(
            text = stringResource(R.string.emulator_data_location_description),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyLarge
        )
        EmulatorDataLocationOption(
            icon = Icons.Rounded.PhoneAndroid,
            title = stringResource(R.string.emulator_data_location_internal),
            description = stringResource(R.string.emulator_data_location_internal_description),
            selected = selectedLocation == EmulatorDataLocation.INTERNAL,
            enabled = true,
            onClick = { onSelect(EmulatorDataLocation.INTERNAL) }
        )
        EmulatorDataLocationOption(
            icon = Icons.Rounded.SdStorage,
            title = stringResource(R.string.emulator_data_location_sd_card),
            description = if (sdCardAvailable) {
                stringResource(R.string.emulator_data_location_sd_card_description)
            } else {
                stringResource(R.string.emulator_data_location_sd_card_unavailable)
            },
            selected = selectedLocation == EmulatorDataLocation.SD_CARD,
            enabled = sdCardAvailable,
            onClick = { onSelect(EmulatorDataLocation.SD_CARD) }
        )
        TextButton(
            onClick = onDismiss,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(R.string.cancel))
        }
    }
}

@Composable
private fun EmulatorDataLocationOption(
    icon: ImageVector,
    title: String,
    description: String,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val borderColor = if (selected) {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.outlineVariant
    }
    val contentAlpha = if (enabled) 1f else 0.52f

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .clickable(enabled = enabled, onClick = onClick),
        shape = RoundedCornerShape(18.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.62f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.56f)
        },
        border = BorderStroke(if (selected) 2.dp else 1.dp, borderColor)
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary.copy(alpha = contentAlpha),
                modifier = Modifier.size(28.dp)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = contentAlpha)
                )
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = contentAlpha)
                )
            }
            if (selected) {
                Spacer(modifier = Modifier.width(8.dp))
                Icon(
                    imageVector = Icons.Rounded.CheckCircle,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

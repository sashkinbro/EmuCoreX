package com.sbro.emucorex.ui.cheats

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import com.sbro.emucorex.R

/**
 * "Installed" status shown next to a catalog pack title. Matches the texture
 * manager: the status is a label, while the action button becomes "Reinstall"
 * so a pack can be refreshed without deleting it first.
 */
@Composable
internal fun InstalledStatusLabel(modifier: Modifier = Modifier) {
    Text(
        text = stringResource(R.string.content_installed),
        modifier = modifier,
        color = MaterialTheme.colorScheme.primary,
        style = MaterialTheme.typography.labelLarge,
        fontWeight = FontWeight.SemiBold
    )
}

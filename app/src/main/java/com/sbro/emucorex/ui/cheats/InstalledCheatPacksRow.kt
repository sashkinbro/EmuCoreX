package com.sbro.emucorex.ui.cheats

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.InstalledCheatPack
import com.sbro.emucorex.ui.common.fullBleedHorizontal
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.neon.neonShape

/**
 * Horizontal list of catalog packs installed for the selected game. The row is
 * full-bleed: [ScreenHorizontalPadding] is applied to its contentPadding rather
 * than to a wrapper, so chips can scroll under both screen edges while the first
 * and last chip line up with the rest of the screen.
 */
@Composable
internal fun InstalledCheatPacksRow(
    packs: List<InstalledCheatPack>,
    removingPackId: String?,
    titleFor: (InstalledCheatPack) -> String,
    onRemove: (InstalledCheatPack) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.padding(start = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.CloudDownload,
                contentDescription = null,
                modifier = Modifier.size(16.dp),
                tint = MaterialTheme.colorScheme.primary
            )
            Text(
                text = stringResource(R.string.cheat_manager_installed_packs),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Spacer(Modifier.height(8.dp))
        LazyRow(
            modifier = Modifier
                .fullBleedHorizontal(ScreenHorizontalPadding)
                .fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(packs, key = { pack -> pack.id }) { pack ->
                InstalledCheatPackChip(
                    pack = pack,
                    title = titleFor(pack),
                    removing = removingPackId == pack.id,
                    enabled = removingPackId == null,
                    onRemove = { onRemove(pack) }
                )
            }
        }
    }
}

@Composable
private fun InstalledCheatPackChip(
    pack: InstalledCheatPack,
    title: String,
    removing: Boolean,
    enabled: Boolean,
    onRemove: () -> Unit
) {
    Surface(
        shape = neonShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Row(
            modifier = Modifier
                .widthIn(max = 290.dp)
                .padding(start = 12.dp, end = 4.dp, top = 6.dp, bottom = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Column(modifier = Modifier.weight(1f, fill = false)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                if (pack.crc.isNotBlank() && pack.blockCount > 0) {
                    Text(
                        text = stringResource(R.string.cheat_manager_pack_meta, pack.crc, pack.blockCount),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            if (removing) {
                Box(modifier = Modifier.size(36.dp), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                }
            } else {
                val deleteLabel = stringResource(R.string.delete)
                IconButton(
                    onClick = onRemove,
                    enabled = enabled,
                    modifier = Modifier.size(36.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Close,
                        contentDescription = "$deleteLabel: $title",
                        modifier = Modifier.size(18.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}

package com.sbro.emucorex.ui.emulation

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.CheatBlock
import com.sbro.emucorex.ui.cheats.CheatCategory
import com.sbro.emucorex.ui.cheats.groupCheatBlocks
import com.sbro.emucorex.ui.theme.neon.neonShape

/**
 * In-game cheat list, grouped the same way as the cheat manager. Every group
 * has a master switch below its cheats; toggles persist through the shared
 * [com.sbro.emucorex.data.CheatRepository], so the manager stays in sync.
 */
@Composable
internal fun EmulationCheatsSection(
    blocks: List<CheatBlock>,
    onCheatToggle: (String, Boolean) -> Unit,
    onGroupToggle: (List<String>, Boolean) -> Unit,
    onSetAllEnabled: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    if (blocks.isEmpty()) {
        Surface(
            modifier = modifier.fillMaxWidth(),
            shape = neonShape(16.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
        ) {
            Column(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    text = stringResource(R.string.emulation_cheats_empty),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = stringResource(R.string.emulation_cheats_empty_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        return
    }

    val groups = remember(blocks) { groupCheatBlocks(blocks) }
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            TextButton(
                onClick = { onSetAllEnabled(true) },
                modifier = Modifier.weight(1f)
            ) {
                Text(stringResource(R.string.emulation_cheats_enable_all))
            }
            TextButton(
                onClick = { onSetAllEnabled(false) },
                modifier = Modifier.weight(1f)
            ) {
                Text(stringResource(R.string.emulation_cheats_disable_all))
            }
        }
        groups.forEach { (category, categoryBlocks) ->
            CheatGroupCard(
                category = category,
                blocks = categoryBlocks,
                onCheatToggle = onCheatToggle,
                onGroupToggle = onGroupToggle
            )
        }
    }
}

@Composable
private fun CheatGroupCard(
    category: CheatCategory,
    blocks: List<CheatBlock>,
    onCheatToggle: (String, Boolean) -> Unit,
    onGroupToggle: (List<String>, Boolean) -> Unit
) {
    val allEnabled = blocks.all { it.enabled }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text(
                text = stringResource(category.titleRes),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.ExtraBold),
                color = MaterialTheme.colorScheme.primary
            )
            blocks.forEach { block ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Text(
                        text = block.title,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Switch(
                        checked = block.enabled,
                        onCheckedChange = { enabled -> onCheatToggle(block.id, enabled) }
                    )
                }
            }
            HorizontalDivider(
                modifier = Modifier.padding(vertical = 4.dp),
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = stringResource(R.string.emulation_cheats_group_enable),
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Switch(
                    checked = allEnabled,
                    onCheckedChange = { enabled -> onGroupToggle(blocks.map { it.id }, enabled) }
                )
            }
        }
    }
}

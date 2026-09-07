package com.sbro.emucorex.ui.settings

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.WarningAmber
import androidx.compose.material3.AssistChip
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonChipShape
import com.sbro.emucorex.ui.theme.neon.neonShape

/**
 * Shared building blocks for the four per-core opcode family screens
 * (EE / IOP / VU0 / VU1). Each screen is its own file; these composables keep
 * the look and behavior identical across all of them.
 */

@Composable
fun OpcodeFamilyScreenScaffold(
    title: String,
    subtitle: String,
    onBackClick: () -> Unit,
    disabledFamilyCount: Int,
    disabledOpcodeCount: Int,
    onResetAll: () -> Unit,
    noteRes: Int = R.string.opcode_families_note,
    content: @Composable () -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                top = appScreenTopPadding(),
                end = ScreenHorizontalPadding,
                bottom = 40.dp
            ),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                ScreenTopBar(
                    title = title,
                    subtitle = subtitle,
                    onBackClick = onBackClick
                )
            }
            item { OpcodeFamilyInfoCard(noteRes) }
            item {
                OpcodeFamilyStatusRow(
                    disabledFamilyCount = disabledFamilyCount,
                    disabledOpcodeCount = disabledOpcodeCount,
                    onResetAll = onResetAll
                )
            }
            item { content() }
        }
    }
}

@Composable
private fun OpcodeFamilyInfoCard(noteRes: Int) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.62f),
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
        shape = neonShape(20.dp)
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            Icon(Icons.Rounded.Memory, null)
            Text(
                text = stringResource(noteRes),
                style = MaterialTheme.typography.bodyMedium
            )
        }
    }
}

@Composable
private fun OpcodeFamilyStatusRow(
    disabledFamilyCount: Int,
    disabledOpcodeCount: Int,
    onResetAll: () -> Unit
) {
    val hasAny = disabledFamilyCount > 0 || disabledOpcodeCount > 0
    Surface(
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        shape = neonShape(20.dp),
        tonalElevation = 2.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.opcode_families_disabled_count, disabledFamilyCount),
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold
                )
                if (disabledOpcodeCount > 0) {
                    Text(
                        text = stringResource(R.string.opcode_families_disabled_opcodes, disabledOpcodeCount),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            OutlinedButton(
                onClick = onResetAll,
                enabled = hasAny,
                shape = neonButtonShape()
            ) {
                Icon(Icons.Rounded.RestartAlt, null, Modifier.size(18.dp))
                Text(
                    text = stringResource(R.string.opcode_families_reset_all),
                    modifier = Modifier.padding(start = 6.dp)
                )
            }
        }
    }
}

/**
 * One opcode family row: technical name, localized description, a switch that
 * keeps the family on the recompiler (on) or sends it to the interpreter
 * (off), and an expandable per-opcode drill-down.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun OpcodeFamilyCard(
    family: OpcodeFamily,
    familyEnabled: Boolean,
    disabledOpcodes: Set<String>,
    onFamilyToggle: (Boolean) -> Unit,
    onOpcodeToggle: ((String) -> Unit)?,
) {
    var expanded by rememberSaveable(family.id) { mutableStateOf(false) }
    val hasDetails = family.opcodes.isNotEmpty() || family.mnemonics.isNotEmpty()
    val disabledInFamily = family.opcodes.count { it.id.toString() in disabledOpcodes }

    Surface(
        color = if (familyEnabled) MaterialTheme.colorScheme.surface
        else MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.45f),
        contentColor = if (familyEnabled) MaterialTheme.colorScheme.onSurface
        else MaterialTheme.colorScheme.onErrorContainer,
        shape = neonShape(22.dp),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(
                            text = family.id,
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                        if (!familyEnabled) {
                            Surface(
                                color = MaterialTheme.colorScheme.error,
                                contentColor = MaterialTheme.colorScheme.onError,
                                shape = neonChipShape()
                            ) {
                                Text(
                                    text = stringResource(R.string.opcode_families_interpreter_badge),
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                    style = MaterialTheme.typography.labelSmall
                                )
                            }
                        }
                    }
                    Text(
                        text = stringResource(family.descriptionRes),
                        style = MaterialTheme.typography.bodySmall,
                        color = if (familyEnabled) MaterialTheme.colorScheme.onSurfaceVariant
                        else MaterialTheme.colorScheme.onErrorContainer
                    )
                }
                Switch(checked = familyEnabled, onCheckedChange = onFamilyToggle)
            }

            if (hasDetails) {
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = if (disabledInFamily > 0) {
                                stringResource(R.string.opcode_families_disabled_opcodes, disabledInFamily)
                            } else {
                                stringResource(R.string.opcode_families_show_opcodes)
                            },
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.weight(1f)
                        )
                        IconButton(onClick = { expanded = !expanded }) {
                            Icon(
                                if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                                contentDescription = null
                            )
                        }
                    }
                    // The 8 dp gap lives inside the animated content (top padding)
                    // so it collapses smoothly together with the chips instead of
                    // snapping away when the AnimatedVisibility node is removed.
                    AnimatedVisibility(
                        visible = expanded,
                        enter = fadeIn(tween(160)) + expandVertically(
                            animationSpec = tween(240, easing = FastOutSlowInEasing),
                            expandFrom = Alignment.Top
                        ),
                        exit = fadeOut(tween(120)) + shrinkVertically(
                            animationSpec = tween(240, easing = FastOutSlowInEasing),
                            shrinkTowards = Alignment.Top
                        )
                    ) {
                        if (onOpcodeToggle != null && family.opcodes.isNotEmpty()) {
                            FlowRow(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(top = 8.dp),
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                val toggle = onOpcodeToggle
                                family.opcodes.forEach { opcode ->
                                    val opcodeDisabled = opcode.id.toString() in disabledOpcodes
                                    FilterChip(
                                        shape = neonChipShape(),
                                        selected = !opcodeDisabled,
                                        onClick = { toggle(opcode.id.toString()) },
                                        colors = FilterChipDefaults.filterChipColors(
                                            containerColor = if (opcodeDisabled)
                                                MaterialTheme.colorScheme.errorContainer
                                            else MaterialTheme.colorScheme.surface,
                                            labelColor = if (opcodeDisabled)
                                                MaterialTheme.colorScheme.onErrorContainer
                                            else MaterialTheme.colorScheme.onSurface
                                        ),
                                        label = {
                                            Text(
                                                text = opcode.mnemonic,
                                                style = MaterialTheme.typography.labelMedium,
                                                fontFamily = FontFamily.Monospace
                                            )
                                        }
                                    )
                                }
                            }
                        } else {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(top = 8.dp)
                            ) {
                                OpcodeFamilyInfoChips(family.mnemonics)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun OpcodeFamilyWarningCard(text: String) {
    Surface(
        color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.6f),
        contentColor = MaterialTheme.colorScheme.onErrorContainer,
        shape = neonShape(20.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.35f))
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            Icon(Icons.Rounded.WarningAmber, null)
            Text(text, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@Composable
fun OpcodeFamilyInfoChips(mnemonics: List<String>) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        mnemonics.forEach { mnemonic ->
            AssistChip(
                shape = neonChipShape(),
                onClick = {},
                label = {
                    Text(
                        text = mnemonic,
                        style = MaterialTheme.typography.labelMedium,
                        fontFamily = FontFamily.Monospace
                    )
                }
            )
        }
    }
}

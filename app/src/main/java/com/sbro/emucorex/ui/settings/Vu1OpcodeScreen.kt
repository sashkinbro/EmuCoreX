package com.sbro.emucorex.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R

/**
 * VU1 (Vector Unit 1) opcode family screen. Families disabled here execute on
 * the VU1 interpreter instead of the microVU1 JIT. Note that the VU1
 * interpreter does not run on the MTVU helper thread, so heavy exclusions may
 * reduce performance; VU instructions are dual-issue, so exclusions apply per
 * instruction family rather than per single opcode.
 */
@Composable
fun Vu1OpcodeScreen(
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()

    val disabledFamilies = uiState.disabledVu1OpcodeFamilies
    val families = OpcodeFamiliesModel.VU

    OpcodeFamilyScreenScaffold(
        title = stringResource(R.string.opcode_screen_vu1_title),
        subtitle = stringResource(R.string.opcode_screen_vu1_subtitle),
        onBackClick = onBackClick,
        disabledFamilyCount = disabledFamilies.size,
        disabledOpcodeCount = 0,
        onResetAll = {
            viewModel.setDisabledVu1OpcodeFamilies(emptySet())
        },
        noteRes = R.string.opcode_families_note_vu
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 2.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            if (uiState.enableMtvu) {
                OpcodeFamilyWarningCard(stringResource(R.string.opcode_families_mtvu_warning))
            }
            families.forEach { family ->
                OpcodeFamilyCard(
                    family = family,
                    familyEnabled = family.id !in disabledFamilies,
                    disabledOpcodes = emptySet(),
                    onFamilyToggle = { enabled ->
                        viewModel.setDisabledVu1OpcodeFamilies(
                            if (enabled) disabledFamilies - family.id
                            else disabledFamilies + family.id
                        )
                    },
                    onOpcodeToggle = null,
                )
            }
        }
    }
}

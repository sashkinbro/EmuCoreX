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
 * IOP (IO Processor / R3000A) opcode family screen. Families or individual
 * opcodes disabled here always execute on the IOP interpreter instead of the
 * IOP JIT.
 */
@Composable
fun IopOpcodeScreen(
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()

    val disabledFamilies = uiState.disabledIopOpcodeFamilies
    val disabledOpcodes = uiState.disabledIopOpcodes
    val families = OpcodeFamiliesModel.IOP

    OpcodeFamilyScreenScaffold(
        title = stringResource(R.string.opcode_screen_iop_title),
        subtitle = stringResource(R.string.opcode_screen_iop_subtitle),
        onBackClick = onBackClick,
        disabledFamilyCount = disabledFamilies.size,
        disabledOpcodeCount = disabledOpcodes.size,
        onResetAll = {
            viewModel.setDisabledIopOpcodeFamilies(emptySet())
            viewModel.setDisabledIopOpcodes(emptySet())
        }
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 2.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            families.forEach { family ->
                OpcodeFamilyCard(
                    family = family,
                    familyEnabled = family.id !in disabledFamilies,
                    disabledOpcodes = disabledOpcodes,
                    onFamilyToggle = { enabled ->
                        viewModel.setDisabledIopOpcodeFamilies(
                            if (enabled) disabledFamilies - family.id
                            else disabledFamilies + family.id
                        )
                    },
                    onOpcodeToggle = { opcodeId ->
                        viewModel.setDisabledIopOpcodes(
                            if (opcodeId in disabledOpcodes) disabledOpcodes - opcodeId
                            else disabledOpcodes + opcodeId
                        )
                    },
                )
            }
        }
    }
}

package com.sbro.emucorex.ui.profile

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Row
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import android.widget.Toast
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.SettingsBackupRepository
import kotlinx.coroutines.launch

@Composable
fun ProfileScreen(
    onBackClick: () -> Unit,
    onOpenGameDetails: (Long) -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val repository = remember(context) {
        SettingsBackupRepository(
            context,
            AppPreferences(context),
            PerGameSettingsRepository(context),
            CheatRepository(context)
        )
    }
    var includeSaveStates by remember { mutableStateOf(true) }

    val exportPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/zip")
    ) { uri ->
        if (uri != null) scope.launch {
            val success = repository.backup(uri, includeSaveStates)
            Toast.makeText(
                context,
                if (success) "Backup exported" else "Backup export failed",
                Toast.LENGTH_LONG
            ).show()
        }
    }
    val importPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) scope.launch {
            val success = repository.restore(uri)
            Toast.makeText(
                context,
                if (success) "Backup restored" else "Backup restore failed",
                Toast.LENGTH_LONG
            ).show()
        }
    }

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            modifier = Modifier.fillMaxSize().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Text("Local data", style = MaterialTheme.typography.headlineMedium)
            Text(
                "No account or cloud service is required. Export a ZIP to any location " +
                    "you choose, then copy it wherever you want to keep it.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(
                    checked = includeSaveStates,
                    onCheckedChange = { includeSaveStates = it }
                )
                Text("Include save states")
            }
            Button(
                onClick = { exportPicker.launch("EmuCoreX-backup.zip") },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Export backup")
            }
            OutlinedButton(
                onClick = { importPicker.launch(arrayOf("application/zip", "application/octet-stream")) },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Import backup")
            }
            Text(
                "The archive includes emulator settings, per-game settings, cheats, memory cards " +
                    "and (optionally) save states, plus supported UI customizations. Game images " +
                    "and BIOS files are not included.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(8.dp))
            OutlinedButton(onClick = onBackClick, modifier = Modifier.fillMaxWidth()) {
                Text("Back")
            }
        }
    }
}

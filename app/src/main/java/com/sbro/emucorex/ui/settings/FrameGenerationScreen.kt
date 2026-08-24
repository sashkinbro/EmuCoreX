package com.sbro.emucorex.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Launch
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.WarningAmber
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.FrameGenerationManager
import com.sbro.emucorex.core.FrameGenerationSettings
import com.sbro.emucorex.core.FrameGenerationSetup
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.launch

@Composable
@OptIn(ExperimentalLayoutApi::class)
fun FrameGenerationScreen(onBackClick: () -> Unit) {
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val manager = remember(context) { FrameGenerationManager(context) }
    val scope = rememberCoroutineScope()
    var setup by remember { mutableStateOf(manager.snapshot()) }
    var componentDownloadBusy by remember { mutableStateOf(false) }
    var dllImportBusy by remember { mutableStateOf(false) }
    val importFailed = stringResource(R.string.frame_generation_import_failed)
    val downloadFailed = stringResource(R.string.frame_generation_download_failed)
    val dllPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        dllImportBusy = true
        scope.launch {
            manager.importLosslessDll(uri)
                .onSuccess {
                    setup = manager.snapshot()
                    Toast.makeText(context, R.string.frame_generation_dll_ready, Toast.LENGTH_SHORT).show()
                }
                .onFailure { Toast.makeText(context, it.message ?: importFailed, Toast.LENGTH_LONG).show() }
            dllImportBusy = false
        }
    }

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
                    title = stringResource(R.string.frame_generation_title),
                    subtitle = stringResource(R.string.frame_generation_subtitle),
                    onBackClick = onBackClick
                )
            }
            item { DisclaimerCard() }
            item {
                SetupStepCard(
                    complete = setup.componentInstalled,
                    icon = Icons.Rounded.CloudDownload,
                    title = stringResource(R.string.frame_generation_component_title),
                    body = if (setup.componentInstalled) {
                        stringResource(R.string.frame_generation_component_installed, setup.componentVersion)
                    } else {
                        stringResource(R.string.frame_generation_component_desc)
                    },
                    action = if (setup.componentInstalled) null else stringResource(R.string.frame_generation_download),
                    busy = componentDownloadBusy,
                    actionsEnabled = !dllImportBusy,
                    onAction = {
                        componentDownloadBusy = true
                        scope.launch {
                            manager.installSupportComponent()
                                .onSuccess { setup = manager.snapshot() }
                                .onFailure { Toast.makeText(context, it.message ?: downloadFailed, Toast.LENGTH_LONG).show() }
                            componentDownloadBusy = false
                        }
                    }
                )
            }
            item {
                SetupStepCard(
                    complete = setup.dllInstalled,
                    icon = Icons.Rounded.Description,
                    title = stringResource(R.string.frame_generation_dll_title),
                    body = if (setup.dllInstalled) {
                        stringResource(R.string.frame_generation_dll_installed)
                    } else {
                        stringResource(R.string.frame_generation_dll_desc)
                    },
                    action = if (setup.dllInstalled) null else stringResource(R.string.frame_generation_choose_dll),
                    secondaryAction = stringResource(R.string.frame_generation_open_steam),
                    busy = dllImportBusy,
                    actionsEnabled = !componentDownloadBusy,
                    onAction = { dllPicker.launch(arrayOf("application/x-msdownload", "application/octet-stream", "*/*")) },
                    onSecondaryAction = { uriHandler.openUri(FrameGenerationManager.STEAM_URL) }
                )
            }
            item {
                FrameGenerationControls(
                    setup = setup,
                    onChange = { settings ->
                        manager.updateSettings(settings)
                        setup = manager.snapshot()
                    }
                )
            }
            item { RequirementsCard(ready = setup.isReady) }
        }
    }
}

@Composable
private fun DisclaimerCard() {
    Surface(
        color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.72f),
        contentColor = MaterialTheme.colorScheme.onErrorContainer,
        shape = RoundedCornerShape(24.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.35f))
    ) {
        Row(
            modifier = Modifier.padding(18.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.Top
        ) {
            Icon(Icons.Rounded.WarningAmber, null, tint = MaterialTheme.colorScheme.onErrorContainer)
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    stringResource(R.string.frame_generation_warning_title),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    stringResource(R.string.frame_generation_warning_body),
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        }
    }
}

@Composable
private fun SetupStepCard(
    complete: Boolean,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    body: String,
    action: String?,
    busy: Boolean,
    actionsEnabled: Boolean = true,
    onAction: () -> Unit,
    secondaryAction: String? = null,
    onSecondaryAction: () -> Unit = {}
) {
    Surface(
        color = MaterialTheme.colorScheme.surface,
        shape = RoundedCornerShape(24.dp),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Icon(if (complete) Icons.Rounded.CheckCircle else icon, null, tint = if (complete) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                Column(modifier = Modifier.weight(1f)) {
                    Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    Text(body, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            if (action != null || secondaryAction != null) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    action?.let {
                        Button(onClick = onAction, enabled = actionsEnabled && !busy) {
                            if (busy) CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                            else Icon(icon, null, Modifier.size(18.dp))
                            Text(it, modifier = Modifier.padding(start = 8.dp))
                        }
                    }
                    secondaryAction?.let {
                        OutlinedButton(onClick = onSecondaryAction, enabled = actionsEnabled && !busy) {
                            Icon(Icons.AutoMirrored.Rounded.Launch, null, Modifier.size(18.dp))
                            Text(it, modifier = Modifier.padding(start = 8.dp))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun FrameGenerationControls(
    setup: FrameGenerationSetup,
    onChange: (FrameGenerationSettings) -> Unit
) {
    val settings = setup.settings
    Surface(
        color = MaterialTheme.colorScheme.surface,
        shape = RoundedCornerShape(24.dp),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.Memory, null, tint = MaterialTheme.colorScheme.primary)
                Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                    Text(stringResource(R.string.frame_generation_enable), fontWeight = FontWeight.SemiBold)
                    Text(
                        if (setup.isReady) stringResource(R.string.frame_generation_ready)
                        else stringResource(R.string.frame_generation_locked),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = settings.enabled,
                    onCheckedChange = { onChange(settings.copy(enabled = it)) },
                    enabled = setup.isReady
                )
            }
            OptionChips(
                title = stringResource(R.string.frame_generation_multiplier),
                values = listOf(2, 3, 4),
                selected = settings.multiplier,
                enabled = setup.isReady,
                label = { "×$it" },
                onSelect = { onChange(settings.copy(multiplier = it)) }
            )
            OptionChips(
                title = stringResource(R.string.frame_generation_flow_scale),
                values = listOf(25, 50, 75, 100),
                selected = settings.flowScalePercent,
                enabled = setup.isReady,
                label = { if (it == 100) stringResource(R.string.settings_auto) else "$it%" },
                onSelect = { onChange(settings.copy(flowScalePercent = it)) }
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.frame_generation_performance), fontWeight = FontWeight.Medium)
                    Text(stringResource(R.string.frame_generation_performance_desc), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Switch(
                    checked = settings.performanceMode,
                    onCheckedChange = { onChange(settings.copy(performanceMode = it)) },
                    enabled = setup.isReady
                )
            }
            OptionChips(
                title = stringResource(R.string.frame_generation_target_rate),
                values = listOf(0, 60, 90, 120, 144),
                selected = settings.targetRefreshRate,
                enabled = setup.isReady,
                label = { if (it == 0) stringResource(R.string.frame_generation_fixed) else "$it Hz" },
                onSelect = { onChange(settings.copy(targetRefreshRate = it)) }
            )
            Text(
                stringResource(R.string.frame_generation_flow_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun OptionChips(
    title: String,
    values: List<Int>,
    selected: Int,
    enabled: Boolean,
    label: @Composable (Int) -> String,
    onSelect: (Int) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title, fontWeight = FontWeight.Medium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            values.forEach { value ->
                FilterChip(
                    selected = value == selected,
                    onClick = { onSelect(value) },
                    enabled = enabled,
                    label = { Text(label(value)) }
                )
            }
        }
    }
}

@Composable
private fun RequirementsCard(ready: Boolean) {
    Surface(
        color = if (ready) MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.48f)
        else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.62f),
        contentColor = if (ready) MaterialTheme.colorScheme.onPrimaryContainer
        else MaterialTheme.colorScheme.onSurfaceVariant,
        shape = RoundedCornerShape(20.dp)
    ) {
        Text(
            text = stringResource(if (ready) R.string.frame_generation_requirements_ready else R.string.frame_generation_requirements),
            modifier = Modifier.padding(16.dp),
            style = MaterialTheme.typography.bodyMedium
        )
    }
}

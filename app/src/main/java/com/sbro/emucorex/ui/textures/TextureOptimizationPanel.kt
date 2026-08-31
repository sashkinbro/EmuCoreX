package com.sbro.emucorex.ui.textures

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorex.R
import com.sbro.emucorex.data.*
import com.sbro.emucorex.core.NativeApp
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Locale

private val astcBlocks = listOf(4 to 4, 5 to 4, 5 to 5, 6 to 5, 6 to 6, 8 to 5, 8 to 6,
    8 to 8, 10 to 5, 10 to 6, 10 to 8, 10 to 10, 12 to 10, 12 to 12)

internal data class TextureQualityRequest(
    val allowOriginal: Boolean,
    val confirmLabel: Int,
    val onConfirm: (Int) -> Unit
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TextureOptimizationPanel(
    manager: TextureOptimizationManager,
    downloads: TextureDownloadManager,
    onFinished: () -> Unit,
    onBusy: (Boolean) -> Unit,
    showCard: Boolean = true,
    showProgressDialogs: Boolean = true,
    progressRequest: Int = 0,
    onShowProgress: (() -> Unit)? = null,
    qualityRequest: TextureQualityRequest? = null,
    onQualityDismiss: () -> Unit = {}
) {
    var requestedBlock by remember(qualityRequest) {
        mutableIntStateOf(manager.block.takeIf { qualityRequest?.allowOriginal != false || it >= 0 } ?: 4)
    }
    var state by remember { mutableStateOf(manager.state()) }
    var download by remember { mutableStateOf<TextureDownloadTask?>(null) }
    var showProgress by rememberSaveable { mutableStateOf(false) }
    var lastShown by rememberSaveable { mutableStateOf("") }
    var supportsAstc by remember { mutableStateOf<Boolean?>(null) }
    LaunchedEffect(qualityRequest) {
        if (qualityRequest != null) supportsAstc = withContext(Dispatchers.IO) {
            runCatching { NativeApp.hasNativeCore && NativeApp.supportsAstcTextures() }.getOrDefault(false)
        }
    }
    LaunchedEffect(progressRequest) { if (progressRequest > 0) showProgress = true }
    LaunchedEffect(manager) {
        while (true) {
            val next = manager.state()
            if (next.phase == "completed" && state.phase != "completed") onFinished()
            state = next
            val previous = download
            val tasks = downloads.tasks()
            download = tasks.firstOrNull { it.status == TextureDownloadStatus.OPTIMIZING }
                ?: tasks.firstOrNull { !state.active && it.key == previous?.key && !manager.isDownloadDismissed(it) }
            if (previous?.status != TextureDownloadStatus.COMPLETED && download?.status == TextureDownloadStatus.COMPLETED) onFinished()
            onBusy(state.active || state.phase == "paused" || tasks.any { it.status.isActive })
            val id = download?.key ?: state.id.takeIf { state.active }.orEmpty()
            if (id.isNotEmpty() && lastShown != id) { lastShown = id; showProgress = true }
            delay(500)
        }
    }
    val resultPhase = download?.let { downloadPhase(it.status) } ?: state.phase
    if (showCard && (state.id.isNotEmpty() || download != null) && resultPhase != "cancelled") {
        Surface(shape = RoundedCornerShape(22.dp), color = MaterialTheme.colorScheme.surface,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant), modifier = Modifier.fillMaxWidth()) {
            Row(Modifier.padding(horizontal = 12.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClick = { onShowProgress?.invoke() ?: run { showProgress = true } }, modifier = Modifier.weight(1f)) {
                    Text(phaseLabel(resultPhase))
                }
                if (resultPhase == "completed") IconButton(onClick = {
                    download?.let(manager::dismissDownload) ?: manager.dismissResult()
                }) { Icon(Icons.Default.Close, contentDescription = stringResource(R.string.close)) }
            }
        }
    }
    if (qualityRequest != null) TextureDialog(onDismiss = onQualityDismiss) {
        LazyColumn(Modifier.weight(1f, fill = false)) {
            item {
                Text(stringResource(R.string.texture_opt_title), style = MaterialTheme.typography.titleLarge,
                    modifier = Modifier.padding(20.dp))
            }
            item {
                Text(stringResource(R.string.texture_opt_quality_help), modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp),
                    style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (supportsAstc == false) item {
                Text(stringResource(R.string.texture_opt_unsupported), color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp))
            }
            items((if (qualityRequest.allowOriginal) listOf(-1) else emptyList()) + astcBlocks.indices.sortedBy { astcBlocks[it].first * astcBlocks[it].second }) { choice ->
                Surface(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 5.dp),
                    shape = RoundedCornerShape(16.dp),
                    color = if (requestedBlock == choice) MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.24f) else MaterialTheme.colorScheme.surfaceContainerLow,
                    border = BorderStroke(if (requestedBlock == choice) 2.dp else 1.dp,
                        if (requestedBlock == choice) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant)) {
                Row(Modifier.fillMaxWidth().clickable(enabled = choice < 0 || supportsAstc == true) {
                        requestedBlock = choice
                    }
                    .padding(horizontal = 14.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(selected = requestedBlock == choice, onClick = null, enabled = choice < 0 || supportsAstc == true)
                    Spacer(Modifier.width(10.dp))
                    Column {
                        Text(if (choice < 0) stringResource(R.string.texture_opt_none) else astcName(choice), style = MaterialTheme.typography.titleSmall)
                        Text(if (choice < 0) stringResource(R.string.texture_opt_original_quality) else {
                            val (x, y) = astcBlocks[choice]
                            stringResource(R.string.texture_opt_memory, String.format(Locale.getDefault(), "%.2f", 128.0 / x / y),
                                String.format(Locale.getDefault(), "%.1f", 400.0 / x / y))
                        }, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
                }
            }
            item {
                FlowRow(Modifier.fillMaxWidth().padding(12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End)) {
                    TextButton(onClick = onQualityDismiss) { Text(stringResource(R.string.close)) }
                    Button(
                        enabled = (qualityRequest.allowOriginal && requestedBlock < 0) || supportsAstc == true,
                        onClick = {
                            manager.block = requestedBlock
                            qualityRequest.onConfirm(requestedBlock)
                            onQualityDismiss()
                        }
                    ) { Text(stringResource(qualityRequest.confirmLabel)) }
                }
            }
        }
    }
    val dialogPhase = download?.let { downloadPhase(it.status) } ?: state.phase
    if (showProgress && showProgressDialogs && dialogPhase !in setOf("", "cancelled")) TextureDialog(onDismiss = { showProgress = false }) {
        val current = download?.let {
            TextureOptimizationState(it.key, downloadPhase(it.status), it.optimizedCount, it.optimizationTotal,
                it.optimizedCount - it.skippedCount, it.skippedCount, it.optimizationElapsedMs)
        } ?: state
        Column(Modifier.verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text(stringResource(R.string.texture_manager_title), style = MaterialTheme.typography.titleLarge)
            Surface(shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.surfaceContainerLow,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)) {
                Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(phaseLabel(current.phase), style = MaterialTheme.typography.titleMedium)
                    val fraction = if (current.total > 0) (current.completed.toFloat() / current.total).coerceIn(0f, 1f) else 0f
                    if (current.active && (current.total == 0 || current.phase != "optimizing")) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    else LinearProgressIndicator(progress = { fraction }, modifier = Modifier.fillMaxWidth())
                    Text(stringResource(R.string.texture_opt_progress, current.completed, current.total, (fraction * 100).toInt()))
                    val eta = if (current.phase == "optimizing") current.etaSeconds else 0
                    Text(stringResource(R.string.texture_opt_eta, if (eta > 0) formatDownloadDuration(eta) else "—"), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(stringResource(R.string.texture_opt_elapsed, if (current.elapsedMs > 0) formatDownloadDuration(current.elapsedMs / 1000) else "—"), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    if (current.skipped > 0) Text(stringResource(R.string.texture_opt_skipped, current.skipped), color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            if (current.phase == "failed") Text(stringResource(if (current.error.contains("ASTC is not supported")) R.string.texture_opt_unsupported else R.string.texture_opt_failed_safe), color = MaterialTheme.colorScheme.error)
            if (current.active || current.phase in setOf("paused", "failed")) {
                // Both variants participate in measurement, so pausing never changes the dialog height.
                Box {
                    listOf(R.string.texture_opt_background, R.string.texture_opt_paused_hint).forEachIndexed { index, text ->
                        val visible = (index == 0) == current.active
                        Text(stringResource(text), style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = if (visible) Modifier else Modifier.alpha(0f).clearAndSetSemantics {})
                    }
                }
            }
            BoxWithConstraints(Modifier.fillMaxWidth()) {
                val buttonWidth = if (maxWidth < 400.dp) maxWidth else (maxWidth - 16.dp) / 3
                FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
                    verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    val buttonModifier = Modifier.width(buttonWidth).heightIn(min = 48.dp)
                    if (current.active || current.phase in setOf("paused", "failed")) {
                        FilledTonalButton(modifier = buttonModifier, shape = RoundedCornerShape(12.dp), onClick = {
                            if (current.active) download?.let { downloads.pause(it.key) } ?: manager.pause()
                            else download?.let { downloads.resume(it.key) } ?: manager.resume()
                        }) {
                            Icon(if (current.active) Icons.Default.Pause else Icons.Default.PlayArrow, null, Modifier.size(20.dp))
                            Spacer(Modifier.width(8.dp))
                            Text(stringResource(if (current.active) R.string.emulation_pause else R.string.texture_opt_resume), maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                        OutlinedButton(modifier = buttonModifier, shape = RoundedCornerShape(12.dp),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.65f)),
                            colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                            onClick = { download?.let { downloads.cancel(it.key) } ?: manager.cancel(); showProgress = false }) {
                            Icon(Icons.Default.Stop, null, Modifier.size(20.dp)); Spacer(Modifier.width(8.dp))
                            Text(stringResource(R.string.cancel), maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    }
                    OutlinedButton(onClick = { showProgress = false }, modifier = buttonModifier, shape = RoundedCornerShape(12.dp)) {
                        Icon(Icons.Default.Close, null, Modifier.size(20.dp)); Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.close), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
        }
    }
}

private fun astcName(index: Int): String = astcBlocks[index].let { "ASTC ${it.first}×${it.second}" }

private fun downloadPhase(status: TextureDownloadStatus) = when (status) {
    TextureDownloadStatus.QUEUED -> "queued"
    TextureDownloadStatus.DOWNLOADING -> "downloading"
    TextureDownloadStatus.WAITING_NETWORK -> "waiting_network"
    TextureDownloadStatus.VERIFYING -> "verifying"
    TextureDownloadStatus.INSTALLING -> "preparing"
    TextureDownloadStatus.OPTIMIZING -> "optimizing"
    TextureDownloadStatus.PAUSED -> "paused"
    TextureDownloadStatus.FAILED -> "failed"
    TextureDownloadStatus.COMPLETED -> "completed"
    TextureDownloadStatus.CANCELLED -> "cancelled"
}

@Composable
private fun phaseLabel(phase: String): String = stringResource(when (phase) {
    "optimizing" -> R.string.texture_opt_running
    "waiting" -> R.string.texture_opt_waiting
    "queued" -> R.string.texture_download_status_queued
    "downloading" -> R.string.texture_download_status_downloading
    "waiting_network" -> R.string.texture_download_status_waiting_network
    "verifying" -> R.string.texture_download_status_verifying
    "completed" -> R.string.texture_download_status_completed
    "paused" -> R.string.texture_download_status_paused
    "cancelled" -> R.string.texture_download_status_cancelled
    "failed" -> R.string.texture_download_status_failed
    else -> R.string.texture_opt_preparing
})

@Composable
private fun TextureDialog(onDismiss: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        BoxWithConstraints(Modifier.fillMaxSize().padding(20.dp), contentAlignment = Alignment.Center) {
            Surface(modifier = Modifier.widthIn(max = 620.dp).fillMaxWidth().heightIn(max = maxHeight),
                shape = RoundedCornerShape(24.dp), color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)) {
                Column(content = content)
            }
        }
    }
}

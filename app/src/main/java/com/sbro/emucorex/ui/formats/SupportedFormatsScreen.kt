package com.sbro.emucorex.ui.formats

import android.net.Uri
import android.os.SystemClock
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Album
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Compress
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.documentfile.provider.DocumentFile
import com.sbro.emucorex.R
import com.sbro.emucorex.core.ImageConversionManager
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale
import com.sbro.emucorex.ui.theme.neon.neonShape

private enum class ConversionDirection {
    ISO_TO_CHD,
    CHD_TO_ISO,
    BIN_TO_ISO
}

private enum class ConverterMode {
    Idle,
    Converting,
    AwaitingSave,
    Saving,
    Success,
    Failed
}

private data class ConverterSessionState(
    val mode: ConverterMode = ConverterMode.Idle,
    val direction: ConversionDirection = ConversionDirection.ISO_TO_CHD,
    val progress: ImageConversionManager.ConversionProgress? = null,
    val startedAtMs: Long = 0L,
    val savedName: String? = null,
    val failureMessageRes: Int? = null,
    val warningMessageRes: Int? = null
) {
    val isRunning: Boolean get() = mode == ConverterMode.Converting || mode == ConverterMode.Saving
    val isVisible: Boolean get() = mode != ConverterMode.Idle
}

// These state and launcher initializers are read across Compose recompositions and callbacks;
// the IDE's local data-flow inspection cannot model that lifecycle correctly.
@Suppress("VARIABLE_WITH_REDUNDANT_INITIALIZER", "RedundantInitializer")
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SupportedFormatsScreen(
    onBackClick: (() -> Unit)? = null
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val scope = rememberCoroutineScope()
    val isoToChdAvailable = ImageConversionManager.isIsoToChdAvailable()
    val chdToIsoAvailable = ImageConversionManager.isChdToIsoAvailable()

    val sessionState = remember { MutableStateFlow(ConverterSessionState()) }
    val session by sessionState.collectAsState()
    var pendingOutputFile by remember { mutableStateOf<File?>(null) }
    var pendingOutputName by remember { mutableStateOf("game.chd") }
    var conversionJob by remember { mutableStateOf<Job?>(null) }
    var nowMs by remember { mutableStateOf(SystemClock.elapsedRealtime()) }
    var showBinPickerDialog by remember { mutableStateOf(false) }

    fun clearPendingOutput() {
        ImageConversionManager.cleanupTempFile(pendingOutputFile)
        pendingOutputFile = null
    }

    fun dismissSession() {
        if (session.isRunning) return
        clearPendingOutput()
        sessionState.value = ConverterSessionState()
    }

    DisposableEffect(Unit) {
        onDispose {
            conversionJob?.cancel()
            ImageConversionManager.cleanupTempFile(pendingOutputFile)
        }
    }

    LaunchedEffect(session.isRunning) {
        while (sessionState.value.isRunning) {
            nowMs = SystemClock.elapsedRealtime()
            delay(500)
        }
    }

    fun postProgress(progress: ImageConversionManager.ConversionProgress) {
        val current = sessionState.value
        if (!current.isRunning) return
        val previous = current.progress
        val isComplete = progress.totalBytes > 0L && progress.processedBytes >= progress.totalBytes
        if (!isComplete && previous != null &&
            previous.stage == progress.stage &&
            progress.processedBytes - previous.processedBytes < PROGRESS_UPDATE_STEP_BYTES
        ) {
            return
        }
        sessionState.value = current.copy(progress = progress)
    }

    fun startSave(destination: Uri?) {
        val outputFile = pendingOutputFile ?: return
        if (destination == null) return
        val current = sessionState.value
        if (current.mode != ConverterMode.AwaitingSave) return
        conversionJob = scope.launch {
            sessionState.value = current.copy(
                mode = ConverterMode.Saving,
                startedAtMs = SystemClock.elapsedRealtime(),
                progress = ImageConversionManager.ConversionProgress(
                    stage = ImageConversionManager.ConversionStage.SAVING,
                    processedBytes = 0L,
                    totalBytes = outputFile.length()
                )
            )
            val saved = withContext(Dispatchers.IO) {
                ImageConversionManager.saveConvertedFile(
                    context = context,
                    sourceFile = outputFile,
                    destinationUri = destination,
                    onProgress = ::postProgress
                )
            }
            clearPendingOutput()
            sessionState.value = if (saved) {
                sessionState.value.copy(
                    mode = ConverterMode.Success,
                    progress = null,
                    savedName = pendingOutputName
                )
            } else {
                sessionState.value.copy(
                    mode = ConverterMode.Failed,
                    progress = null,
                    failureMessageRes = R.string.formats_converter_save_failed
                )
            }
        }
    }

    val saveLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { destination: Uri? ->
        startSave(destination)
    }

    fun startConversion(
        direction: ConversionDirection,
        sourceUri: Uri,
        cueUri: Uri? = null
    ) {
        if (conversionJob?.isActive == true) return
        clearPendingOutput()
        conversionJob = scope.launch {
            sessionState.value = ConverterSessionState(
                mode = ConverterMode.Converting,
                direction = direction,
                startedAtMs = SystemClock.elapsedRealtime()
            )
            val result = withContext(Dispatchers.IO) {
                when (direction) {
                    ConversionDirection.ISO_TO_CHD -> ImageConversionManager.convertIsoToChd(
                        context = context,
                        sourceUri = sourceUri,
                        onProgress = ::postProgress
                    )
                    ConversionDirection.CHD_TO_ISO -> ImageConversionManager.convertChdToIso(
                        context = context,
                        sourceUri = sourceUri,
                        onProgress = ::postProgress
                    )
                    ConversionDirection.BIN_TO_ISO -> ImageConversionManager.convertBinToIso(
                        context = context,
                        sourceUri = sourceUri,
                        cueUri = cueUri,
                        onProgress = ::postProgress
                    )
                }
            }
            val current = sessionState.value
            if (!current.isRunning) return@launch
            if (result.success && result.outputFile != null) {
                pendingOutputFile = result.outputFile
                pendingOutputName = result.suggestedFileName ?: pendingOutputName
                sessionState.value = current.copy(
                    mode = ConverterMode.AwaitingSave,
                    progress = null,
                    warningMessageRes = conversionWarningMessageRes(result.warningMessage)
                )
                saveLauncher.launch(pendingOutputName)
            } else {
                sessionState.value = current.copy(
                    mode = ConverterMode.Failed,
                    progress = null,
                    failureMessageRes = conversionFailureMessageRes(result.message)
                )
            }
        }
    }

    val pickIsoLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        startConversion(ConversionDirection.ISO_TO_CHD, uri)
    }
    val pickChdLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        startConversion(ConversionDirection.CHD_TO_ISO, uri)
    }
    fun applyBinSelection(selection: DiscSelectionResult) {
        when (selection) {
            is DiscSelectionResult.Selected -> startConversion(
                direction = ConversionDirection.BIN_TO_ISO,
                sourceUri = selection.binUri,
                cueUri = selection.cueUri
            )
            DiscSelectionResult.MissingBin -> {
                sessionState.value = ConverterSessionState(
                    mode = ConverterMode.Failed,
                    direction = ConversionDirection.BIN_TO_ISO,
                    failureMessageRes = R.string.formats_converter_invalid_bin_body
                )
            }
            DiscSelectionResult.MultipleDiscs -> {
                sessionState.value = ConverterSessionState(
                    mode = ConverterMode.Failed,
                    direction = ConversionDirection.BIN_TO_ISO,
                    failureMessageRes = R.string.formats_converter_multiple_discs_body
                )
            }
        }
    }

    fun resolveBinUris(uris: List<Uri>) {
        if (uris.isEmpty()) {
            applyBinSelection(DiscSelectionResult.MissingBin)
            return
        }
        scope.launch {
            val selection = withContext(Dispatchers.IO) {
                resolveDiscSelection(context, uris)
            }
            applyBinSelection(selection)
        }
    }

    val pickBinFilesLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenMultipleDocuments()
    ) { uris: List<Uri> ->
        resolveBinUris(uris)
    }
    val pickBinFolderLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { treeUri: Uri? ->
        if (treeUri == null) return@rememberLauncherForActivityResult
        scope.launch {
            val selection = withContext(Dispatchers.IO) {
                val uris = listDiscUrisFromTree(context, treeUri)
                resolveDiscSelection(context, uris)
            }
            applyBinSelection(selection)
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(bottom = 24.dp + bottomInset)
        ) {
            FormatsTopBar(topInset = topInset, onBackClick = onBackClick)

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                HeroFormatsCard()

                ConverterActionsCard(
                    isoToChdAvailable = isoToChdAvailable,
                    chdToIsoAvailable = chdToIsoAvailable,
                    busy = session.isRunning,
                    onConvertIsoToChd = {
                        if (!session.isRunning) pickIsoLauncher.launch(ImageConversionManager.isoMimeTypes)
                    },
                    onConvertChdToIso = {
                        if (!session.isRunning && chdToIsoAvailable) {
                            pickChdLauncher.launch(ImageConversionManager.chdMimeTypes)
                        }
                    },
                    onConvertBinToIso = {
                        if (!session.isRunning) showBinPickerDialog = true
                    }
                )

                FormatGroupCard(
                    icon = Icons.Rounded.Description,
                    title = stringResource(R.string.formats_detected_title),
                    body = stringResource(R.string.formats_detected_body),
                    formats = ImageConversionManager.libraryFormats
                )

                FormatGroupCard(
                    icon = Icons.Rounded.Memory,
                    title = stringResource(R.string.formats_recommended_title),
                    body = stringResource(R.string.formats_recommended_body),
                    formats = ImageConversionManager.recommendedFormats
                )
            }
        }

        if (showBinPickerDialog) {
            AlertDialog(
                onDismissRequest = { showBinPickerDialog = false },
                title = { Text(stringResource(R.string.formats_converter_action_bin_to_iso)) },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        Text(stringResource(R.string.formats_converter_bin_picker_desc))
                        Button(
                            modifier = Modifier.fillMaxWidth(),
                            shape = neonShape(14.dp),
                            onClick = {
                                showBinPickerDialog = false
                                pickBinFilesLauncher.launch(ImageConversionManager.binMimeTypes)
                            }
                        ) {
                            Text(stringResource(R.string.formats_converter_bin_select_files))
                        }
                        OutlinedButton(
                            modifier = Modifier.fillMaxWidth(),
                            shape = neonShape(14.dp),
                            onClick = {
                                showBinPickerDialog = false
                                pickBinFolderLauncher.launch(null)
                            }
                        ) {
                            Text(stringResource(R.string.formats_converter_bin_select_folder))
                        }
                        TextButton(
                            modifier = Modifier.fillMaxWidth(),
                            onClick = { showBinPickerDialog = false }
                        ) {
                            Text(stringResource(R.string.close))
                        }
                    }
                },
                confirmButton = {}
            )
        }

        if (session.isVisible) {
            ConverterProgressDialog(
                session = session,
                nowMs = nowMs,
                onCancel = {
                    conversionJob?.cancel()
                    conversionJob = null
                    clearPendingOutput()
                    sessionState.value = ConverterSessionState()
                },
                onSave = { saveLauncher.launch(pendingOutputName) },
                onDismiss = ::dismissSession
            )
        }
    }
}

private const val PROGRESS_UPDATE_STEP_BYTES = 8L * 1024L * 1024L

private sealed interface DiscSelectionResult {
    data class Selected(val binUri: Uri, val cueUri: Uri?) : DiscSelectionResult
    data object MissingBin : DiscSelectionResult
    data object MultipleDiscs : DiscSelectionResult
}

private fun listDiscUrisFromTree(context: android.content.Context, treeUri: Uri): List<Uri> {
    return runCatching {
        DocumentFile.fromTreeUri(context, treeUri)
            ?.listFiles()
            .orEmpty()
            .filter(DocumentFile::isFile)
            .map(DocumentFile::getUri)
    }.getOrDefault(emptyList())
}

private fun resolveDiscSelection(context: android.content.Context, uris: List<Uri>): DiscSelectionResult {
    val named = uris.mapNotNull { uri ->
        displayNameFor(context, uri)?.let { name -> uri to name }
    }
    val bins = named.filter { (_, name) -> name.endsWith(".bin", ignoreCase = true) }
    val cueUri = named.firstOrNull { (_, name) -> name.endsWith(".cue", ignoreCase = true) }?.first
    if (bins.isEmpty()) return DiscSelectionResult.MissingBin
    if (bins.size == 1) return DiscSelectionResult.Selected(bins.first().first, cueUri)

    if (cueUri != null) {
        val sheet = ImageConversionManager.readCueSheet(context, cueUri)
        val referenced = sheet?.blocks
            ?.firstNotNullOfOrNull { it.fileName }
            ?.substringAfterLast('/')
            ?.substringAfterLast('\\')
            ?.trim()
        val matched = referenced?.let { name ->
            bins.firstOrNull { (_, binName) -> binName.equals(name, ignoreCase = true) }
        }
        if (matched != null) return DiscSelectionResult.Selected(matched.first, cueUri)
    }
    return DiscSelectionResult.MultipleDiscs
}

private fun displayNameFor(context: android.content.Context, uri: Uri): String? =
    runCatching { DocumentFile.fromSingleUri(context, uri)?.name }.getOrNull()
        ?: uri.lastPathSegment

private fun conversionFailureMessageRes(message: String?): Int = when (message) {
    "not_iso" -> R.string.formats_converter_invalid_iso_body
    "not_chd" -> R.string.formats_converter_invalid_chd_body
    "not_bin", "invalid_bin", "unsupported_sector_mode" -> R.string.formats_converter_invalid_bin_body
    "chd_unreadable", "chd_requires_parent" -> R.string.formats_converter_failed_chd_body
    "native_unavailable" -> R.string.formats_converter_unavailable
    else -> R.string.formats_converter_failed_body
}

private fun conversionWarningMessageRes(message: String?): Int? = when (message) {
    "bin_audio_skipped" -> R.string.formats_converter_bin_audio_skipped
    else -> null
}

@Composable
private fun directionLabel(direction: ConversionDirection): String = stringResource(
    when (direction) {
        ConversionDirection.ISO_TO_CHD -> R.string.formats_converter_action_iso_to_chd
        ConversionDirection.CHD_TO_ISO -> R.string.formats_converter_action_chd_to_iso
        ConversionDirection.BIN_TO_ISO -> R.string.formats_converter_action_bin_to_iso
    }
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ConverterProgressDialog(
    session: ConverterSessionState,
    nowMs: Long,
    onCancel: () -> Unit,
    onSave: () -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(
        onDismissRequest = { if (!session.isRunning) onDismiss() },
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(20.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .widthIn(max = 560.dp)
                    .fillMaxWidth()
                    .heightIn(max = maxHeight),
                shape = neonShape(24.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 6.dp,
                shadowElevation = 16.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
            ) {
                Column(
                    modifier = Modifier
                        .verticalScroll(rememberScrollState())
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Text(
                        text = directionLabel(session.direction),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    Surface(
                        shape = neonShape(16.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                text = sessionStatusLabel(session),
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )

                            val progress = session.progress
                            val fraction = progress?.fraction ?: 0f
                            if (session.isRunning) {
                                if (progress == null || progress.totalBytes <= 0L) {
                                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                                } else {
                                    LinearProgressIndicator(
                                        progress = { fraction },
                                        modifier = Modifier.fillMaxWidth()
                                    )
                                    val percent = (fraction * 100f).toInt()
                                    Text(
                                        text = "$percent%  •  ${formatBytes(progress.processedBytes)} / " +
                                            formatBytes(progress.totalBytes),
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }

                            if (session.isRunning) {
                                session.startedAtMs.takeIf { it > 0L }?.let { startedAt ->
                                    val elapsedSeconds = ((nowMs - startedAt).coerceAtLeast(0L)) / 1000
                                    Text(
                                        text = stringResource(
                                            R.string.texture_opt_elapsed,
                                            formatDuration(elapsedSeconds)
                                        ),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    val etaSeconds = estimateRemainingSeconds(
                                        progress = progress,
                                        elapsedSeconds = elapsedSeconds
                                    )
                                    Text(
                                        text = stringResource(
                                            R.string.texture_opt_eta,
                                            if (etaSeconds > 0L) formatDuration(etaSeconds) else "—"
                                        ),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }

                            session.savedName?.let { name ->
                                Text(
                                    text = name,
                                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                                    color = MaterialTheme.colorScheme.primary,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }

                            session.failureMessageRes?.let { messageRes ->
                                Text(
                                    text = stringResource(messageRes),
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.error
                                )
                            }

                            session.warningMessageRes?.let { messageRes ->
                                Text(
                                    text = stringResource(messageRes),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.tertiary
                                )
                            }
                        }
                    }

                    BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
                        val buttonWidth = if (maxWidth < 420.dp) maxWidth else (maxWidth - 12.dp) / 2
                        FlowRow(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            val buttonModifier = Modifier
                                .width(buttonWidth)
                                .heightIn(min = 48.dp)
                            when (session.mode) {
                                ConverterMode.Converting,
                                ConverterMode.Saving -> {
                                    OutlinedButton(
                                        modifier = buttonModifier,
                                        shape = neonShape(12.dp),
                                        border = BorderStroke(
                                            1.dp,
                                            MaterialTheme.colorScheme.error.copy(alpha = 0.65f)
                                        ),
                                        colors = ButtonDefaults.outlinedButtonColors(
                                            contentColor = MaterialTheme.colorScheme.error
                                        ),
                                        onClick = onCancel
                                    ) {
                                        Icon(Icons.Rounded.Close, contentDescription = null, modifier = Modifier.size(18.dp))
                                        Spacer(modifier = Modifier.width(8.dp))
                                        Text(stringResource(R.string.cancel), maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    }
                                }
                                ConverterMode.AwaitingSave -> {
                                    Button(
                                        modifier = buttonModifier,
                                        shape = neonShape(12.dp),
                                        onClick = onSave
                                    ) {
                                        Text(stringResource(R.string.formats_converter_save_action), maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    }
                                    OutlinedButton(
                                        modifier = buttonModifier,
                                        shape = neonShape(12.dp),
                                        onClick = onDismiss
                                    ) {
                                        Text(stringResource(R.string.close), maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    }
                                }
                                ConverterMode.Success,
                                ConverterMode.Failed -> {
                                    OutlinedButton(
                                        modifier = buttonModifier,
                                        shape = neonShape(12.dp),
                                        onClick = onDismiss
                                    ) {
                                        Text(stringResource(R.string.close), maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    }
                                }
                                ConverterMode.Idle -> Unit
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun sessionStatusLabel(session: ConverterSessionState): String = when (session.mode) {
    ConverterMode.Converting -> when (session.progress?.stage) {
        ImageConversionManager.ConversionStage.CONVERTING ->
            stringResource(R.string.formats_converter_status_converting)
        else -> stringResource(R.string.formats_converter_status_preparing)
    }
    ConverterMode.Saving -> stringResource(R.string.formats_converter_status_saving)
    ConverterMode.AwaitingSave -> stringResource(R.string.formats_converter_status_ready)
    ConverterMode.Success -> stringResource(R.string.formats_converter_status_done)
    ConverterMode.Failed -> stringResource(R.string.formats_converter_status_failed)
    ConverterMode.Idle -> stringResource(R.string.formats_converter_status_idle)
}

private fun estimateRemainingSeconds(
    progress: ImageConversionManager.ConversionProgress?,
    elapsedSeconds: Long
): Long {
    if (progress == null) return 0L
    val processed = progress.processedBytes
    val total = progress.totalBytes
    if (processed <= 0L || total <= processed || elapsedSeconds <= 0L) return 0L
    val secondsPerByte = elapsedSeconds.toDouble() / processed.toDouble()
    return ((total - processed) * secondsPerByte).toLong().coerceAtLeast(0L)
}

private fun formatBytes(bytes: Long): String {
    if (bytes < 1024L) return "$bytes B"
    val units = arrayOf("KB", "MB", "GB", "TB")
    var value = bytes.toDouble()
    var unitIndex = -1
    while (value >= 1024.0 && unitIndex < units.lastIndex) {
        value /= 1024.0
        unitIndex++
    }
    return String.format(Locale.US, if (value >= 100.0) "%.0f %s" else "%.1f %s", value, units[unitIndex])
}

private fun formatDuration(totalSeconds: Long): String {
    val seconds = totalSeconds.coerceAtLeast(0L)
    val hours = seconds / 3600L
    val minutes = (seconds % 3600L) / 60L
    val remainingSeconds = seconds % 60L
    return if (hours > 0L) {
        "%d:%02d:%02d".format(hours, minutes, remainingSeconds)
    } else {
        "%d:%02d".format(minutes, remainingSeconds)
    }
}

@Composable
private fun FormatsTopBar(
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: (() -> Unit)?
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset,
                bottom = 14.dp
            ),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        shadowElevation = 6.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (onBackClick != null) {
                ScreenTopBar(
                    title = stringResource(R.string.formats_title),
                    onBackClick = onBackClick,
                    embedded = true
                )
            } else {
                Text(
                    text = stringResource(R.string.formats_title),
                    modifier = Modifier.padding(horizontal = 10.dp),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
            }
        }
    }
}

@Composable
private fun HeroFormatsCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(30.dp),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                text = stringResource(R.string.formats_overview_title),
                style = MaterialTheme.typography.headlineSmall,
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = stringResource(R.string.formats_overview_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ConverterActionsCard(
    isoToChdAvailable: Boolean,
    chdToIsoAvailable: Boolean,
    busy: Boolean,
    onConvertIsoToChd: () -> Unit,
    onConvertChdToIso: () -> Unit,
    onConvertBinToIso: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
        tonalElevation = 4.dp,
        shadowElevation = 8.dp
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 18.dp, vertical = 20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Surface(
                    shape = neonShape(14.dp),
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
                    tonalElevation = 2.dp
                ) {
                    Box(modifier = Modifier.padding(10.dp)) {
                        Icon(
                            imageVector = Icons.Rounded.FolderZip,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }
                Column(modifier = Modifier.padding(start = 12.dp)) {
                    Text(
                        text = stringResource(R.string.formats_converter_title),
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(R.string.formats_converter_body),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = 48.dp),
                    shape = neonShape(16.dp),
                    enabled = isoToChdAvailable && !busy,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                    ),
                    onClick = onConvertIsoToChd
                ) {
                    Icon(Icons.Rounded.Compress, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = stringResource(R.string.formats_converter_action_iso_to_chd),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                Button(
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = 48.dp),
                    shape = neonShape(16.dp),
                    enabled = chdToIsoAvailable && !busy,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                    ),
                    onClick = onConvertChdToIso
                ) {
                    Icon(Icons.Rounded.Restore, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = stringResource(R.string.formats_converter_action_chd_to_iso),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }

            Button(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 48.dp),
                shape = neonShape(16.dp),
                enabled = !busy,
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer,
                    contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                ),
                onClick = onConvertBinToIso
            ) {
                Icon(Icons.Rounded.Album, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = stringResource(R.string.formats_converter_action_bin_to_iso),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }

            Text(
                text = stringResource(R.string.formats_converter_bin_to_chd_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            if (!chdToIsoAvailable) {
                Text(
                    text = stringResource(R.string.formats_converter_unavailable),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FormatGroupCard(
    icon: ImageVector,
    title: String,
    body: String,
    formats: List<String>
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
        tonalElevation = 4.dp,
        shadowElevation = 8.dp
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 18.dp, vertical = 20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Surface(
                    shape = neonShape(14.dp),
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
                    tonalElevation = 2.dp
                ) {
                    Box(modifier = Modifier.padding(10.dp)) {
                        Icon(
                            imageVector = icon,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }
                Column(modifier = Modifier.padding(start = 12.dp)) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = body,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                formats.forEach { format ->
                    Surface(
                        shape = neonShape(16.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.52f),
                        tonalElevation = 1.dp
                    ) {
                        Text(
                            text = format,
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 9.dp),
                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                }
            }
        }
    }
}


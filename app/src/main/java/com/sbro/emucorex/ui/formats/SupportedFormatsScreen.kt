package com.sbro.emucorex.ui.formats

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Compress
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
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
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.ImageConversionManager
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.launch
import java.io.File

private enum class ConversionUiState {
    Idle,
    Preparing,
    ReadyToSave,
    Saving,
    Success,
    Error
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SupportedFormatsScreen(
    onBackClick: (() -> Unit)? = null
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val scope = rememberCoroutineScope()
    val isConverterAvailable = ImageConversionManager.isIsoToChdAvailable()

    var conversionState by remember { mutableStateOf(ConversionUiState.Idle) }
    var pendingOutputFile by remember { mutableStateOf<File?>(null) }
    var pendingOutputName by remember { mutableStateOf("game.chd") }
    var dialogMessage by remember { mutableStateOf<Int?>(null) }

    DisposableEffect(Unit) {
        onDispose {
            ImageConversionManager.cleanupTempFile(pendingOutputFile)
        }
    }

    val saveChdLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { destination: Uri? ->
        if (destination == null) {
            conversionState = if (pendingOutputFile != null) ConversionUiState.ReadyToSave else ConversionUiState.Idle
            return@rememberLauncherForActivityResult
        }

        val outputFile = pendingOutputFile ?: return@rememberLauncherForActivityResult
        scope.launch {
            conversionState = ConversionUiState.Saving
            val saved = ImageConversionManager.saveConvertedFile(context, outputFile, destination)
            ImageConversionManager.cleanupTempFile(outputFile)
            pendingOutputFile = null
            conversionState = if (saved) ConversionUiState.Success else ConversionUiState.Error
            dialogMessage = if (saved) R.string.formats_converter_save_success else R.string.formats_converter_save_failed
        }
    }

    val pickIsoLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult

        scope.launch {
            conversionState = ConversionUiState.Preparing
            val result = ImageConversionManager.convertIsoToChd(context, uri)
            if (result.success) {
                pendingOutputFile = result.outputFile
                pendingOutputName = result.suggestedFileName ?: "game.chd"
                conversionState = ConversionUiState.ReadyToSave
                saveChdLauncher.launch(pendingOutputName)
            } else {
                conversionState = ConversionUiState.Error
                dialogMessage = when (result.message) {
                    "not_iso" -> R.string.formats_converter_invalid_iso_body
                    "native_tools_unavailable" -> R.string.formats_converter_unavailable
                    else -> R.string.formats_converter_failed_body
                }
            }
        }
    }

    val launchConversion = rememberDebouncedClick {
        if (!isConverterAvailable ||
            conversionState == ConversionUiState.Preparing ||
            conversionState == ConversionUiState.Saving
        ) {
            return@rememberDebouncedClick
        }
        if (pendingOutputFile != null) {
            saveChdLauncher.launch(pendingOutputName)
        } else {
            pickIsoLauncher.launch(ImageConversionManager.isoMimeTypes)
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
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
                HeroFormatsCard(
                    status = conversionState,
                    isConverterAvailable = isConverterAvailable,
                    actionLabel = if (pendingOutputFile != null) {
                        stringResource(R.string.formats_converter_save_action)
                    } else {
                        stringResource(R.string.formats_converter_action)
                    },
                    onActionClick = launchConversion
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

                FormatGroupCard(
                    icon = Icons.Rounded.FolderZip,
                    title = stringResource(R.string.formats_converter_title),
                    body = stringResource(R.string.formats_converter_body),
                    formats = listOf("ISO", "CHD")
                )
            }
        }

        dialogMessage?.let { messageRes ->
            AlertDialog(
                onDismissRequest = { dialogMessage = null },
                confirmButton = {
                    Surface(
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.primary,
                        onClick = { dialogMessage = null }
                    ) {
                        Text(
                            text = stringResource(R.string.ok),
                            modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                            color = MaterialTheme.colorScheme.onPrimary,
                            style = MaterialTheme.typography.labelLarge
                        )
                    }
                },
                title = {
                    Text(
                        text = stringResource(
                            if (conversionState == ConversionUiState.Success) {
                                R.string.formats_converter_success_title
                            } else {
                                R.string.formats_converter_failed_title
                            }
                        )
                    )
                },
                text = { Text(text = stringResource(messageRes)) }
            )
        }
    }
}

@Composable
private fun FormatsTopBar(
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: (() -> Unit)?
) {
    val handleBackClick = onBackClick?.let { rememberDebouncedClick(onClick = it) }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset + 8.dp,
                bottom = 14.dp
            ),
        shape = RoundedCornerShape(28.dp),
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
            if (handleBackClick != null) {
                NavigationBackButton(onClick = handleBackClick)
            }

            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = if (onBackClick != null) 2.dp else 10.dp, end = 10.dp)
            ) {
                Text(
                    text = stringResource(R.string.formats_title),
                    style = MaterialTheme.typography.headlineMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
            }
        }
    }
}

@Composable
private fun HeroFormatsCard(
    status: ConversionUiState,
    isConverterAvailable: Boolean,
    actionLabel: String,
    onActionClick: () -> Unit
) {
    val isBusy = status == ConversionUiState.Preparing || status == ConversionUiState.Saving

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(30.dp),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
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
            StatusPill(status = status, isConverterAvailable = isConverterAvailable)
            Surface(
                shape = RoundedCornerShape(18.dp),
                color = if (isConverterAvailable) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.surfaceVariant
                },
                onClick = onActionClick,
                enabled = isConverterAvailable && !isBusy
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 18.dp, vertical = 16.dp),
                    horizontalArrangement = Arrangement.Center,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (isBusy) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(18.dp),
                            strokeWidth = 2.2.dp,
                            color = if (isConverterAvailable) {
                                MaterialTheme.colorScheme.onPrimary
                            } else {
                                MaterialTheme.colorScheme.onSurfaceVariant
                            }
                        )
                    } else {
                        Icon(
                            imageVector = Icons.Rounded.Compress,
                            contentDescription = null,
                            tint = if (isConverterAvailable) {
                                MaterialTheme.colorScheme.onPrimary
                            } else {
                                MaterialTheme.colorScheme.onSurfaceVariant
                            },
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    Text(
                        text = actionLabel,
                        style = MaterialTheme.typography.titleMedium,
                        color = if (isConverterAvailable) {
                            MaterialTheme.colorScheme.onPrimary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                        modifier = Modifier.padding(start = 10.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun StatusPill(
    status: ConversionUiState,
    isConverterAvailable: Boolean
) {
    val label = when {
        !isConverterAvailable -> stringResource(R.string.formats_converter_unavailable)
        status == ConversionUiState.Preparing -> stringResource(R.string.formats_converter_status_preparing)
        status == ConversionUiState.ReadyToSave -> stringResource(R.string.formats_converter_status_ready)
        status == ConversionUiState.Saving -> stringResource(R.string.formats_converter_status_saving)
        status == ConversionUiState.Success -> stringResource(R.string.formats_converter_status_done)
        status == ConversionUiState.Error -> stringResource(R.string.formats_converter_status_failed)
        else -> stringResource(R.string.formats_converter_status_idle)
    }

    Surface(
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.72f)
    ) {
        Text(
            text = label,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurface
        )
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
        shape = RoundedCornerShape(28.dp),
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
                    shape = RoundedCornerShape(14.dp),
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
                        shape = RoundedCornerShape(16.dp),
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

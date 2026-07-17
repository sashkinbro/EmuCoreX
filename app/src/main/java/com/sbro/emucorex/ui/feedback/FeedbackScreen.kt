package com.sbro.emucorex.ui.feedback

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material.icons.rounded.AttachFile
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Collections
import androidx.compose.material.icons.rounded.UploadFile
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameLibraryCacheRepository
import com.sbro.emucorex.feedback.FeedbackAttachment
import com.sbro.emucorex.feedback.FeedbackAttachmentError
import com.sbro.emucorex.feedback.FeedbackAttachmentInspector
import com.sbro.emucorex.feedback.FeedbackLimits
import com.sbro.emucorex.feedback.FeedbackUploadScheduler
import com.sbro.emucorex.ui.common.NavigationBackButton
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private data class FeedbackCategory(val id: String, val label: String)

@Composable
@OptIn(ExperimentalMaterial3Api::class)
fun FeedbackScreen(onBackClick: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val preferences = remember(context) { AppPreferences(context) }
    val libraryCacheRepository = remember(context) { GameLibraryCacheRepository(context) }
    val libraryPaths by preferences.gamePaths.collectAsState(initial = emptyList())
    val preferEnglishTitles by preferences.preferEnglishGameTitles.collectAsState(initial = false)
    val attachments = remember { mutableStateListOf<FeedbackAttachment>() }
    var category by rememberSaveable { mutableStateOf("compatibility") }
    var categoryMenuExpanded by remember { mutableStateOf(false) }
    var gameQuery by rememberSaveable { mutableStateOf("") }
    var selectedGamePath by rememberSaveable { mutableStateOf<String?>(null) }
    var gameMenuExpanded by remember { mutableStateOf(false) }
    var libraryGames by remember { mutableStateOf(emptyList<GameItem>()) }
    var isLoadingLibrary by remember { mutableStateOf(true) }
    var message by rememberSaveable { mutableStateOf("") }
    var isQueueing by remember { mutableStateOf(false) }
    var isInspectingAttachments by remember { mutableStateOf(false) }
    var showAttachmentSourceDialog by remember { mutableStateOf(false) }
    var showDiscardDialog by remember { mutableStateOf(false) }

    val categories = listOf(
        FeedbackCategory("compatibility", stringResource(R.string.feedback_category_compatibility)),
        FeedbackCategory("performance", stringResource(R.string.feedback_category_performance)),
        FeedbackCategory("graphics", stringResource(R.string.feedback_category_graphics)),
        FeedbackCategory("audio", stringResource(R.string.feedback_category_audio)),
        FeedbackCategory("controls", stringResource(R.string.feedback_category_controls)),
        FeedbackCategory("crash", stringResource(R.string.feedback_category_crash)),
        FeedbackCategory("feature", stringResource(R.string.feedback_category_feature)),
        FeedbackCategory("ui", stringResource(R.string.feedback_category_ui)),
        FeedbackCategory("other", stringResource(R.string.feedback_category_other))
    )
    val attachmentErrorTexts = mapOf(
        FeedbackAttachmentError.TooMany to stringResource(R.string.feedback_error_too_many_files),
        FeedbackAttachmentError.ItemTooLarge to stringResource(R.string.feedback_error_file_too_large),
        FeedbackAttachmentError.TotalTooLarge to stringResource(R.string.feedback_error_total_too_large),
        FeedbackAttachmentError.Unreadable to stringResource(R.string.feedback_error_file_unreadable)
    )
    val requiredMessage = stringResource(R.string.feedback_error_message_required)
    val queueFailedMessage = stringResource(R.string.feedback_error_queue_failed)
    val queuedMessage = stringResource(R.string.feedback_queued)

    LaunchedEffect(libraryPaths, preferEnglishTitles) {
        isLoadingLibrary = true
        libraryGames = withContext(Dispatchers.IO) {
            libraryPaths.takeIf { it.isNotEmpty() }
                ?.let { paths ->
                    libraryCacheRepository.loadSnapshot(
                        GameLibraryCacheRepository.libraryKey(paths),
                        preferEnglishTitles
                    ).games
                }
                .orEmpty()
                .distinctBy { it.path }
                .sortedBy { it.title.lowercase(Locale.getDefault()) }
        }
        if (selectedGamePath != null && libraryGames.none { it.path == selectedGamePath }) {
            selectedGamePath = null
        }
        isLoadingLibrary = false
    }

    val selectedGame = remember(libraryGames, selectedGamePath) {
        selectedGamePath?.let { path -> libraryGames.firstOrNull { it.path == path } }
    }
    val filteredGames = remember(libraryGames, gameQuery, selectedGamePath, gameMenuExpanded) {
        if (!gameMenuExpanded || selectedGamePath != null || gameQuery.isBlank()) {
            libraryGames
        } else {
            val query = gameQuery.trim()
            libraryGames.filter { game ->
                game.title.contains(query, ignoreCase = true) ||
                    game.serial?.contains(query, ignoreCase = true) == true ||
                    game.fileName.contains(query, ignoreCase = true)
            }
        }.take(100)
    }

    fun acceptUris(newUris: List<Uri>) {
        if (newUris.isEmpty()) return
        scope.launch {
            isInspectingAttachments = true
            try {
                val mergedUris = (attachments.map { it.uri } + newUris).distinct()
                val inspection = withContext(Dispatchers.IO) {
                    FeedbackAttachmentInspector.inspect(context, mergedUris)
                }
                val error = inspection.error
                if (error != null) {
                    Toast.makeText(context, attachmentErrorTexts[error], Toast.LENGTH_LONG).show()
                } else {
                    attachments.clear()
                    attachments.addAll(inspection.attachments)
                }
            } finally {
                isInspectingAttachments = false
            }
        }
    }

    val mediaPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.PickMultipleVisualMedia(FeedbackLimits.MAX_ATTACHMENTS)
    ) { uris -> acceptUris(uris) }
    val filePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris -> acceptUris(uris) }

    val hasChanges = message.isNotBlank() || gameQuery.isNotBlank() || attachments.isNotEmpty() ||
        category != "compatibility"
    val requestBack = {
        if (!isQueueing) {
            if (hasChanges) showDiscardDialog = true else onBackClick()
        }
    }
    BackHandler(onBack = requestBack)

    val submitFeedback: () -> Unit = {
        if (message.isBlank()) {
            Toast.makeText(context, requiredMessage, Toast.LENGTH_SHORT).show()
        } else if (!isQueueing && !isInspectingAttachments && FeedbackUploadScheduler.isConfigured) {
            scope.launch {
                isQueueing = true
                runCatching {
                    FeedbackUploadScheduler.enqueue(
                        context = context,
                        category = category,
                        gameTitle = selectedGame?.title ?: gameQuery,
                        gameSerial = selectedGame?.serial.orEmpty(),
                        message = message,
                        includeDiagnostics = true,
                        attachments = attachments.toList()
                    )
                }.onSuccess {
                    Toast.makeText(context, queuedMessage, Toast.LENGTH_LONG).show()
                    onBackClick()
                }.onFailure {
                    Toast.makeText(context, queueFailedMessage, Toast.LENGTH_LONG).show()
                }
                isQueueing = false
            }
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        contentPadding = PaddingValues(
            start = 8.dp,
            top = WindowInsets.statusBars.asPaddingValues().calculateTopPadding() + 6.dp,
            end = 8.dp,
            bottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 18.dp
        ),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp)
                    .padding(top = 12.dp, bottom = 2.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                NavigationBackButton(
                    onClick = requestBack,
                    contentColor = MaterialTheme.colorScheme.onSurface
                )
                Spacer(Modifier.width(8.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.feedback_title),
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.onBackground,
                        fontWeight = FontWeight.Bold,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(R.string.feedback_subtitle),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
        if (!FeedbackUploadScheduler.isConfigured) {
            item { FeedbackConfigurationWarning(Modifier.widthIn(max = 760.dp)) }
        }
        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.22f),
                contentColor = MaterialTheme.colorScheme.onSurface,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outline.copy(alpha = 0.16f)
                )
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(9.dp)
                ) {
                    Text(
                        text = stringResource(R.string.feedback_category_title),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    ExposedDropdownMenuBox(
                        expanded = categoryMenuExpanded,
                        onExpandedChange = { categoryMenuExpanded = it }
                    ) {
                        OutlinedTextField(
                            value = categories.firstOrNull { it.id == category }?.label.orEmpty(),
                            onValueChange = {},
                            readOnly = true,
                            singleLine = true,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier
                                .fillMaxWidth()
                                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable),
                            trailingIcon = {
                                ExposedDropdownMenuDefaults.TrailingIcon(expanded = categoryMenuExpanded)
                            },
                            colors = feedbackTextFieldColors()
                        )
                        ExposedDropdownMenu(
                            expanded = categoryMenuExpanded,
                            onDismissRequest = { categoryMenuExpanded = false },
                            modifier = Modifier.heightIn(max = 384.dp),
                            shape = RoundedCornerShape(18.dp),
                            containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.98f),
                            border = BorderStroke(
                                1.dp,
                                MaterialTheme.colorScheme.outline.copy(alpha = 0.38f)
                            )
                        ) {
                            categories.forEach { item ->
                                val isSelected = item.id == category
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            item.label,
                                            color = if (isSelected) {
                                                MaterialTheme.colorScheme.primary
                                            } else {
                                                MaterialTheme.colorScheme.onSurface
                                            },
                                            fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Medium
                                        )
                                    },
                                    trailingIcon = {
                                        if (isSelected) {
                                            Icon(
                                                Icons.Rounded.Check,
                                                contentDescription = null,
                                                tint = MaterialTheme.colorScheme.primary
                                            )
                                        }
                                    },
                                    onClick = {
                                        category = item.id
                                        categoryMenuExpanded = false
                                    }
                                )
                            }
                        }
                    }

                    ExposedDropdownMenuBox(
                        expanded = gameMenuExpanded,
                        onExpandedChange = {
                            if (!isLoadingLibrary && libraryGames.isNotEmpty()) gameMenuExpanded = it
                        }
                    ) {
                        OutlinedTextField(
                            value = selectedGame?.let(::formatLibraryGame) ?: gameQuery,
                            onValueChange = { value ->
                                selectedGamePath = null
                                gameQuery = value.take(FeedbackLimits.MAX_GAME_LENGTH)
                                gameMenuExpanded = libraryGames.isNotEmpty()
                            },
                            singleLine = true,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier
                                .fillMaxWidth()
                                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryEditable),
                            label = { Text(stringResource(R.string.feedback_game_label)) },
                            placeholder = {
                                Text(
                                    stringResource(
                                        if (isLoadingLibrary) R.string.feedback_game_loading
                                        else R.string.feedback_game_placeholder
                                    ),
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            },
                            trailingIcon = {
                                if (selectedGame != null) {
                                    IconButton(
                                        onClick = {
                                            selectedGamePath = null
                                            gameQuery = ""
                                        }
                                    ) {
                                        Icon(
                                            Icons.Rounded.Close,
                                            contentDescription = stringResource(R.string.feedback_game_clear_selection)
                                        )
                                    }
                                } else if (libraryGames.isNotEmpty()) {
                                    ExposedDropdownMenuDefaults.TrailingIcon(expanded = gameMenuExpanded)
                                }
                            },
                            colors = feedbackTextFieldColors()
                        )
                        ExposedDropdownMenu(
                            expanded = gameMenuExpanded,
                            onDismissRequest = { gameMenuExpanded = false },
                            modifier = Modifier.heightIn(max = 360.dp),
                            shape = RoundedCornerShape(18.dp),
                            containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.98f),
                            border = BorderStroke(
                                1.dp,
                                MaterialTheme.colorScheme.outline.copy(alpha = 0.38f)
                            )
                        ) {
                            if (filteredGames.isEmpty()) {
                                DropdownMenuItem(
                                    text = { Text(stringResource(R.string.feedback_game_no_matches)) },
                                    onClick = { gameMenuExpanded = false },
                                    enabled = false
                                )
                            } else {
                                filteredGames.forEach { game ->
                                    val isSelected = selectedGamePath == game.path
                                    DropdownMenuItem(
                                        text = {
                                            Column {
                                                Text(
                                                    game.title,
                                                    color = if (isSelected) {
                                                        MaterialTheme.colorScheme.primary
                                                    } else {
                                                        MaterialTheme.colorScheme.onSurface
                                                    },
                                                    fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Medium,
                                                    maxLines = 1,
                                                    overflow = TextOverflow.Ellipsis
                                                )
                                                Text(
                                                    game.serial ?: game.fileName,
                                                    style = MaterialTheme.typography.bodySmall,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                    maxLines = 1,
                                                    overflow = TextOverflow.Ellipsis
                                                )
                                            }
                                        },
                                        trailingIcon = {
                                            if (isSelected) {
                                                Icon(
                                                    Icons.Rounded.Check,
                                                    contentDescription = null,
                                                    tint = MaterialTheme.colorScheme.primary
                                                )
                                            }
                                        },
                                        onClick = {
                                            selectedGamePath = game.path
                                            gameQuery = ""
                                            gameMenuExpanded = false
                                        }
                                    )
                                }
                            }
                        }
                    }
                    if (libraryGames.isEmpty() && !isLoadingLibrary) {
                        Text(
                            text = stringResource(R.string.feedback_game_library_empty),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }

                    OutlinedTextField(
                        value = message,
                        onValueChange = { message = it.take(FeedbackLimits.MAX_MESSAGE_LENGTH) },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(170.dp),
                        shape = RoundedCornerShape(16.dp),
                        label = { Text(stringResource(R.string.feedback_message_label)) },
                        placeholder = { Text(stringResource(R.string.feedback_message_placeholder)) },
                        minLines = 4,
                        maxLines = 7,
                        colors = feedbackTextFieldColors(),
                        supportingText = {
                            Text(
                                stringResource(
                                    R.string.feedback_character_counter,
                                    message.length,
                                    FeedbackLimits.MAX_MESSAGE_LENGTH
                                )
                            )
                        }
                    )

                    Button(
                        onClick = { showAttachmentSourceDialog = true },
                        enabled = !isInspectingAttachments && attachments.size < FeedbackLimits.MAX_ATTACHMENTS,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp)
                    ) {
                        Icon(Icons.Rounded.AttachFile, contentDescription = null, modifier = Modifier.size(20.dp))
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.feedback_add_attachment))
                    }
                    attachments.forEach { attachment ->
                        AttachmentRow(
                            attachment = attachment,
                            onRemove = { attachments.remove(attachment) }
                        )
                    }

                }
            }
        }
        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.14f),
                contentColor = MaterialTheme.colorScheme.onSurface,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outline.copy(alpha = 0.16f)
                )
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = stringResource(R.string.feedback_tip_title),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = stringResource(R.string.feedback_tip_body),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(
                            R.string.feedback_attachment_counter,
                            attachments.size,
                            FeedbackLimits.MAX_ATTACHMENTS
                        ),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
        item {
            Button(
                onClick = submitFeedback,
                enabled = !isQueueing && !isInspectingAttachments && FeedbackUploadScheduler.isConfigured,
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp)
                    .height(50.dp)
            ) {
                if (isQueueing) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(18.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.onPrimary
                    )
                } else {
                    Icon(Icons.AutoMirrored.Rounded.Send, contentDescription = null, modifier = Modifier.size(19.dp))
                }
                Spacer(Modifier.size(8.dp))
                Text(stringResource(if (isQueueing) R.string.feedback_queueing else R.string.feedback_send))
            }
        }
    }

    if (showAttachmentSourceDialog) {
        AlertDialog(
            onDismissRequest = { showAttachmentSourceDialog = false },
            title = { Text(stringResource(R.string.feedback_attachment_source_title)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = {
                            showAttachmentSourceDialog = false
                            mediaPicker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.Collections, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.feedback_add_media))
                    }
                    OutlinedButton(
                        onClick = {
                            showAttachmentSourceDialog = false
                            filePicker.launch(arrayOf("*/*"))
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.UploadFile, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.feedback_add_files))
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { showAttachmentSourceDialog = false }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        )
    }

    if (showDiscardDialog) {
        AlertDialog(
            onDismissRequest = { showDiscardDialog = false },
            title = { Text(stringResource(R.string.feedback_discard_title)) },
            text = { Text(stringResource(R.string.feedback_discard_message)) },
            confirmButton = {
                TextButton(onClick = onBackClick) { Text(stringResource(R.string.feedback_discard_action)) }
            },
            dismissButton = {
                TextButton(onClick = { showDiscardDialog = false }) { Text(stringResource(R.string.cancel)) }
            }
        )
    }
}

@Composable
private fun feedbackTextFieldColors() = OutlinedTextFieldDefaults.colors(
    focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.42f),
    unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.28f),
    focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.78f),
    unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.38f),
    cursorColor = MaterialTheme.colorScheme.primary
)

@Composable
private fun AttachmentRow(attachment: FeedbackAttachment, onRemove: () -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        contentColor = MaterialTheme.colorScheme.onSurface
    ) {
        Row(
            modifier = Modifier.padding(start = 14.dp, top = 8.dp, bottom = 8.dp, end = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(Icons.Rounded.AttachFile, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            Column(modifier = Modifier.weight(1f).padding(horizontal = 10.dp)) {
                Text(attachment.displayName, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(
                    listOfNotNull(
                        attachment.mimeType,
                        attachment.sizeBytes?.let(::formatFileSize)
                    ).joinToString(" · "),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            IconButton(onClick = onRemove) {
                Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.feedback_remove_attachment))
            }
        }
    }
}

@Composable
private fun FeedbackConfigurationWarning(modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.errorContainer
    ) {
        Text(
            text = stringResource(R.string.feedback_not_configured),
            modifier = Modifier.padding(16.dp),
            color = MaterialTheme.colorScheme.onErrorContainer
        )
    }
}

private fun formatFileSize(bytes: Long): String {
    val mebibytes = bytes / (1024.0 * 1024.0)
    return if (mebibytes >= 1.0) {
        String.format(Locale.getDefault(), "%.1f MB", mebibytes)
    } else {
        String.format(Locale.getDefault(), "%.0f KB", bytes / 1024.0)
    }
}

private fun formatLibraryGame(game: GameItem): String {
    return game.serial?.takeIf(String::isNotBlank)?.let { serial -> "${game.title} · $serial" }
        ?: game.title
}

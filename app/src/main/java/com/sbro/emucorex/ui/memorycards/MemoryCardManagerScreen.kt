package com.sbro.emucorex.ui.memorycards

import android.annotation.SuppressLint
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
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
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.InsertDriveFile
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.RadioButtonChecked
import androidx.compose.material.icons.rounded.RadioButtonUnchecked
import androidx.compose.material.icons.rounded.Save
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.MemoryCardAssignments
import com.sbro.emucorex.data.MemoryCardInfo
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import java.text.DateFormat
import java.util.Date
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun MemoryCardManagerScreen(
    onBackClick: () -> Unit
) {
    val context = LocalContext.current
    val repository = remember(context) { MemoryCardRepository(context, AppPreferences(context)) }
    val scope = rememberCoroutineScope()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()

    val createSuccessMessage = stringResource(R.string.memory_card_create_success)
    val createFailureMessage = stringResource(R.string.memory_card_create_failed)
    val backupSuccessMessage = stringResource(R.string.memory_card_backup_success)
    val backupFailureMessage = stringResource(R.string.memory_card_backup_failed)
    val restoreSuccessMessage = stringResource(R.string.memory_card_restore_success)
    val restoreFailureMessage = stringResource(R.string.memory_card_restore_failed)
    val exportSuccessMessage = stringResource(R.string.memory_card_export_success)
    val exportFailureMessage = stringResource(R.string.memory_card_export_failed)
    val renameSuccessMessage = stringResource(R.string.memory_card_rename_success)
    val renameFailureMessage = stringResource(R.string.memory_card_rename_failed)
    val duplicateSuccessMessage = stringResource(R.string.memory_card_duplicate_success)
    val duplicateFailureMessage = stringResource(R.string.memory_card_duplicate_failed)
    val deleteSuccessMessage = stringResource(R.string.memory_card_delete_success)
    val deleteFailureMessage = stringResource(R.string.memory_card_delete_failed)
    val assignSuccessMessage = stringResource(R.string.memory_card_assign_success)
    val assignFailureMessage = stringResource(R.string.memory_card_assign_failed)

    var cards by remember { mutableStateOf<List<MemoryCardInfo>>(emptyList()) }
    var assignments by remember { mutableStateOf(MemoryCardAssignments(slot1 = null, slot2 = null)) }
    var isLoading by remember { mutableStateOf(true) }
    var isWorking by remember { mutableStateOf(false) }
    val showCreateDialog = remember { mutableStateOf(false) }
    val pendingRename = remember { mutableStateOf<MemoryCardInfo?>(null) }
    val pendingDuplicate = remember { mutableStateOf<MemoryCardInfo?>(null) }
    val pendingDelete = remember { mutableStateOf<MemoryCardInfo?>(null) }
    val pendingExport = remember { mutableStateOf<MemoryCardInfo?>(null) }

    fun refresh() {
        scope.launch {
            isLoading = true
            val result = withContext(Dispatchers.IO) {
                val ensuredAssignments = repository.ensureDefaultCardsAssigned()
                repository.listCards() to ensuredAssignments
            }
            cards = result.first
            assignments = result.second
            isLoading = false
        }
    }

    LaunchedEffect(Unit) {
        refresh()
    }

    val backupLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/zip")
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            isWorking = true
            val success = withContext(Dispatchers.IO) { repository.backupCards(cards, uri) }
            isWorking = false
            Toast.makeText(
                context,
                if (success) backupSuccessMessage else backupFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
            refresh()
        }
    }

    val restoreLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            isWorking = true
            val success = withContext(Dispatchers.IO) { repository.restoreCards(uri) }
            isWorking = false
            Toast.makeText(
                context,
                if (success) restoreSuccessMessage else restoreFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
            refresh()
        }
    }

    val exportLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri ->
        val card = pendingExport.value
        pendingExport.value = null
        if (uri == null || card == null) return@rememberLauncherForActivityResult
        scope.launch {
            isWorking = true
            val success = withContext(Dispatchers.IO) { repository.exportCard(card, uri) }
            isWorking = false
            Toast.makeText(
                context,
                if (success) exportSuccessMessage else exportFailureMessage,
                Toast.LENGTH_SHORT
            ).show()
            refresh()
        }
    }

    pendingDelete.value?.let { card ->
        AlertDialog(
            onDismissRequest = { pendingDelete.value = null },
            title = { Text(stringResource(R.string.memory_card_delete_confirm_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.memory_card_delete_confirm_body,
                        card.name
                    )
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            isWorking = true
                            val success = withContext(Dispatchers.IO) { repository.deleteCard(card) }
                            isWorking = false
                            pendingDelete.value = null
                            Toast.makeText(
                                context,
                                if (success) deleteSuccessMessage else deleteFailureMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                            refresh()
                        }
                    }
                ) {
                    Text(stringResource(R.string.memory_card_delete_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete.value = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showCreateDialog.value) {
        MemoryCardCreateDialog(
            title = stringResource(R.string.memory_card_create_title),
            confirmLabel = stringResource(R.string.memory_card_create_action),
            onDismiss = { showCreateDialog.value = false },
            onConfirm = { name, createType, sizeMb ->
                scope.launch {
                    isWorking = true
                    val success = withContext(Dispatchers.IO) {
                        when (createType) {
                            MemoryCardCreateType.Folder -> repository.createFolderCard(name)
                            MemoryCardCreateType.File -> repository.createPs2Card(name, sizeMb)
                        }
                    }
                    isWorking = false
                    showCreateDialog.value = false
                    Toast.makeText(
                        context,
                        if (success) createSuccessMessage else createFailureMessage,
                        Toast.LENGTH_SHORT
                    ).show()
                    refresh()
                }
            }
        )
    }

    pendingRename.value?.let { card ->
        MemoryCardNameDialog(
            title = stringResource(R.string.memory_card_rename_title),
            confirmLabel = stringResource(R.string.memory_card_rename_action),
            initialName = card.name.removeSuffix(".ps2"),
            onDismiss = { pendingRename.value = null },
            onConfirm = { name, _ ->
                scope.launch {
                    isWorking = true
                    val success = withContext(Dispatchers.IO) { repository.renameCard(card, name) }
                    isWorking = false
                    pendingRename.value = null
                    Toast.makeText(
                        context,
                        if (success) renameSuccessMessage else renameFailureMessage,
                        Toast.LENGTH_SHORT
                    ).show()
                    refresh()
                }
            }
        )
    }

    pendingDuplicate.value?.let { card ->
        MemoryCardNameDialog(
            title = stringResource(R.string.memory_card_duplicate_title),
            confirmLabel = stringResource(R.string.memory_card_duplicate_action),
            initialName = card.name.removeSuffix(".ps2") + " Copy",
            onDismiss = { pendingDuplicate.value = null },
            onConfirm = { name, _ ->
                scope.launch {
                    isWorking = true
                    val success = withContext(Dispatchers.IO) { repository.duplicateCard(card, name) }
                    isWorking = false
                    pendingDuplicate.value = null
                    Toast.makeText(
                        context,
                        if (success) duplicateSuccessMessage else duplicateFailureMessage,
                        Toast.LENGTH_SHORT
                    ).show()
                    refresh()
                }
            }
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = 0.dp,
                bottom = 24.dp + bottomInset
            ),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                MemoryCardHeader(
                    topInset = topInset,
                    isWorking = isWorking,
                    onBackClick = onBackClick
                )
            }

            item {
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    FilledTonalButton(
                        onClick = { showCreateDialog.value = true },
                        enabled = !isWorking,
                        colors = ButtonDefaults.filledTonalButtonColors(
                            containerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                            contentColor = MaterialTheme.colorScheme.primary
                        )
                    ) {
                        Icon(Icons.Rounded.Add, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(stringResource(R.string.memory_card_create_action))
                    }
                    FilledTonalButton(
                        onClick = { backupLauncher.launch("EmuCoreX-memory-cards.zip") },
                        enabled = cards.isNotEmpty() && !isWorking,
                        colors = ButtonDefaults.filledTonalButtonColors(
                            containerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                            contentColor = MaterialTheme.colorScheme.primary
                        )
                    ) {
                        Icon(Icons.Rounded.Save, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(stringResource(R.string.memory_card_backup_action))
                    }
                    OutlinedButton(
                        onClick = { restoreLauncher.launch(arrayOf("application/zip", "*/*")) },
                        enabled = !isWorking
                    ) {
                        Icon(Icons.Rounded.CloudDownload, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(stringResource(R.string.memory_card_restore_action))
                    }
                }
            }

            if (isLoading) {
                item {
                    LoadingCard()
                }
            } else if (cards.isEmpty()) {
                item {
                    EmptyMemoryCardsCard()
                }
            } else {
                items(cards, key = { it.path }) { card ->
                    MemoryCardItem(
                        card = card,
                        assignments = assignments,
                        onToggleSlot = { slot ->
                            scope.launch {
                                isWorking = true
                                val success = withContext(Dispatchers.IO) {
                                    val currentName = when (slot) {
                                        1 -> assignments.slot1
                                        else -> assignments.slot2
                                    }
                                    repository.assignCardToSlot(
                                        slot = slot,
                                        cardName = if (currentName.equals(card.name, ignoreCase = true)) null else card.name
                                    )
                                    true
                                }
                                isWorking = false
                                Toast.makeText(
                                    context,
                                    if (success) assignSuccessMessage else assignFailureMessage,
                                    Toast.LENGTH_SHORT
                                ).show()
                                refresh()
                            }
                        },
                        onExport = {
                            pendingExport.value = card
                            exportLauncher.launch(card.exportFileName())
                        },
                        onDuplicate = { pendingDuplicate.value = card },
                        onRename = { pendingRename.value = card },
                        onDelete = { pendingDelete.value = card }
                    )
                }
            }
        }
    }
}

@Composable
private fun MemoryCardHeader(
    topInset: androidx.compose.ui.unit.Dp,
    isWorking: Boolean,
    onBackClick: () -> Unit
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = topInset, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            ScreenTopBar(
                title = stringResource(R.string.memory_card_manager_title),
                onBackClick = onBackClick,
                modifier = Modifier.weight(1f)
            )
            if (isWorking) {
                CircularProgressIndicator(
                    modifier = Modifier.size(22.dp),
                    strokeWidth = 2.dp
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun MemoryCardItem(
    card: MemoryCardInfo,
    assignments: MemoryCardAssignments,
    onToggleSlot: (Int) -> Unit,
    onExport: () -> Unit,
    onDuplicate: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit
) {
    val assignedSlot1 = assignments.slot1.equals(card.name, ignoreCase = true)
    val assignedSlot2 = assignments.slot2.equals(card.name, ignoreCase = true)
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        tonalElevation = 1.dp,
        shadowElevation = 3.dp,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(
            width = 1.dp,
            color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top
            ) {
                Box(
                    modifier = Modifier
                        .size(54.dp)
                        .background(
                            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f),
                            shape = RoundedCornerShape(16.dp)
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Memory,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 14.dp)
                ) {
                    Text(
                        text = card.name,
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(
                            R.string.memory_card_meta,
                            card.storageLabel(),
                            DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT)
                                .format(Date(card.modifiedTime))
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(
                            if (card.formatted) R.string.memory_card_state_formatted else R.string.memory_card_state_unformatted
                        ),
                        style = MaterialTheme.typography.labelMedium,
                        color = if (card.formatted) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.error
                        }
                    )
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterChip(
                    selected = assignedSlot1,
                    onClick = { onToggleSlot(1) },
                    label = { Text(stringResource(R.string.memory_card_slot_1)) }
                )
                FilterChip(
                    selected = assignedSlot2,
                    onClick = { onToggleSlot(2) },
                    label = { Text(stringResource(R.string.memory_card_slot_2)) }
                )
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                SmallActionButton(
                    icon = Icons.Rounded.Save,
                    label = stringResource(R.string.memory_card_export_action),
                    onClick = onExport
                )
                SmallActionButton(
                    icon = Icons.Rounded.ContentCopy,
                    label = stringResource(R.string.memory_card_duplicate_action),
                    onClick = onDuplicate
                )
                if (!card.isDefaultCard) {
                    SmallActionButton(
                        icon = Icons.Rounded.Edit,
                        label = stringResource(R.string.memory_card_rename_action),
                        onClick = onRename
                    )
                    SmallActionButton(
                        icon = Icons.Rounded.DeleteOutline,
                        label = stringResource(R.string.memory_card_delete_action),
                        onClick = onDelete
                    )
                }
            }
        }
    }
}

@Composable
private fun SmallActionButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    onClick: () -> Unit
) {
    OutlinedButton(onClick = onClick) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(16.dp))
        Spacer(modifier = Modifier.width(6.dp))
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun EmptyMemoryCardsCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(R.string.memory_card_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
            )
            Text(
                text = stringResource(R.string.memory_card_empty_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun LoadingCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Row(
            modifier = Modifier.padding(18.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            CircularProgressIndicator(modifier = Modifier.size(22.dp), strokeWidth = 2.dp)
            Text(
                text = stringResource(R.string.memory_card_loading),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(start = 12.dp)
            )
        }
    }
}

private enum class MemoryCardCreateType {
    Folder,
    File
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun MemoryCardCreateDialog(
    title: String,
    confirmLabel: String,
    onDismiss: () -> Unit,
    onConfirm: (String, MemoryCardCreateType, Int) -> Unit
) {
    var value by remember { mutableStateOf("") }
    var selectedType by remember { mutableStateOf(MemoryCardCreateType.Folder) }
    var selectedSize by remember { mutableIntStateOf(8) }
    val sizes = remember { listOf(8, 16, 32, 64) }
    val scrollState = rememberScrollState()
    val containerSize = LocalWindowInfo.current.containerSize
    val density = LocalDensity.current
    val windowWidth = with(density) { containerSize.width.toDp() }
    val windowHeight = with(density) { containerSize.height.toDp() }
    val isLandscape = windowWidth > windowHeight
    val maxDialogHeight = if (isLandscape) {
        (windowHeight - 36.dp).coerceAtLeast(240.dp)
    } else {
        (windowHeight - 48.dp)
            .coerceAtMost(620.dp)
            .coerceAtLeast(300.dp)
    }
    val dialogWidthFraction = if (isLandscape) 0.98f else 0.94f
    val dialogMaxWidth = if (isLandscape) 1600.dp else 720.dp

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(
                    start = if (isLandscape) 10.dp else 14.dp,
                    top = if (isLandscape) 10.dp else 14.dp,
                    end = if (isLandscape) 10.dp else 14.dp,
                    bottom = if (isLandscape) 4.dp else 8.dp
                ),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth(dialogWidthFraction)
                    .widthIn(max = dialogMaxWidth),
                shape = RoundedCornerShape(26.dp),
                tonalElevation = 6.dp,
                shadowElevation = 10.dp,
                color = MaterialTheme.colorScheme.surface
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = maxDialogHeight)
                        .verticalScroll(scrollState)
                        .padding(22.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = stringResource(R.string.memory_card_create_subtitle),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    OutlinedTextField(
                        value = value,
                        onValueChange = { value = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(14.dp),
                        label = { Text(stringResource(R.string.memory_card_name_field)) }
                    )

                    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        Text(
                            text = stringResource(R.string.memory_card_type_title),
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        MemoryCardCreateTypeRow(
                            selected = selectedType == MemoryCardCreateType.Folder,
                            icon = Icons.Rounded.Folder,
                            title = stringResource(R.string.memory_card_type_folder_title),
                            description = stringResource(R.string.memory_card_type_folder_desc),
                            onClick = { selectedType = MemoryCardCreateType.Folder }
                        )
                        MemoryCardCreateTypeRow(
                            selected = selectedType == MemoryCardCreateType.File,
                            icon = Icons.AutoMirrored.Rounded.InsertDriveFile,
                            title = stringResource(R.string.memory_card_type_file_title),
                            description = stringResource(R.string.memory_card_type_file_desc),
                            onClick = { selectedType = MemoryCardCreateType.File }
                        )
                    }

                    if (selectedType == MemoryCardCreateType.File) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(
                                text = stringResource(R.string.memory_card_size_title),
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            FlowRow(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                sizes.forEach { size ->
                                    FilterChip(
                                        selected = selectedSize == size,
                                        onClick = { selectedSize = size },
                                        label = { Text(stringResource(R.string.memory_card_size_value, size)) }
                                    )
                                }
                            }
                        }
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        TextButton(onClick = onDismiss) {
                            Text(stringResource(android.R.string.cancel))
                        }
                        Spacer(modifier = Modifier.width(8.dp))
                        FilledTonalButton(
                            enabled = value.isNotBlank(),
                            onClick = {
                                val trimmed = value.trim()
                                if (trimmed.isNotBlank()) {
                                    onConfirm(trimmed, selectedType, selectedSize)
                                }
                            }
                        ) {
                            Text(confirmLabel)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MemoryCardCreateTypeRow(
    selected: Boolean,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    description: String,
    onClick: () -> Unit
) {
    val borderColor = if (selected) {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.outlineVariant
    }
    val containerColor = if (selected) {
        MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f)
    } else {
        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f)
    }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .clickable(onClick = onClick),
        shape = RoundedCornerShape(16.dp),
        color = containerColor,
        border = BorderStroke(1.dp, borderColor.copy(alpha = 0.85f))
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(26.dp)
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 12.dp),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Icon(
                imageVector = if (selected) Icons.Rounded.RadioButtonChecked else Icons.Rounded.RadioButtonUnchecked,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(22.dp)
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun MemoryCardNameDialog(
    title: String,
    confirmLabel: String,
    initialName: String,
    showSizeOptions: Boolean = false,
    onDismiss: () -> Unit,
    onConfirm: (String, Int) -> Unit
) {
    var value by remember(initialName) { mutableStateOf(initialName) }
    var selectedSize by remember { mutableIntStateOf(8) }
    val sizes = remember { listOf(8, 16, 32, 64) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = value,
                    onValueChange = { value = it },
                    singleLine = true,
                    shape = RoundedCornerShape(18.dp),
                    label = { Text(stringResource(R.string.memory_card_name_field)) }
                )
                if (showSizeOptions) {
                    Text(
                        text = stringResource(R.string.memory_card_size_title),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        sizes.forEach { size ->
                            FilterChip(
                                selected = selectedSize == size,
                                onClick = { selectedSize = size },
                                label = { Text(stringResource(R.string.memory_card_size_value, size)) }
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    val trimmed = value.trim()
                    if (trimmed.isNotBlank()) {
                        onConfirm(trimmed, selectedSize)
                    }
                }
            ) {
                Text(confirmLabel)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(android.R.string.cancel))
            }
        }
    )
}

@SuppressLint("DefaultLocale")
private fun formatBytes(bytes: Long): String {
    return when {
        bytes >= 1_073_741_824L -> String.format("%.2f GB", bytes / 1_073_741_824.0)
        bytes >= 1_048_576L -> String.format("%.1f MB", bytes / 1_048_576.0)
        bytes >= 1024L -> String.format("%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }
}

@Composable
private fun MemoryCardInfo.storageLabel(): String {
    return if (isFolder) {
        stringResource(R.string.memory_card_folder_unlimited)
    } else {
        stringResource(R.string.memory_card_file_size, formatBytes(sizeBytes))
    }
}

private fun MemoryCardInfo.exportFileName(): String {
    return if (isFolder) "${name.removeSuffix(".ps2")}.zip" else name
}

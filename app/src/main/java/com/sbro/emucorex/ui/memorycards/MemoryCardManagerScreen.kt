package com.sbro.emucorex.ui.memorycards

import android.annotation.SuppressLint
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.MemoryCardAssignments
import com.sbro.emucorex.data.MemoryCardInfo
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.SettingHelpButton
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
    var showCreateDialog by remember { mutableStateOf(false) }
    var pendingRename by remember { mutableStateOf<MemoryCardInfo?>(null) }
    var pendingDuplicate by remember { mutableStateOf<MemoryCardInfo?>(null) }
    var pendingDelete by remember { mutableStateOf<MemoryCardInfo?>(null) }
    var pendingExport by remember { mutableStateOf<MemoryCardInfo?>(null) }

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
        val card = pendingExport
        pendingExport = null
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

    pendingDelete?.let { card ->
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
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
                            pendingDelete = null
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
                TextButton(onClick = { pendingDelete = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showCreateDialog) {
        MemoryCardNameDialog(
            title = stringResource(R.string.memory_card_create_title),
            confirmLabel = stringResource(R.string.memory_card_create_action),
            initialName = "",
            showSizeOptions = true,
            onDismiss = { showCreateDialog = false },
            onConfirm = { name, sizeMb ->
                scope.launch {
                    isWorking = true
                    val success = withContext(Dispatchers.IO) { repository.createPs2Card(name, sizeMb) }
                    isWorking = false
                    showCreateDialog = false
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

    pendingRename?.let { card ->
        MemoryCardNameDialog(
            title = stringResource(R.string.memory_card_rename_title),
            confirmLabel = stringResource(R.string.memory_card_rename_action),
            initialName = card.name.removeSuffix(".ps2"),
            onDismiss = { pendingRename = null },
            onConfirm = { name, _ ->
                scope.launch {
                    isWorking = true
                    val success = withContext(Dispatchers.IO) { repository.renameCard(card, name) }
                    isWorking = false
                    pendingRename = null
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

    pendingDuplicate?.let { card ->
        MemoryCardNameDialog(
            title = stringResource(R.string.memory_card_duplicate_title),
            confirmLabel = stringResource(R.string.memory_card_duplicate_action),
            initialName = card.name.removeSuffix(".ps2") + " Copy",
            onDismiss = { pendingDuplicate = null },
            onConfirm = { name, _ ->
                scope.launch {
                    isWorking = true
                    val success = withContext(Dispatchers.IO) { repository.duplicateCard(card, name) }
                    isWorking = false
                    pendingDuplicate = null
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
            .navigationBarsPadding()
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = 0.dp,
                bottom = 24.dp
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
                        onClick = { showCreateDialog = true },
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
                            pendingExport = card
                            exportLauncher.launch(card.name)
                        },
                        onDuplicate = { pendingDuplicate = card },
                        onRename = { pendingRename = card },
                        onDelete = { pendingDelete = card }
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
            NavigationBackButton(
                onClick = onBackClick,
                contentColor = MaterialTheme.colorScheme.onBackground
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 4.dp, end = 8.dp)
            ) {
                Text(
                    text = stringResource(R.string.memory_card_manager_title),
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onBackground
                )
            }
            SettingHelpButton(
                title = stringResource(R.string.memory_card_manager_title),
                description = stringResource(R.string.memory_card_help_text),
                modifier = Modifier.padding(end = 8.dp)
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
        shape = RoundedCornerShape(22.dp),
        tonalElevation = 2.dp,
        shadowElevation = 0.dp,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top
            ) {
                Box(
                    modifier = Modifier
                        .size(54.dp)
                        .background(
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
                            shape = RoundedCornerShape(16.dp)
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Memory,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary
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
                            formatBytes(card.sizeBytes),
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

package com.sbro.emucorex.ui.common

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Environment
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import kotlinx.coroutines.delay
import kotlin.time.Duration.Companion.milliseconds

enum class TvStorageRequest {
    BIOS_FILE,
    GAME_FOLDER
}

/**
 * TV-only helper around Android's Storage Access Framework.
 *
 * The existing phone/tablet picker path remains untouched. This layer only gives a remote user
 * a deterministic storage-volume choice before Android grants the actual file-system permission.
 */
@Composable
fun TvStoragePickerHost(
    request: TvStorageRequest?,
    onDismiss: () -> Unit,
    onBiosSelected: (Uri) -> Unit,
    onGameFolderSelected: (Uri) -> Unit
) {
    if (request == null) return

    val context = LocalContext.current
    val sources = remember(context) { TvStorageAccess.availableSources(context) }
    val firstSourceFocusRequester = remember { FocusRequester() }
    val unavailableMessage = stringResource(R.string.tv_storage_picker_unavailable)

    val biosLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        result.data?.data?.let(onBiosSelected)
    }
    val folderLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        result.data?.data?.let(onGameFolderSelected)
    }

    fun launch(source: TvStorageSource) {
        val intent = TvStorageAccess.createPickerIntent(request, source.volume)
        val launched = runCatching {
            when (request) {
                TvStorageRequest.BIOS_FILE -> biosLauncher.launch(intent)
                TvStorageRequest.GAME_FOLDER -> folderLauncher.launch(intent)
            }
        }.isSuccess
        if (!launched) {
            Toast.makeText(context, unavailableMessage, Toast.LENGTH_LONG).show()
            return
        }
        onDismiss()
    }

    LaunchedEffect(request, sources) {
        delay(100.milliseconds)
        runCatching { firstSourceFocusRequester.requestFocus() }
    }

    AppAlertDialog(
        onDismissRequest = onDismiss,
        icon = {
            Icon(
                imageVector = Icons.Rounded.FolderOpen,
                contentDescription = null
            )
        },
        title = { Text(stringResource(R.string.tv_storage_source_title)) },
        text = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(max = 320.dp)
                    .verticalScroll(rememberScrollState())
                    .padding(top = 4.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    stringResource(
                        if (request == TvStorageRequest.BIOS_FILE) {
                            R.string.onboarding_bios_desc
                        } else {
                            R.string.onboarding_games_desc
                        }
                    )
                )
                sources.forEachIndexed { index, source ->
                    OutlinedButton(
                        onClick = { launch(source) },
                        modifier = Modifier
                            .fillMaxWidth()
                            .then(
                                if (index == 0) {
                                    Modifier.focusRequester(firstSourceFocusRequester)
                                } else {
                                    Modifier
                                }
                            )
                    ) {
                        Text(
                            text = source.label,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.cancel))
            }
        }
    )
}

private data class TvStorageSource(
    val label: String,
    val volume: StorageVolume?
)

private object TvStorageAccess {
    fun availableSources(context: Context): List<TvStorageSource> {
        val storageManager = context.getSystemService(StorageManager::class.java)
        val mountedVolumes = storageManager?.storageVolumes.orEmpty()
            .filter { volume ->
                volume.state == Environment.MEDIA_MOUNTED ||
                    volume.state == Environment.MEDIA_MOUNTED_READ_ONLY
            }
            .sortedWith(compareByDescending<StorageVolume> { it.isPrimary }.thenBy { it.getDescription(context) })
            .map { volume -> TvStorageSource(volume.getDescription(context), volume) }

        return listOf(TvStorageSource(context.getString(R.string.tv_storage_all_locations), null)) + mountedVolumes
    }

    fun createPickerIntent(request: TvStorageRequest, volume: StorageVolume?): Intent {
        val treeIntent = volume?.createOpenDocumentTreeIntent()
        val intent = when (request) {
            TvStorageRequest.BIOS_FILE -> Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
                treeIntent?.initialUri()?.let { putExtra(DocumentsContract.EXTRA_INITIAL_URI, it) }
            }
            TvStorageRequest.GAME_FOLDER -> treeIntent ?: Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
        }
        return intent.addFlags(
            Intent.FLAG_GRANT_READ_URI_PERMISSION or
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or
                Intent.FLAG_GRANT_PREFIX_URI_PERMISSION
        )
    }

    @Suppress("DEPRECATION")
    private fun Intent.initialUri(): Uri? {
        return getParcelableExtra(DocumentsContract.EXTRA_INITIAL_URI)
    }
}

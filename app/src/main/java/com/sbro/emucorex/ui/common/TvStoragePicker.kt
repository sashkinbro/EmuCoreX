package com.sbro.emucorex.ui.common

import android.content.Context
import android.content.Intent
import android.content.ComponentName
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Environment
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.provider.DocumentsContract
import android.util.Log
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.interaction.MutableInteractionSource
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
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.documentfile.provider.DocumentFile
import com.sbro.emucorex.R
import com.sbro.emucorex.core.StorageAccess
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.time.Duration.Companion.milliseconds
import com.sbro.emucorex.ui.theme.neon.neonShape
import com.sbro.emucorex.ui.theme.neon.neonButtonShape

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
    val invalidSelectionMessage = stringResource(R.string.tv_storage_picker_invalid_selection)
    val scope = rememberCoroutineScope()

    fun handlePickedTree(uri: Uri?, onAccepted: (Uri) -> Unit) {
        if (uri == null) {
            onDismiss()
            return
        }
        scope.launch {
            val usable = withContext(Dispatchers.IO) {
                TvStorageAccess.isUsableTreeUri(context, uri)
            }
            if (usable) {
                onAccepted(uri)
                onDismiss()
            } else {
                Toast.makeText(context, invalidSelectionMessage, Toast.LENGTH_LONG).show()
            }
        }
    }

    val biosLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        handlePickedTree(result.data?.data, onBiosSelected)
    }
    val folderLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        handlePickedTree(result.data?.data, onGameFolderSelected)
    }

    fun launch(source: TvStorageSource) {
        val intent = TvStorageAccess.createCompatiblePickerIntent(
            context = context,
            request = request,
            volume = source.volume
        )
        if (intent == null) {
            Toast.makeText(context, unavailableMessage, Toast.LENGTH_LONG).show()
            return
        }
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
                    val interactionSource = remember(index, source.label) { MutableInteractionSource() }
                    OutlinedButton(
                        shape = neonButtonShape(),
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
                            .gamepadFocusableCard(
                                shape = neonShape(18.dp),
                                interactionSource = interactionSource,
                                addFocusTarget = false,
                                focusHighlightMode = GamepadFocusHighlightMode.Always
                            ),
                        interactionSource = interactionSource
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
    private const val TAG = "TvStoragePicker"

    fun availableSources(context: Context): List<TvStorageSource> {
        val storageManager = context.getSystemService(StorageManager::class.java)
        val mountedVolumes = storageManager?.storageVolumes.orEmpty()
            .filter { volume ->
                volume.state == Environment.MEDIA_MOUNTED ||
                    volume.state == Environment.MEDIA_MOUNTED_READ_ONLY
            }
            .sortedWith(compareByDescending<StorageVolume> { it.isPrimary }.thenBy { it.getDescription(context) })
            .map { volume ->
                Log.d(
                    TAG,
                    "Available volume: ${volume.getDescription(context)} state=${volume.state} " +
                        "removable=${volume.isRemovable} primary=${volume.isPrimary}"
                )
                TvStorageSource(volume.getDescription(context), volume)
            }

        return listOf(TvStorageSource(context.getString(R.string.tv_storage_all_locations), null)) + mountedVolumes
    }

    fun createCompatiblePickerIntent(
        context: Context,
        request: TvStorageRequest,
        volume: StorageVolume?
    ): Intent? {
        val candidates = buildList {
            if (volume != null) {
                runCatching { volume.createOpenDocumentTreeIntent() }.getOrNull()?.let(::add)
            }
            add(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE))
            knownTreePickerComponents(context).forEach { component ->
                add(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).setComponent(component))
            }
        }.map { intent -> intent.withPickerFlags() }

        candidates.firstOrNull { hasCompatiblePicker(context, it) }?.let { intent ->
            val resolved = intent.component
                ?: context.packageManager
                    .resolveActivity(intent, PackageManager.MATCH_DEFAULT_ONLY)
                    ?.activityInfo?.packageName
            Log.d(TAG, "Using storage picker: $resolved")
            return intent
        }

        // Some TV variants of AnExplorer implement folder selection but accidentally omit the
        // OPEN_DOCUMENT_TREE manifest filter. Reuse its exported document activity explicitly
        // only when Android has no real tree picker of its own.
        val anExplorerActivity = findAnExplorerDocumentActivity(context)
        if (anExplorerActivity != null) {
            Log.d(TAG, "Using storage picker fallback: $anExplorerActivity")
            return Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                .setComponent(anExplorerActivity)
                .withPickerFlags()
        }
        Log.w(TAG, "No compatible storage picker found on this device")
        return null
    }

    /**
     * TV boxes with cut-down firmwares sometimes return a folder that cannot actually be read
     * or whose permission is not persistable, which used to be stored silently and failed later.
     */
    fun isUsableTreeUri(context: Context, uri: Uri): Boolean {
        if (!DocumentsContract.isTreeUri(uri)) {
            Log.w(TAG, "Rejected picker result, not a tree URI: $uri")
            return false
        }
        val tree = runCatching { DocumentFile.fromTreeUri(context, uri) }.getOrNull()
        if (tree == null || !tree.isDirectory || !tree.canRead()) {
            Log.w(TAG, "Rejected picker result, tree is not readable: $uri")
            return false
        }
        if (runCatching { tree.listFiles() }.isFailure) {
            Log.w(TAG, "Rejected picker result, provider cannot list children: $uri")
            return false
        }
        if (!StorageAccess.takePersistableReadPermission(context, uri)) {
            Log.w(TAG, "Picked tree permission is not persistable, access may be lost after restart: $uri")
        }
        return true
    }

    private fun Intent.withPickerFlags(): Intent = addFlags(
        Intent.FLAG_GRANT_READ_URI_PERMISSION or
            Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or
            Intent.FLAG_GRANT_PREFIX_URI_PERMISSION
    )

    @Suppress("DEPRECATION")
    private fun knownTreePickerComponents(context: Context): List<ComponentName> {
        return context.packageManager
            .queryIntentActivities(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), 0)
            .mapNotNull { resolveInfo ->
                val activityInfo = resolveInfo.activityInfo ?: return@mapNotNull null
                if (activityInfo.exported && activityInfo.packageName in KNOWN_PICKER_PACKAGES) {
                    ComponentName(activityInfo.packageName, activityInfo.name)
                } else {
                    null
                }
            }
    }

    @Suppress("DEPRECATION")
    fun hasCompatiblePicker(context: Context, intent: Intent): Boolean {
        return context.packageManager
            .queryIntentActivities(intent, PackageManager.MATCH_DEFAULT_ONLY)
            .any { resolveInfo ->
                resolveInfo.activityInfo?.packageName?.let { it != TV_FRAMEWORK_STUB_PACKAGE } == true
            }
    }

    @Suppress("DEPRECATION")
    private fun findAnExplorerDocumentActivity(context: Context): ComponentName? {
        val probeIntent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        return context.packageManager
            .queryIntentActivities(probeIntent, PackageManager.MATCH_DEFAULT_ONLY)
            .firstNotNullOfOrNull { resolveInfo ->
                val activityInfo = resolveInfo.activityInfo ?: return@firstNotNullOfOrNull null
                if (activityInfo.packageName == ANEXPLORER_PACKAGE && activityInfo.exported) {
                    ComponentName(activityInfo.packageName, activityInfo.name)
                } else {
                    null
                }
            }
    }

    private const val TV_FRAMEWORK_STUB_PACKAGE = "com.android.tv.frameworkpackagestubs"
    private const val ANEXPLORER_PACKAGE = "dev.dworks.apps.anexplorer"
    private val KNOWN_PICKER_PACKAGES = setOf(
        "com.android.documentsui",
        "com.google.android.documentsui",
        ANEXPLORER_PACKAGE
    )
}

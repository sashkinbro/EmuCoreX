package com.sbro.emucorex.ui.emulation

import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.RemoteCheatPack
import com.sbro.emucorex.ui.cheats.CheatCatalogEmptyState
import com.sbro.emucorex.ui.cheats.ContentCompatibilityBadge
import com.sbro.emucorex.ui.cheats.InstalledStatusLabel
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * In-game cheat pack browser. It mirrors the cheat manager catalog but stays
 * compact: the header always shows how many compatible packs exist and the
 * cards themselves only expand when the player asks for them (or when no
 * cheats are installed yet). Downloading and importing are delegated to the
 * view model so they are not cancelled when the menu closes.
 */
@Composable
internal fun EmulationCheatPacksSection(
    state: CheatPackBrowserState,
    hasInstalledCheats: Boolean,
    onLoad: (Boolean) -> Unit,
    onInstallPack: (RemoteCheatPack) -> Unit,
    onImportText: (String, String?) -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val scope = rememberCoroutineScope()
    var expanded by rememberSaveable(state.identityKey) { mutableStateOf(!hasInstalledCheats) }
    var otherVersionsExpanded by rememberSaveable(state.identityKey) { mutableStateOf(false) }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val payload = withContext(Dispatchers.IO) {
                runCatching {
                    val contents = context.contentResolver.openInputStream(uri)?.use { input ->
                        input.readBytes().decodeToString().removePrefix("\uFEFF")
                    }.orEmpty()
                    val fallbackName = runCatching {
                        context.contentResolver
                            .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
                    }.getOrNull()?.substringBeforeLast('.')?.takeIf(String::isNotBlank)
                    contents to fallbackName
                }.getOrDefault("" to null)
            }
            onImportText(payload.first, payload.second)
        }
    }

    val busy = state.isBusy
    val importFeedback = state.importSucceeded

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = tween(220))
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            CheatPackHeader(
                state = state,
                expanded = expanded,
                onRefresh = { onLoad(true) },
                onToggleExpanded = { expanded = !expanded }
            )
            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(animationSpec = tween(260), expandFrom = Alignment.Top) +
                    fadeIn(animationSpec = tween(200)),
                exit = shrinkVertically(animationSpec = tween(220), shrinkTowards = Alignment.Top) +
                    fadeOut(animationSpec = tween(140))
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    CheatPackIdentity(state = state)
                    OutlinedButton(
                        onClick = { importLauncher.launch(arrayOf("*/*")) },
                        enabled = !busy,
                        shape = neonButtonShape(),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        if (state.importing) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                strokeWidth = 2.dp,
                                color = LocalContentColor.current
                            )
                        } else {
                            Icon(Icons.Rounded.FolderOpen, contentDescription = null, modifier = Modifier.size(18.dp))
                        }
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = stringResource(R.string.emulation_cheat_packs_import),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    AnimatedContent(
                        targetState = importFeedback,
                        transitionSpec = {
                            fadeIn(animationSpec = tween(180)) togetherWith fadeOut(animationSpec = tween(120))
                        },
                        label = "cheatPackImportFeedback"
                    ) { feedback ->
                        if (feedback != null) {
                            CheatPackFeedbackLine(
                                success = feedback,
                                text = stringResource(
                                    if (feedback) {
                                        R.string.cheat_manager_import_success
                                    } else {
                                        R.string.cheat_manager_import_failed
                                    }
                                )
                            )
                        }
                    }
                    AnimatedContent(
                        targetState = state.phase,
                        transitionSpec = {
                            (fadeIn(animationSpec = tween(220)) +
                                slideInVertically(animationSpec = tween(220)) { height -> height / 14 }) togetherWith
                                (fadeOut(animationSpec = tween(140)) +
                                    slideOutVertically(animationSpec = tween(140)) { height -> -height / 18 })
                        },
                        label = "cheatPackContent"
                    ) { phase ->
                        when (phase) {
                            CheatPackBrowserPhase.Idle,
                            CheatPackBrowserPhase.Loading -> CheatPackLoadingSkeleton()

                            CheatPackBrowserPhase.Error -> CheatPackErrorCard(onRetry = { onLoad(true) })

                            CheatPackBrowserPhase.Empty -> CheatCatalogEmptyState()

                            CheatPackBrowserPhase.Content -> {
                                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                                    if (state.compatiblePacks.isEmpty()) {
                                        CheatCatalogEmptyState()
                                    } else {
                                        state.compatiblePacks.forEach { pack ->
                                            CheatPackCard(
                                                pack = pack,
                                                working = state.workingPackId == pack.id,
                                                installed = pack.id in state.installedPackIds,
                                                failed = state.failedPackId == pack.id,
                                                busy = busy,
                                                onDownload = { onInstallPack(pack) },
                                                onSource = { openSource(uriHandler, pack) }
                                            )
                                        }
                                    }
                                    if (state.otherVersionPacks.isNotEmpty()) {
                                        CheatPackOtherVersions(
                                            packs = state.otherVersionPacks,
                                            expanded = otherVersionsExpanded,
                                            onToggle = { otherVersionsExpanded = !otherVersionsExpanded },
                                            onSource = { pack -> openSource(uriHandler, pack) }
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

private fun openSource(uriHandler: androidx.compose.ui.platform.UriHandler, pack: RemoteCheatPack) {
    runCatching { uriHandler.openUri(pack.sourceUrl) }
}

@Composable
private fun CheatPackHeader(
    state: CheatPackBrowserState,
    expanded: Boolean,
    onRefresh: () -> Unit,
    onToggleExpanded: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Surface(
            shape = neonShape(12.dp),
            color = MaterialTheme.colorScheme.primaryContainer,
            contentColor = MaterialTheme.colorScheme.onPrimaryContainer
        ) {
            Icon(
                imageVector = Icons.Rounded.CloudDownload,
                contentDescription = null,
                modifier = Modifier.padding(8.dp).size(19.dp)
            )
        }
        Column(modifier = Modifier.weight(1f)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = stringResource(R.string.emulation_cheat_packs_title),
                    modifier = Modifier.weight(1f, fill = false),
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                AnimatedVisibility(
                    visible = state.phase == CheatPackBrowserPhase.Content &&
                        state.compatiblePacks.isNotEmpty()
                ) {
                    CheatPackCountBadge(count = state.compatiblePacks.size)
                }
            }
            Text(
                text = stringResource(R.string.emulation_cheat_packs_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = if (expanded) 2 else 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        IconButton(
            onClick = onRefresh,
            enabled = state.phase != CheatPackBrowserPhase.Loading && !state.isBusy,
            colors = IconButtonDefaults.iconButtonColors(
                contentColor = MaterialTheme.colorScheme.primary,
                disabledContentColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.35f)
            )
        ) {
            if (state.phase == CheatPackBrowserPhase.Loading) {
                CircularProgressIndicator(
                    modifier = Modifier.size(18.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
            } else {
                Icon(
                    imageVector = Icons.Rounded.Refresh,
                    contentDescription = stringResource(R.string.home_refresh),
                    modifier = Modifier.size(20.dp),
                    tint = LocalContentColor.current
                )
            }
        }
        IconButton(
            onClick = onToggleExpanded,
            colors = IconButtonDefaults.iconButtonColors(
                contentColor = MaterialTheme.colorScheme.onSurface
            )
        ) {
            Icon(
                imageVector = if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                contentDescription = stringResource(
                    if (expanded) R.string.emulation_cheat_packs_hide else R.string.emulation_cheat_packs_show
                ),
                modifier = Modifier.size(22.dp),
                tint = LocalContentColor.current
            )
        }
    }
}

@Composable
private fun CheatPackCountBadge(count: Int) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.16f),
        contentColor = MaterialTheme.colorScheme.primary
    ) {
        Text(
            text = count.toString(),
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold)
        )
    }
}

@Composable
private fun CheatPackIdentity(state: CheatPackBrowserState) {
    val text = when {
        state.serial.isNotEmpty() && state.crc.isNotEmpty() -> stringResource(
            R.string.cheat_manager_detected_identity,
            state.serial,
            state.crc
        )
        state.serial.isNotEmpty() -> stringResource(R.string.cheat_manager_crc_unavailable, state.serial)
        state.crc.isNotEmpty() -> "CRC ${state.crc}"
        else -> null
    } ?: return
    Text(
        text = text,
        style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 2,
        overflow = TextOverflow.Ellipsis
    )
}

@Composable
private fun CheatPackFeedbackLine(success: Boolean, text: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(
            imageVector = if (success) Icons.Rounded.CheckCircle else Icons.Rounded.ErrorOutline,
            contentDescription = null,
            tint = if (success) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
            modifier = Modifier.size(18.dp)
        )
        Text(
            text = text,
            style = MaterialTheme.typography.bodySmall,
            color = if (success) {
                MaterialTheme.colorScheme.onSurfaceVariant
            } else {
                MaterialTheme.colorScheme.error
            }
        )
    }
}

@Composable
private fun CheatPackLoadingSkeleton() {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        repeat(2) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(112.dp)
                    .clip(neonShape(16.dp))
                    .shimmer()
            )
        }
    }
}

@Composable
private fun CheatPackErrorCard(onRetry: () -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text(
                text = stringResource(R.string.content_catalog_failed),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            TextButton(onClick = onRetry, contentPadding = PaddingValues(0.dp)) {
                Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text(stringResource(R.string.home_refresh))
            }
        }
    }
}

@Composable
private fun CheatPackCard(
    pack: RemoteCheatPack,
    working: Boolean,
    installed: Boolean,
    failed: Boolean,
    busy: Boolean,
    onDownload: () -> Unit,
    onSource: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(animationSpec = tween(200)),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = pack.title,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                if (installed) {
                    InstalledStatusLabel()
                } else {
                    ContentCompatibilityBadge(compatible = true)
                }
            }
            Text(
                text = stringResource(R.string.cheat_manager_pack_meta, pack.crc, pack.blockCount),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (pack.serials.isNotEmpty()) {
                Text(
                    text = stringResource(R.string.content_supported_serials, pack.serials.joinToString()),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
            }
            if (pack.description.isNotBlank()) {
                Text(
                    text = pack.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis
                )
            }
            if (pack.authors.isNotEmpty()) {
                Text(
                    text = stringResource(R.string.content_authors, pack.authors.joinToString()),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = onDownload,
                    enabled = !busy,
                    shape = neonButtonShape(),
                    modifier = Modifier.weight(1f),
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp)
                ) {
                    AnimatedContent(
                        targetState = working,
                        transitionSpec = {
                            fadeIn(animationSpec = tween(160)) togetherWith fadeOut(animationSpec = tween(120))
                        },
                        label = "cheatPackWorkingState"
                    ) { isWorking ->
                        if (isWorking) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(16.dp),
                                    strokeWidth = 2.dp,
                                    color = LocalContentColor.current
                                )
                                Text(
                                    text = stringResource(R.string.emulation_cheat_packs_installing),
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        } else {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Icon(
                                    imageVector = if (installed) Icons.Rounded.Refresh else Icons.Rounded.CloudDownload,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp)
                                )
                                Text(
                                    text = stringResource(
                                        if (installed) R.string.content_reinstall else R.string.content_download_install
                                    ),
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                    }
                }
                OutlinedButton(
                    onClick = onSource,
                    enabled = !busy,
                    shape = neonButtonShape(),
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp)
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Rounded.OpenInNew,
                        contentDescription = stringResource(R.string.content_source),
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
            AnimatedVisibility(
                visible = failed,
                enter = expandVertically(animationSpec = tween(200)) + fadeIn(animationSpec = tween(160)),
                exit = shrinkVertically(animationSpec = tween(140)) + fadeOut(animationSpec = tween(120))
            ) {
                CheatPackFeedbackLine(
                    success = false,
                    text = stringResource(R.string.cheat_manager_download_failed)
                )
            }
        }
    }
}

@Composable
private fun CheatPackOtherVersions(
    packs: List<RemoteCheatPack>,
    expanded: Boolean,
    onToggle: () -> Unit,
    onSource: (RemoteCheatPack) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        TextButton(
            onClick = onToggle,
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 2.dp, vertical = 4.dp)
        ) {
            Text(
                text = stringResource(R.string.content_other_game_versions_section),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                textAlign = TextAlign.Start,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
            Spacer(Modifier.width(6.dp))
            Text(
                text = packs.size.toString(),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Icon(
                imageVector = if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                contentDescription = null,
                modifier = Modifier.size(20.dp)
            )
        }
        AnimatedVisibility(
            visible = expanded,
            enter = expandVertically(animationSpec = tween(240), expandFrom = Alignment.Top) +
                fadeIn(animationSpec = tween(180)),
            exit = shrinkVertically(animationSpec = tween(200), shrinkTowards = Alignment.Top) +
                fadeOut(animationSpec = tween(120))
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                packs.forEach { pack ->
                    CheatPackOtherVersionCard(pack = pack, onSource = { onSource(pack) })
                }
            }
        }
    }
}

@Composable
private fun CheatPackOtherVersionCard(pack: RemoteCheatPack, onSource: () -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = pack.title,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = stringResource(R.string.cheat_manager_pack_meta, pack.crc, pack.blockCount),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                ContentCompatibilityBadge(compatible = false)
            }
            IconButton(onClick = onSource) {
                Icon(
                    imageVector = Icons.AutoMirrored.Rounded.OpenInNew,
                    contentDescription = stringResource(R.string.content_source),
                    modifier = Modifier.size(20.dp)
                )
            }
        }
    }
}

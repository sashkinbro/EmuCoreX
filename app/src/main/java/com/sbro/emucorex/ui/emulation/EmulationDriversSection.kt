package com.sbro.emucorex.ui.emulation

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.material.icons.rounded.KeyboardArrowDown
import androidx.compose.material3.IconButton
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.draw.rotate
import com.sbro.emucorex.core.DeviceGpuInfoProvider
import java.util.Locale
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GpuDriverCompatibility
import com.sbro.emucorex.core.GpuDriverVendor
import com.sbro.emucorex.core.InstalledGpuDriver
import com.sbro.emucorex.core.RemoteGpuDriver
import com.sbro.emucorex.data.formatDownloadBytes
import com.sbro.emucorex.ui.common.SettingHelpButton
import com.sbro.emucorex.ui.settings.installedDriverName
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonChipShape
import com.sbro.emucorex.ui.theme.neon.neonShape

/**
 * In-game Drivers tab. Picking a driver persists it first (per game or app-wide, depending on
 * the selected scope) and then reloads the Vulkan device on a background coroutine.
 *
 * Every card is its own lazy item with a stable key, so applying a driver (which rewrites the
 * state) never rebuilds the list and the scroll position is kept.
 */
internal fun LazyListScope.emulationDriversItems(
    state: GpuDriverBrowserState,
    onScopeChange: (GpuDriverScope) -> Unit,
    onSelect: (String?) -> Unit,
    onUseGlobal: () -> Unit,
    onDownload: (RemoteGpuDriver) -> Unit,
    onImportClick: () -> Unit,
    onRefreshCatalog: () -> Unit
) {
    val scopeSelectedPath = if (state.activeScope == GpuDriverScope.Game) state.gamePath else state.globalPath
    val hasSelection = state.activeScope == GpuDriverScope.Global || state.gameOverride
    fun isSelected(path: String?) = hasSelection && path == scopeSelectedPath
    val effective = state.effectivePath
    val effectiveFailed = effective != null && effective in state.failedPaths

    item(key = "drivers:active", contentType = "drivers-card") {
        DriverActiveCard(state = state, onScopeChange = onScopeChange)
    }

    if (state.canUseGameScope && state.activeScope == GpuDriverScope.Game) {
        item(key = "drivers:global-link", contentType = "drivers-card") {
            DriverGlobalLinkCard(state = state, onUseGlobal = onUseGlobal)
        }
    }

    item(key = "drivers:installed-header", contentType = "drivers-header") {
        DriverSectionHeader(text = stringResource(R.string.settings_gpu_driver_installed))
    }

    item(key = "drivers:system", contentType = "drivers-card") {
        DriverInstalledCard(
            title = stringResource(R.string.emulation_drivers_system_driver),
            subtitle = deviceVendorName(),
            selected = isSelected(null),
            active = effective == null || effectiveFailed,
            failed = false,
            applying = state.applyingTarget?.path == null && state.applyingTarget != null,
            enabled = !state.busy,
            onClick = { onSelect(null) }
        )
    }

    items(state.installed, key = { "drivers:installed:${it.name}" }, contentType = { "drivers-card" }) { driver ->
        val identity = state.identityOf(driver)
        val failed = driver.mainLibraryPath in state.failedPaths
        DriverInstalledCard(
            title = identity.title,
            subtitle = driverSubtitle(driver.vendor, friendlyGpuDriverIdentity(driver.name).version),
            selected = isSelected(driver.mainLibraryPath),
            active = effective == driver.mainLibraryPath && !failed,
            failed = failed,
            applying = state.applyingTarget?.path == driver.mainLibraryPath,
            enabled = !state.busy,
            onClick = { onSelect(driver.mainLibraryPath) }
        )
    }

    item(key = "drivers:import", contentType = "drivers-card") {
        DriverImportCard(importing = state.importing, onImportClick = onImportClick)
    }

    item(key = "drivers:catalog-header", contentType = "drivers-header") {
        DriverSectionHeader(
            text = stringResource(R.string.settings_gpu_driver_catalog),
            action = {
                TextButton(onClick = onRefreshCatalog, enabled = !state.catalogLoading) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.settings_gpu_driver_refresh))
                }
            }
        )
    }

    when {
        state.catalogLoading && state.catalog.isEmpty() -> item(key = "drivers:catalog-state", contentType = "drivers-card") {
            DriverCardSurface {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(18.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_catalog_loading),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
        state.catalogFailed && state.catalog.isEmpty() -> item(key = "drivers:catalog-state", contentType = "drivers-card") {
            DriverCardSurface {
                Text(
                    text = stringResource(R.string.settings_gpu_driver_catalog_failed),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error
                )
            }
        }
        state.catalog.isEmpty() -> item(key = "drivers:catalog-state", contentType = "drivers-card") {
            DriverCardSurface {
                Text(
                    text = stringResource(R.string.settings_gpu_driver_catalog_empty),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        else -> items(state.catalog, key = { "drivers:catalog:${it.id}" }, contentType = { "drivers-card" }) { driver ->
            val installed = state.installed.firstOrNull { it.name == driver.installedDriverName() }
            val top = state.catalog.firstOrNull()?.id == driver.id &&
                (driver.recommended || state.catalog.size > 1)
            DriverCatalogCard(
                driver = driver,
                installed = installed,
                recommended = top,
                progress = state.downloads[driver.id],
                selected = installed != null && isSelected(installed.mainLibraryPath),
                failed = installed != null && installed.mainLibraryPath in state.failedPaths,
                applyingThis = installed != null && state.applyingTarget?.path == installed.mainLibraryPath,
                selectEnabled = !state.busy,
                onSelect = { installed?.let { onSelect(it.mainLibraryPath) } },
                onDownload = { onDownload(driver) }
            )
        }
    }
}

/** Opens the driver archive picker; the returned lambda is what the Import button calls. */
@Composable
internal fun rememberDriverImportLauncher(onImport: (Uri) -> Unit): () -> Unit {
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let(onImport)
    }
    return remember(launcher) {
        { launcher.launch(arrayOf("application/zip", "application/octet-stream", "*/*")) }
    }
}

/** Shows the latest apply/install result as a toast. Lives outside the lazy list so it never leaves composition. */
@Composable
internal fun DriverMessageEffect(state: GpuDriverBrowserState, onMessageShown: () -> Unit) {
    val context = LocalContext.current
    val messageText = when (state.message) {
        GpuDriverTabMessage.Applied -> stringResource(R.string.emulation_drivers_msg_applied)
        GpuDriverTabMessage.FellBackToSystem -> stringResource(R.string.emulation_drivers_msg_fell_back)
        GpuDriverTabMessage.StoredForVulkan -> stringResource(R.string.emulation_drivers_msg_stored)
        GpuDriverTabMessage.ApplyFailed -> stringResource(R.string.emulation_drivers_msg_failed)
        GpuDriverTabMessage.SavedGlobalGameKeeps -> stringResource(R.string.emulation_drivers_msg_saved_global)
        GpuDriverTabMessage.Installed ->
            stringResource(R.string.settings_gpu_driver_install_success, state.messageArg.orEmpty())
        GpuDriverTabMessage.InstallFailed -> stringResource(R.string.settings_gpu_driver_install_failed)
        null -> null
    }
    LaunchedEffect(state.messageNonce) {
        if (messageText != null) {
            Toast.makeText(context, messageText, Toast.LENGTH_LONG).show()
            onMessageShown()
        }
    }
}

@Composable
private fun deviceVendorName(): String? {
    val vendor = remember { runCatching { GpuDriverCompatibility.deviceVendor() }.getOrDefault(GpuDriverVendor.UNKNOWN) }
    return vendor.displayName.takeIf { vendor != GpuDriverVendor.UNKNOWN }
}

private fun driverSubtitle(vendor: GpuDriverVendor, version: String?): String? =
    listOfNotNull(
        vendor.displayName.takeIf { vendor != GpuDriverVendor.UNKNOWN },
        version?.takeIf { it.isNotBlank() }
    ).joinToString(" · ").ifBlank { null }

private fun GpuDriverBrowserState.driverLabel(path: String?, systemLabel: String): String {
    if (path == null) return systemLabel
    installed.firstOrNull { it.mainLibraryPath == path }?.let { return identityOf(it).title }
    val file = java.io.File(path)
    return friendlyGpuDriverIdentity(file.parentFile?.name?.takeIf { it.isNotBlank() } ?: file.name).title
}

// region Shared building blocks (same shapes, paddings and colors as the Shaders tab cards)

@Composable
private fun DriverCardSurface(
    modifier: Modifier = Modifier,
    selected: Boolean = false,
    onClick: (() -> Unit)? = null,
    enabled: Boolean = true,
    content: @Composable ColumnScope.() -> Unit
) {
    val color = if (selected) {
        MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    } else {
        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
    }
    val border = BorderStroke(
        1.dp,
        if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
        } else {
            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f)
        }
    )
    val body: @Composable () -> Unit = {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
            content = content
        )
    }
    if (onClick != null) {
        Surface(
            onClick = onClick,
            enabled = enabled,
            modifier = modifier.fillMaxWidth(),
            shape = neonShape(18.dp),
            color = color,
            border = border,
            content = body
        )
    } else {
        Surface(
            modifier = modifier.fillMaxWidth(),
            shape = neonShape(18.dp),
            color = color,
            border = border,
            content = body
        )
    }
}

@Composable
private fun DriverIconBox(icon: androidx.compose.ui.graphics.vector.ImageVector = Icons.Rounded.Memory) {
    Surface(
        shape = neonShape(12.dp),
        color = MaterialTheme.colorScheme.primaryContainer,
        contentColor = MaterialTheme.colorScheme.onPrimaryContainer
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.padding(8.dp).size(19.dp)
        )
    }
}

private enum class DriverBadgeTone { Primary, Error, Neutral, Success }

@Composable
private fun DriverBadge(text: String, tone: DriverBadgeTone) {
    val accent: Color = when (tone) {
        DriverBadgeTone.Success -> Color(0xFF3FB56B)
        DriverBadgeTone.Primary -> MaterialTheme.colorScheme.primary
        DriverBadgeTone.Error -> MaterialTheme.colorScheme.error
        DriverBadgeTone.Neutral -> MaterialTheme.colorScheme.onSurfaceVariant
    }
    Surface(
        shape = neonChipShape(),
        color = if (tone == DriverBadgeTone.Neutral) {
            MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
        } else {
            accent.copy(alpha = 0.16f)
        },
        border = BorderStroke(
            1.dp,
            if (tone == DriverBadgeTone.Neutral) {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            } else {
                accent.copy(alpha = 0.32f)
            }
        )
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
            color = accent
        )
    }
}

@Composable
private fun DriverSectionHeader(text: String, action: (@Composable () -> Unit)? = null) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(
            text = text,
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        action?.invoke()
    }
}

// endregion

@Composable
private fun DriverActiveCard(
    state: GpuDriverBrowserState,
    onScopeChange: (GpuDriverScope) -> Unit
) {
    val systemLabel = stringResource(R.string.emulation_drivers_system_driver)
    val effective = state.effectivePath
    val effectiveFailed = effective != null && effective in state.failedPaths
    val activePath = if (effectiveFailed) null else effective
    val activeLabel = state.driverLabel(activePath, systemLabel)
    val activeDriver = state.installed.firstOrNull { it.mainLibraryPath == activePath }
    val subtitle = if (activeDriver != null) {
        driverSubtitle(activeDriver.vendor, friendlyGpuDriverIdentity(activeDriver.name).version)
    } else {
        deviceVendorName()
    }
    val scopeHelp = stringResource(R.string.emulation_drivers_help_scope)

    DriverCardSurface {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(R.string.settings_gpu_driver_active_title),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            SettingHelpButton(
                title = stringResource(R.string.settings_gpu_driver_manager_title),
                description = scopeHelp
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            DriverIconBox()
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = activeLabel,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (!subtitle.isNullOrBlank()) {
                    Text(
                        text = subtitle,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            if (state.applying) {
                CircularProgressIndicator(
                    modifier = Modifier.size(18.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
        if (state.applying) {
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            Text(
                text = stringResource(R.string.emulation_drivers_applying),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (effectiveFailed) {
            Text(
                text = stringResource(R.string.emulation_drivers_msg_fell_back),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error
            )
        }

        if (state.canUseGameScope) {
            Text(
                text = stringResource(R.string.emulation_drivers_scope_label),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                ScopeChip(
                    text = stringResource(R.string.emulation_shaders_source_game),
                    selected = state.activeScope == GpuDriverScope.Game,
                    enabled = !state.busy,
                    modifier = Modifier.weight(1f),
                    onClick = { onScopeChange(GpuDriverScope.Game) }
                )
                ScopeChip(
                    text = stringResource(R.string.emulation_drivers_scope_global),
                    selected = state.activeScope == GpuDriverScope.Global,
                    enabled = !state.busy,
                    modifier = Modifier.weight(1f),
                    onClick = { onScopeChange(GpuDriverScope.Global) }
                )
            }
        }
    }
}

@Composable
private fun ScopeChip(
    text: String,
    selected: Boolean,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier,
        shape = neonChipShape(),
        color = if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
        } else {
            MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            }
        )
    ) {
        Text(
            text = text,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 7.dp),
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

/** Game scope only: the explanation on its own line and the reset button on its own full-width row. */
@Composable
private fun DriverGlobalLinkCard(state: GpuDriverBrowserState, onUseGlobal: () -> Unit) {
    val systemLabel = stringResource(R.string.emulation_drivers_system_driver)
    val globalLabel = state.driverLabel(state.globalPath, systemLabel)
    DriverCardSurface {
        if (state.gameOverride) {
            Text(
                text = stringResource(R.string.emulation_drivers_global_value, globalLabel),
                modifier = Modifier.fillMaxWidth(),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            OutlinedButton(
                onClick = onUseGlobal,
                enabled = !state.busy,
                shape = neonButtonShape(),
                modifier = Modifier.fillMaxWidth(),
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp)
            ) {
                Text(
                    text = stringResource(R.string.emulation_drivers_use_global),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        } else {
            Text(
                text = stringResource(R.string.emulation_drivers_inherits_global) + ": " + globalLabel,
                modifier = Modifier.fillMaxWidth(),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun DriverInstalledCard(
    title: String,
    subtitle: String?,
    selected: Boolean,
    active: Boolean,
    failed: Boolean,
    applying: Boolean,
    enabled: Boolean,
    onClick: () -> Unit
) {
    DriverCardSurface(selected = selected, onClick = onClick, enabled = enabled) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            DriverIconBox()
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (!subtitle.isNullOrBlank()) {
                    Text(
                        text = subtitle,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                if (active || failed) {
                    Row(
                        modifier = Modifier.padding(top = 2.dp),
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        if (active) {
                            DriverBadge(stringResource(R.string.emulation_drivers_active_badge), DriverBadgeTone.Primary)
                        }
                        if (failed) {
                            DriverBadge(stringResource(R.string.emulation_drivers_failed_badge), DriverBadgeTone.Error)
                        }
                    }
                }
            }
            when {
                applying -> CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
                selected -> Icon(
                    imageVector = Icons.Rounded.Check,
                    contentDescription = stringResource(R.string.settings_gpu_driver_applied),
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
        }
    }
}

@Composable
private fun DriverImportCard(importing: Boolean, onImportClick: () -> Unit) {
    DriverCardSurface {
        OutlinedButton(
            onClick = onImportClick,
            enabled = !importing,
            shape = neonButtonShape(),
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 10.dp)
        ) {
            if (importing) {
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
                text = stringResource(R.string.emulation_drivers_import),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun DriverCatalogCard(
    driver: RemoteGpuDriver,
    installed: InstalledGpuDriver?,
    recommended: Boolean,
    progress: Float?,
    selected: Boolean,
    failed: Boolean,
    applyingThis: Boolean,
    selectEnabled: Boolean,
    onSelect: () -> Unit,
    onDownload: () -> Unit
) {
    val family = driver.gpu.substringBefore('(').trim().ifBlank { driver.variant.trim() }
    val size = driver.sizeBytes?.takeIf { it > 0L }?.let { formatDownloadBytes(it) }
    val deviceGpuName = remember { GpuDriverCompatibility.currentDevice().gpuName.takeIf { it.isNotBlank() } }
    val userModelToken = remember {
        GpuDriverCompatibility.maliSx2ModelToken(DeviceGpuInfoProvider.snapshot.value?.model)
    }
    val models = driver.supportedGpuModels
    var expanded by rememberSaveable(driver.id) { mutableStateOf(false) }
    val chevronRotation by animateFloatAsState(if (expanded) 180f else 0f, label = "driverGpuChevron")

    DriverCardSurface(selected = selected) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.Top,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            DriverIconBox(Icons.Rounded.CloudDownload)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = driver.name,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (family.isNotBlank()) {
                    Text(
                        text = family,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                if (deviceGpuName != null) {
                    Box(modifier = Modifier.padding(top = 2.dp)) {
                        DriverBadge(
                            "✓ " + stringResource(R.string.emulation_drivers_supports_your, deviceGpuName),
                            DriverBadgeTone.Success
                        )
                    }
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .then(if (models.isNotEmpty()) Modifier.clickable { expanded = !expanded } else Modifier),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (size != null) {
                        Text(
                            text = size,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1
                        )
                    }
                    if (recommended) {
                        DriverBadge(stringResource(R.string.settings_gpu_driver_recommended), DriverBadgeTone.Primary)
                    }
                    if (failed) {
                        DriverBadge(stringResource(R.string.emulation_drivers_failed_badge), DriverBadgeTone.Error)
                    }
                    Spacer(Modifier.weight(1f))
                    if (models.isNotEmpty()) {
                        IconButton(onClick = { expanded = !expanded }, modifier = Modifier.size(28.dp)) {
                            Icon(
                                imageVector = Icons.Rounded.KeyboardArrowDown,
                                contentDescription = stringResource(
                                    if (expanded) R.string.emulation_drivers_hide_gpus else R.string.emulation_drivers_show_gpus
                                ),
                                modifier = Modifier.size(20.dp).rotate(chevronRotation),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
                AnimatedVisibility(
                    visible = expanded && models.isNotEmpty(),
                    enter = expandVertically() + fadeIn(),
                    exit = shrinkVertically() + fadeOut()
                ) {
                    Column(
                        modifier = Modifier.padding(top = 4.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.emulation_drivers_all_supported_gpus, models.size),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(4.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            models.forEach { model ->
                                val normalized = model.trim().uppercase(Locale.US)
                                val own = userModelToken != null &&
                                    (normalized == userModelToken || normalized == userModelToken + "AE")
                                DriverBadge(model, if (own) DriverBadgeTone.Primary else DriverBadgeTone.Neutral)
                            }
                        }
                    }
                }
            }
        }
        if (progress != null) {
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier.fillMaxWidth()
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.End
        ) {
            when {
                installed != null -> if (selected) {
                    Icon(
                        imageVector = Icons.Rounded.Check,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_applied),
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.primary
                    )
                } else {
                    OutlinedButton(
                        onClick = onSelect,
                        enabled = selectEnabled,
                        shape = neonButtonShape(),
                        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                    ) {
                        if (applyingThis) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                strokeWidth = 2.dp,
                                color = LocalContentColor.current
                            )
                            Spacer(Modifier.width(8.dp))
                        }
                        Text(stringResource(R.string.settings_gpu_driver_apply), maxLines = 1)
                    }
                }
                progress != null -> Text(
                    text = stringResource(R.string.content_downloading_percent, (progress * 100f).toInt()),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                else -> Button(
                    onClick = onDownload,
                    shape = neonButtonShape(),
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                ) {
                    Icon(Icons.Rounded.CloudDownload, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.emulation_drivers_download), maxLines = 1)
                }
            }
        }
    }
}

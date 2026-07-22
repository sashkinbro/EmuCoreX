package com.sbro.emucorex.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.AdrenoFamily
import com.sbro.emucorex.core.InstalledGpuDriver
import com.sbro.emucorex.core.GpuDriverRecommendations
import com.sbro.emucorex.core.GpuDriverMatch
import com.sbro.emucorex.core.RemoteGpuDriver
import com.sbro.emucorex.core.SnapdragonGpuProfile
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.skipGamepadTextFieldFocus
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding

@Composable
@OptIn(ExperimentalLayoutApi::class)
fun GpuDriverScreen(
    onBackClick: () -> Unit,
    selectedDriverPathOverride: String? = null,
    rendererOverride: Int? = null,
    onSelectDriverForGame: ((String?) -> Unit)? = null,
    viewModel: SettingsViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val perGameMode = onSelectDriverForGame != null
    val activeDriverPath = if (perGameMode) selectedDriverPathOverride else uiState.customDriverPath
    val activeRenderer = rendererOverride ?: uiState.renderer
    val selectedDriver = remember(uiState.installedGpuDrivers, activeDriverPath) {
        uiState.installedGpuDrivers.firstOrNull { it.mainLibraryPath == activeDriverPath }
    }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 16.dp
    val installFailedMessage = stringResource(R.string.settings_gpu_driver_install_failed)
    val installSuccessTemplate = stringResource(R.string.settings_gpu_driver_install_success, "%s")
    val backClick = rememberDebouncedClick(onClick = onBackClick)
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var searchVisible by rememberSaveable { mutableStateOf(false) }
    var filtersVisible by rememberSaveable { mutableStateOf(false) }
    var variantFilter by rememberSaveable { mutableStateOf(GPU_DRIVER_FILTER_ALL) }
    var sourceFilter by rememberSaveable { mutableStateOf(GPU_DRIVER_FILTER_ALL) }
    var expandedDriverId by rememberSaveable { mutableStateOf<String?>(null) }
    val deviceProfile = remember { GpuDriverRecommendations.currentDeviceProfile() }
    val remoteDrivers = uiState.remoteGpuDrivers
        .filter { driver ->
            driver.matchesSearch(searchQuery) &&
                (variantFilter == GPU_DRIVER_FILTER_ALL || driver.variant.equals(variantFilter, ignoreCase = true)) &&
                (sourceFilter == GPU_DRIVER_FILTER_ALL || driver.sourceLabel().equals(sourceFilter, ignoreCase = true))
        }
        .sortedByDescending { driver -> driver.recommendationRank(deviceProfile) }
    val variantFilters = buildList {
        add(GPU_DRIVER_FILTER_ALL)
        addAll(uiState.remoteGpuDrivers.map { it.variant }.filter { it.isNotBlank() }.distinct().sorted())
    }
    val sourceFilters = buildList {
        add(GPU_DRIVER_FILTER_ALL)
        addAll(uiState.remoteGpuDrivers.map { it.sourceLabel() }.distinct().sorted())
    }

    val localDriverPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.installGpuDriver(uri, activate = !perGameMode) { result ->
            result.onSuccess { driverName ->
                onSelectDriverForGame?.invoke(driverName)
                Toast.makeText(context, installSuccessTemplate.format(driverName), Toast.LENGTH_SHORT).show()
            }.onFailure { error ->
                Toast.makeText(context, error.message ?: installFailedMessage, Toast.LENGTH_LONG).show()
            }
        }
    }

    LaunchedEffect(Unit) {
        if (uiState.remoteGpuDrivers.isEmpty() && !uiState.gpuDriverCatalogLoading) {
            viewModel.refreshGpuDriverCatalog()
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            top = topInset,
            end = ScreenHorizontalPadding,
            bottom = 40.dp
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            GpuDriverHeader(
                title = stringResource(R.string.settings_gpu_driver_manager_title),
                onBackClick = backClick,
                searchActive = searchVisible || searchQuery.isNotBlank(),
                filtersActive = variantFilter != GPU_DRIVER_FILTER_ALL || sourceFilter != GPU_DRIVER_FILTER_ALL,
                onSearchClick = { searchVisible = !searchVisible },
                onFiltersClick = { filtersVisible = !filtersVisible }
            )
        }
        item {
            ActiveDriverCard(
                selectedDriver = selectedDriver,
                renderer = activeRenderer,
                onUseSystem = {
                    onSelectDriverForGame?.invoke(null) ?: viewModel.useSystemGpuDriver()
                },
                onInstallFromFile = { localDriverPicker.launch(arrayOf("application/zip", "*/*")) },
                onRemove = if (perGameMode) null else {
                    { selectedDriver?.let { viewModel.removeGpuDriver(it.name) } }
                }
            )
        }
        deviceProfile?.let { profile ->
            item {
                DeviceCompatibilityCard(profile = profile)
            }
        }
        if (uiState.installedGpuDrivers.isNotEmpty()) {
            item {
                SectionLabel(text = stringResource(R.string.settings_gpu_driver_installed))
            }
            items(uiState.installedGpuDrivers, key = { it.name }) { driver ->
                InstalledDriverRow(
                    driver = driver,
                    selected = driver.mainLibraryPath == activeDriverPath,
                    onSelect = {
                        onSelectDriverForGame?.invoke(driver.name) ?: viewModel.selectGpuDriver(driver.name)
                    },
                    onRemove = if (perGameMode) null else {
                        { viewModel.removeGpuDriver(driver.name) }
                    }
                )
            }
        }
        item {
            AnimatedVisibility(
                visible = searchVisible,
                enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(220)),
                exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(180))
            ) {
                DriverSearchField(
                    searchQuery = searchQuery,
                    onSearchQueryChange = { searchQuery = it },
                    onClose = {
                        searchQuery = ""
                        searchVisible = false
                    }
                )
            }
        }
        item {
            AnimatedVisibility(
                visible = filtersVisible && uiState.remoteGpuDrivers.isNotEmpty(),
                enter = fadeIn(animationSpec = tween(180)) + expandVertically(animationSpec = tween(220)),
                exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(animationSpec = tween(180))
            ) {
                DriverFilterPanel(
                    variantFilter = variantFilter,
                    onVariantFilterChange = { variantFilter = it },
                    sourceFilter = sourceFilter,
                    onSourceFilterChange = { sourceFilter = it },
                    variantFilters = variantFilters,
                    sourceFilters = sourceFilters
                )
            }
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                SectionLabel(
                    text = stringResource(R.string.settings_gpu_driver_catalog),
                    modifier = Modifier.weight(1f)
                )
                OutlinedButton(
                    onClick = viewModel::refreshGpuDriverCatalog,
                    enabled = !uiState.gpuDriverCatalogLoading
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_refresh),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
        }
        if (uiState.gpuDriverCatalogLoading) {
            item {
                LoadingCard(text = stringResource(R.string.settings_gpu_driver_catalog_loading))
            }
        }
        uiState.gpuDriverCatalogError?.let { error ->
            item {
                ErrorCard(
                    title = stringResource(R.string.settings_gpu_driver_catalog_failed),
                    message = error
                )
            }
        }
        if (!uiState.gpuDriverCatalogLoading && uiState.remoteGpuDrivers.isEmpty() && uiState.gpuDriverCatalogError == null) {
            item {
                LoadingCard(text = stringResource(R.string.settings_gpu_driver_catalog_empty))
            }
        }
        if (!uiState.gpuDriverCatalogLoading && uiState.remoteGpuDrivers.isNotEmpty() && remoteDrivers.isEmpty()) {
            item {
                ErrorCard(
                    title = stringResource(R.string.settings_gpu_driver_no_matches),
                    message = stringResource(R.string.settings_gpu_driver_no_matches_desc)
                )
            }
        }
        items(remoteDrivers, key = { it.id }) { driver ->
            val installedDriver = uiState.installedGpuDrivers.firstOrNull { it.name == driver.installedDriverName() }
            val downloadingProgress = uiState.gpuDriverDownloads[driver.id]
            RemoteDriverRow(
                driver = driver,
                match = GpuDriverRecommendations.match(driver, deviceProfile),
                expanded = expandedDriverId == driver.id,
                installedDriver = installedDriver,
                selected = installedDriver?.mainLibraryPath == activeDriverPath,
                downloading = downloadingProgress != null,
                progress = downloadingProgress ?: 0f,
                onToggleExpanded = {
                    expandedDriverId = if (expandedDriverId == driver.id) null else driver.id
                },
                onDownload = {
                    viewModel.installRemoteGpuDriver(driver, activate = !perGameMode) { result ->
                        result.onSuccess { driverName ->
                            onSelectDriverForGame?.invoke(driverName)
                            Toast.makeText(context, installSuccessTemplate.format(driverName), Toast.LENGTH_SHORT).show()
                        }.onFailure { error ->
                            Toast.makeText(context, error.message ?: installFailedMessage, Toast.LENGTH_LONG).show()
                        }
                    }
                },
                onSelect = {
                    installedDriver?.let { selected ->
                        onSelectDriverForGame?.invoke(selected.name) ?: viewModel.selectGpuDriver(selected.name)
                    }
                },
                onRemove = if (perGameMode) null else {
                    { installedDriver?.let { viewModel.removeGpuDriver(it.name) } }
                }
            )
        }
    }
}

@Composable
private fun DriverSearchField(
    searchQuery: String,
    onSearchQueryChange: (String) -> Unit,
    onClose: () -> Unit
) {
    OutlinedTextField(
        value = searchQuery,
        onValueChange = onSearchQueryChange,
        modifier = Modifier
            .fillMaxWidth()
            .skipGamepadTextFieldFocus(),
        singleLine = true,
        leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
        trailingIcon = {
            IconButton(onClick = onClose) {
                Icon(Icons.Rounded.Close, contentDescription = null)
            }
        },
        placeholder = { Text(stringResource(R.string.settings_gpu_driver_search)) },
        shape = RoundedCornerShape(28.dp)
    )
}

@Composable
private fun DriverFilterPanel(
    variantFilter: String,
    onVariantFilterChange: (String) -> Unit,
    sourceFilter: String,
    onSourceFilterChange: (String) -> Unit,
    variantFilters: List<String>,
    sourceFilters: List<String>
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(modifier = Modifier.padding(vertical = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            FilterChipRow(
                filters = variantFilters,
                selected = variantFilter,
                onSelected = onVariantFilterChange
            )
            FilterChipRow(
                filters = sourceFilters,
                selected = sourceFilter,
                onSelected = onSourceFilterChange
            )
        }
    }
}

@Composable
private fun FilterChipRow(
    filters: List<String>,
    selected: String,
    onSelected: (String) -> Unit
) {
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = PaddingValues(horizontal = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(filters) { filter ->
            FilterChip(
                selected = selected == filter,
                onClick = { onSelected(filter) },
                label = { Text(if (filter == GPU_DRIVER_FILTER_ALL) stringResource(R.string.settings_gpu_driver_filter_all) else filter) }
            )
        }
    }
}

@Composable
private fun GpuDriverHeader(
    title: String,
    onBackClick: () -> Unit,
    searchActive: Boolean,
    filtersActive: Boolean,
    onSearchClick: () -> Unit,
    onFiltersClick: () -> Unit
) {
    ScreenTopBar(
        title = title,
        onBackClick = onBackClick,
        backContentColor = MaterialTheme.colorScheme.onSurface,
        actions = {
        HeaderIconButton(
            selected = searchActive,
            onClick = onSearchClick
        ) {
            Icon(
                imageVector = Icons.Rounded.Search,
                contentDescription = null,
                modifier = Modifier.size(18.dp)
            )
        }
        HeaderIconButton(
            selected = filtersActive,
            onClick = onFiltersClick
        ) {
            Icon(
                imageVector = Icons.Rounded.MoreVert,
                contentDescription = null,
                modifier = Modifier.size(18.dp)
            )
        }
        }
    )
}

@Composable
private fun HeaderIconButton(
    selected: Boolean,
    onClick: () -> Unit,
    content: @Composable () -> Unit
) {
    Surface(
        modifier = Modifier.size(40.dp),
        shape = RoundedCornerShape(14.dp),
        color = if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface,
        contentColor = if (selected) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurface,
        tonalElevation = 3.dp,
        shadowElevation = 5.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            content()
        }
    }
}

@Composable
private fun ActiveDriverCard(
    selectedDriver: InstalledGpuDriver?,
    renderer: Int,
    onUseSystem: () -> Unit,
    onInstallFromFile: () -> Unit,
    onRemove: (() -> Unit)?
) {
    val isActive = renderer == VULKAN_RENDERER && selectedDriver?.isUsable == true
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 3.dp,
        shadowElevation = 8.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(
                text = stringResource(R.string.settings_gpu_driver_active_title),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            DriverStatusText(
                selectedDriver = selectedDriver,
                renderer = renderer,
                isActive = isActive
            )
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = selectedDriver == null,
                    onClick = onUseSystem,
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.settings_gpu_driver_system)) }
                )
                Button(
                    onClick = onInstallFromFile,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.FolderZip, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_install_from_file),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
            if (selectedDriver != null && onRemove != null) {
                OutlinedButton(
                    onClick = onRemove,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.Delete, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_remove),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun DriverStatusText(
    selectedDriver: InstalledGpuDriver?,
    renderer: Int,
    isActive: Boolean
) {
    val rendererLabel = when (renderer) {
        VULKAN_RENDERER -> stringResource(R.string.settings_renderer_vulkan)
        SOFTWARE_RENDERER -> stringResource(R.string.settings_renderer_software)
        else -> stringResource(R.string.settings_renderer_opengl)
    }
    val text = when {
        selectedDriver == null -> stringResource(R.string.settings_gpu_driver_status_system)
        !selectedDriver.isUsable -> stringResource(R.string.settings_gpu_driver_status_broken, selectedDriver.name)
        renderer != VULKAN_RENDERER -> stringResource(R.string.settings_gpu_driver_status_renderer, rendererLabel)
        else -> stringResource(R.string.settings_gpu_driver_status_active, selectedDriver.name)
    }
    val supporting = when {
        selectedDriver == null -> stringResource(R.string.settings_gpu_driver_status_system_desc)
        !selectedDriver.isUsable -> stringResource(R.string.settings_gpu_driver_status_broken_desc)
        !isActive -> stringResource(R.string.settings_gpu_driver_status_renderer_desc)
        else -> stringResource(R.string.settings_gpu_driver_status_active_desc)
    }
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
        color = MaterialTheme.colorScheme.onSurface
    )
    Text(
        text = supporting,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
    )
}

@Composable
private fun InstalledDriverRow(
    driver: InstalledGpuDriver,
    selected: Boolean,
    onSelect: () -> Unit,
    onRemove: (() -> Unit)?
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = if (selected) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f)),
        onClick = onSelect
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = driver.name,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = if (selected) MaterialTheme.colorScheme.onSecondaryContainer else MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = driver.mainLibrary,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            if (onRemove != null) {
                OutlinedButton(onClick = onRemove) {
                    Icon(Icons.Rounded.Delete, contentDescription = null, modifier = Modifier.size(18.dp))
                }
            }
        }
    }
}

@Composable
private fun RemoteDriverRow(
    driver: RemoteGpuDriver,
    match: GpuDriverMatch,
    expanded: Boolean,
    installedDriver: InstalledGpuDriver?,
    selected: Boolean,
    downloading: Boolean,
    progress: Float,
    onToggleExpanded: () -> Unit,
    onDownload: () -> Unit,
    onSelect: () -> Unit,
    onRemove: (() -> Unit)?
) {
    val uriHandler = LocalUriHandler.current
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onToggleExpanded),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Row(verticalAlignment = Alignment.Top) {
                    Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(
                            text = driver.name,
                            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        DriverBadgeRow(
                            recommended = driver.recommended,
                            downloaded = installedDriver != null,
                            match = match
                        )
                        Text(
                            text = listOf(driver.gpu, driver.variant).filter { it.isNotBlank() }.joinToString(" / "),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Icon(
                        imageVector = if (expanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                        contentDescription = stringResource(R.string.settings_gpu_driver_compatibility),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (installedDriver == null) {
                    Button(
                        onClick = onDownload,
                        enabled = !downloading,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.CloudDownload, contentDescription = null, modifier = Modifier.size(18.dp))
                        Text(
                            text = stringResource(R.string.settings_gpu_driver_download_apply),
                            modifier = Modifier.padding(start = 6.dp)
                        )
                    }
                } else {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        CompactOutlinedActionButton(
                            onClick = onSelect,
                            enabled = !selected,
                            modifier = Modifier.weight(1f),
                            text = if (selected) {
                                stringResource(R.string.settings_gpu_driver_applied)
                            } else {
                                stringResource(R.string.settings_gpu_driver_apply)
                            }
                        )
                        if (onRemove != null) {
                            CompactOutlinedActionButton(
                                onClick = onRemove,
                                modifier = Modifier.weight(1f),
                                icon = { Icon(Icons.Rounded.Delete, contentDescription = null, modifier = Modifier.size(18.dp)) },
                                text = stringResource(R.string.settings_gpu_driver_remove_short)
                            )
                        }
                    }
                }
            }
            AnimatedVisibility(
                visible = expanded,
                enter = fadeIn(animationSpec = tween(160)) + expandVertically(animationSpec = tween(200)),
                exit = fadeOut(animationSpec = tween(100)) + shrinkVertically(animationSpec = tween(160))
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (driver.description.isNotBlank()) {
                        Text(
                            text = driver.description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Text(
                        text = stringResource(R.string.settings_gpu_driver_compatibility),
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = driver.snapdragonSummary(),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    if (driver.credits.isNotBlank() || driver.sourceUrl.isNotBlank()) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                text = driver.credits,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.weight(1f)
                            )
                            if (driver.sourceUrl.isNotBlank()) {
                                CompactOutlinedActionButton(
                                    onClick = { uriHandler.openUri(driver.sourceUrl) },
                                    icon = { Icon(Icons.Rounded.Link, contentDescription = null, modifier = Modifier.size(18.dp)) },
                                    text = stringResource(R.string.settings_gpu_driver_source)
                                )
                            }
                        }
                    }
                }
            }
            if (downloading) {
                LinearProgressIndicator(
                    progress = { progress.coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}

@Composable
private fun DeviceCompatibilityCard(profile: SnapdragonGpuProfile) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.55f)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = stringResource(
                    R.string.settings_gpu_driver_detected_device,
                    profile.socName,
                    profile.adrenoName
                ),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onPrimaryContainer
            )
            Text(
                text = stringResource(R.string.settings_gpu_driver_device_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.78f)
            )
        }
    }
}

@Composable
private fun CompactOutlinedActionButton(
    onClick: () -> Unit,
    text: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    icon: (@Composable () -> Unit)? = null
) {
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.height(40.dp),
        shape = RoundedCornerShape(14.dp),
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f)),
        colors = ButtonDefaults.outlinedButtonColors(
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
            disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.45f)
        )
    ) {
        if (icon != null) {
            icon()
            Spacer(modifier = Modifier.width(6.dp))
        }
        Text(
            text = text,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun DriverBadgeRow(
    recommended: Boolean,
    downloaded: Boolean,
    match: GpuDriverMatch = GpuDriverMatch.UNKNOWN
) {
    if (!recommended && !downloaded && match == GpuDriverMatch.UNKNOWN) return
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        if (match == GpuDriverMatch.COMPATIBLE) {
            DriverBadge(
                text = stringResource(
                    if (recommended) R.string.settings_gpu_driver_best_match
                    else R.string.settings_gpu_driver_compatible
                ),
                color = MaterialTheme.colorScheme.primary
            )
        } else if (match == GpuDriverMatch.OTHER_FAMILY) {
            DriverBadge(
                text = stringResource(R.string.settings_gpu_driver_other_family),
                color = MaterialTheme.colorScheme.error
            )
        } else if (recommended) {
            DriverBadge(
                text = stringResource(R.string.settings_gpu_driver_recommended),
                color = MaterialTheme.colorScheme.primary
            )
        }
        if (downloaded) {
            DriverBadge(
                text = stringResource(R.string.settings_gpu_driver_downloaded),
                color = MaterialTheme.colorScheme.tertiary
            )
        }
    }
}

private fun RemoteGpuDriver.recommendationRank(profile: SnapdragonGpuProfile?): Int =
    when (GpuDriverRecommendations.match(this, profile)) {
        GpuDriverMatch.COMPATIBLE -> if (recommended) 3 else 2
        GpuDriverMatch.UNKNOWN -> if (recommended) 1 else 0
        GpuDriverMatch.OTHER_FAMILY -> -1
    }

@Composable
private fun RemoteGpuDriver.snapdragonSummary(): String {
    val lines = GpuDriverRecommendations.supportedFamilies(gpu)
        .sortedBy(AdrenoFamily::ordinal)
        .map { family ->
            stringResource(
                when (family) {
                    AdrenoFamily.A6XX -> R.string.settings_gpu_driver_compatibility_a6xx
                    AdrenoFamily.A7XX -> R.string.settings_gpu_driver_compatibility_a7xx
                    AdrenoFamily.A8XX -> R.string.settings_gpu_driver_compatibility_a8xx
                }
            )
        }
    return lines.joinToString(separator = "\n").ifBlank { gpu }
}

@Composable
private fun DriverBadge(text: String, color: androidx.compose.ui.graphics.Color) {
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = color.copy(alpha = 0.12f)
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
            color = color,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 4.dp)
        )
    }
}

private fun RemoteGpuDriver.installedDriverName(): String {
    val rawName = downloadUrl.substringBefore('?').substringAfterLast('/').ifBlank { "$id.zip" }
    val safeName = rawName.replace(Regex("[^A-Za-z0-9._-]"), "_")
    val archiveName = if (safeName.endsWith(".zip", ignoreCase = true)) safeName else "$safeName.zip"
    return archiveName.substringBeforeLast('.').ifBlank { id }
}

@Composable
private fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
        color = MaterialTheme.colorScheme.onBackground,
        modifier = modifier
    )
}

@Composable
private fun LoadingCard(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(text = text, color = MaterialTheme.colorScheme.onSurface)
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun ErrorCard(title: String, message: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.errorContainer
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onErrorContainer
            )
            Text(
                text = message,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onErrorContainer,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

private const val GPU_DRIVER_FILTER_ALL = "all"
private const val VULKAN_RENDERER = 14
private const val SOFTWARE_RENDERER = 13

private fun RemoteGpuDriver.matchesSearch(query: String): Boolean {
    val normalized = query.trim()
    if (normalized.isBlank()) return true
    return listOf(id, name, variant, gpu, description, credits, sourceLabel())
        .any { value -> value.contains(normalized, ignoreCase = true) }
}

private fun RemoteGpuDriver.sourceLabel(): String {
    return when {
        credits.contains("nihui", ignoreCase = true) -> "nihui"
        credits.contains("K11MCH1", ignoreCase = true) -> "K11MCH1"
        else -> credits.substringBefore('/').trim().ifBlank { "Source" }
    }
}

package com.sbro.emucorex.navigation

import android.annotation.SuppressLint
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Home
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material3.DrawerState
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadUiActions
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import kotlinx.coroutines.launch

enum class PrimaryDestination {
    Home, Search, Formats, Achievements, Settings
}

private enum class MobileLeadingAction {
    Drawer,
    Back
}

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun AdaptiveShell(
    selected: PrimaryDestination,
    onNavigateHome: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateFormats: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateSaveManager: (() -> Unit)? = null,
    onBackClick: (() -> Unit)? = null,
    onOpenManageFolders: (() -> Unit)? = null,
    onLaunchBios: (() -> Unit)? = null,
    content: @Composable ((() -> Unit)?) -> Unit
) {
    val navContent: @Composable () -> Unit = {
        SideNavigation(
            selected = selected,
            onNavigateHome = onNavigateHome,
            onNavigateSearch = onNavigateSearch,
            onNavigateFormats = onNavigateFormats,
            onNavigateSettings = onNavigateSettings,
            onNavigateAchievements = onNavigateAchievements,
            onNavigateSaveManager = onNavigateSaveManager,
            onOpenManageFolders = onOpenManageFolders,
            onLaunchBios = onLaunchBios,
            onCloseDrawer = { }
        )
    }
    val configuration = LocalConfiguration.current
    val isWide = configuration.screenWidthDp >= 900

    if (isWide) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .width(320.dp)
                    .padding(start = 12.dp, top = 12.dp, bottom = 12.dp)
            ) {
                navContent()
            }
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
            ) {
                content(null)
            }
        }
    } else {
        CompactAdaptiveShell(
            selected = selected,
            onNavigateHome = onNavigateHome,
            onNavigateSearch = onNavigateSearch,
            onNavigateFormats = onNavigateFormats,
            onNavigateSettings = onNavigateSettings,
            onNavigateAchievements = onNavigateAchievements,
            onNavigateSaveManager = onNavigateSaveManager,
            onBackClick = onBackClick,
            onOpenManageFolders = onOpenManageFolders,
            onLaunchBios = onLaunchBios,
            content = content
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun CompactAdaptiveShell(
    selected: PrimaryDestination,
    onNavigateHome: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateFormats: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateSaveManager: (() -> Unit)?,
    onBackClick: (() -> Unit)?,
    onOpenManageFolders: (() -> Unit)?,
    onLaunchBios: (() -> Unit)?,
    content: @Composable ((() -> Unit)?) -> Unit
) {
    val configuration = LocalConfiguration.current
    val statusPadding = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val isLandscapeCompact = configuration.screenWidthDp > configuration.screenHeightDp
    val drawerWidthFraction = if (isLandscapeCompact) 0.54f else 0.74f
    val selectedDrawerItemFocusRequester = remember { FocusRequester() }
    val drawerState = remember { DrawerState(DrawerValue.Closed) }
    val scope = rememberCoroutineScope()

    val mobileLeadingAction = if (
        selected != PrimaryDestination.Home &&
        onBackClick != null
    ) {
        MobileLeadingAction.Back
    } else {
        MobileLeadingAction.Drawer
    }
    val leadingActionClick = when (mobileLeadingAction) {
        MobileLeadingAction.Drawer -> rememberDebouncedClick {
            scope.launch {
                if (drawerState.isClosed) drawerState.open() else drawerState.close()
            }
        }
        MobileLeadingAction.Back -> {
            { onBackClick?.invoke(); Unit }
        }
    }

    LaunchedEffect(selected, mobileLeadingAction) {
        drawerState.close()
    }
    LaunchedEffect(drawerState.isOpen, mobileLeadingAction, selected) {
        if (drawerState.isOpen && mobileLeadingAction == MobileLeadingAction.Drawer) {
            selectedDrawerItemFocusRequester.requestFocus()
        }
    }
    DisposableEffect(mobileLeadingAction, drawerState) {
        if (mobileLeadingAction == MobileLeadingAction.Drawer) {
            GamepadUiActions.setToggleDrawerAction {
                scope.launch {
                    if (drawerState.isClosed) drawerState.open() else drawerState.close()
                }
            }
        } else {
            GamepadUiActions.setToggleDrawerAction(null)
        }
        onDispose {
            GamepadUiActions.setToggleDrawerAction(null)
        }
    }
    BackHandler(enabled = drawerState.isOpen) {
        scope.launch { drawerState.close() }
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        gesturesEnabled = mobileLeadingAction == MobileLeadingAction.Drawer,
        scrimColor = MaterialTheme.colorScheme.scrim.copy(alpha = 0.42f),
        drawerContent = {
            ModalDrawerSheet(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(drawerWidthFraction)
                    .widthIn(min = 292.dp, max = 360.dp),
                drawerShape = RoundedCornerShape(topEnd = 30.dp, bottomEnd = 30.dp),
                drawerContainerColor = MaterialTheme.colorScheme.surface,
                drawerContentColor = MaterialTheme.colorScheme.onSurface,
                drawerTonalElevation = 6.dp,
                windowInsets = WindowInsets(0, 0, 0, 0)
            ) {
                SideNavigation(
                    selected = selected,
                    onNavigateHome = onNavigateHome,
                    onNavigateSearch = onNavigateSearch,
                    onNavigateFormats = onNavigateFormats,
                    onNavigateSettings = onNavigateSettings,
                    onNavigateAchievements = onNavigateAchievements,
                    onNavigateSaveManager = onNavigateSaveManager,
                    onOpenManageFolders = onOpenManageFolders,
                    onLaunchBios = onLaunchBios,
                    selectedItemFocusRequester = selectedDrawerItemFocusRequester,
                    wrapInSurface = false,
                    topInset = statusPadding,
                    onCloseDrawer = { scope.launch { drawerState.close() } }
                )
            }
        }
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            content(
                if (mobileLeadingAction == MobileLeadingAction.Drawer) {
                    leadingActionClick
                } else {
                    null
                }
            )
            if (mobileLeadingAction == MobileLeadingAction.Drawer && selected != PrimaryDestination.Home) {
                Surface(
                    modifier = Modifier
                        .padding(top = statusPadding + 12.dp, start = 12.dp),
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.surface.copy(alpha = 0.85f),
                    tonalElevation = 4.dp,
                    shadowElevation = 6.dp,
                    onClick = leadingActionClick
                ) {
                    Box(
                        modifier = Modifier.padding(horizontal = 11.dp, vertical = 10.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Menu,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SideNavigation(
    selected: PrimaryDestination,
    onNavigateHome: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateFormats: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateSaveManager: (() -> Unit)?,
    onOpenManageFolders: (() -> Unit)?,
    onLaunchBios: (() -> Unit)?,
    selectedItemFocusRequester: FocusRequester? = null,
    wrapInSurface: Boolean = true,
    topInset: androidx.compose.ui.unit.Dp = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
    onCloseDrawer: () -> Unit
) {
    val drawerInset = 18.dp
    val drawerSectionSpacing = 14.dp

    val navigateHome = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateHome()
    }
    val navigateSettings = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateSettings()
    }
    val navigateAchievements = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateAchievements()
    }
    val navigateSaveManager = onNavigateSaveManager?.let {
        rememberDebouncedClick {
            onCloseDrawer()
            it()
        }
    }
    val navigateFormats = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateFormats()
    }
    val navigateSearch = rememberDebouncedClick {
        onCloseDrawer()
        onNavigateSearch()
    }
    val openManageFolders = onOpenManageFolders?.let {
        rememberDebouncedClick {
            onCloseDrawer()
            it()
        }
    }
    val launchBios = onLaunchBios?.let {
        rememberDebouncedClick {
            onCloseDrawer()
            it()
        }
    }

    val content: @Composable () -> Unit = {
        Column(
            modifier = Modifier
                .fillMaxHeight()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = drawerInset, vertical = drawerInset),
            verticalArrangement = Arrangement.spacedBy(drawerSectionSpacing)
        ) {
            Text(
                text = stringResource(R.string.app_name),
                style = MaterialTheme.typography.headlineSmall,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(top = topInset + 4.dp, start = 6.dp, end = 6.dp)
            )
            Text(
                text = stringResource(R.string.shell_quick_actions),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 4.dp)
            )
            ShellItem(
                icon = Icons.Rounded.Home,
                label = stringResource(R.string.shell_library),
                selected = selected == PrimaryDestination.Home,
                modifier = if (selected == PrimaryDestination.Home && selectedItemFocusRequester != null) {
                    Modifier.focusRequester(selectedItemFocusRequester)
                } else Modifier,
                onClick = navigateHome
            )
            ShellItem(
                icon = Icons.Rounded.Search,
                label = stringResource(R.string.shell_catalog_search),
                selected = selected == PrimaryDestination.Search,
                modifier = if (selected == PrimaryDestination.Search && selectedItemFocusRequester != null) {
                    Modifier.focusRequester(selectedItemFocusRequester)
                } else Modifier,
                onClick = navigateSearch
            )
            ShellItem(
                icon = Icons.Rounded.Memory,
                label = stringResource(R.string.shell_supported_formats),
                selected = selected == PrimaryDestination.Formats,
                modifier = if (selected == PrimaryDestination.Formats && selectedItemFocusRequester != null) {
                    Modifier.focusRequester(selectedItemFocusRequester)
                } else Modifier,
                onClick = navigateFormats
            )
            ShellItem(
                icon = Icons.Rounded.Star,
                label = stringResource(R.string.settings_achievements_tab),
                selected = selected == PrimaryDestination.Achievements,
                modifier = if (selected == PrimaryDestination.Achievements && selectedItemFocusRequester != null) {
                    Modifier.focusRequester(selectedItemFocusRequester)
                } else Modifier,
                onClick = navigateAchievements
            )
            ShellItem(
                icon = Icons.Rounded.Settings,
                label = stringResource(R.string.nav_settings),
                selected = selected == PrimaryDestination.Settings,
                modifier = if (selected == PrimaryDestination.Settings && selectedItemFocusRequester != null) {
                    Modifier.focusRequester(selectedItemFocusRequester)
                } else Modifier,
                onClick = navigateSettings
            )
            if (launchBios != null) {
                ShellAction(
                    icon = Icons.Rounded.PlayArrow,
                    label = stringResource(R.string.shell_launch_bios),
                    onClick = launchBios
                )
            }
            HorizontalDivider(
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            )
            Column(
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = stringResource(R.string.settings_paths_tab),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                if (openManageFolders != null) {
                    ShellAction(
                        icon = Icons.Rounded.FolderOpen,
                        label = stringResource(R.string.shell_manage_folders),
                        onClick = openManageFolders
                    )
                }
                if (navigateSaveManager != null) {
                    ShellAction(
                        icon = Icons.Rounded.Save,
                        label = stringResource(R.string.shell_save_states),
                        onClick = navigateSaveManager
                    )
                }
            }
        }
    }

    if (wrapInSurface) {
        Surface(
            modifier = Modifier.fillMaxHeight(),
            shape = RoundedCornerShape(30.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 2.dp
        ) {
            content()
        }
    } else {
        content()
    }
}

@Composable
private fun ShellAction(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    label: String,
    onClick: () -> Unit
) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .focusable(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(18.dp)
            )
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(start = 12.dp)
            )
        }
    }
}

@Composable
private fun ShellItem(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    label: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .focusable(),
        shape = RoundedCornerShape(18.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
        },
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .clip(RoundedCornerShape(18.dp))
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(20.dp)
            )
            Text(
                text = label,
                style = MaterialTheme.typography.titleMedium,
                color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(start = 12.dp)
            )
        }
    }
}

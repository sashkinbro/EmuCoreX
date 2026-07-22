package com.sbro.emucorex.navigation

import android.annotation.SuppressLint
import android.view.KeyEvent as AndroidKeyEvent
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
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
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Home
import androidx.compose.material.icons.rounded.FolderZip
import androidx.compose.material.icons.rounded.Forum
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.RateReview
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.SwapVert
import androidx.compose.material.icons.rounded.Tune
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.DrawerState
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.InputMode
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.LocalTvUiEnvironment
import com.sbro.emucorex.core.TvUiMetrics
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.DrawerItemId
import com.sbro.emucorex.data.DrawerVisualStyle
import com.sbro.emucorex.ui.common.ProvideGamepadMenuAction
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.milliseconds

enum class PrimaryDestination {
    Home, Search, Formats, Achievements, Profile, Settings, Feedback
}

private enum class MobileLeadingAction {
    Drawer,
    Back
}

internal fun shouldUseCompactModalDrawer(
    drawerEnabled: Boolean,
    selected: PrimaryDestination,
    hasBackClick: Boolean
): Boolean = drawerEnabled && (selected == PrimaryDestination.Home || !hasBackClick)

internal fun shouldEnableDrawerInteraction(
    useModalDrawer: Boolean,
    destinationSettled: Boolean
): Boolean = useModalDrawer && destinationSettled

private const val DRAWER_DESTINATION_SETTLE_MS = 220L

private val LocalDrawerVisualStyle = staticCompositionLocalOf { DrawerVisualStyle.CLASSIC }

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun AdaptiveShell(
    selected: PrimaryDestination,
    isProUnlocked: Boolean = false,
    drawerEnabled: Boolean = true,
    onNavigateHome: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateFormats: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateProfile: (() -> Unit)? = null,
    onNavigateFeedback: (() -> Unit)? = null,
    onNavigateGameSettingsManager: (() -> Unit)? = null,
    onNavigateDataTransfer: (() -> Unit)? = null,
    onResetAllSettings: (() -> Unit)? = null,
    onNavigateSaveManager: (() -> Unit)? = null,
    onNavigateMemoryCardManager: (() -> Unit)? = null,
    onNavigateTextureManager: (() -> Unit)? = null,
    onNavigateCheatManager: (() -> Unit)? = null,
    onBackClick: (() -> Unit)? = null,
    onLaunchGame: (() -> Unit)? = null,
    onLaunchBios: (() -> Unit)? = null,
    content: @Composable ((() -> Unit)?) -> Unit
) {
    val context = LocalContext.current
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled
    val preferences = remember(context) { AppPreferences(context) }
    val hiddenDrawerItems by preferences.hiddenDrawerItems.collectAsState(initial = emptySet())
    val drawerVisualStyle by preferences.drawerVisualStyle.collectAsState(initial = DrawerVisualStyle.CLASSIC)
    val tvNavigationFocusRequester = remember { FocusRequester() }
    LaunchedEffect(tvUiEnabled, selected) {
        if (tvUiEnabled) {
            withFrameNanos { }
            runCatching { tvNavigationFocusRequester.requestFocus() }
        }
    }
    val navContent: @Composable () -> Unit = {
        SideNavigation(
            selected = selected,
            isProUnlocked = isProUnlocked,
            hiddenDrawerItems = hiddenDrawerItems,
            drawerVisualStyle = drawerVisualStyle,
            onNavigateHome = onNavigateHome,
            onNavigateSearch = onNavigateSearch,
            onNavigateFormats = onNavigateFormats,
            onNavigateSettings = onNavigateSettings,
            onNavigateAchievements = onNavigateAchievements,
            onNavigateProfile = onNavigateProfile,
            onNavigateFeedback = onNavigateFeedback,
            onNavigateGameSettingsManager = onNavigateGameSettingsManager,
            onNavigateDataTransfer = onNavigateDataTransfer,
            onResetAllSettings = onResetAllSettings,
            onNavigateSaveManager = onNavigateSaveManager,
            onNavigateMemoryCardManager = onNavigateMemoryCardManager,
            onNavigateTextureManager = onNavigateTextureManager,
            onNavigateCheatManager = onNavigateCheatManager,
            onLaunchGame = onLaunchGame,
            onLaunchBios = onLaunchBios,
            selectedItemFocusRequester = if (tvUiEnabled) tvNavigationFocusRequester else null,
            topInset = if (tvUiEnabled) 0.dp else WindowInsets.statusBarsIgnoringVisibility
                .asPaddingValues()
                .calculateTopPadding(),
            onCloseDrawer = { }
        )
    }
    val configuration = LocalConfiguration.current
    val isTabletClass = configuration.smallestScreenWidthDp >= 600
    val isWide = tvUiEnabled || (isTabletClass && configuration.screenWidthDp >= 900)
    val tvSafeHorizontal = TvUiMetrics.safeHorizontalDp(configuration.screenWidthDp).dp
    val tvSafeVertical = TvUiMetrics.safeVerticalDp(configuration.screenHeightDp).dp
    val tvNavigationWidth = TvUiMetrics.navigationWidthDp(configuration.screenWidthDp).dp

    if (isWide) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .then(
                    if (tvUiEnabled) {
                        Modifier.padding(horizontal = tvSafeHorizontal, vertical = tvSafeVertical)
                    } else {
                        Modifier
                    }
                )
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .width(
                        when {
                            tvUiEnabled -> tvNavigationWidth
                            else -> when (drawerVisualStyle) {
                            DrawerVisualStyle.COMPACT -> 272.dp
                            DrawerVisualStyle.CONSOLE -> 348.dp
                            else -> 320.dp
                        }
                        }
                    )
                    .padding(
                        start = if (tvUiEnabled) 0.dp else 12.dp,
                        end = if (tvUiEnabled) 16.dp else 0.dp,
                        top = if (tvUiEnabled) 0.dp else 12.dp,
                        bottom = if (tvUiEnabled) 0.dp else 12.dp
                    )
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
            isProUnlocked = isProUnlocked,
            hiddenDrawerItems = hiddenDrawerItems,
            drawerVisualStyle = drawerVisualStyle,
            drawerEnabled = drawerEnabled,
            onNavigateHome = onNavigateHome,
            onNavigateSearch = onNavigateSearch,
            onNavigateFormats = onNavigateFormats,
            onNavigateSettings = onNavigateSettings,
            onNavigateAchievements = onNavigateAchievements,
            onNavigateProfile = onNavigateProfile,
            onNavigateFeedback = onNavigateFeedback,
            onNavigateGameSettingsManager = onNavigateGameSettingsManager,
            onNavigateDataTransfer = onNavigateDataTransfer,
            onResetAllSettings = onResetAllSettings,
            onNavigateSaveManager = onNavigateSaveManager,
            onNavigateMemoryCardManager = onNavigateMemoryCardManager,
            onNavigateTextureManager = onNavigateTextureManager,
            onNavigateCheatManager = onNavigateCheatManager,
            onBackClick = onBackClick,
            onLaunchGame = onLaunchGame,
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
    isProUnlocked: Boolean,
    hiddenDrawerItems: Set<DrawerItemId>,
    drawerVisualStyle: DrawerVisualStyle,
    drawerEnabled: Boolean,
    onNavigateHome: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateFormats: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateProfile: (() -> Unit)?,
    onNavigateFeedback: (() -> Unit)?,
    onNavigateGameSettingsManager: (() -> Unit)?,
    onNavigateDataTransfer: (() -> Unit)?,
    onResetAllSettings: (() -> Unit)?,
    onNavigateSaveManager: (() -> Unit)?,
    onNavigateMemoryCardManager: (() -> Unit)?,
    onNavigateTextureManager: (() -> Unit)?,
    onNavigateCheatManager: (() -> Unit)?,
    onBackClick: (() -> Unit)?,
    onLaunchGame: (() -> Unit)?,
    onLaunchBios: (() -> Unit)?,
    content: @Composable ((() -> Unit)?) -> Unit
) {
    val configuration = LocalConfiguration.current
    val statusPadding = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val isTabletClass = configuration.smallestScreenWidthDp >= 600
    val isLandscapeCompact = configuration.screenWidthDp > configuration.screenHeightDp
    val drawerWidthFraction = when {
        drawerVisualStyle == DrawerVisualStyle.COMPACT && isLandscapeCompact -> 0.40f
        drawerVisualStyle == DrawerVisualStyle.COMPACT -> 0.66f
        drawerVisualStyle == DrawerVisualStyle.CONSOLE && isLandscapeCompact -> 0.58f
        drawerVisualStyle == DrawerVisualStyle.CONSOLE -> 0.82f
        isLandscapeCompact && isTabletClass -> 0.54f
        isLandscapeCompact -> 0.46f
        else -> 0.74f
    }
    val selectedDrawerItemFocusRequester = remember { FocusRequester() }
    // A drawer should never be restored as open after a configuration change.
    // This is especially important for back-only destinations such as Feedback and Settings.
    val drawerState = remember(selected) { DrawerState(initialValue = DrawerValue.Closed) }
    var destinationSettled by remember(selected) { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val inputModeManager = LocalInputModeManager.current
    val drawerScrollState = rememberScrollState()

    val useModalDrawer = shouldUseCompactModalDrawer(
        drawerEnabled = drawerEnabled,
        selected = selected,
        hasBackClick = onBackClick != null
    )
    val mobileLeadingAction = if (useModalDrawer) {
        MobileLeadingAction.Drawer
    } else {
        MobileLeadingAction.Back
    }
    val drawerInteractionEnabled = shouldEnableDrawerInteraction(
        useModalDrawer = useModalDrawer,
        destinationSettled = destinationSettled
    )
    val leadingActionClick = when (mobileLeadingAction) {
        MobileLeadingAction.Drawer -> rememberDebouncedClick {
            if (!drawerInteractionEnabled) return@rememberDebouncedClick
            scope.launch {
                if (drawerState.isClosed) drawerState.open() else drawerState.close()
            }
        }
        MobileLeadingAction.Back -> {
            { onBackClick?.invoke(); Unit }
        }
    }

    LaunchedEffect(
        selected,
        mobileLeadingAction,
        configuration.screenWidthDp,
        configuration.screenHeightDp
    ) {
        destinationSettled = false
        drawerState.snapTo(DrawerValue.Closed)
        // Consume pointer/controller events that belonged to the disappearing game surface.
        withFrameNanos { }
        withFrameNanos { }
        delay(DRAWER_DESTINATION_SETTLE_MS.milliseconds)
        destinationSettled = true
    }
    LaunchedEffect(drawerState.isOpen, mobileLeadingAction, selected) {
        if (drawerState.isOpen && mobileLeadingAction == MobileLeadingAction.Drawer) {
            if (GamepadManager.isGamepadConnected() || inputModeManager.inputMode == InputMode.Keyboard) {
                delay(40.milliseconds)
                selectedDrawerItemFocusRequester.requestFocus()
            }
        }
    }
    ProvideGamepadMenuAction(
        enabled = mobileLeadingAction == MobileLeadingAction.Drawer && drawerInteractionEnabled,
        onMenu = {
            scope.launch {
                if (drawerState.isClosed) drawerState.open() else drawerState.close()
            }
        }
    )
    BackHandler(enabled = drawerState.isOpen) {
        scope.launch { drawerState.close() }
    }

    val screenContent: @Composable () -> Unit = {
        Box(modifier = Modifier.fillMaxSize()) {
            content(if (useModalDrawer) leadingActionClick else null)
            if (useModalDrawer && selected != PrimaryDestination.Home) {
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

    if (!useModalDrawer) {
        screenContent()
        return
    }

    ModalNavigationDrawer(
        modifier = Modifier.testTag("adaptive_shell_modal_drawer"),
        drawerState = drawerState,
        gesturesEnabled = drawerInteractionEnabled,
        scrimColor = MaterialTheme.colorScheme.scrim.copy(alpha = 0.42f),
        drawerContent = {
            ModalDrawerSheet(
                modifier = Modifier
                    .testTag("adaptive_shell_drawer_sheet")
                    .fillMaxHeight()
                    .fillMaxWidth(drawerWidthFraction)
                    .widthIn(min = 292.dp, max = if (isTabletClass) 360.dp else 320.dp),
                drawerShape = when (drawerVisualStyle) {
                    DrawerVisualStyle.COMPACT -> RoundedCornerShape(topEnd = 12.dp, bottomEnd = 12.dp)
                    DrawerVisualStyle.GLASS -> RoundedCornerShape(topEnd = 38.dp, bottomEnd = 38.dp)
                    DrawerVisualStyle.CONSOLE -> RoundedCornerShape(0.dp)
                    DrawerVisualStyle.CLASSIC -> RoundedCornerShape(topEnd = 30.dp, bottomEnd = 30.dp)
                },
                drawerContainerColor = when (drawerVisualStyle) {
                    DrawerVisualStyle.GLASS -> MaterialTheme.colorScheme.surface.copy(alpha = 0.88f)
                    DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.surfaceContainerLowest
                    else -> MaterialTheme.colorScheme.surface
                },
                drawerContentColor = MaterialTheme.colorScheme.onSurface,
                drawerTonalElevation = 6.dp,
                windowInsets = WindowInsets(0, 0, 0, 0)
            ) {
                SideNavigation(
                    selected = selected,
                    isProUnlocked = isProUnlocked,
                    hiddenDrawerItems = hiddenDrawerItems,
                    drawerVisualStyle = drawerVisualStyle,
                    onNavigateHome = onNavigateHome,
                    onNavigateSearch = onNavigateSearch,
                    onNavigateFormats = onNavigateFormats,
                    onNavigateSettings = onNavigateSettings,
                    onNavigateAchievements = onNavigateAchievements,
                    onNavigateProfile = onNavigateProfile,
                    onNavigateFeedback = onNavigateFeedback,
                    onNavigateGameSettingsManager = onNavigateGameSettingsManager,
                    onNavigateDataTransfer = onNavigateDataTransfer,
                    onResetAllSettings = onResetAllSettings,
                    onNavigateSaveManager = onNavigateSaveManager,
                    onNavigateMemoryCardManager = onNavigateMemoryCardManager,
                    onNavigateTextureManager = onNavigateTextureManager,
                    onNavigateCheatManager = onNavigateCheatManager,
                    onLaunchGame = onLaunchGame,
                    onLaunchBios = onLaunchBios,
                    selectedItemFocusRequester = selectedDrawerItemFocusRequester,
                    wrapInSurface = false,
                    topInset = statusPadding,
                    scrollState = drawerScrollState,
                    onCloseDrawer = { drawerState.close() }
                )
            }
        }
    ) { screenContent() }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SideNavigation(
    selected: PrimaryDestination,
    isProUnlocked: Boolean,
    hiddenDrawerItems: Set<DrawerItemId>,
    drawerVisualStyle: DrawerVisualStyle,
    onNavigateHome: () -> Unit,
    onNavigateSearch: () -> Unit,
    onNavigateFormats: () -> Unit,
    onNavigateSettings: () -> Unit,
    onNavigateAchievements: () -> Unit,
    onNavigateProfile: (() -> Unit)?,
    onNavigateFeedback: (() -> Unit)?,
    onNavigateGameSettingsManager: (() -> Unit)?,
    onNavigateDataTransfer: (() -> Unit)?,
    onResetAllSettings: (() -> Unit)?,
    onNavigateSaveManager: (() -> Unit)?,
    onNavigateMemoryCardManager: (() -> Unit)?,
    onNavigateTextureManager: (() -> Unit)?,
    onNavigateCheatManager: (() -> Unit)?,
    onLaunchGame: (() -> Unit)?,
    onLaunchBios: (() -> Unit)?,
    selectedItemFocusRequester: FocusRequester? = null,
    wrapInSurface: Boolean = true,
    topInset: androidx.compose.ui.unit.Dp = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
    scrollState: ScrollState = rememberScrollState(),
    onCloseDrawer: suspend () -> Unit
) {
    val drawerInset = when (drawerVisualStyle) {
        DrawerVisualStyle.COMPACT -> 12.dp
        DrawerVisualStyle.CONSOLE -> 22.dp
        else -> 18.dp
    }
    val drawerSectionSpacing = when (drawerVisualStyle) {
        DrawerVisualStyle.COMPACT -> 8.dp
        DrawerVisualStyle.CONSOLE -> 18.dp
        else -> 14.dp
    }
    val drawerBottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val scope = rememberCoroutineScope()
    val uriHandler = LocalUriHandler.current
    var showResetDialog by remember { mutableStateOf(false) }
    val closeDrawerThen: (() -> Unit) -> Unit = { action ->
        scope.launch {
            onCloseDrawer()
            action()
        }
    }

    val navigateHome = rememberDebouncedClick {
        closeDrawerThen(onNavigateHome)
    }
    val navigateSettings = rememberDebouncedClick {
        closeDrawerThen(onNavigateSettings)
    }
    val navigateAchievements = rememberDebouncedClick {
        closeDrawerThen(onNavigateAchievements)
    }
    val navigateProfile = onNavigateProfile?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateFeedback = onNavigateFeedback?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateGameSettingsManager = onNavigateGameSettingsManager?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateDataTransfer = onNavigateDataTransfer?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val resetAllSettings = onResetAllSettings?.let {
        rememberDebouncedClick {
            closeDrawerThen { showResetDialog = true }
        }
    }
    val navigateSaveManager = onNavigateSaveManager?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateMemoryCardManager = onNavigateMemoryCardManager?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateTextureManager = onNavigateTextureManager?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateCheatManager = onNavigateCheatManager?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val navigateFormats = rememberDebouncedClick {
        closeDrawerThen(onNavigateFormats)
    }
    val navigateSearch = rememberDebouncedClick {
        closeDrawerThen(onNavigateSearch)
    }
    val launchGame = onLaunchGame?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val launchBios = onLaunchBios?.let {
        rememberDebouncedClick {
            closeDrawerThen(it)
        }
    }
    val openDiscord = rememberDebouncedClick {
        closeDrawerThen {
            runCatching { uriHandler.openUri(DISCORD_INVITE_URL) }
        }
    }
    val showExecutables =
        (launchGame != null && DrawerItemId.LAUNCH_GAME !in hiddenDrawerItems) ||
            (launchBios != null && DrawerItemId.LAUNCH_BIOS !in hiddenDrawerItems)
    val showAppActions =
        (navigateGameSettingsManager != null && DrawerItemId.GAME_SETTINGS !in hiddenDrawerItems) ||
            (navigateDataTransfer != null && DrawerItemId.DATA_TRANSFER !in hiddenDrawerItems) ||
            (resetAllSettings != null && DrawerItemId.RESET_SETTINGS !in hiddenDrawerItems)
    val showTools =
        (navigateMemoryCardManager != null && DrawerItemId.MEMORY_CARDS !in hiddenDrawerItems) ||
            (navigateTextureManager != null && DrawerItemId.TEXTURE_MANAGER !in hiddenDrawerItems) ||
            (navigateCheatManager != null && DrawerItemId.CHEAT_MANAGER !in hiddenDrawerItems) ||
            (navigateSaveManager != null && DrawerItemId.SAVE_STATES !in hiddenDrawerItems)

    val content: @Composable () -> Unit = {
        Column(
            modifier = Modifier
                .fillMaxHeight()
                .verticalScroll(scrollState)
                .padding(
                    start = drawerInset,
                    end = drawerInset,
                    top = drawerInset,
                    bottom = drawerInset + drawerBottomInset
                ),
            verticalArrangement = Arrangement.spacedBy(drawerSectionSpacing)
        ) {
            Row(
                modifier = Modifier.padding(top = topInset + 4.dp, start = 6.dp, end = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Image(
                    painter = painterResource(
                        if (isProUnlocked) R.drawable.ic_drawer_app_pro else R.drawable.ic_drawer_app
                    ),
                    contentDescription = null,
                    modifier = Modifier
                        .size(52.dp)
                        .clip(CircleShape)
                )
                Text(
                    text = stringResource(R.string.app_name),
                    style = MaterialTheme.typography.headlineSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 14.dp)
                )
            }
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
            if (DrawerItemId.CATALOG_SEARCH !in hiddenDrawerItems) {
                ShellItem(
                    icon = Icons.Rounded.Search,
                    label = stringResource(R.string.shell_catalog_search),
                    selected = selected == PrimaryDestination.Search,
                    modifier = if (selected == PrimaryDestination.Search && selectedItemFocusRequester != null) {
                        Modifier.focusRequester(selectedItemFocusRequester)
                    } else Modifier,
                    onClick = navigateSearch
                )
            }
            if (DrawerItemId.ACHIEVEMENTS !in hiddenDrawerItems) {
                ShellItem(
                    icon = Icons.Rounded.Star,
                    label = stringResource(R.string.settings_achievements_tab),
                    selected = selected == PrimaryDestination.Achievements,
                    modifier = if (selected == PrimaryDestination.Achievements && selectedItemFocusRequester != null) {
                        Modifier.focusRequester(selectedItemFocusRequester)
                    } else Modifier,
                    onClick = navigateAchievements
                )
            }
            if (navigateProfile != null && DrawerItemId.PROFILE !in hiddenDrawerItems) {
                ShellItem(
                    icon = Icons.Rounded.Person,
                    label = stringResource(R.string.profile_title),
                    selected = selected == PrimaryDestination.Profile,
                    modifier = if (selected == PrimaryDestination.Profile && selectedItemFocusRequester != null) {
                        Modifier.focusRequester(selectedItemFocusRequester)
                    } else Modifier,
                    onClick = navigateProfile
                )
            }

            if (showExecutables) {
            HorizontalDivider(
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            )
            Column(
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = stringResource(R.string.shell_executables_section),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                if (launchGame != null && DrawerItemId.LAUNCH_GAME !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.PlayArrow,
                        label = stringResource(R.string.shell_launch_game),
                        onClick = launchGame
                    )
                }
                if (launchBios != null && DrawerItemId.LAUNCH_BIOS !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.PlayArrow,
                        label = stringResource(R.string.shell_launch_bios),
                        onClick = launchBios
                    )
                }
            }
            }
            if (showAppActions) {
            HorizontalDivider(
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            )
            Column(
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = stringResource(R.string.shell_app_section),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                if (navigateGameSettingsManager != null && DrawerItemId.GAME_SETTINGS !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.Tune,
                        label = stringResource(R.string.shell_game_settings_manager),
                        onClick = navigateGameSettingsManager
                    )
                }
                if (navigateDataTransfer != null && DrawerItemId.DATA_TRANSFER !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.SwapVert,
                        label = stringResource(R.string.shell_data_transfer),
                        onClick = navigateDataTransfer
                    )
                }
                if (resetAllSettings != null && DrawerItemId.RESET_SETTINGS !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.Refresh,
                        label = stringResource(R.string.settings_reset_all_action),
                        onClick = resetAllSettings
                    )
                }
            }
            }
            if (showTools) {
            HorizontalDivider(
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            )
            Column(
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = stringResource(R.string.shell_tools_section),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(horizontal = 4.dp)
                )
                if (navigateMemoryCardManager != null && DrawerItemId.MEMORY_CARDS !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.Memory,
                        label = stringResource(R.string.shell_memory_cards),
                        onClick = navigateMemoryCardManager
                    )
                }
                if (navigateTextureManager != null && DrawerItemId.TEXTURE_MANAGER !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.FolderZip,
                        label = stringResource(R.string.shell_texture_manager),
                        onClick = navigateTextureManager
                    )
                }
                if (navigateCheatManager != null && DrawerItemId.CHEAT_MANAGER !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.Gamepad,
                        label = stringResource(R.string.shell_cheat_manager),
                        onClick = navigateCheatManager
                    )
                }
                if (navigateSaveManager != null && DrawerItemId.SAVE_STATES !in hiddenDrawerItems) {
                    ShellAction(
                        icon = Icons.Rounded.Save,
                        label = stringResource(R.string.shell_save_states),
                        onClick = navigateSaveManager
                    )
                }
            }
            }
            HorizontalDivider(
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            )
            ShellItem(
                icon = Icons.Rounded.Settings,
                label = stringResource(R.string.shell_app_settings),
                selected = selected == PrimaryDestination.Settings,
                modifier = if (selected == PrimaryDestination.Settings && selectedItemFocusRequester != null) {
                    Modifier.focusRequester(selectedItemFocusRequester)
                } else Modifier,
                onClick = navigateSettings
            )
            if (DrawerItemId.SUPPORTED_FORMATS !in hiddenDrawerItems) {
                ShellItem(
                    icon = Icons.Rounded.Memory,
                    label = stringResource(R.string.shell_supported_formats),
                    selected = selected == PrimaryDestination.Formats,
                    modifier = if (selected == PrimaryDestination.Formats && selectedItemFocusRequester != null) {
                        Modifier.focusRequester(selectedItemFocusRequester)
                    } else Modifier,
                    onClick = navigateFormats
                )
            }
            if (navigateFeedback != null && DrawerItemId.FEEDBACK !in hiddenDrawerItems) {
                ShellItem(
                    icon = Icons.Rounded.RateReview,
                    label = stringResource(R.string.feedback_title),
                    selected = selected == PrimaryDestination.Feedback,
                    modifier = if (selected == PrimaryDestination.Feedback && selectedItemFocusRequester != null) {
                        Modifier.focusRequester(selectedItemFocusRequester)
                    } else Modifier,
                    onClick = navigateFeedback
                )
            }
            if (DrawerItemId.DISCORD !in hiddenDrawerItems) {
                HorizontalDivider(
                    thickness = 1.dp,
                    color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                )
                ShellAction(
                    icon = Icons.Rounded.Forum,
                    label = stringResource(R.string.shell_discord_server),
                    onClick = openDiscord
                )
            }
        }

        if (showResetDialog && onResetAllSettings != null) {
            AlertDialog(
                onDismissRequest = { showResetDialog = false },
                title = {
                    Text(stringResource(R.string.settings_reset_all_title))
                },
                text = {
                    Text(stringResource(R.string.settings_reset_all_confirm))
                },
                confirmButton = {
                    TextButton(
                        onClick = {
                            showResetDialog = false
                            onResetAllSettings()
                        }
                    ) {
                        Text(stringResource(R.string.settings_reset_all_action))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { showResetDialog = false }) {
                        Text(stringResource(R.string.cancel))
                    }
                }
            )
        }
    }

    CompositionLocalProvider(LocalDrawerVisualStyle provides drawerVisualStyle) {
        if (wrapInSurface) {
            Surface(
                modifier = Modifier.fillMaxHeight(),
                shape = when (drawerVisualStyle) {
                    DrawerVisualStyle.COMPACT -> RoundedCornerShape(14.dp)
                    DrawerVisualStyle.GLASS -> RoundedCornerShape(36.dp)
                    DrawerVisualStyle.CONSOLE -> RoundedCornerShape(8.dp)
                    DrawerVisualStyle.CLASSIC -> RoundedCornerShape(30.dp)
                },
                color = when (drawerVisualStyle) {
                    DrawerVisualStyle.GLASS -> MaterialTheme.colorScheme.surface.copy(alpha = 0.86f)
                    DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.surfaceContainerLowest
                    else -> MaterialTheme.colorScheme.surface
                },
                tonalElevation = if (drawerVisualStyle == DrawerVisualStyle.COMPACT) 0.dp else 2.dp
            ) {
                content()
            }
        } else {
            content()
        }
    }
}

@Composable
private fun ShellAction(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    label: String,
    onClick: () -> Unit
) {
    val style = LocalDrawerVisualStyle.current
    val shape = when (style) {
        DrawerVisualStyle.COMPACT -> RoundedCornerShape(9.dp)
        DrawerVisualStyle.GLASS -> RoundedCornerShape(22.dp)
        DrawerVisualStyle.CONSOLE -> RoundedCornerShape(6.dp)
        DrawerVisualStyle.CLASSIC -> RoundedCornerShape(18.dp)
    }
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    val border = if (isFocused) {
        BorderStroke(2.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.95f))
    } else {
        BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    }
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .activateDrawerItemOnGamepad(onClick)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
            .focusable(interactionSource = interactionSource),
        shape = shape,
        color = when (style) {
            DrawerVisualStyle.COMPACT -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.08f)
            DrawerVisualStyle.GLASS -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f)
            DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.surfaceContainerHigh
            DrawerVisualStyle.CLASSIC -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
        },
        border = border
    ) {
        Row(
            modifier = Modifier.padding(
                horizontal = if (style == DrawerVisualStyle.COMPACT) 12.dp else 16.dp,
                vertical = when (style) {
                    DrawerVisualStyle.COMPACT -> 10.dp
                    DrawerVisualStyle.CONSOLE -> 16.dp
                    else -> 14.dp
                }
            ),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(if (style == DrawerVisualStyle.COMPACT) 17.dp else 18.dp)
            )
            Text(
                text = label,
                style = if (style == DrawerVisualStyle.CONSOLE) {
                    MaterialTheme.typography.titleMedium
                } else {
                    MaterialTheme.typography.labelLarge
                },
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 12.dp)
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
    val style = LocalDrawerVisualStyle.current
    val shape = when (style) {
        DrawerVisualStyle.COMPACT -> RoundedCornerShape(9.dp)
        DrawerVisualStyle.GLASS -> RoundedCornerShape(22.dp)
        DrawerVisualStyle.CONSOLE -> RoundedCornerShape(6.dp)
        DrawerVisualStyle.CLASSIC -> RoundedCornerShape(18.dp)
    }
    val interactionSource = remember { MutableInteractionSource() }
    val isFocused by interactionSource.collectIsFocusedAsState()
    val border = if (isFocused) {
        BorderStroke(2.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.95f))
    } else {
        BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
    }
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .activateDrawerItemOnGamepad(onClick)
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
            .focusable(interactionSource = interactionSource),
        shape = shape,
        color = when {
            selected && style == DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.primaryContainer
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = if (style == DrawerVisualStyle.GLASS) 0.22f else 0.16f)
            style == DrawerVisualStyle.COMPACT -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.06f)
            style == DrawerVisualStyle.GLASS -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.26f)
            style == DrawerVisualStyle.CONSOLE -> MaterialTheme.colorScheme.surfaceContainerHigh
            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.18f)
        },
        border = border
    ) {
        Row(
            modifier = Modifier
                .clip(shape)
                .padding(
                    horizontal = if (style == DrawerVisualStyle.COMPACT) 12.dp else 16.dp,
                    vertical = when (style) {
                        DrawerVisualStyle.COMPACT -> 10.dp
                        DrawerVisualStyle.CONSOLE -> 16.dp
                        else -> 14.dp
                    }
                ),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(if (style == DrawerVisualStyle.CONSOLE) 22.dp else 20.dp)
            )
            Text(
                text = label,
                style = when (style) {
                    DrawerVisualStyle.COMPACT -> MaterialTheme.typography.labelLarge
                    DrawerVisualStyle.CONSOLE -> MaterialTheme.typography.titleMedium
                    else -> MaterialTheme.typography.titleMedium
                },
                color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 12.dp)
            )
        }
    }
}

private fun Modifier.activateDrawerItemOnGamepad(onClick: () -> Unit): Modifier {
    return onPreviewKeyEvent { keyEvent ->
        val nativeEvent = keyEvent.nativeKeyEvent
        if (nativeEvent.action != AndroidKeyEvent.ACTION_DOWN || nativeEvent.repeatCount != 0) {
            return@onPreviewKeyEvent false
        }
        when (nativeEvent.keyCode) {
            AndroidKeyEvent.KEYCODE_BUTTON_A,
            AndroidKeyEvent.KEYCODE_BUTTON_1,
            AndroidKeyEvent.KEYCODE_DPAD_CENTER,
            AndroidKeyEvent.KEYCODE_ENTER,
            AndroidKeyEvent.KEYCODE_NUMPAD_ENTER -> {
                onClick()
                true
            }
            else -> false
        }
    }
}

private const val DISCORD_INVITE_URL = "https://discord.gg/82hhArvYwC"

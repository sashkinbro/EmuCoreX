package com.sbro.emucorex.navigation

import android.net.Uri
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.EaseIn
import androidx.compose.animation.core.EaseOut
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.toRoute
import com.sbro.emucorex.R
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.GameLaunchShortcut
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.core.StorageAccess
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.SaveStateRepository
import com.sbro.emucorex.ui.achievements.AccountUnlockedAchievementsScreen
import com.sbro.emucorex.ui.achievements.AchievementsHubScreen
import com.sbro.emucorex.ui.achievements.GameAchievementsScreen
import com.sbro.emucorex.ui.catalog.CatalogSearchScreen
import com.sbro.emucorex.ui.detail.GameDetailScreen
import com.sbro.emucorex.ui.emulation.EmulationScreen
import com.sbro.emucorex.ui.formats.SupportedFormatsScreen
import com.sbro.emucorex.ui.feedback.FeedbackScreen
import com.sbro.emucorex.ui.gamedb.GameDbBrowserScreen
import com.sbro.emucorex.ui.home.HomeScreen
import com.sbro.emucorex.ui.memorycards.MemoryCardManagerScreen
import com.sbro.emucorex.ui.onboarding.OnboardingScreen
import com.sbro.emucorex.ui.profile.ProfileScreen
import com.sbro.emucorex.ui.saves.SaveManagerScreen
import com.sbro.emucorex.ui.settings.LanguageSettingsScreen
import com.sbro.emucorex.ui.settings.ControlsLayoutEditorHostScreen
import com.sbro.emucorex.ui.settings.PerGameSettingsManagerScreen
import com.sbro.emucorex.ui.settings.SettingsScreen
import com.sbro.emucorex.ui.settings.SettingsViewModel
import com.sbro.emucorex.ui.textures.TextureManagerScreen
import com.sbro.emucorex.ui.common.PremiumLoadingAnimation
import com.sbro.emucorex.ui.common.ProvideGamepadUiNavigation
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable

@Serializable
object HomeRoute

@Serializable
data class GameDetailRoute(val catalogGameId: Long)

@Serializable
data class EmulationRoute(
    val gamePath: String? = null,
    val saveSlot: Int? = null,
    val bootBios: Boolean = false,
    val bootSmokeProbe: Boolean = false,
    val autotestMode: Boolean = false,
    val enableEeRecompiler: Boolean? = null,
    val enableIopRecompiler: Boolean? = null,
    val enableVu0Recompiler: Boolean? = null,
    val enableVu1Recompiler: Boolean? = null,
    val enableFastmem: Boolean? = null,
    val enableMtvu: Boolean? = null,
    val renderer: Int? = null,
    val gsDumpFrames: Int? = null,
    val gsDumpDelayMs: Int? = null,
    val exitAppOnExit: Boolean = false
)

internal fun EmulationRoute.isMeaningfulReviewSession(): Boolean =
    !bootBios &&
        !bootSmokeProbe &&
        !autotestMode &&
        !exitAppOnExit &&
        !gamePath.isNullOrBlank()

@Serializable
data class SettingsRoute(val tab: String = "general")

@Serializable
object LanguageSettingsRoute

@Serializable
object OnboardingRoute

@Serializable
object CatalogSearchRoute

@Serializable
object SupportedFormatsRoute

@Serializable
data class SaveManagerRoute(val gamePath: String? = null, val gameTitle: String? = null)

@Serializable
object MemoryCardManagerRoute

@Serializable
object GpuDriverSettingsRoute

@Serializable
data class GameDbBrowserRoute(val query: String? = null)

@Serializable
object TextureManagerRoute

@Serializable
object AchievementsRoute

@Serializable
object ProfileRoute

@Serializable
object FeedbackRoute

@Serializable
object AccountUnlockedAchievementsRoute

@Serializable
data class GameSettingsManagerRoute(val gamePath: String? = null)

@Serializable
data class ControlsLayoutEditorRoute(
    val gamePath: String? = null,
    val gameTitle: String? = null,
    val gameSerial: String? = null
)

@Serializable
data class GameAchievementsRoute(val gamePath: String, val gameTitle: String? = null)

internal enum class StartupDestination {
    HOME,
    ONBOARDING
}

internal fun shouldReleaseStartupSplash(destination: StartupDestination?): Boolean = destination != null

private const val TAG = "AppNavigation"

private fun appScreenEnterTransition(): EnterTransition = appScreenPopEnterTransition()

private fun appScreenExitTransition(): ExitTransition = ExitTransition.None

private fun appScreenPopEnterTransition(): EnterTransition {
    return fadeIn(animationSpec = tween(durationMillis = 260, delayMillis = 70, easing = EaseOut)) +
        scaleIn(initialScale = 0.96f, animationSpec = tween(260, delayMillis = 70, easing = EaseOut))
}

private fun appScreenPopExitTransition(): ExitTransition {
    return fadeOut(animationSpec = tween(durationMillis = 110, easing = EaseIn)) +
        scaleOut(targetScale = 1.0f, animationSpec = tween(110, easing = EaseIn))
}

@Composable
fun AppNavigation(
    launchIntentVersion: Int = 0,
    restoredFromSavedState: Boolean = false,
    onStartupReady: () -> Unit = {},
    onEmulationSessionCompleted: (activePlayTimeMs: Long) -> Unit = {}
) {
    val context = LocalContext.current
    val activity = context as? ComponentActivity
    val preferences = AppPreferences(context)
    val saveStateRepository = SaveStateRepository(context)
    val startupDestination by produceState<StartupDestination?>(
        initialValue = null,
        key1 = preferences,
        key2 = launchIntentVersion
    ) {
        val launchRequest = GameLaunchShortcut.parseLaunchRequest(activity?.intent)
        if (launchRequest?.autotestMode == true) {
            Log.i(TAG, "Startup destination forced HOME for autotest launch path=${launchRequest.gamePath}")
            value = StartupDestination.HOME
            return@produceState
        }
        value = combine(
            preferences.onboardingCompleted,
            preferences.biosPath,
            preferences.gamePaths
        ) { onboardingCompleted, biosPath, gamePaths ->
            val (hasUsableBios, hasGameFolder) = withContext(Dispatchers.IO) {
                BiosValidator.hasUsableBiosFiles(context, biosPath) to
                    SetupValidator.isAnyGameFolderPresentForStartup(context, gamePaths)
            }
            val shouldOpenHome = onboardingCompleted && hasUsableBios && hasGameFolder
            Log.i(
                TAG,
                "Startup destination onboarding=$onboardingCompleted bios=$hasUsableBios gameFolder=$hasGameFolder launch=${launchRequest != null}"
            )
            if (shouldOpenHome) StartupDestination.HOME else StartupDestination.ONBOARDING
        }.first()
    }
    if (startupDestination == null) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background),
            contentAlignment = Alignment.Center
        )
        {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(24.dp)
            ) {
                PremiumLoadingAnimation(size = 80.dp)
            }
        }
        return
    }

    LaunchedEffect(startupDestination) {
        if (!shouldReleaseStartupSplash(startupDestination)) return@LaunchedEffect
        // Keep the platform splash above the first fully composed destination frame.
        withFrameNanos { }
        withFrameNanos { }
        onStartupReady()
    }

    val navController = rememberNavController()
    val currentBackStackEntry by navController.currentBackStackEntryAsState()
    val settingsViewModel: SettingsViewModel = viewModel()
    val settingsUiState by settingsViewModel.uiState.collectAsState()
    val scope = rememberCoroutineScope()
    var homeDrawerEnabled by remember { mutableStateOf(true) }
    var blockRestoredEmulationRoute by remember(restoredFromSavedState) {
        mutableStateOf(restoredFromSavedState)
    }
    val unsupportedGameImageMessage = stringResource(R.string.shell_launch_game_unsupported)
    val continueUnavailableMessage = stringResource(R.string.home_game_menu_continue_unavailable)
    LaunchedEffect(currentBackStackEntry?.destination, restoredFromSavedState) {
        if (!restoredFromSavedState) return@LaunchedEffect
        val destination = currentBackStackEntry?.destination ?: return@LaunchedEffect
        if (!destination.hasRoute<EmulationRoute>()) {
            blockRestoredEmulationRoute = false
        }
    }
    val launchGamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            val rawPath = uri.toString()
            val displayName = withContext(Dispatchers.IO) {
                StorageAccess.takePersistableReadPermission(context, uri)
                DocumentPathResolver.getDisplayName(context, rawPath)
            }
            if (!isSupportedGameImage(displayName)) {
                Toast.makeText(context, unsupportedGameImageMessage, Toast.LENGTH_SHORT).show()
                return@launch
            }
            navController.navigate(EmulationRoute(gamePath = rawPath)) {
                launchSingleTop = true
            }
        }
    }
    val startDestination = when (startupDestination) {
        StartupDestination.ONBOARDING -> OnboardingRoute
        StartupDestination.HOME -> HomeRoute
        else -> {}
    }
    val navigateGameSettingsManager: () -> Unit = {
        navController.navigate(GameSettingsManagerRoute()) {
            launchSingleTop = true
        }
    }
    val navigateDataTransfer: () -> Unit = {
        navController.navigate(SettingsRoute(tab = "data_transfer")) {
            launchSingleTop = true
        }
    }
    val navigateMemoryCardManager: () -> Unit = {
        navController.navigate(MemoryCardManagerRoute) {
            launchSingleTop = true
        }
    }
    val navigateTextureManager: () -> Unit = {
        navController.navigate(TextureManagerRoute) {
            launchSingleTop = true
        }
    }
    val navigateProfile: () -> Unit = {
        navController.navigate(ProfileRoute) {
            launchSingleTop = true
        }
    }
    val navigateFeedback: () -> Unit = {
        navController.navigate(FeedbackRoute) {
            launchSingleTop = true
        }
    }
    val launchGamePickerAction: () -> Unit = {
        launchGamePicker.launch(arrayOf("*/*"))
    }
    val resetAllSettingsAndOpenOnboarding: () -> Unit = {
        scope.launch {
            preferences.resetAllSettings()
            navController.navigate(OnboardingRoute) {
                launchSingleTop = true
                popUpTo(navController.graph.id) { inclusive = true }
            }
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        ProvideGamepadUiNavigation(
            enabled = true,
            onBack = {
                if (navController.previousBackStackEntry != null) {
                    navController.popBackStack()
                } else {
                    false
                }
            }
        )
        NavHost(
            navController = navController,
            startDestination = startDestination,
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background),
            enterTransition = { appScreenEnterTransition() },
            exitTransition = { appScreenExitTransition() },
            popEnterTransition = { appScreenPopEnterTransition() },
            popExitTransition = { appScreenPopExitTransition() },
            sizeTransform = { SizeTransform(clip = false) }
        ) {
            composable<OnboardingRoute> {
                OnboardingScreen(
                    onComplete = {
                        navController.navigate(HomeRoute) {
                            popUpTo(OnboardingRoute) { inclusive = true }
                        }
                    }
                )
            }

            composable<HomeRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Home,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    drawerEnabled = homeDrawerEnabled,
                    onNavigateHome = { },
                    onNavigateSearch = {
                        navController.navigate(CatalogSearchRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateFormats = {
                        navController.navigate(SupportedFormatsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateSettings = {
                        navController.navigate(SettingsRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateAchievements = {
                        navController.navigate(AchievementsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateProfile = navigateProfile,
                    onNavigateFeedback = navigateFeedback,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onLaunchGame = launchGamePickerAction,
                    onLaunchBios = {
                        navController.navigate(EmulationRoute(bootBios = true)) {
                            launchSingleTop = true
                        }
                    }
                ) { openDrawer ->
                    HomeScreen(
                        onGameClick = { game ->
                            navController.navigate(EmulationRoute(gamePath = game.path)) {
                                launchSingleTop = true
                            }
                        },
                        onContinueGame = { game ->
                            val saveSlot = saveStateRepository.findLatestSlot(game.path)
                            if (saveSlot == null) {
                                Toast.makeText(context, continueUnavailableMessage, Toast.LENGTH_SHORT).show()
                                return@HomeScreen
                            }
                            navController.navigate(EmulationRoute(gamePath = game.path, saveSlot = saveSlot)) {
                                launchSingleTop = true
                            }
                        },
                        onLoadSaveClick = { game ->
                            navController.navigate(SaveManagerRoute(gamePath = game.path, gameTitle = game.title)) {
                                launchSingleTop = true
                            }
                        },
                        onManageGameClick = { game ->
                            navController.navigate(GameSettingsManagerRoute(gamePath = game.path)) {
                                launchSingleTop = true
                            }
                        },
                        onCreateShortcutClick = { game ->
                            GameLaunchShortcut.requestPinnedShortcut(context, game)
                        },
                        onOpenGameDbClick = { game ->
                            navController.navigate(
                                GameDbBrowserRoute(query = game.serial?.takeIf { it.isNotBlank() } ?: game.title)
                            ) {
                                launchSingleTop = true
                            }
                        },
                        onMenuClick = openDrawer,
                        onShelfModeChanged = { isShelfMode ->
                            homeDrawerEnabled = !isShelfMode
                        }
                    )
                }
            }

            composable<GameDetailRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<GameDetailRoute>()
                GameDetailScreen(
                    catalogGameId = route.catalogGameId,
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<CatalogSearchRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Search,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    onNavigateHome = {
                        navController.navigate(HomeRoute) {
                            launchSingleTop = true
                            popUpTo(HomeRoute) { inclusive = false }
                        }
                    },
                    onNavigateSearch = { },
                    onNavigateFormats = {
                        navController.navigate(SupportedFormatsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateSettings = {
                        navController.navigate(SettingsRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateAchievements = {
                        navController.navigate(AchievementsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateProfile = navigateProfile,
                    onNavigateFeedback = navigateFeedback,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onBackClick = { navController.popBackStack() },
                    onLaunchGame = launchGamePickerAction
                ) {
                    CatalogSearchScreen(
                        onGameClick = { igdbId ->
                            navController.navigate(GameDetailRoute(catalogGameId = igdbId)) {
                                launchSingleTop = true
                            }
                        },
                        onBackClick = { navController.popBackStack() }
                    )
                }
            }

            composable<EmulationRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<EmulationRoute>()
                EmulationScreen(
                    gamePath = route.gamePath,
                    bootToBios = route.bootBios,
                    bootSmokeProbe = route.bootSmokeProbe,
                    saveSlot = route.saveSlot,
                    autotestMode = route.autotestMode,
                    enableEeRecompilerOverride = route.enableEeRecompiler,
                    enableIopRecompilerOverride = route.enableIopRecompiler,
                    enableVu0RecompilerOverride = route.enableVu0Recompiler,
                    enableVu1RecompilerOverride = route.enableVu1Recompiler,
                    enableFastmemOverride = route.enableFastmem,
                    enableMtvuOverride = route.enableMtvu,
                    rendererOverride = route.renderer,
                    gsDumpFrames = route.gsDumpFrames,
                    gsDumpDelayMs = route.gsDumpDelayMs,
                    restoredAfterProcessDeath = blockRestoredEmulationRoute,
                    onExit = { activePlayTimeMs ->
                        if (route.exitAppOnExit) {
                            activity?.finishAndRemoveTask()
                        } else {
                            if (!navController.popBackStack(HomeRoute, inclusive = false)) {
                                navController.navigate(HomeRoute) {
                                    launchSingleTop = true
                                    popUpTo(navController.graph.startDestinationId) { inclusive = false }
                                }
                            }
                            if (route.isMeaningfulReviewSession()) {
                                onEmulationSessionCompleted(activePlayTimeMs)
                            }
                        }
                    }
                )
            }

            composable<SupportedFormatsRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Formats,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    onNavigateHome = {
                        navController.navigate(HomeRoute) {
                            launchSingleTop = true
                            popUpTo(HomeRoute) { inclusive = false }
                        }
                    },
                    onNavigateSearch = {
                        navController.navigate(CatalogSearchRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateFormats = { },
                    onNavigateSettings = {
                        navController.navigate(SettingsRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateAchievements = {
                        navController.navigate(AchievementsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateProfile = navigateProfile,
                    onNavigateFeedback = navigateFeedback,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onBackClick = { navController.popBackStack() },
                    onLaunchGame = launchGamePickerAction
                ) {
                    SupportedFormatsScreen(
                        onBackClick = { navController.popBackStack() }
                    )
                }
            }

            composable<SettingsRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<SettingsRoute>()
                AdaptiveShell(
                    selected = PrimaryDestination.Settings,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    onNavigateHome = {
                        navController.navigate(HomeRoute) {
                            launchSingleTop = true
                            popUpTo(HomeRoute) { inclusive = false }
                        }
                    },
                    onNavigateSearch = {
                        navController.navigate(CatalogSearchRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateFormats = {
                        navController.navigate(SupportedFormatsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateSettings = { },
                    onNavigateAchievements = {
                        navController.navigate(AchievementsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateProfile = navigateProfile,
                    onNavigateFeedback = navigateFeedback,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onBackClick = { navController.popBackStack() },
                    onLaunchGame = launchGamePickerAction
                ) {
                    SettingsScreen(
                        initialTab = route.tab,
                        onBackClick = { navController.popBackStack() },
                        onOpenMemoryCardManager = navigateMemoryCardManager,
                        onOpenLanguageScreen = {
                            navController.navigate(LanguageSettingsRoute) {
                                launchSingleTop = true
                            }
                        },
                        onOpenGpuDriverManager = {
                            navController.navigate(GpuDriverSettingsRoute) {
                                launchSingleTop = true
                            }
                        },
                        onOpenGameDbBrowser = {
                            navController.navigate(GameDbBrowserRoute()) {
                                launchSingleTop = true
                            }
                        },
                        onOpenControlsLayoutEditor = {
                            navController.navigate(ControlsLayoutEditorRoute()) {
                                launchSingleTop = true
                            }
                        },
                        viewModel = settingsViewModel
                    )
                }
            }

            composable<LanguageSettingsRoute> {
                LanguageSettingsScreen(
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<GpuDriverSettingsRoute> {
                com.sbro.emucorex.ui.settings.GpuDriverScreen(
                    onBackClick = { navController.popBackStack() },
                    viewModel = settingsViewModel
                )
            }

            composable<GameDbBrowserRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<GameDbBrowserRoute>()
                GameDbBrowserScreen(
                    initialQuery = route.query,
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<GameSettingsManagerRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<GameSettingsManagerRoute>()
                PerGameSettingsManagerScreen(
                    initialGamePath = route.gamePath,
                    onOpenControlsLayoutEditor = { game ->
                        navController.navigate(
                            ControlsLayoutEditorRoute(
                                gamePath = game.path,
                                gameTitle = game.title,
                                gameSerial = game.serial
                            )
                        ) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<ControlsLayoutEditorRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<ControlsLayoutEditorRoute>()
                ControlsLayoutEditorHostScreen(
                    gamePath = route.gamePath,
                    gameTitle = route.gameTitle,
                    gameSerial = route.gameSerial,
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<AchievementsRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Achievements,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    onNavigateHome = {
                        navController.navigate(HomeRoute) {
                            launchSingleTop = true
                            popUpTo(HomeRoute) { inclusive = false }
                        }
                    },
                    onNavigateSearch = {
                        navController.navigate(CatalogSearchRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateFormats = {
                        navController.navigate(SupportedFormatsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateSettings = {
                        navController.navigate(SettingsRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateAchievements = { },
                    onNavigateProfile = navigateProfile,
                    onNavigateFeedback = navigateFeedback,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onBackClick = { navController.popBackStack() },
                    onLaunchGame = launchGamePickerAction
                ) {
                    AchievementsHubScreen(
                        onOpenGameAchievements = { path, title ->
                            navController.navigate(GameAchievementsRoute(gamePath = path, gameTitle = title)) {
                                launchSingleTop = true
                            }
                        },
                        onOpenUnlockedAchievements = {
                            navController.navigate(AccountUnlockedAchievementsRoute) {
                                launchSingleTop = true
                            }
                        },
                        onBackClick = { navController.popBackStack() }
                    )
                }
            }

            composable<ProfileRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Profile,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    onNavigateHome = {
                        navController.navigate(HomeRoute) {
                            launchSingleTop = true
                            popUpTo(HomeRoute) { inclusive = false }
                        }
                    },
                    onNavigateSearch = {
                        navController.navigate(CatalogSearchRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateFormats = {
                        navController.navigate(SupportedFormatsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateSettings = {
                        navController.navigate(SettingsRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateAchievements = {
                        navController.navigate(AchievementsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateProfile = { },
                    onNavigateFeedback = navigateFeedback,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onBackClick = { navController.popBackStack() },
                    onLaunchGame = launchGamePickerAction
                ) {
                    ProfileScreen(
                        onBackClick = { navController.popBackStack() },
                        onOpenGameDetails = { catalogGameId ->
                            navController.navigate(GameDetailRoute(catalogGameId = catalogGameId)) {
                                launchSingleTop = true
                            }
                        }
                    )
                }
            }

            composable<AccountUnlockedAchievementsRoute> {
                AccountUnlockedAchievementsScreen(
                    onOpenGameAchievements = { path, title ->
                        navController.navigate(GameAchievementsRoute(gamePath = path, gameTitle = title)) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<FeedbackRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Feedback,
                    isProUnlocked = settingsUiState.isProUnlocked,
                    onNavigateHome = {
                        navController.navigate(HomeRoute) {
                            launchSingleTop = true
                            popUpTo(HomeRoute) { inclusive = false }
                        }
                    },
                    onNavigateSearch = {
                        navController.navigate(CatalogSearchRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateFormats = {
                        navController.navigate(SupportedFormatsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateSettings = {
                        navController.navigate(SettingsRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateAchievements = {
                        navController.navigate(AchievementsRoute) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateProfile = navigateProfile,
                    onNavigateFeedback = { },
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndOpenOnboarding,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onNavigateMemoryCardManager = navigateMemoryCardManager,
                    onNavigateTextureManager = navigateTextureManager,
                    onBackClick = { navController.popBackStack() },
                    onLaunchGame = launchGamePickerAction
                ) {
                    FeedbackScreen(onBackClick = { navController.popBackStack() })
                }
            }

            composable<GameAchievementsRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<GameAchievementsRoute>()
                GameAchievementsScreen(
                    gamePath = route.gamePath,
                    gameTitle = route.gameTitle,
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<SaveManagerRoute> { backStackEntry ->
                val route = backStackEntry.toRoute<SaveManagerRoute>()
                SaveManagerScreen(
                    gamePath = route.gamePath,
                    gameTitle = route.gameTitle,
                    onLoadClick = { path, slot ->
                        navController.navigate(EmulationRoute(gamePath = path, saveSlot = slot)) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<MemoryCardManagerRoute> {
                MemoryCardManagerScreen(
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<TextureManagerRoute> {
                TextureManagerScreen(
                    onBackClick = { navController.popBackStack() }
                )
            }
        }

        LaunchedEffect(navController, startupDestination, launchIntentVersion) {
            val launchRequest = GameLaunchShortcut.parseLaunchRequest(activity?.intent) ?: return@LaunchedEffect
            Log.i(
                TAG,
                "Handling launch request destination=$startupDestination path=${launchRequest.gamePath} bios=${launchRequest.bootBios} autotest=${launchRequest.autotestMode}"
            )
            if (startupDestination != StartupDestination.HOME) return@LaunchedEffect
            navController.navigate(
                EmulationRoute(
                    gamePath = launchRequest.gamePath,
                    saveSlot = launchRequest.saveSlot,
                    bootBios = launchRequest.bootBios,
                    bootSmokeProbe = launchRequest.bootSmokeProbe,
                    autotestMode = launchRequest.autotestMode,
                    enableEeRecompiler = launchRequest.enableEeRecompiler,
                    enableIopRecompiler = launchRequest.enableIopRecompiler,
                    enableVu0Recompiler = launchRequest.enableVu0Recompiler,
                    enableVu1Recompiler = launchRequest.enableVu1Recompiler,
                    enableFastmem = launchRequest.enableFastmem,
                    enableMtvu = launchRequest.enableMtvu,
                    renderer = launchRequest.renderer,
                    gsDumpFrames = launchRequest.gsDumpFrames,
                    gsDumpDelayMs = launchRequest.gsDumpDelayMs,
                    exitAppOnExit = true
                )
            ) {
                launchSingleTop = true
            }
            GameLaunchShortcut.clearLaunchRequest(activity?.intent)
        }

    }
}

private fun isSupportedGameImage(fileName: String): Boolean {
    val extension = fileName.substringAfterLast('.', "").lowercase()
    return extension in setOf("iso", "bin", "img", "mdf", "gz", "cso", "zso", "chd", "elf")
}

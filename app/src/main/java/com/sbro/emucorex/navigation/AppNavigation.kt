package com.sbro.emucorex.navigation

import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Modifier
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.toRoute
import com.sbro.emucorex.R
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.ui.achievements.AchievementsHubScreen
import com.sbro.emucorex.ui.achievements.AccountUnlockedAchievementsScreen
import com.sbro.emucorex.ui.achievements.GameAchievementsScreen
import com.sbro.emucorex.ui.catalog.CatalogSearchScreen
import com.sbro.emucorex.ui.detail.GameDetailScreen
import com.sbro.emucorex.ui.emulation.EmulationScreen
import com.sbro.emucorex.ui.formats.SupportedFormatsScreen
import com.sbro.emucorex.ui.home.HomeScreen
import com.sbro.emucorex.ui.onboarding.OnboardingScreen
import com.sbro.emucorex.ui.saves.SaveManagerScreen
import com.sbro.emucorex.ui.settings.ControlsEditorScreen
import com.sbro.emucorex.ui.settings.LanguageSettingsScreen
import com.sbro.emucorex.ui.settings.PerGameSettingsManagerScreen
import com.sbro.emucorex.ui.settings.SettingsScreen
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.serialization.Serializable

@Serializable
object HomeRoute

@Serializable
data class GameDetailRoute(val catalogGameId: Long)

@Serializable
data class EmulationRoute(val gamePath: String? = null, val saveSlot: Int? = null, val bootBios: Boolean = false)

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
object ControlsEditorRoute

@Serializable
data class SaveManagerRoute(val gamePath: String? = null, val gameTitle: String? = null)

@Serializable
object AchievementsRoute

@Serializable
object AccountUnlockedAchievementsRoute

@Serializable
object GameSettingsManagerRoute

@Serializable
data class GameAchievementsRoute(val gamePath: String, val gameTitle: String? = null)

private enum class StartupDestination {
    HOME,
    ONBOARDING
}

private fun AnimatedContentTransitionScope<NavBackStackEntry>.isRouteTransitioningToHome(): Boolean {
    return initialState.destination.hasRoute<OnboardingRoute>() && targetState.destination.hasRoute<HomeRoute>()
}

private fun AnimatedContentTransitionScope<NavBackStackEntry>.isPrimaryLevelTransition(): Boolean {
    val fromPrimary = initialState.destination.hasRoute<HomeRoute>() ||
        initialState.destination.hasRoute<CatalogSearchRoute>() ||
        initialState.destination.hasRoute<SupportedFormatsRoute>() ||
        initialState.destination.hasRoute<SettingsRoute>() ||
        initialState.destination.hasRoute<AchievementsRoute>()
    val toPrimary = targetState.destination.hasRoute<HomeRoute>() ||
        targetState.destination.hasRoute<CatalogSearchRoute>() ||
        targetState.destination.hasRoute<SupportedFormatsRoute>() ||
        targetState.destination.hasRoute<SettingsRoute>() ||
        targetState.destination.hasRoute<AchievementsRoute>()
    return fromPrimary && toPrimary
}

private fun AnimatedContentTransitionScope<NavBackStackEntry>.isPushingIntoDetail(): Boolean {
    return targetState.destination.hasRoute<GameDetailRoute>() ||
        targetState.destination.hasRoute<EmulationRoute>() ||
        targetState.destination.hasRoute<LanguageSettingsRoute>() ||
        targetState.destination.hasRoute<ControlsEditorRoute>() ||
        targetState.destination.hasRoute<GameSettingsManagerRoute>() ||
        targetState.destination.hasRoute<AccountUnlockedAchievementsRoute>() ||
        targetState.destination.hasRoute<GameAchievementsRoute>() ||
        targetState.destination.hasRoute<SaveManagerRoute>()
}

private fun AnimatedContentTransitionScope<NavBackStackEntry>.isPoppingFromDetail(): Boolean {
    return initialState.destination.hasRoute<GameDetailRoute>() ||
        initialState.destination.hasRoute<EmulationRoute>() ||
        initialState.destination.hasRoute<LanguageSettingsRoute>() ||
        initialState.destination.hasRoute<ControlsEditorRoute>() ||
        initialState.destination.hasRoute<GameSettingsManagerRoute>() ||
        initialState.destination.hasRoute<AccountUnlockedAchievementsRoute>() ||
        initialState.destination.hasRoute<GameAchievementsRoute>() ||
        initialState.destination.hasRoute<SaveManagerRoute>()
}

private fun sharedAxisEnter(
    durationMillis: Int,
    initialScale: Float = 0.94f,
    initialOffsetY: Int = 24,
    delayMillis: Int = 0
): EnterTransition {
    return fadeIn(
        animationSpec = tween(
            durationMillis = durationMillis,
            delayMillis = delayMillis,
            easing = LinearOutSlowInEasing
        )
    ) + scaleIn(
        initialScale = initialScale,
        animationSpec = tween(
            durationMillis = durationMillis,
            delayMillis = delayMillis,
            easing = FastOutSlowInEasing
        )
    ) + slideInVertically(
        initialOffsetY = { initialOffsetY },
        animationSpec = tween(
            durationMillis = durationMillis,
            delayMillis = delayMillis,
            easing = FastOutSlowInEasing
        )
    )
}

@Composable
fun AppNavigation() {
    val context = LocalContext.current
    val preferences = AppPreferences(context)
    val startupDestination by produceState<StartupDestination?>(initialValue = null, key1 = preferences) {
        value = combine(
            preferences.onboardingCompleted,
            preferences.biosPath,
            preferences.gamePath
        ) { onboardingCompleted, biosPath, gamePath ->
            val hasStoredSetup = onboardingCompleted &&
                !biosPath.isNullOrBlank() &&
                !gamePath.isNullOrBlank()
            val biosReady = BiosValidator.hasUsableBiosFiles(context, biosPath)
            val gameFolderPresent = SetupValidator.isGameFolderPresentForStartup(context, gamePath)
            if (!hasStoredSetup || !biosReady || !gameFolderPresent) {
                StartupDestination.ONBOARDING
            } else {
                StartupDestination.HOME
            }
        }.first()
    }
    if (startupDestination == null) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        )
        return
    }

    val navController = rememberNavController()
    val scope = rememberCoroutineScope()
    val unsupportedGameImageMessage = context.getString(R.string.shell_launch_game_unsupported)
    val launchGamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        runCatching {
            context.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        }
        val rawPath = uri.toString()
        val displayName = DocumentPathResolver.getDisplayName(context, rawPath)
        if (!isSupportedGameImage(displayName)) {
            Toast.makeText(context, unsupportedGameImageMessage, Toast.LENGTH_SHORT).show()
            return@rememberLauncherForActivityResult
        }
        navController.navigate(EmulationRoute(gamePath = rawPath)) {
            launchSingleTop = true
        }
    }
    val startDestination = when (startupDestination) {
        StartupDestination.ONBOARDING -> OnboardingRoute
        StartupDestination.HOME -> HomeRoute
        else -> {}
    }
    val navigateControlsEditor: () -> Unit = {
        navController.navigate(ControlsEditorRoute) {
            launchSingleTop = true
        }
    }
    val navigateCheats: () -> Unit = {
        navController.navigate(SettingsRoute(tab = "cheats")) {
            launchSingleTop = true
        }
    }
    val navigateGameSettingsManager: () -> Unit = {
        navController.navigate(GameSettingsManagerRoute) {
            launchSingleTop = true
        }
    }
    val navigateDataTransfer: () -> Unit = {
        navController.navigate(SettingsRoute(tab = "data_transfer")) {
            launchSingleTop = true
        }
    }
    val launchGamePickerAction: () -> Unit = {
        launchGamePicker.launch(arrayOf("*/*"))
    }
    val resetAllSettingsAndReturnHome: () -> Unit = {
        scope.launch {
            preferences.resetAllSettings()
            navController.navigate(HomeRoute) {
                launchSingleTop = true
                popUpTo(HomeRoute) { inclusive = false }
            }
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        NavHost(
            navController = navController,
            startDestination = startDestination,
            enterTransition = {
                when {
                    isRouteTransitioningToHome() -> {
                        sharedAxisEnter(
                            durationMillis = 320,
                            initialScale = 0.96f,
                            initialOffsetY = 18,
                            delayMillis = 20
                        )
                    }

                    isPrimaryLevelTransition() -> {
                        sharedAxisEnter(
                            durationMillis = 300,
                            initialScale = 0.955f,
                            initialOffsetY = 20
                        )
                    }

                    isPushingIntoDetail() -> {
                        sharedAxisEnter(
                            durationMillis = 280,
                            initialScale = 0.94f,
                            initialOffsetY = 26
                        )
                    }

                    else -> {
                        sharedAxisEnter(
                            durationMillis = 240,
                            initialScale = 0.97f,
                            initialOffsetY = 16
                        )
                    }
                }
            },
            exitTransition = {
                when {
                    initialState.destination.hasRoute<OnboardingRoute>() -> ExitTransition.None

                    isPrimaryLevelTransition() -> {
                        fadeOut(
                            animationSpec = tween(
                                durationMillis = 180,
                                easing = FastOutSlowInEasing
                            )
                        ) + scaleOut(
                            targetScale = 1.03f,
                            animationSpec = tween(
                                durationMillis = 180,
                                easing = FastOutSlowInEasing
                            )
                        ) + slideOutVertically(
                            targetOffsetY = { -14 },
                            animationSpec = tween(
                                durationMillis = 180,
                                easing = FastOutSlowInEasing
                            )
                        )
                    }

                    isPushingIntoDetail() -> {
                        fadeOut(
                            animationSpec = tween(
                                durationMillis = 160,
                                easing = FastOutSlowInEasing
                            )
                        ) + scaleOut(
                            targetScale = 1.022f,
                            animationSpec = tween(
                                durationMillis = 160,
                                easing = FastOutSlowInEasing
                            )
                        ) + slideOutVertically(
                            targetOffsetY = { -18 },
                            animationSpec = tween(
                                durationMillis = 160,
                                easing = FastOutSlowInEasing
                            )
                        )
                    }

                    else -> ExitTransition.None
                }
            },
            popEnterTransition = {
                when {
                    isPrimaryLevelTransition() -> {
                        sharedAxisEnter(
                            durationMillis = 280,
                            initialScale = 0.955f,
                            initialOffsetY = -18
                        )
                    }

                    isPoppingFromDetail() -> {
                        sharedAxisEnter(
                            durationMillis = 260,
                            initialScale = 0.95f,
                            initialOffsetY = -20
                        )
                    }

                    else -> {
                        sharedAxisEnter(
                            durationMillis = 220,
                            initialScale = 0.975f,
                            initialOffsetY = -14
                        )
                    }
                }
            },
            popExitTransition = {
                when {
                    isPrimaryLevelTransition() -> {
                        fadeOut(
                            animationSpec = tween(
                                durationMillis = 170,
                                easing = FastOutSlowInEasing
                            )
                        ) + scaleOut(
                            targetScale = 1.026f,
                            animationSpec = tween(
                                durationMillis = 170,
                                easing = FastOutSlowInEasing
                            )
                        ) + slideOutVertically(
                            targetOffsetY = { 12 },
                            animationSpec = tween(
                                durationMillis = 170,
                                easing = FastOutSlowInEasing
                            )
                        )
                    }

                    isPoppingFromDetail() -> {
                        fadeOut(
                            animationSpec = tween(
                                durationMillis = 150,
                                easing = FastOutSlowInEasing
                            )
                        ) + scaleOut(
                            targetScale = 1.018f,
                            animationSpec = tween(
                                durationMillis = 150,
                                easing = FastOutSlowInEasing
                            )
                        ) + slideOutVertically(
                            targetOffsetY = { 14 },
                            animationSpec = tween(
                                durationMillis = 150,
                                easing = FastOutSlowInEasing
                            )
                        )
                    }

                    else -> {
                        fadeOut(
                            animationSpec = tween(
                                durationMillis = 140,
                                easing = FastOutSlowInEasing
                            )
                        ) + slideOutVertically(
                            targetOffsetY = { 10 },
                            animationSpec = tween(
                                durationMillis = 140,
                                easing = FastOutSlowInEasing
                            )
                        )
                    }
                }
            }
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
                    onNavigateCheats = navigateCheats,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateControlsEditor = navigateControlsEditor,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndReturnHome,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onOpenManageFolders = {
                        navController.navigate(SettingsRoute(tab = "paths")) {
                            launchSingleTop = true
                        }
                    },
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
                        onMenuClick = openDrawer
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
                    onNavigateCheats = navigateCheats,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateControlsEditor = navigateControlsEditor,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndReturnHome,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() },
                    onOpenManageFolders = {
                        navController.navigate(SettingsRoute(tab = "paths")) {
                            launchSingleTop = true
                        }
                    },
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
                    saveSlot = route.saveSlot,
                    onExit = {
                        navController.popBackStack(HomeRoute, inclusive = false)
                    }
                )
            }

            composable<SupportedFormatsRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Formats,
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
                    onNavigateCheats = navigateCheats,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateControlsEditor = navigateControlsEditor,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndReturnHome,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() },
                    onOpenManageFolders = {
                        navController.navigate(SettingsRoute(tab = "paths")) {
                            launchSingleTop = true
                        }
                    },
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
                    onNavigateCheats = navigateCheats,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateControlsEditor = navigateControlsEditor,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndReturnHome,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() },
                    onOpenManageFolders = {
                        navController.navigate(SettingsRoute(tab = "paths")) {
                            launchSingleTop = true
                        }
                    },
                    onLaunchGame = launchGamePickerAction
                ) {
                    SettingsScreen(
                        initialTab = route.tab,
                        onBackClick = { navController.popBackStack() },
                        onOpenLanguageScreen = {
                            navController.navigate(LanguageSettingsRoute) {
                                launchSingleTop = true
                            }
                        },
                        onEditControlsClick = navigateControlsEditor
                    )
                }
            }

            composable<LanguageSettingsRoute> {
                LanguageSettingsScreen(
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<GameSettingsManagerRoute> {
                PerGameSettingsManagerScreen(
                    onBackClick = { navController.popBackStack() }
                )
            }

            composable<AchievementsRoute> {
                AdaptiveShell(
                    selected = PrimaryDestination.Achievements,
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
                    onNavigateCheats = navigateCheats,
                    onNavigateGameSettingsManager = navigateGameSettingsManager,
                    onNavigateControlsEditor = navigateControlsEditor,
                    onNavigateDataTransfer = navigateDataTransfer,
                    onResetAllSettings = resetAllSettingsAndReturnHome,
                    onNavigateSaveManager = {
                        navController.navigate(SaveManagerRoute()) {
                            launchSingleTop = true
                        }
                    },
                    onBackClick = { navController.popBackStack() },
                    onOpenManageFolders = {
                        navController.navigate(SettingsRoute(tab = "paths")) {
                            launchSingleTop = true
                        }
                    },
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

            composable<ControlsEditorRoute> {
                ControlsEditorScreen(
                    onBackClick = { navController.popBackStack() }
                )
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
        }
    }
}

private fun isSupportedGameImage(fileName: String): Boolean {
    val extension = fileName.substringAfterLast('.', "").lowercase()
    return extension in setOf("iso", "bin", "chd", "cso", "gz")
}

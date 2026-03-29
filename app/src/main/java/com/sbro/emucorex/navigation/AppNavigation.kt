package com.sbro.emucorex.navigation

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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Modifier
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.toRoute
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.ui.achievements.AchievementsHubScreen
import com.sbro.emucorex.ui.achievements.GameAchievementsScreen
import com.sbro.emucorex.ui.catalog.CatalogSearchScreen
import com.sbro.emucorex.ui.detail.GameDetailScreen
import com.sbro.emucorex.ui.emulation.EmulationScreen
import com.sbro.emucorex.ui.formats.SupportedFormatsScreen
import com.sbro.emucorex.ui.home.HomeScreen
import com.sbro.emucorex.ui.onboarding.OnboardingScreen
import com.sbro.emucorex.ui.saves.SaveManagerScreen
import com.sbro.emucorex.ui.settings.ControlsEditorScreen
import com.sbro.emucorex.ui.settings.SettingsScreen
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.serialization.Serializable

@Serializable
object HomeRoute

@Serializable
data class GameDetailRoute(val gamePath: String? = null, val catalogGameId: Long? = null)

@Serializable
data class EmulationRoute(val gamePath: String? = null, val saveSlot: Int? = null, val bootBios: Boolean = false)

@Serializable
data class SettingsRoute(val tab: String = "general")

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
    return targetState.destination.hasRoute<GameDetailRoute>() || targetState.destination.hasRoute<EmulationRoute>()
}

private fun AnimatedContentTransitionScope<NavBackStackEntry>.isPoppingFromDetail(): Boolean {
    return initialState.destination.hasRoute<GameDetailRoute>() || initialState.destination.hasRoute<EmulationRoute>()
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
            val biosReady = BiosValidator.hasUsableBiosFiles(context, biosPath)
            val gameFolderReady = SetupValidator.isGameFolderAccessible(context, gamePath)
            if (!onboardingCompleted || !biosReady || !gameFolderReady) {
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
    val startDestination = when (startupDestination) {
        StartupDestination.ONBOARDING -> OnboardingRoute
        StartupDestination.HOME -> HomeRoute
        else -> {}
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
                    onLaunchBios = {
                        navController.navigate(EmulationRoute(bootBios = true)) {
                            launchSingleTop = true
                        }
                    }
                ) { openDrawer ->
                    HomeScreen(
                        onGameClick = { game ->
                            navController.navigate(GameDetailRoute(gamePath = game.path, catalogGameId = game.catalogGameId)) {
                                launchSingleTop = true
                            }
                        },
                        onSettingsClick = {
                            navController.navigate(SettingsRoute()) {
                                launchSingleTop = true
                            }
                        },
                        onSetupClick = {
                            navController.navigate(OnboardingRoute) {
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
                    gamePath = route.gamePath,
                    catalogGameId = route.catalogGameId,
                    onPlayClick = { path, slot ->
                        navController.navigate(EmulationRoute(gamePath = path, saveSlot = slot)) {
                            launchSingleTop = true
                        }
                    },
                    onOpenSaveManager = { path, title ->
                        navController.navigate(SaveManagerRoute(gamePath = path, gameTitle = title)) {
                            launchSingleTop = true
                        }
                    },
                    onOpenAchievements = { path, title ->
                        navController.navigate(GameAchievementsRoute(gamePath = path, gameTitle = title)) {
                            launchSingleTop = true
                        }
                    },
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
                    }
                ) {
                    CatalogSearchScreen(
                        onGameClick = { igdbId ->
                            navController.navigate(GameDetailRoute(gamePath = null, catalogGameId = igdbId)) {
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
                    }
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
                    }
                ) {
                    SettingsScreen(
                        initialTab = route.tab,
                        onBackClick = { navController.popBackStack() },
                        onEditControlsClick = {
                            navController.navigate(ControlsEditorRoute)
                        }
                    )
                }
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
                    }
                ) {
                    AchievementsHubScreen(
                        onOpenGameAchievements = { path, title ->
                            navController.navigate(GameAchievementsRoute(gamePath = path, gameTitle = title)) {
                                launchSingleTop = true
                            }
                        },
                        onBackClick = { navController.popBackStack() }
                    )
                }
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

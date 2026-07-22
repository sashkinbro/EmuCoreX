package com.sbro.emucorex.data

enum class AppFontChoice(val preferenceValue: Int) {
    SYSTEM(0),
    RUBIK(1),
    EXO_2(2),
    CUSTOM(3);

    companion object {
        fun fromPreference(value: Int?): AppFontChoice =
            entries.firstOrNull { it.preferenceValue == value } ?: SYSTEM
    }
}

enum class HomeBackgroundType(val preferenceValue: Int) {
    NONE(0),
    IMAGE(1),
    GIF(2),
    VIDEO(3);

    companion object {
        fun fromPreference(value: Int?): HomeBackgroundType =
            entries.firstOrNull { it.preferenceValue == value } ?: NONE
    }
}

/** Visual treatment only; controller geometry and input hit targets never depend on this value. */
enum class TouchControlVisualStyle(val preferenceValue: Int) {
    CLASSIC(0),
    LEGACY(1),
    MODERN(2),
    ARCADE(3),
    MINIMAL(4);

    companion object {
        fun fromPreference(value: Int?): TouchControlVisualStyle =
            entries.firstOrNull { it.preferenceValue == value } ?: CLASSIC
    }
}

/** Visual feedback only; input hit targets and controller geometry never depend on this value. */
enum class TouchControlPressEffect(val preferenceValue: Int) {
    GROW(0),
    SHRINK(1),
    SPRING(2),
    GLOW(3);

    companion object {
        fun fromPreference(value: Int?): TouchControlPressEffect =
            entries.firstOrNull { it.preferenceValue == value } ?: GROW
    }
}

/** Changes the structure of the in-game menu without changing its actions or saved content order. */
enum class GameMenuLayoutStyle(val preferenceValue: Int) {
    SIDEBAR(0),
    DASHBOARD(1),
    COMMAND_CENTER(2),
    COMPACT(3);

    companion object {
        fun fromPreference(value: Int?): GameMenuLayoutStyle =
            entries.firstOrNull { it.preferenceValue == value } ?: SIDEBAR
    }
}

/** Visual and density treatment for the application navigation drawer. */
enum class DrawerVisualStyle(val preferenceValue: Int) {
    CLASSIC(0),
    COMPACT(1),
    GLASS(2),
    CONSOLE(3);

    companion object {
        fun fromPreference(value: Int?): DrawerVisualStyle =
            entries.firstOrNull { it.preferenceValue == value } ?: CLASSIC
    }
}

enum class GameMenuTabId {
    SESSION,
    CONTROLS,
    EMULATION,
    GRAPHICS,
    FIXES,
    ACHIEVEMENTS
}

enum class DrawerItemId(val required: Boolean = false) {
    LIBRARY(required = true),
    CATALOG_SEARCH,
    ACHIEVEMENTS,
    PROFILE,
    LAUNCH_GAME,
    LAUNCH_BIOS,
    GAME_SETTINGS,
    DATA_TRANSFER,
    RESET_SETTINGS,
    MEMORY_CARDS,
    TEXTURE_MANAGER,
    CHEAT_MANAGER,
    SAVE_STATES,
    APP_SETTINGS(required = true),
    SUPPORTED_FORMATS,
    FEEDBACK,
    DISCORD
}

enum class GameMenuSectionId(val tab: GameMenuTabId) {
    SAVE_STATES(GameMenuTabId.SESSION),
    AUTO_SAVE(GameMenuTabId.SESSION),
    QUICK_ACTIONS(GameMenuTabId.SESSION),
    SESSION_DEBUG_TOOLS(GameMenuTabId.SESSION),
    AUTOMATION(GameMenuTabId.SESSION),
    GAME_PROFILE(GameMenuTabId.SESSION),
    CONTROLS_GENERAL(GameMenuTabId.CONTROLS),
    CONTROLS_TOUCH(GameMenuTabId.CONTROLS),
    CONTROLS_GAMEPAD(GameMenuTabId.CONTROLS),
    EMULATION_PERFORMANCE(GameMenuTabId.EMULATION),
    EMULATION_SPEED(GameMenuTabId.EMULATION),
    EMULATION_CHEATS(GameMenuTabId.EMULATION),
    GRAPHICS_DISPLAY(GameMenuTabId.GRAPHICS),
    GRAPHICS_RENDERING(GameMenuTabId.GRAPHICS),
    GRAPHICS_SCREEN(GameMenuTabId.GRAPHICS),
    FIXES_PATCHES(GameMenuTabId.FIXES),
    FIXES_HARDWARE(GameMenuTabId.FIXES),
    FIXES_UPSCALING(GameMenuTabId.FIXES),
    ACHIEVEMENTS_PROGRESS(GameMenuTabId.ACHIEVEMENTS)
}

val DefaultGameMenuTabOrder: List<GameMenuTabId> = GameMenuTabId.entries.toList()
val DefaultGameMenuSectionOrder: List<GameMenuSectionId> = GameMenuSectionId.entries.toList()

fun gameMenuSectionsForTab(
    tab: GameMenuTabId,
    order: List<GameMenuSectionId> = DefaultGameMenuSectionOrder
): List<GameMenuSectionId> = order.filter { it.tab == tab }

fun sanitizeGameMenuTabOrder(raw: String?): List<GameMenuTabId> {
    val stored = raw.orEmpty()
        .split(',')
        .mapNotNull { token ->
            GameMenuTabId.entries.firstOrNull { it.name == token.trim().uppercase() }
        }
        .distinct()
    return (stored + DefaultGameMenuTabOrder).distinct()
}

fun sanitizeHiddenGameMenuTabs(raw: String?): Set<GameMenuTabId> = raw.orEmpty()
    .split(',')
    .mapNotNull { token ->
        GameMenuTabId.entries.firstOrNull { it.name == token.trim().uppercase() }
    }
    .filterNot { it == GameMenuTabId.SESSION }
    .toSet()

fun sanitizeHiddenDrawerItems(raw: String?): Set<DrawerItemId> = raw.orEmpty()
    .split(',')
    .mapNotNull { token ->
        DrawerItemId.entries.firstOrNull { it.name == token.trim().uppercase() }
    }
    .filterNot(DrawerItemId::required)
    .toSet()

fun sanitizeGameMenuSectionOrder(raw: String?): List<GameMenuSectionId> {
    val stored = raw.orEmpty()
        .split(',')
        .mapNotNull { token ->
            GameMenuSectionId.entries.firstOrNull { it.name == token.trim().uppercase() }
        }
        .distinct()
    return GameMenuTabId.entries.flatMap { tab ->
        val storedForTab = stored.filter { it.tab == tab }
        val defaultsForTab = DefaultGameMenuSectionOrder.filter { it.tab == tab }
        (storedForTab + defaultsForTab).distinct()
    }
}

fun sanitizeHiddenGameMenuSections(raw: String?): Set<GameMenuSectionId> = raw.orEmpty()
    .split(',')
    .mapNotNull { token ->
        GameMenuSectionId.entries.firstOrNull { it.name == token.trim().uppercase() }
    }
    .toSet()

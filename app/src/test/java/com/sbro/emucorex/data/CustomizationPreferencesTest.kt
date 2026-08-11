package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomizationPreferencesTest {
    @Test
    fun fontChoiceFallsBackToSystemForUnknownValues() {
        assertEquals(AppFontChoice.SYSTEM, AppFontChoice.fromPreference(null))
        assertEquals(AppFontChoice.SYSTEM, AppFontChoice.fromPreference(999))
        assertEquals(AppFontChoice.RUBIK, AppFontChoice.fromPreference(1))
        assertEquals(AppFontChoice.EXO_2, AppFontChoice.fromPreference(2))
        assertEquals(AppFontChoice.CUSTOM, AppFontChoice.fromPreference(3))
    }

    @Test
    fun backgroundTypeFallsBackToNoneForUnknownValues() {
        assertEquals(HomeBackgroundType.NONE, HomeBackgroundType.fromPreference(null))
        assertEquals(HomeBackgroundType.NONE, HomeBackgroundType.fromPreference(-1))
        assertEquals(HomeBackgroundType.IMAGE, HomeBackgroundType.fromPreference(1))
        assertEquals(HomeBackgroundType.GIF, HomeBackgroundType.fromPreference(2))
        assertEquals(HomeBackgroundType.VIDEO, HomeBackgroundType.fromPreference(3))
        assertEquals(HomeBackgroundType.BUILT_IN, HomeBackgroundType.fromPreference(4))
    }

    @Test
    fun backgroundPresetFallsBackSafelyAndKeepsStableValues() {
        assertEquals(HomeBackgroundPreset.OLYMPUS, HomeBackgroundPreset.fromPreference(null))
        assertEquals(HomeBackgroundPreset.OLYMPUS, HomeBackgroundPreset.fromPreference(99))
        HomeBackgroundPreset.entries.forEachIndexed { index, preset ->
            assertEquals(index, preset.preferenceValue)
            assertEquals(preset, HomeBackgroundPreset.fromPreference(index))
        }
    }

    @Test
    fun defaultsRemainInsideSupportedRanges() {
        assertTrue(AppPreferences.DEFAULT_APP_FONT_SCALE in AppPreferences.MIN_APP_FONT_SCALE..AppPreferences.MAX_APP_FONT_SCALE)
        assertTrue(AppPreferences.DEFAULT_HOME_GRID_SCALE in AppPreferences.MIN_HOME_GRID_SCALE..AppPreferences.MAX_HOME_GRID_SCALE)
        assertTrue(AppPreferences.DEFAULT_HOME_BACKGROUND_DIM in 0..85)
        assertEquals(0.75f, AppPreferences.MIN_APP_FONT_SCALE, 0f)
        assertEquals(1.50f, AppPreferences.MAX_APP_FONT_SCALE, 0f)
        assertEquals(0.60f, AppPreferences.MIN_HOME_GRID_SCALE, 0f)
        assertEquals(1.60f, AppPreferences.MAX_HOME_GRID_SCALE, 0f)
        assertTrue(
            AppPreferences.DEFAULT_FPS_OVERLAY_SCALE in
                AppPreferences.MIN_FPS_OVERLAY_SCALE..AppPreferences.MAX_FPS_OVERLAY_SCALE
        )
        assertEquals(
            PerformanceOverlayMetrics.DEFAULT,
            PerformanceOverlayMetrics.sanitize(PerformanceOverlayMetrics.DEFAULT)
        )
        assertTrue(
            !PerformanceOverlayMetrics.isEnabled(
                PerformanceOverlayMetrics.DEFAULT,
                PerformanceOverlayMetrics.QUEUE
            )
        )
        assertTrue(
            PerformanceOverlayMetrics.isEnabled(
                PerformanceOverlayMetrics.DEFAULT,
                PerformanceOverlayMetrics.HOST_CPU
            )
        )
        assertTrue(
            PerformanceOverlayMetrics.isEnabled(
                PerformanceOverlayMetrics.DEFAULT,
                PerformanceOverlayMetrics.HOST_GPU
            )
        )
    }

    @Test
    fun touchStyleFallsBackToClassicForUnknownValues() {
        assertEquals(TouchControlVisualStyle.CLASSIC, TouchControlVisualStyle.fromPreference(null))
        assertEquals(TouchControlVisualStyle.CLASSIC, TouchControlVisualStyle.fromPreference(99))
        assertEquals(TouchControlVisualStyle.LEGACY, TouchControlVisualStyle.fromPreference(1))
        assertEquals(TouchControlVisualStyle.MODERN, TouchControlVisualStyle.fromPreference(2))
        assertEquals(TouchControlVisualStyle.ARCADE, TouchControlVisualStyle.fromPreference(3))
        assertEquals(TouchControlVisualStyle.MINIMAL, TouchControlVisualStyle.fromPreference(4))
    }

    @Test
    fun touchPressEffectFallsBackToGrowForUnknownValues() {
        assertEquals(TouchControlPressEffect.GROW, TouchControlPressEffect.fromPreference(null))
        assertEquals(TouchControlPressEffect.GROW, TouchControlPressEffect.fromPreference(99))
        assertEquals(TouchControlPressEffect.SHRINK, TouchControlPressEffect.fromPreference(1))
        assertEquals(TouchControlPressEffect.SPRING, TouchControlPressEffect.fromPreference(2))
        assertEquals(TouchControlPressEffect.GLOW, TouchControlPressEffect.fromPreference(3))
    }

    @Test
    fun gameMenuLayoutFallsBackToSidebarForUnknownValues() {
        assertEquals(GameMenuLayoutStyle.SIDEBAR, GameMenuLayoutStyle.fromPreference(null))
        assertEquals(GameMenuLayoutStyle.SIDEBAR, GameMenuLayoutStyle.fromPreference(99))
        assertEquals(GameMenuLayoutStyle.DASHBOARD, GameMenuLayoutStyle.fromPreference(1))
        assertEquals(GameMenuLayoutStyle.COMMAND_CENTER, GameMenuLayoutStyle.fromPreference(2))
        assertEquals(GameMenuLayoutStyle.COMPACT, GameMenuLayoutStyle.fromPreference(3))
    }

    @Test
    fun drawerStyleFallsBackToClassicForUnknownValues() {
        assertEquals(DrawerVisualStyle.CLASSIC, DrawerVisualStyle.fromPreference(null))
        assertEquals(DrawerVisualStyle.CLASSIC, DrawerVisualStyle.fromPreference(-1))
        assertEquals(DrawerVisualStyle.COMPACT, DrawerVisualStyle.fromPreference(1))
        assertEquals(DrawerVisualStyle.GLASS, DrawerVisualStyle.fromPreference(2))
        assertEquals(DrawerVisualStyle.CONSOLE, DrawerVisualStyle.fromPreference(3))
    }

    @Test
    fun gameMenuOrderKeepsStoredOrderAndAppendsNewTabs() {
        val order = sanitizeGameMenuTabOrder("GRAPHICS,SESSION,GRAPHICS,UNKNOWN")
        assertEquals(GameMenuTabId.GRAPHICS, order[0])
        assertEquals(GameMenuTabId.SESSION, order[1])
        assertEquals(GameMenuTabId.entries.toSet(), order.toSet())
        assertEquals(GameMenuTabId.entries.size, order.size)
    }

    @Test
    fun sessionTabCannotBeHiddenByStoredData() {
        val hidden = sanitizeHiddenGameMenuTabs("SESSION,GRAPHICS,UNKNOWN")
        assertTrue(GameMenuTabId.SESSION !in hidden)
        assertEquals(setOf(GameMenuTabId.GRAPHICS), hidden)
    }

    @Test
    fun requiredDrawerDestinationsCannotBeHidden() {
        val hidden = sanitizeHiddenDrawerItems(
            "LIBRARY,APP_SETTINGS,PROFILE,DISCORD,UNKNOWN"
        )

        assertEquals(setOf(DrawerItemId.PROFILE, DrawerItemId.DISCORD), hidden)
        assertTrue(DrawerItemId.LIBRARY !in hidden)
        assertTrue(DrawerItemId.APP_SETTINGS !in hidden)
    }

    @Test
    fun gameMenuSectionOrderKeepsEachTabGroupedAndAppendsNewSections() {
        val order = sanitizeGameMenuSectionOrder(
            "CONTROLS_GAMEPAD,SAVE_STATES,CONTROLS_TOUCH,CONTROLS_GAMEPAD,UNKNOWN"
        )

        assertEquals(GameMenuSectionId.SAVE_STATES, gameMenuSectionsForTab(GameMenuTabId.SESSION, order).first())
        assertEquals(
            listOf(GameMenuSectionId.CONTROLS_GAMEPAD, GameMenuSectionId.CONTROLS_TOUCH),
            gameMenuSectionsForTab(GameMenuTabId.CONTROLS, order).take(2)
        )
        assertEquals(GameMenuSectionId.entries.toSet(), order.toSet())
        assertEquals(GameMenuSectionId.entries.size, order.size)
    }

    @Test
    fun legacySessionVisibilityValuesRemainValid() {
        val hidden = sanitizeHiddenGameMenuSections("SAVE_STATES,AUTO_SAVE,UNKNOWN")

        assertEquals(
            setOf(GameMenuSectionId.SAVE_STATES, GameMenuSectionId.AUTO_SAVE),
            hidden
        )
    }
}

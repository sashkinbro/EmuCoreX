package com.sbro.emucorex.ui.home

import android.content.Context
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.unit.DpOffset
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomCoverResetMenuInstrumentedTest {
    @get:Rule val composeRule = createComposeRule()
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val game = GameItem("Cover reset test", "/games/cover-reset-test.iso", "cover-reset-test.iso", 1, 0)

    @Test
    fun resetActionsAreLocalizedInAll18Languages() {
        val locales = listOf("en", "uk", "ru", "de", "fr", "es", "it", "pt", "pl", "cs", "tr", "id", "hi", "ar", "fa", "ja", "ko", "zh")
        val ids = listOf(R.string.home_game_menu_restore_cover, R.string.home_game_menu_restore_cover_success,
            R.string.home_game_menu_restore_cover_failed, R.string.settings_reset_custom_covers,
            R.string.settings_reset_custom_covers_desc, R.string.settings_reset_custom_covers_confirm_title,
            R.string.settings_reset_custom_covers_confirm_body, R.string.settings_reset_custom_covers_action,
            R.string.settings_reset_custom_covers_success, R.string.settings_reset_custom_covers_partial)
        val english = context.createConfigurationContext(android.content.res.Configuration(context.resources.configuration)
            .apply { setLocale(java.util.Locale.ENGLISH) }).resources
        for (language in locales) {
            val localized = context.createConfigurationContext(android.content.res.Configuration(context.resources.configuration)
                .apply { setLocale(java.util.Locale.forLanguageTag(language)) }).resources
            for (id in ids) org.junit.Assert.assertTrue(localized.getString(id).isNotBlank())
            if (language != "en") org.junit.Assert.assertNotEquals(language,
                english.getString(R.string.home_game_menu_restore_cover),
                localized.getString(R.string.home_game_menu_restore_cover))
        }
    }

    @Test
    fun defaultCoverDoesNotOfferReset() {
        composeRule.setContent {
            EmuCoreXTheme { menu(game, {}) }
        }
        composeRule.onNodeWithText(context.getString(R.string.home_game_menu_restore_cover)).assertDoesNotExist()
        composeRule.onNodeWithText(context.getString(R.string.home_game_menu_custom_cover)).assertExists()
    }

    @Test
    fun customCoverOffersResetAndRemovesActionAfterRestoring() {
        val state = mutableStateOf(game.copy(coverArtPath =
            File(context.filesDir, "library/custom-game-covers/menu-test.png").absolutePath))
        var restored = 0
        composeRule.setContent {
            EmuCoreXTheme {
                menu(state.value) {
                    restored++
                    state.value = game
                }
            }
        }
        composeRule.onNodeWithText(context.getString(R.string.home_game_menu_restore_cover))
            .performScrollTo().assertIsDisplayed()
        composeRule.waitForIdle()
        androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()?.let { bitmap ->
            File(context.getExternalFilesDir("logs"), "custom-cover-reset-menu.png").outputStream().use {
                bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
            }
            bitmap.recycle()
        }
        composeRule.onNodeWithText(context.getString(R.string.home_game_menu_restore_cover)).performClick()
        composeRule.onNodeWithText(context.getString(R.string.home_game_menu_restore_cover)).assertDoesNotExist()
        composeRule.runOnIdle { assertEquals(1, restored) }
    }

    @androidx.compose.runtime.Composable
    private fun menu(game: GameItem, reset: () -> Unit) {
        GameCardContextMenu(game, true, DpOffset.Zero, {}, {}, {}, {}, {}, {}, {}, {}, reset, {}, null, null)
    }
}

package com.sbro.emucorex.ui.theme

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.data.CustomThemeConfig
import com.sbro.emucorex.data.CustomThemeLibrary
import com.sbro.emucorex.data.SavedCustomTheme
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ThemeManagerScreenInstrumentedTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun rendersEditorAndAppliesSanitizedDraft() {
        val applied = AtomicReference<CustomThemeLibrary?>()
        val initial = CustomThemeLibrary(
            activeThemeId = "device-test",
            themes = listOf(
                SavedCustomTheme(
                    id = "device-test",
                    config = CustomThemeConfig(name = "Device test")
                )
            )
        )
        composeRule.setContent {
            EmuCoreXTheme {
                ThemeManagerScreen(
                    initialLibrary = initial,
                    isProUnlocked = true,
                    onPurchasePro = {},
                    onSave = {},
                    onApply = applied::set,
                    onBackClick = {}
                )
            }
        }

        composeRule.onNodeWithTag("theme_manager_top_bar").assertIsDisplayed()
        composeRule.onNodeWithTag("theme_manager_list")
            .performScrollToNode(hasTestTag("theme_manager_categories"))
        composeRule.onNodeWithTag("theme_manager_categories").assertIsDisplayed()
        composeRule.onNodeWithTag("theme_manager_list")
            .performScrollToNode(hasTestTag("theme_manager_apply"))
        composeRule.onNodeWithTag("theme_manager_apply").performClick()

        composeRule.runOnIdle {
            assertEquals("Device test", applied.get()?.activeTheme()?.config?.name)
            assertEquals(
                CustomThemeLibrary.CURRENT_SCHEMA_VERSION,
                applied.get()?.schemaVersion
            )
        }
    }

    @Test
    fun freeUserCanExploreButCannotApplyTheme() {
        composeRule.setContent {
            EmuCoreXTheme {
                ThemeManagerScreen(
                    initialLibrary = CustomThemeLibrary.Empty,
                    isProUnlocked = false,
                    onPurchasePro = {},
                    onSave = {},
                    onApply = {},
                    onBackClick = {}
                )
            }
        }

        composeRule.onNodeWithTag("theme_manager_top_bar").assertIsDisplayed()
        composeRule.onNodeWithTag("theme_manager_preview_banner").assertIsDisplayed()
        composeRule.onNodeWithTag("theme_manager_list")
            .performScrollToNode(hasTestTag("theme_manager_apply"))
        composeRule.onNodeWithTag("theme_manager_apply").assertIsNotEnabled()
    }

    @Test
    fun textOnPrimaryDraftDoesNotSnapBackWhileEditing() {
        val initial = CustomThemeLibrary(
            activeThemeId = "slider-test",
            themes = listOf(
                SavedCustomTheme(
                    id = "slider-test",
                    config = CustomThemeConfig(name = "Slider test")
                )
            )
        )
        composeRule.setContent {
            EmuCoreXTheme {
                ThemeManagerScreen(
                    initialLibrary = initial,
                    isProUnlocked = true,
                    onPurchasePro = {},
                    onSave = {},
                    onApply = {},
                    onBackClick = {}
                )
            }
        }

        composeRule.onNodeWithTag("theme_manager_list")
            .performScrollToNode(hasTestTag("theme_manager_color_on_primary"))
        composeRule.onNodeWithTag("theme_manager_color_on_primary").performClick()
        val hexField = hasTestTag("theme_manager_hex_on_primary")
        composeRule.onNodeWithTag("theme_manager_list")
            .performScrollToNode(hexField)
        composeRule.onNode(hexField).performTextReplacement("#808080")
        composeRule.onNode(hexField).assertTextContains("#808080")
    }
}

package com.sbro.emucorex.ui.controls

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.data.CustomTouchControl
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TouchControlCreatorScreenInstrumentedTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun freeUserCanPreviewButCannotSave() {
        composeRule.setContent {
            EmuCoreXTheme {
                TouchControlCreatorScreen(
                    initialLibrary = CustomTouchControlLibrary.Empty,
                    isProUnlocked = false,
                    onPurchasePro = {},
                    onSave = {},
                    onBackClick = {}
                )
            }
        }

        composeRule.onNodeWithTag("touch_control_creator_top_bar").assertIsDisplayed()
        composeRule.onNodeWithTag("touch_control_creator_preview_banner").assertIsDisplayed()
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(hasTestTag("touch_control_creator_save"))
        composeRule.onNodeWithTag("touch_control_creator_save").assertIsNotEnabled()
    }

    @Test
    fun proUserEditsAndSavesMultipleControlLibrary() {
        val saved = AtomicReference<CustomTouchControlLibrary?>()
        val initial = CustomTouchControlLibrary(
            controls = listOf(
                CustomTouchControl(id = "jump", name = "Jump", actionId = "cross"),
                CustomTouchControl(id = "brake", name = "Brake", actionId = "l2")
            )
        )
        composeRule.setContent {
            EmuCoreXTheme {
                TouchControlCreatorScreen(
                    initialLibrary = initial,
                    isProUnlocked = true,
                    onPurchasePro = {},
                    onSave = saved::set,
                    onBackClick = {}
                )
            }
        }

        val nameField = hasTestTag("touch_control_creator_name")
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(nameField)
        composeRule.onNode(nameField).performTextReplacement("Jump turbo")
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(hasTestTag("touch_control_creator_save"))
        composeRule.onNodeWithTag("touch_control_creator_save").performClick()

        composeRule.runOnIdle {
            assertEquals(2, saved.get()?.controls?.size)
            assertEquals("Jump turbo", saved.get()?.controls?.first()?.name)
        }
    }

    @Test
    fun studioCategoriesKeepTheirOwnPreviewCloseToTheEditor() {
        composeRule.setContent {
            EmuCoreXTheme {
                TouchControlCreatorScreen(
                    initialLibrary = CustomTouchControlLibrary.Empty,
                    isProUnlocked = false,
                    onPurchasePro = {},
                    onSave = {},
                    onBackClick = {}
                )
            }
        }

        val appearance = hasTestTag("touch_control_creator_category_appearance")
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(hasTestTag("touch_control_creator_categories"))
        composeRule.onNodeWithTag("touch_control_creator_categories")
            .performScrollToNode(appearance)
        composeRule.onNode(appearance).performClick()
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(hasTestTag("touch_control_creator_local_preview"))
        composeRule.onNodeWithTag("touch_control_creator_local_preview").assertIsDisplayed()
    }
}

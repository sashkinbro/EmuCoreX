package com.sbro.emucorex.ui.controls

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
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

        composeRule.onNodeWithText("Control Creator").assertIsDisplayed()
        composeRule.onNodeWithText("Preview mode").assertIsDisplayed()
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(hasText("Save controls"))
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

        val nameField = hasSetTextAction() and hasText("Control name")
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(nameField)
        composeRule.onNode(nameField).performTextReplacement("Jump turbo")
        composeRule.onNodeWithTag("touch_control_creator_list")
            .performScrollToNode(hasText("Save controls"))
        composeRule.onNodeWithTag("touch_control_creator_save").performClick()

        composeRule.runOnIdle {
            assertEquals(2, saved.get()?.controls?.size)
            assertEquals("Jump turbo", saved.get()?.controls?.first()?.name)
        }
    }
}

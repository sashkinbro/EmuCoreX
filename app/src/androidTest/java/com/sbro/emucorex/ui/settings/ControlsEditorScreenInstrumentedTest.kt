package com.sbro.emucorex.ui.settings

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.data.CustomTouchControl
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ControlsEditorScreenInstrumentedTest {
    @get:Rule
    val composeRule = createComposeRule()

    private fun showEditor(
        customControls: CustomTouchControlLibrary,
        onCustomControlsChange: (CustomTouchControlLibrary) -> Unit
    ) {
        composeRule.setContent {
            EmuCoreXTheme {
                ControlsEditorScreen(
                    state = ControlsEditorState(customControls = customControls),
                    onBackClick = {},
                    manageActivityOrientation = false,
                    onUpdateControlOffset = { _, _ -> },
                    onUpdateControlOffsets = {},
                    onUpdateControlScale = { _, _ -> },
                    onUpdateControlWidthScale = { _, _ -> },
                    onUpdateControlOpacity = { _, _ -> },
                    onUpdateControlSecondaryAction = { _, _ -> },
                    onSetControlVisible = { _, _ -> },
                    onSetStickSurfaceMode = { _, _ -> },
                    onResetLayout = {},
                    onCustomControlsChange = onCustomControlsChange
                )
            }
        }
    }

    @Test
    fun addDialogCreatesComboButtonWithTwoActions() {
        val saved = AtomicReference<CustomTouchControlLibrary?>()
        showEditor(CustomTouchControlLibrary.Empty, saved::set)

        composeRule.onNodeWithTag("controls_editor_add_button").performClick()
        composeRule.onNodeWithTag("controls_editor_combo_primary_square").performScrollTo().performClick()
        composeRule.onNodeWithTag("controls_editor_combo_secondary_l1").performScrollTo().performClick()
        composeRule.onNodeWithTag("controls_editor_combo_confirm").performScrollTo().performClick()

        composeRule.runOnIdle {
            val control = saved.get()?.controls?.single()
            assertEquals("square", control?.actionId)
            assertEquals("l1", control?.secondaryActionId)
        }
    }

    @Test
    fun duplicateCreatesOffsetCopyThatKeepsComboAction() {
        val saved = AtomicReference<CustomTouchControlLibrary?>()
        val initial = CustomTouchControlLibrary(
            controls = listOf(
                CustomTouchControl(
                    id = "jump",
                    name = "Jump",
                    actionId = "cross",
                    secondaryActionId = "l1",
                    positionX = 0.3f,
                    positionY = 0.4f
                )
            )
        )
        showEditor(initial, saved::set)

        composeRule.onNodeWithTag("controls_editor_custom_jump", useUnmergedTree = true).performClick()
        composeRule.waitForIdle()
        composeRule.onNodeWithTag("controls_editor_duplicate").performClick()

        composeRule.runOnIdle {
            val controls = saved.get()?.controls.orEmpty()
            assertEquals(2, controls.size)
            val copy = controls.first { it.id != "jump" }
            assertEquals("cross", copy.actionId)
            assertEquals("l1", copy.secondaryActionId)
            assertTrue(copy.positionX > 0.3f)
            assertTrue(copy.positionY > 0.4f)
        }
    }

    @Test
    fun comboDialogUpdatesSecondaryActionOfSelectedControl() {
        val saved = AtomicReference<CustomTouchControlLibrary?>()
        val initial = CustomTouchControlLibrary(
            controls = listOf(
                CustomTouchControl(id = "jump", name = "Jump", actionId = "cross")
            )
        )
        showEditor(initial, saved::set)

        composeRule.onNodeWithTag("controls_editor_custom_jump", useUnmergedTree = true).performClick()
        composeRule.onNodeWithTag("controls_editor_combo").performClick()
        composeRule.onNodeWithTag("controls_editor_combo_secondary_circle").performScrollTo().performClick()
        composeRule.onNodeWithTag("controls_editor_combo_confirm").performScrollTo().performClick()

        composeRule.runOnIdle {
            val control = saved.get()?.controls?.single()
            assertEquals("cross", control?.actionId)
            assertEquals("circle", control?.secondaryActionId)
        }
    }
}

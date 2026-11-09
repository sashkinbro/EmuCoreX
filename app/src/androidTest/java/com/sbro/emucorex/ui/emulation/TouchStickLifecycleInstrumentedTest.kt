package com.sbro.emucorex.ui.emulation

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.ui.common.VectorAnalogStick
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TouchStickLifecycleInstrumentedTest {
    @get:Rule val composeRule = createComposeRule()

    @Test
    fun disposingUnownedGestureAreaDoesNotReleaseVisibleOrPhysicalSticks() {
        val shown = mutableStateOf(true)
        val events = mutableListOf<Triple<Int, Int, Boolean>>()
        composeRule.setContent {
            EmuCoreXTheme {
                if (shown.value) TouchButtonGroup(
                    specs = emptyList(), inputWidth = 300.dp, inputHeight = 200.dp,
                    touchscreenRightStick = true, touchscreenRightStickSensitivity = 1f,
                    invertRightStick = false, invertRightStickHorizontal = false,
                    onPadInput = { key, range, pressed -> events += Triple(key, range, pressed) },
                    visualStyle = TouchControlVisualStyle.CLASSIC,
                    pressEffect = TouchControlPressEffect.GROW, onTouchHaptic = {}
                )
            }
        }
        composeRule.runOnIdle { shown.value = false }
        composeRule.runOnIdle { assertTrue(events.isEmpty()) }
    }

    @Test
    fun disposingOwnedGestureReleasesOnlyThatStick() {
        val shown = mutableStateOf(true)
        val events = mutableListOf<Triple<Int, Int, Boolean>>()
        composeRule.setContent {
            EmuCoreXTheme {
                if (shown.value) TouchButtonGroup(
                    specs = emptyList(), inputWidth = 300.dp, inputHeight = 200.dp,
                    touchscreenRightStick = true, touchscreenRightStickSensitivity = 1f,
                    invertRightStick = false, invertRightStickHorizontal = false,
                    onPadInput = { key, range, pressed -> events += Triple(key, range, pressed) },
                    visualStyle = TouchControlVisualStyle.CLASSIC,
                    pressEffect = TouchControlPressEffect.GROW, onTouchHaptic = {},
                    modifier = Modifier.testTag("gestures")
                )
            }
        }
        composeRule.onNodeWithTag("gestures").performTouchInput {
            down(Offset(width * 0.5f, height * 0.5f))
            moveTo(Offset(width * 0.8f, height * 0.5f))
        }
        composeRule.runOnIdle { events.clear(); shown.value = false }
        composeRule.runOnIdle {
            assertEquals(listOf(Triple(120, 0, false), Triple(121, 0, false),
                Triple(122, 0, false), Triple(123, 0, false)), events)
        }
    }

    @Test
    fun visibleStickUsesLatestCallbackDuringDragAndForRelease() {
        val generation = mutableStateOf(0)
        val events = mutableListOf<Triple<Int, Float, Float>>()
        composeRule.setContent {
            EmuCoreXTheme {
                val capturedGeneration = generation.value
                VectorAnalogStick(analogSize = 200.dp,
                    modifier = Modifier.testTag("stick"),
                    onValueChange = { x, y -> events += Triple(capturedGeneration, x, y) })
            }
        }
        composeRule.onNodeWithTag("stick").performTouchInput {
            down(Offset(width * 0.75f, height * 0.5f))
        }
        composeRule.runOnIdle { assertTrue(events.isNotEmpty()); events.clear(); generation.value = 1 }
        composeRule.waitForIdle()
        composeRule.onNodeWithTag("stick").performTouchInput {
            moveTo(Offset(width * 0.9f, height * 0.5f))
            up()
        }
        composeRule.runOnIdle {
            assertTrue(events.isNotEmpty())
            assertTrue(events.all { it.first == 1 })
            assertEquals(Triple(1, 0f, 0f), events.last())
        }
    }
}

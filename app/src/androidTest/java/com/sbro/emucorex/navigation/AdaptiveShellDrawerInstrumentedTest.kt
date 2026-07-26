package com.sbro.emucorex.navigation

import androidx.compose.material3.Text
import androidx.compose.foundation.clickable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.runtime.mutableStateOf
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import com.sbro.emucorex.ui.theme.ThemeMode
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AdaptiveShellDrawerInstrumentedTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun feedbackDestinationDoesNotComposeModalDrawer() {
        composeRule.setContent {
            EmuCoreXTheme {
                TestShell(selected = PrimaryDestination.Feedback, onBackClick = {})
            }
        }

        val drawerNodes = composeRule
            .onAllNodesWithTag("adaptive_shell_modal_drawer", useUnmergedTree = true)
            .fetchSemanticsNodes()
        assertEquals(0, drawerNodes.size)
    }

    @Test
    fun homeDestinationStillComposesModalDrawer() {
        composeRule.setContent {
            EmuCoreXTheme {
                TestShell(selected = PrimaryDestination.Home, onBackClick = null)
            }
        }

        val drawerNodes = composeRule
            .onAllNodesWithTag("adaptive_shell_modal_drawer", useUnmergedTree = true)
            .fetchSemanticsNodes()
        assertEquals(1, drawerNodes.size)
    }

    @Test
    fun feedbackDestinationStaysDrawerFreeAfterStateRestoration() {
        val restorationTester = StateRestorationTester(composeRule)
        restorationTester.setContent {
            EmuCoreXTheme {
                TestShell(selected = PrimaryDestination.Feedback, onBackClick = {})
            }
        }

        assertEquals(0, modalDrawerNodeCount())
        restorationTester.emulateSavedInstanceStateRestore()
        assertEquals(0, modalDrawerNodeCount())
    }

    @Test
    fun openHomeDrawerIsClosedAfterStateRestoration() {
        val restorationTester = StateRestorationTester(composeRule)
        restorationTester.setContent {
            EmuCoreXTheme {
                TestShell(selected = PrimaryDestination.Home, onBackClick = null)
            }
        }

        composeRule.mainClock.advanceTimeBy(500)
        composeRule.onNodeWithTag("test_open_drawer").performClick()
        composeRule.mainClock.advanceTimeBy(500)
        composeRule.onNodeWithTag("adaptive_shell_drawer_sheet", useUnmergedTree = true)
            .assertIsDisplayed()

        restorationTester.emulateSavedInstanceStateRestore()
        composeRule.mainClock.advanceTimeBy(500)
        composeRule.onNodeWithTag("adaptive_shell_drawer_sheet", useUnmergedTree = true)
            .assertIsNotDisplayed()
    }

    @Test
    fun changingThemeDoesNotOpenHomeDrawer() {
        val themeMode = mutableStateOf(ThemeMode.DARK)
        composeRule.setContent {
            EmuCoreXTheme(themeMode = themeMode.value) {
                TestShell(selected = PrimaryDestination.Home, onBackClick = null)
            }
        }

        composeRule.mainClock.advanceTimeBy(500)
        composeRule.onNodeWithTag("adaptive_shell_drawer_sheet", useUnmergedTree = true)
            .assertIsNotDisplayed()

        composeRule.runOnIdle { themeMode.value = ThemeMode.LIGHT }
        composeRule.mainClock.advanceTimeBy(500)
        composeRule.onNodeWithTag("adaptive_shell_drawer_sheet", useUnmergedTree = true)
            .assertIsNotDisplayed()

        composeRule.runOnIdle { themeMode.value = ThemeMode.CUSTOM }
        composeRule.mainClock.advanceTimeBy(500)
        composeRule.onNodeWithTag("adaptive_shell_drawer_sheet", useUnmergedTree = true)
            .assertIsNotDisplayed()
    }

    private fun modalDrawerNodeCount(): Int = composeRule
        .onAllNodesWithTag("adaptive_shell_modal_drawer", useUnmergedTree = true)
        .fetchSemanticsNodes()
        .size
}

@androidx.compose.runtime.Composable
private fun TestShell(selected: PrimaryDestination, onBackClick: (() -> Unit)?) {
    AdaptiveShell(
        selected = selected,
        onNavigateHome = {},
        onNavigateSearch = {},
        onNavigateFormats = {},
        onNavigateSettings = {},
        onNavigateAchievements = {},
        onBackClick = onBackClick
    ) { openDrawer ->
        Text(
            text = "Test content",
            modifier = Modifier
                .testTag("test_open_drawer")
                .clickable(enabled = openDrawer != null) { openDrawer?.invoke() }
        )
    }
}

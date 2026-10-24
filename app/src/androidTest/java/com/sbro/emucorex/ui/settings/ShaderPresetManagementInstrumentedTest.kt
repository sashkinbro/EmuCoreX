package com.sbro.emucorex.ui.settings

import android.content.Context
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.R
import com.sbro.emucorex.data.RetroArchShaderPreset
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ShaderPresetManagementInstrumentedTest {
    @get:Rule val compose = createComposeRule()
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test fun managementProtectsNoneAndInheritanceAndRequiresConfirmation() {
        val alpha = RetroArchShaderPreset("Alpha", "/presets/alpha.slangp")
        val beta = RetroArchShaderPreset("Beta", "/presets/beta.slangp")
        val presets = mutableStateOf(listOf(alpha, beta))
        val removed = mutableListOf<Set<String>>()
        var selections = 0
        var attempts = 0
        compose.setContent {
            EmuCoreXTheme {
                ShaderPresetSelector(
                    title = "Shader choice",
                    presets = presets.value,
                    selectedPath = alpha.absolutePath,
                    onSelect = { selections++ },
                    helpText = "Help",
                    leadingOptions = listOf("inherit" to "Use global"),
                    onRemove = { paths ->
                        attempts++
                        if (attempts == 1) false else {
                            removed.add(paths)
                            presets.value = presets.value.filterNot { it.absolutePath in paths }
                            true
                        }
                    }
                )
            }
        }
        val delete = context.getString(R.string.delete)
        compose.onNodeWithText("Shader choice").performClick()
        compose.onNodeWithText(context.getString(R.string.settings_shader_presets_manage)).performClick()
        compose.onNodeWithText(context.getString(R.string.settings_general_tab)).performClick()
        // Opening a category scrolls to its current shader; protected leading options may be offscreen.
        compose.onNode(hasScrollAction()).performScrollToNode(hasText("Use global"))
        compose.onNodeWithText("Use global").assertIsNotEnabled()
        val none = context.getString(R.string.settings_shader_preset_none)
        compose.onNode(hasScrollAction()).performScrollToNode(hasText(none))
        compose.onNodeWithText(none).assertIsNotEnabled()
        compose.onNode(hasScrollAction()).performScrollToNode(hasText("Alpha"))
        compose.onAllNodesWithText("Alpha").onLast().performClick()
        compose.onNodeWithText(delete).performClick()
        compose.onNodeWithText(context.getString(R.string.settings_shader_presets_remove_body, 1)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.cancel)).performClick()
        compose.runOnIdle { assertEquals(emptyList<Set<String>>(), removed) }
        compose.onNodeWithText(delete).performClick()
        compose.onAllNodesWithText(delete).onLast().performClick()
        compose.waitForIdle()
        compose.runOnIdle { assertEquals(emptyList<Set<String>>(), removed) }
        compose.onAllNodesWithText("Alpha").onLast().assertIsDisplayed()
        compose.onNodeWithText(delete).performClick()
        compose.onAllNodesWithText(delete).onLast().performClick()
        compose.waitForIdle()
        compose.runOnIdle {
            assertEquals(2, attempts)
            assertEquals(listOf(setOf(alpha.absolutePath)), removed)
            assertEquals(0, selections)
        }
        compose.onAllNodesWithText("Alpha").assertCountEquals(0)
        compose.onNodeWithText("Beta").assertIsDisplayed()
    }
    @Test fun installedPackCanBeRetriedWheneverNoInstallIsRunning() {
        val busy = mutableStateOf(false)
        var clicks = 0
        compose.setContent {
            EmuCoreXTheme {
                ShaderPackDownloadItem(installed = true, busy = busy.value, progress = null,
                    onClick = { clicks++ })
            }
        }
        val download = context.getString(R.string.settings_shader_pack_download)
        compose.onNodeWithText(download).assertIsEnabled().performClick()
        compose.runOnIdle { busy.value = true }
        compose.onNodeWithText(download).assertIsNotEnabled()
        compose.runOnIdle { busy.value = false }
        // Settings cards deliberately debounce repeated physical taps for 550 ms.
        android.os.SystemClock.sleep(600)
        compose.onNodeWithText(download).assertIsEnabled().performClick()
        compose.runOnIdle { assertEquals(2, clicks) }
    }

}

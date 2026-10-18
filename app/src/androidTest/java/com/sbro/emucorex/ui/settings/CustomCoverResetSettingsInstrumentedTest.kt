package com.sbro.emucorex.ui.settings

import android.content.Context
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.R
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomCoverResetSettingsInstrumentedTest {
    @get:Rule val composeRule = createComposeRule()

    @Test
    fun libraryOffersBulkResetBelowCacheClearingAndAllowsCancel() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        composeRule.setContent { EmuCoreXTheme { SettingsScreen(initialTab = "library") } }
        val reset = composeRule.onNodeWithText(context.getString(R.string.settings_reset_custom_covers))
        reset.performScrollTo().assertIsDisplayed()
        val clear = composeRule.onNodeWithText(context.getString(R.string.settings_clear_cover_cache))
        assertTrue(clear.fetchSemanticsNode().boundsInRoot.top < reset.fetchSemanticsNode().boundsInRoot.top)
        saveScreenshot(context, "custom-cover-reset-settings.png")
        reset.performClick()
        composeRule.onNodeWithText(context.getString(R.string.settings_reset_custom_covers_confirm_title))
            .assertIsDisplayed()
        composeRule.onNodeWithText(context.getString(R.string.settings_reset_custom_covers_confirm_body))
            .assertIsDisplayed()
        saveScreenshot(context, "custom-cover-reset-confirmation.png")
        composeRule.onNodeWithText(context.getString(android.R.string.cancel)).performClick()
        composeRule.onNodeWithText(context.getString(R.string.settings_reset_custom_covers_confirm_title))
            .assertDoesNotExist()
    }
    private fun saveScreenshot(context: Context, name: String) {
        composeRule.waitForIdle()
        android.os.SystemClock.sleep(350) // Allow the platform dialog window animation to finish.
        androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()?.let { bitmap ->
            java.io.File(context.getExternalFilesDir("logs"), name).outputStream().use {
                bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
            }
            bitmap.recycle()
        }
    }

}

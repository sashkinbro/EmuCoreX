package com.sbro.emucorex.ui.onboarding

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorex.R
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SupportedConsolesCardInstrumentedTest {
    @get:Rule val compose = createComposeRule()

    @Test fun cardShowsAndHidesSupportedFormats() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.setContent { MaterialTheme { SupportedConsolesCard() } }
        compose.onNodeWithText(context.getString(R.string.onboarding_supported_consoles)).assertIsDisplayed()
        compose.onNodeWithText("${context.getString(R.string.home_platform_ps2)} · ${context.getString(R.string.home_platform_arcade)}").assertIsDisplayed()
        compose.onNodeWithText(".acgame").assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.formats_title)).performClick()
        compose.onNodeWithText(".bin  .chd  .cso  .elf  .gz  .img  .iso  .mdf  .zso").assertIsDisplayed()
        compose.onNodeWithText(".acgame").assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.formats_title)).performClick()
        compose.onNodeWithText(".acgame").assertDoesNotExist()
    }
}



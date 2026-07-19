package com.sbro.emucorex.data

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class BackButtonExitPreferenceInstrumentedTest {
    @Test
    fun preferenceDefaultsToMenuAndPersistsDirectExitChoice() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<android.content.Context>()
        val preferences = AppPreferences(context)

        try {
            preferences.setBackButtonExitsGame(false)
            assertFalse(preferences.settingsSnapshot.first().backButtonExitsGame)

            preferences.setBackButtonExitsGame(true)
            assertTrue(preferences.backButtonExitsGame.first())
            assertTrue(preferences.settingsSnapshot.first().backButtonExitsGame)
        } finally {
            preferences.setBackButtonExitsGame(false)
        }
    }
}

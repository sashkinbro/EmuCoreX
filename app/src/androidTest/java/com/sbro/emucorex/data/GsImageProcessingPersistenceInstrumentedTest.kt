package com.sbro.emucorex.data

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.core.GsHackDefaults
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GsImageProcessingPersistenceInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    @Test
    fun globalSettingsPersistAndRejectInvalidCoreValues() = runBlocking {
        val preferences = AppPreferences(context)
        preferences.setDeinterlaceMode(8)
        preferences.setDithering(0)

        assertEquals(8, preferences.deinterlaceMode.first())
        assertEquals(0, preferences.dithering.first())

        preferences.setDeinterlaceMode(100)
        preferences.setDithering(-100)
        assertEquals(GsHackDefaults.DEINTERLACE_MODE_DEFAULT, preferences.deinterlaceMode.first())
        assertEquals(GsHackDefaults.DITHERING_DEFAULT, preferences.dithering.first())
    }

    @Test
    fun globalSettingsSurviveJsonBackupRoundTrip() = runBlocking {
        val preferences = AppPreferences(context)
        preferences.setDeinterlaceMode(9)
        preferences.setDithering(1)
        val backup = preferences.exportJson()

        preferences.setDeinterlaceMode(0)
        preferences.setDithering(2)
        preferences.importJson(backup)

        assertEquals(9, preferences.deinterlaceMode.first())
        assertEquals(1, preferences.dithering.first())
    }

    @Test
    fun perGameSettingsRoundTripWithIndependentValues() {
        val repository = PerGameSettingsRepository(context)
        val gameKey = "image-processing-${System.nanoTime()}"
        try {
            repository.save(
                PerGameSettings(
                    gameKey = gameKey,
                    gameTitle = "Image processing test",
                    deinterlaceMode = 4,
                    dithering = 3,
                    providedKeys = setOf("deinterlaceMode", "dithering")
                )
            )

            val restored = requireNotNull(repository.get(gameKey))
            assertEquals(4, restored.deinterlaceMode)
            assertEquals(3, restored.dithering)
            assertEquals(setOf("deinterlaceMode", "dithering"), restored.providedKeys)
        } finally {
            repository.delete(gameKey)
        }
    }
}

package com.sbro.emucorex.ui.home

import android.app.Application
import android.os.SystemClock
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class HomeSortPersistenceInstrumentedTest {
    @Test fun selectionSurvivesLibraryRecreationAndProcessRestart() {
        val application = ApplicationProvider.getApplicationContext<Application>()
        val preferences = AppPreferences(application)
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val phase = InstrumentationRegistry.getArguments().getString("sortPersistencePhase") ?: "roundTrip"
        val modes = if (phase == "roundTrip") HomeSortOption.entries else listOf(HomeSortOption.PLATFORM)
        for (mode in modes) {
            val store = ViewModelStore()
            lateinit var model: HomeViewModel
            instrumentation.runOnMainSync {
                model = HomeViewModel(application)
                store.put("home", model)
            }
            try {
                if (phase != "read") {
                    instrumentation.runOnMainSync { model.updateSortOption(mode) }
                    val deadline = SystemClock.elapsedRealtime() + 5_000
                    var stored = ""
                    while (SystemClock.elapsedRealtime() < deadline) {
                        stored = runBlocking { preferences.exportJson().optString("homeLibrarySortOption") }
                        if (stored == mode.name) break
                        SystemClock.sleep(20)
                    }
                    assertEquals("Sort choice must be persisted before recreating the library", mode.name, stored)
                }
                if (phase != "write") {
                    if (phase == "roundTrip") {
                        instrumentation.runOnMainSync {
                            store.clear()
                            model = HomeViewModel(application)
                            store.put("home", model)
                        }
                    }
                    val restored = runBlocking {
                        withTimeout(5_000) { model.uiState.first { it.sortOption == mode }.sortOption }
                    }
                    assertEquals(mode, restored)
                }
            } finally {
                instrumentation.runOnMainSync { store.clear() }
            }
        }
    }
}

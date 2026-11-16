package com.sbro.emucorex.ui.emulation

import android.app.Application
import android.os.SystemClock
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.saveTouchControlsLayout
import com.sbro.emucorex.data.toTouchControlsLayoutProfile
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TouchControlsLayoutPersistenceDeviceTest {
    @Test
    fun hidingLeftStickAndPairedDpadSurvivesSavingAndReloading() = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val application = ApplicationProvider.getApplicationContext<Application>()
        val preferences = AppPreferences(application)
        val original = preferences.overlayLayoutSnapshot.first().toTouchControlsLayoutProfile()
        val baseline = (AppPreferences.defaultOverlayControlLayouts(original.stickScale) + original.controlLayouts)
            .toMutableMap().apply {
                this["left_stick"] = getValue("left_stick").copy(visible = true)
                this["dpad_toggle"] = getValue("dpad_toggle").copy(visible = false)
            }
        preferences.saveTouchControlsLayout(original.copy(controlLayouts = baseline))
        val store = ViewModelStore()
        lateinit var viewModel: EmulationViewModel
        instrumentation.runOnMainSync {
            viewModel = ViewModelProvider(store, ViewModelProvider.AndroidViewModelFactory(application))[
                EmulationViewModel::class.java
            ]
        }
        try {
            // These are the two consecutive callbacks made by the editor's eye button.
            instrumentation.runOnMainSync {
                viewModel.setTouchControlVisible("left_stick", false)
                viewModel.setTouchControlVisible("dpad_toggle", false)
            }
            val deadline = SystemClock.elapsedRealtime() + 10_000
            while (SystemClock.elapsedRealtime() < deadline) {
                val layouts = viewModel.uiState.value.controlLayouts
                val stored = preferences.overlayLayoutSnapshot.first().controlLayouts
                if (layouts["left_stick"]?.visible == false &&
                    layouts["dpad_toggle"]?.visible == false &&
                    stored["left_stick"]?.visible == false &&
                    stored["dpad_toggle"]?.visible == false) break
                Thread.sleep(25)
            }
            // Allow any later collector emission or save continuation to settle.
            Thread.sleep(500)
            assertFalse("Live overlay must keep the left stick hidden",
                viewModel.uiState.value.controlLayouts.getValue("left_stick").visible)
            val reloaded = AppPreferences(application).overlayLayoutSnapshot.first().controlLayouts
            assertFalse("Reloading preferences must keep the left stick hidden", reloaded.getValue("left_stick").visible)
            assertFalse("The paired D-pad must stay hidden", reloaded.getValue("dpad_toggle").visible)
        } finally {
            instrumentation.runOnMainSync { store.clear() }
            preferences.saveTouchControlsLayout(original)
        }
    }
}

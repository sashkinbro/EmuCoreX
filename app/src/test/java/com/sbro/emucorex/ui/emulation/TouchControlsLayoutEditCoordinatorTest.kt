package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.TouchControlsLayoutProfile
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class TouchControlsLayoutEditCoordinatorTest {
    @Test
    fun pairedVisibilitySaveWaitsForTheFirstEditToReachTheLiveLayout() = runBlocking {
        val edits = TouchControlsLayoutEditCoordinator()
        var live = TouchControlsLayoutProfile()
        var stored = live
        val saved = CompletableDeferred<Unit>()
        val publish = CompletableDeferred<Unit>()

        val hideStick = async(start = CoroutineStart.UNDISPATCHED) {
            edits.edit {
                val layouts = live.controlLayouts.toMutableMap()
                layouts["left_stick"] = layouts.getValue("left_stick").copy(visible = false)
                stored = live.copy(controlLayouts = layouts)
                saved.complete(Unit)
                // Reproduce the gap after the IO transaction releases its lock,
                // before the continuation on Main publishes the saved layout.
                publish.await()
                live = stored
            }
        }
        saved.await()
        var pairedEditStarted = false
        val hideDpad = async(start = CoroutineStart.UNDISPATCHED) {
            edits.edit {
                pairedEditStarted = true
                val layouts = live.controlLayouts.toMutableMap()
                layouts["dpad_toggle"] = layouts.getValue("dpad_toggle").copy(visible = false)
                stored = live.copy(controlLayouts = layouts)
                live = stored
            }
        }

        assertFalse(pairedEditStarted)
        publish.complete(Unit)
        withTimeout(2_000) { hideStick.await(); hideDpad.await() }
        assertFalse(live.controlLayouts.getValue("left_stick").visible)
        assertFalse(stored.controlLayouts.getValue("left_stick").visible)
        assertFalse(live.controlLayouts.getValue("dpad_toggle").visible)
        assertEquals(stored, live)
    }

    @Test
    fun resetCannotBeOverwrittenByAnEarlierSavePublication() = runBlocking {
        val edits = TouchControlsLayoutEditCoordinator()
        val defaults = AppPreferences.defaultOverlayControlLayouts()
        var live = defaults
        val publish = CompletableDeferred<Unit>()
        val edit = async(start = CoroutineStart.UNDISPATCHED) {
            edits.edit {
                val changed = live.toMutableMap().apply {
                    this["right_stick"] = getValue("right_stick").copy(visible = false)
                }
                publish.await()
                live = changed
            }
        }
        val reset = async(start = CoroutineStart.UNDISPATCHED) {
            edits.edit { live = defaults }
        }
        publish.complete(Unit)
        withTimeout(2_000) { edit.await(); reset.await() }
        assertEquals(defaults, live)
    }
}

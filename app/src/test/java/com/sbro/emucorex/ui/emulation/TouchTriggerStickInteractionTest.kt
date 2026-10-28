package com.sbro.emucorex.ui.emulation

import org.junit.Assert.assertEquals
import org.junit.Test

class TouchTriggerStickInteractionTest {
    @Test
    fun normalTouchTriggersDoNotReleaseTheRightStick() {
        val events = mutableListOf<Triple<Int, Int, Boolean>>()
        val callback: (Int, Int, Boolean) -> Unit = { key, value, pressed ->
            events += Triple(key, value, pressed)
        }
        dispatchTouchTrigger(true, false, 104, 122, callback)
        dispatchTouchTrigger(true, false, 105, 120, callback)
        dispatchTouchTrigger(false, false, 104, 122, callback)
        dispatchTouchTrigger(false, false, 105, 120, callback)
        assertEquals(listOf(Triple(104, 255, true), Triple(105, 255, true),
            Triple(104, 0, false), Triple(105, 0, false)), events)
    }

    @Test
    fun remappedTouchTriggersControlTheAssignedStickDirections() {
        val events = mutableListOf<Triple<Int, Int, Boolean>>()
        val callback: (Int, Int, Boolean) -> Unit = { key, value, pressed ->
            events += Triple(key, value, pressed)
        }
        dispatchTouchTrigger(true, true, 104, 122, callback)
        dispatchTouchTrigger(true, true, 105, 120, callback)
        dispatchTouchTrigger(false, true, 104, 122, callback)
        dispatchTouchTrigger(false, true, 105, 120, callback)
        assertEquals(listOf(Triple(122, 255, true), Triple(120, 255, true),
            Triple(122, 0, false), Triple(120, 0, false)), events)
    }
}

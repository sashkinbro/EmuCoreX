package com.sbro.emucorex.ui.emulation

import org.junit.Assert.assertEquals
import org.junit.Test

class TouchscreenRightStickGestureTest {
    @Test
    fun `center remains neutral`() {
        val value = calculateRightStickGestureValue(0f, 0f, 100f)

        assertEquals(0f, value.x, 0.0001f)
        assertEquals(0f, value.y, 0.0001f)
    }

    @Test
    fun `movement is normalized against gesture radius`() {
        val value = calculateRightStickGestureValue(50f, -25f, 100f)

        assertEquals(0.5f, value.x, 0.0001f)
        assertEquals(-0.25f, value.y, 0.0001f)
    }

    @Test
    fun `diagonal movement is clamped radially`() {
        val value = calculateRightStickGestureValue(100f, 100f, 100f)

        assertEquals(0.7071f, value.x, 0.0001f)
        assertEquals(0.7071f, value.y, 0.0001f)
    }

    @Test
    fun `invalid radius returns neutral`() {
        val value = calculateRightStickGestureValue(50f, 50f, 0f)

        assertEquals(0f, value.x, 0.0001f)
        assertEquals(0f, value.y, 0.0001f)
    }
}

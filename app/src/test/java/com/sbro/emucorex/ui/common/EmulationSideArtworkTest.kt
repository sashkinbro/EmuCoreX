package com.sbro.emucorex.ui.common

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EmulationSideArtworkTest {
    @Test
    fun `stretch never exposes artwork gutters`() {
        assertFalse(calculateSideArtworkGutters(2400, 1080, 0).isVisible)
    }

    @Test
    fun `four by three centers equal side gutters on wide screen`() {
        val gutters = calculateSideArtworkGutters(2400, 1080, 2)

        assertEquals(480, gutters.leftPx)
        assertEquals(480, gutters.rightPx)
    }

    @Test
    fun `sixteen by nine hides artwork on matching display`() {
        assertFalse(calculateSideArtworkGutters(1920, 1080, 3).isVisible)
    }

    @Test
    fun `native renderer rectangle defines exact safe gutters`() {
        val gutters = calculateSideArtworkGutters(
            2400, 1080, 1, floatArrayOf(470.4f, 0f, 1929.6f, 1080f)
        )
        assertEquals(470, gutters.leftPx)
        assertEquals(470, gutters.rightPx)
    }

    @Test
    fun `auto starts with four by three until renderer rectangle arrives`() {
        val gutters = calculateSideArtworkGutters(2400, 1080, 1)

        assertEquals(480, gutters.leftPx)
        assertEquals(480, gutters.rightPx)
    }

    @Test
    fun `ten by seven leaves balanced integer gutters`() {
        val gutters = calculateSideArtworkGutters(2533, 1080, 4)

        assertTrue(gutters.isVisible)
        assertEquals(495, gutters.leftPx)
        assertEquals(495, gutters.rightPx)
    }

    @Test
    fun `invalid dimensions and unknown modes are safe`() {
        assertEquals(SideArtworkGutters(0, 0), calculateSideArtworkGutters(0, 1080, 2))
        assertEquals(SideArtworkGutters(0, 0), calculateSideArtworkGutters(1920, 1080, 99))
    }
}

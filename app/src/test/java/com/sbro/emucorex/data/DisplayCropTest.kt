package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DisplayCropTest {
    @Test
    fun defaultCropDoesNotRemoveGamePixels() {
        assertEquals(DisplayCrop(0, 0, 0, 0), DisplayCrop.None)
        assertTrue(DisplayCrop.None.isDisabled)
    }

    @Test
    fun cropIsClampedIndependentlyOnEverySide() {
        val sanitized = DisplayCrop(
            left = -10,
            top = 3,
            right = 999,
            bottom = 8
        ).sanitized()

        assertEquals(DisplayCrop(0, 3, 64, 8), sanitized)
    }

    @Test
    fun conservativePresetsAreSymmetric() {
        assertEquals(DisplayCrop(2, 2, 2, 2), DisplayCrop.ThinEdges)
        assertEquals(DisplayCrop(4, 4, 4, 4), DisplayCrop.SafeEdges)
    }
}

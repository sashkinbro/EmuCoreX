package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomTouchControlTest {
    @Test
    fun libraryRoundTripPreservesMultipleControls() {
        val library = CustomTouchControlLibrary(
            controls = listOf(
                CustomTouchControl(
                    id = "jump",
                    name = "Jump",
                    actionId = "cross",
                    secondaryActionId = "l1",
                    pressMode = CustomTouchControlPressMode.TOGGLE,
                    rotationDegrees = 24,
                    contentScalePercent = 135,
                    shadowElevationDp = 8f
                ),
                CustomTouchControl(id = "brake", name = "Brake", actionId = "l2")
            )
        )

        val decoded = CustomTouchControlLibrary.decode(library.encode())

        assertEquals(2, decoded.controls.size)
        assertEquals("l1", decoded.controls.first().secondaryActionId)
        assertEquals(CustomTouchControlPressMode.TOGGLE, decoded.controls.first().pressMode)
        assertEquals(24, decoded.controls.first().rotationDegrees)
        assertEquals(135, decoded.controls.first().contentScalePercent)
        assertEquals(8f, decoded.controls.first().shadowElevationDp)
        assertEquals("l2", decoded.controls[1].actionId)
    }

    @Test
    fun sanitizerClampsGeometryAndRejectsUnknownAction() {
        val safe = CustomTouchControl(
            id = "safe",
            actionId = "launch_missiles",
            positionX = Float.NaN,
            positionY = 4f,
            widthDp = 1,
            heightDp = 9_999,
            opacity = 0,
            borderWidthDp = Float.POSITIVE_INFINITY,
            secondaryActionId = CustomTouchControl.DEFAULT_ACTION_ID,
            rotationDegrees = 999,
            contentScalePercent = 1,
            shadowElevationDp = Float.NaN
        ).sanitized()!!

        assertEquals(CustomTouchControl.DEFAULT_ACTION_ID, safe.actionId)
        assertTrue(safe.positionX in 0f..1f)
        assertEquals(1f, safe.positionY)
        assertEquals(CustomTouchControl.MIN_SIZE_DP, safe.widthDp)
        assertEquals(CustomTouchControl.MAX_SIZE_DP, safe.heightDp)
        assertEquals(CustomTouchControl.MIN_OPACITY, safe.opacity)
        assertEquals(0f, safe.borderWidthDp)
        assertNull(safe.secondaryActionId)
        assertEquals(CustomTouchControl.MAX_ROTATION_DEGREES, safe.rotationDegrees)
        assertEquals(CustomTouchControl.MIN_CONTENT_SCALE_PERCENT, safe.contentScalePercent)
        assertEquals(0f, safe.shadowElevationDp)
    }

    @Test
    fun blankIdCannotBePersisted() {
        assertNull(CustomTouchControl(id = " ").sanitized())
    }

    @Test
    fun duplicateIdsAreRemoved() {
        val library = CustomTouchControlLibrary(
            controls = listOf(
                CustomTouchControl(id = "same", name = "First"),
                CustomTouchControl(id = "same", name = "Second")
            )
        ).sanitized()

        assertEquals(1, library.controls.size)
        assertEquals("First", library.controls.single().name)
    }
}

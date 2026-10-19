package com.sbro.emucorex.ui.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ControlsEditorVisibilityTest {

    @Test
    fun `hiding the toggle stick keeps its d-pad hidden too`() {
        val result = resolveControlVisibilityToggle(
            controlId = "left_stick",
            nextVisible = false,
            toggleStickId = "left_stick"
        )

        assertEquals(false, result.controlVisible)
        assertEquals("dpad_toggle", result.pairedControlId)
        assertEquals(false, result.pairedControlVisible)
    }

    @Test
    fun `showing the toggle stick hides the d-pad face`() {
        val result = resolveControlVisibilityToggle(
            controlId = "left_stick",
            nextVisible = true,
            toggleStickId = "left_stick"
        )

        assertEquals(true, result.controlVisible)
        assertEquals("dpad_toggle", result.pairedControlId)
        assertEquals(false, result.pairedControlVisible)
    }

    @Test
    fun `showing the d-pad face hides the stick`() {
        val result = resolveControlVisibilityToggle(
            controlId = "dpad_toggle",
            nextVisible = true,
            toggleStickId = "left_stick"
        )

        assertEquals(true, result.controlVisible)
        assertEquals("left_stick", result.pairedControlId)
        assertEquals(false, result.pairedControlVisible)
    }

    @Test
    fun `hiding the d-pad face leaves the stick alone`() {
        val result = resolveControlVisibilityToggle(
            controlId = "dpad_toggle",
            nextVisible = false,
            toggleStickId = "left_stick"
        )

        assertEquals(false, result.controlVisible)
        assertNull(result.pairedControlId)
    }

    @Test
    fun `unrelated controls have no paired visibility`() {
        val result = resolveControlVisibilityToggle(
            controlId = "dpad_cluster",
            nextVisible = false,
            toggleStickId = "left_stick"
        )

        assertEquals(false, result.controlVisible)
        assertNull(result.pairedControlId)
    }
}

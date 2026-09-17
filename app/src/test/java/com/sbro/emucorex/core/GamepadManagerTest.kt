package com.sbro.emucorex.core

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GamepadManagerTest {
    @Test
    fun twoControllersAreAssignedToPlayerOneAndPlayerTwo() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true
        )

        assertEquals(linkedMapOf(41 to 0, 72 to 1), assignments)
    }

    @Test
    fun existingControllerOrderStaysStableWhenAndroidEnumerationChanges() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = mapOf(41 to 0, 72 to 1),
            connectedDeviceIds = listOf(72, 41),
            singleGamepadReplacesTouch = true
        )

        assertEquals(linkedMapOf(41 to 0, 72 to 1), assignments)
    }

    @Test
    fun externalControllerReplacesBuiltInHandheldAsPlayerOne() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = mapOf(41 to 0),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            preferExternalGamepadAsPlayerOne = true,
            externalDeviceIds = setOf(72)
        )

        assertEquals(linkedMapOf(72 to 0, 41 to 1), assignments)
    }

    @Test
    fun externalControllerPriorityCanBeDisabledForLocalMultiplayer() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = mapOf(41 to 0),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            preferExternalGamepadAsPlayerOne = false,
            externalDeviceIds = setOf(72)
        )

        assertEquals(linkedMapOf(41 to 0, 72 to 1), assignments)
    }

    @Test
    fun builtInControllerReturnsToPlayerOneAfterExternalDisconnects() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = mapOf(72 to 0, 41 to 1),
            connectedDeviceIds = listOf(41),
            singleGamepadReplacesTouch = true,
            preferExternalGamepadAsPlayerOne = true,
            externalDeviceIds = emptySet()
        )

        assertEquals(linkedMapOf(41 to 0), assignments)
    }

    @Test
    fun touchPlusGamepadUsesPlayerTwoForOnePhysicalController() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41),
            singleGamepadReplacesTouch = false
        )

        assertEquals(linkedMapOf(41 to 1), assignments)
    }

    @Test
    fun secondControllerPromotesPhysicalPadsToPlayerOneAndPlayerTwo() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = mapOf(41 to 1),
            connectedDeviceIds = listOf(72, 41),
            singleGamepadReplacesTouch = false
        )

        assertEquals(linkedMapOf(41 to 0, 72 to 1), assignments)
    }

    @Test
    fun onlyTwoPhysicalControllersReceiveSlots() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72, 93),
            singleGamepadReplacesTouch = true
        )

        assertEquals(2, assignments.size)
        assertTrue(assignments.keys.containsAll(listOf(41, 72)))
    }

    @Test
    fun explicitAssignmentMovesSelectedControllerToPlayerOne() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            deviceKeysByDeviceId = mapOf(41 to "xbox", 72 to "mangmi"),
            padDeviceKeys = mapOf(0 to "mangmi")
        )

        assertEquals(linkedMapOf(72 to 0, 41 to 1), assignments)
    }

    @Test
    fun explicitAssignmentOverridesExternalControllerPriority() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            preferExternalGamepadAsPlayerOne = true,
            externalDeviceIds = setOf(72),
            deviceKeysByDeviceId = mapOf(41 to "built-in", 72 to "external"),
            padDeviceKeys = mapOf(0 to "built-in")
        )

        assertEquals(linkedMapOf(41 to 0, 72 to 1), assignments)
    }

    @Test
    fun explicitPlayerTwoAssignmentKeepsOtherControllerOnPlayerOne() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            deviceKeysByDeviceId = mapOf(41 to "a", 72 to "b"),
            padDeviceKeys = mapOf(1 to "a")
        )

        assertEquals(linkedMapOf(72 to 0, 41 to 1), assignments)
    }

    @Test
    fun ignoredControllerIsExcludedFromSlots() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            deviceKeysByDeviceId = mapOf(41 to "xbox", 72 to "mangmi"),
            ignoredDeviceKeys = setOf("xbox")
        )

        assertEquals(linkedMapOf(72 to 0), assignments)
    }

    @Test
    fun explicitAssignmentOfDisconnectedControllerFallsBackToAutomatic() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            deviceKeysByDeviceId = mapOf(41 to "a", 72 to "b"),
            padDeviceKeys = mapOf(0 to "disconnected")
        )

        assertEquals(linkedMapOf(41 to 0, 72 to 1), assignments)
    }

    @Test
    fun ignoredExplicitControllerFallsBackToRemainingDevice() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41, 72),
            singleGamepadReplacesTouch = true,
            deviceKeysByDeviceId = mapOf(41 to "a", 72 to "b"),
            padDeviceKeys = mapOf(0 to "a"),
            ignoredDeviceKeys = setOf("a")
        )

        assertEquals(linkedMapOf(72 to 0), assignments)
    }

    @Test
    fun explicitPlayerOneAssignmentWinsInTouchPlusGamepadMode() {
        val assignments = GamepadManager.assignConnectedGamepadSlots(
            previousAssignments = emptyMap(),
            connectedDeviceIds = listOf(41),
            singleGamepadReplacesTouch = false,
            deviceKeysByDeviceId = mapOf(41 to "xbox"),
            padDeviceKeys = mapOf(0 to "xbox")
        )

        assertEquals(linkedMapOf(41 to 0), assignments)
    }

    @Test
    fun defaultButtonMappingIsPreservedWithoutOverrides() {
        assertEquals(
            "square",
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, emptyMap())
        )
        assertEquals(
            "r2",
            GamepadManager.resolveMappedActionIdForTriggerAxis("r2", emptyMap())
        )
    }

    @Test
    fun movingActionSuppressesItsOldDefaultButton() {
        val bindings = mapOf("r2" to KeyEvent.KEYCODE_BUTTON_X)

        assertEquals(
            "r2",
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings)
        )
        assertNull(
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_R2, bindings)
        )
        assertNull(GamepadManager.resolveMappedActionIdForTriggerAxis("r2", bindings))
    }

    @Test
    fun squareAndR2SwapAppliesToButtonsAndAnalogTriggerAxis() {
        val bindings = mapOf(
            "square" to KeyEvent.KEYCODE_BUTTON_R2,
            "r2" to KeyEvent.KEYCODE_BUTTON_X
        )

        assertEquals(
            "square",
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_R2, bindings)
        )
        assertEquals(
            "r2",
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings)
        )
        assertEquals("square", GamepadManager.resolveMappedActionIdForTriggerAxis("r2", bindings))
    }

    @Test
    fun analogTriggerRecognizesAndroidAlternateButtonCode() {
        val bindings = mapOf(
            "square" to KeyEvent.KEYCODE_BUTTON_8,
            "r2" to KeyEvent.KEYCODE_BUTTON_X
        )

        assertEquals("square", GamepadManager.resolveMappedActionIdForTriggerAxis("r2", bindings))
    }

    @Test
    fun phoneFallbackAttenuatesDualShockSmallMotor() {
        assertEquals(
            0.35f,
            GamepadManager.resolveRumbleIntensity(
                largeMotor = 0f,
                smallMotor = 1f,
                strength = 1f,
                systemFallback = true
            ),
            0.001f
        )
    }

    @Test
    fun physicalControllerKeepsOriginalMotorIntensity() {
        assertEquals(
            1f,
            GamepadManager.resolveRumbleIntensity(
                largeMotor = 0f,
                smallMotor = 1f,
                strength = 1f,
                systemFallback = false
            ),
            0.001f
        )
    }
}

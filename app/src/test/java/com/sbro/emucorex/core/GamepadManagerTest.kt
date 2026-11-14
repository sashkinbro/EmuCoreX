package com.sbro.emucorex.core

import android.view.KeyEvent
import com.sbro.emucorex.data.GamepadBindingRules
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
    fun gunBindingUsesTheSameButtonOnlyInGunMode() {
        val bindings = mapOf("gun_trigger" to KeyEvent.KEYCODE_BUTTON_X)
        GamepadManager.setLightGunModeActive(false)
        try {
            assertEquals(
                "square",
                GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings)
            )
            GamepadManager.setLightGunModeActive(true)
            assertEquals(
                "gun_trigger",
                GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings)
            )
        } finally {
            GamepadManager.setLightGunModeActive(false)
        }
    }

    @Test
    fun assigningGunButtonKeepsExistingCustomPs2Binding() {
        val bindings = GamepadBindingRules.assign(
            mapOf("square" to KeyEvent.KEYCODE_BUTTON_B),
            "gun_trigger",
            KeyEvent.KEYCODE_BUTTON_B
        )
        assertEquals(KeyEvent.KEYCODE_BUTTON_B, bindings["square"])
        GamepadManager.setLightGunModeActive(false)
        try {
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_B, bindings))
            GamepadManager.setLightGunModeActive(true)
            assertEquals("gun_trigger", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_B, bindings))
        } finally {
            GamepadManager.setLightGunModeActive(false)
        }
    }

    @Test
    fun gameBindingsOverrideOnlyTheirOwnActions() {
        val global = mapOf(0 to mapOf(
            "square" to KeyEvent.KEYCODE_BUTTON_X,
            "gun_pedal" to KeyEvent.KEYCODE_BUTTON_L1
        ))
        val game = mapOf(0 to mapOf("gun_trigger" to KeyEvent.KEYCODE_BUTTON_X))
        val effective = GamepadBindingRules.merge(global, game).getValue(0)

        assertEquals(KeyEvent.KEYCODE_BUTTON_X, effective["square"])
        assertEquals(KeyEvent.KEYCODE_BUTTON_X, effective["gun_trigger"])
        assertEquals(KeyEvent.KEYCODE_BUTTON_L1, effective["gun_pedal"])
        assertEquals(global, GamepadBindingRules.merge(global, emptyMap()))
    }

    @Test
    fun gunBindingOnAnalogTriggerDoesNotStealPs2Trigger() {
        val bindings = mapOf("gun_trigger" to KeyEvent.KEYCODE_BUTTON_R2)
        GamepadManager.setLightGunModeActive(false)
        try {
            assertEquals("r2", GamepadManager.resolveMappedActionIdForTriggerAxis("r2", bindings))
            GamepadManager.setLightGunModeActive(true)
            assertEquals("gun_trigger", GamepadManager.resolveMappedActionIdForTriggerAxis("r2", bindings))
        } finally {
            GamepadManager.setLightGunModeActive(false)
        }
    }

    @Test
    fun arcadeCoinBindingDoesNotStealFightingButtonInPs2Games() {
        val bindings = mapOf("coin" to KeyEvent.KEYCODE_BUTTON_X)
        GamepadManager.setArcadeModeActive(false)
        try {
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
            GamepadManager.setArcadeModeActive(true)
            assertEquals("coin", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
        } finally {
            GamepadManager.setArcadeModeActive(false)
        }
    }

    @Test
    fun gunArcadeFighterAndPs2ContextsKeepTheirOwnAssignments() {
        val bindings = GamepadBindingRules.assign(
            mapOf("square" to KeyEvent.KEYCODE_BUTTON_X),
            "gun_trigger", KeyEvent.KEYCODE_BUTTON_X
        )
        GamepadManager.setArcadeModeActive(true)
        GamepadManager.setLightGunModeActive(true)
        try {
            assertEquals("gun_trigger", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
            GamepadManager.setLightGunModeActive(false)
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
            GamepadManager.setArcadeModeActive(false)
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
        } finally {
            GamepadManager.setLightGunModeActive(false)
            GamepadManager.setArcadeModeActive(false)
        }
    }

    @Test
    fun perGameGunRemapSurvivesSwitchBackToRegularPs2Controls() {
        val global = mapOf(0 to mapOf(
            "square" to KeyEvent.KEYCODE_BUTTON_X,
            "gun_trigger" to KeyEvent.KEYCODE_BUTTON_X,
            "gun_pedal" to KeyEvent.KEYCODE_BUTTON_L1
        ))
        val perGame = mapOf(0 to mapOf("gun_trigger" to KeyEvent.KEYCODE_BUTTON_B))
        val bindings = GamepadBindingRules.merge(global, perGame).getValue(0)

        GamepadManager.setArcadeModeActive(true)
        GamepadManager.setLightGunModeActive(true)
        try {
            assertEquals("gun_trigger", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_B, bindings))
            assertEquals("gun_pedal", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_L1, bindings))
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))

            GamepadManager.setLightGunModeActive(false)
            GamepadManager.setArcadeModeActive(false)
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
            assertEquals("circle", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_B, bindings))
            assertEquals("l1", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_L1, bindings))
        } finally {
            GamepadManager.setLightGunModeActive(false)
            GamepadManager.setArcadeModeActive(false)
        }
    }

    @Test
    fun perGamePs2RemapDoesNotEraseGlobalGunBinding() {
        val global = mapOf(0 to mapOf("gun_trigger" to KeyEvent.KEYCODE_BUTTON_X))
        val perGame = mapOf(0 to mapOf("square" to KeyEvent.KEYCODE_BUTTON_B))
        val bindings = GamepadBindingRules.merge(global, perGame).getValue(0)

        GamepadManager.setLightGunModeActive(false)
        try {
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_B, bindings))
            assertNull(GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
            GamepadManager.setLightGunModeActive(true)
            assertEquals("gun_trigger", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings))
            assertEquals("square", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_B, bindings))
        } finally {
            GamepadManager.setLightGunModeActive(false)
        }
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
    fun stickDirectionActionsAreMappableToPhysicalButtons() {
        val actions = GamepadManager.mappableButtonActions().associateBy { it.id }

        assertEquals(110, actions.getValue("left_stick_up").padKey)
        assertEquals(112, actions.getValue("left_stick_down").padKey)
        assertEquals(113, actions.getValue("left_stick_left").padKey)
        assertEquals(111, actions.getValue("left_stick_right").padKey)
        assertEquals(120, actions.getValue("right_stick_up").padKey)
        assertEquals(122, actions.getValue("right_stick_down").padKey)
        assertEquals(123, actions.getValue("right_stick_left").padKey)
        assertEquals(121, actions.getValue("right_stick_right").padKey)

        val bindings = GamepadBindingRules.assign(
            emptyMap(),
            "left_stick_up",
            KeyEvent.KEYCODE_DPAD_UP
        )
        assertEquals(
            "left_stick_up",
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_DPAD_UP, bindings)
        )
    }

    @Test
    fun bindingAStickDirectionReplacesTheButtonOnItsOldAction() {
        val bindings = GamepadBindingRules.assign(
            mapOf("square" to KeyEvent.KEYCODE_BUTTON_X),
            "right_stick_right",
            KeyEvent.KEYCODE_BUTTON_X
        )

        assertNull(bindings["square"])
        assertEquals(
            "right_stick_right",
            GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_X, bindings)
        )
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

    @Test
    fun scancodeOnlyKeysUseNegativeSyntheticCodes() {
        val code = GamepadManager.physicalKeyCode(KeyEvent.KEYCODE_UNKNOWN, 0x13c)
        assertEquals(-(0x10000 or 0x13c), code)
        assertTrue(GamepadManager.isScanButtonCode(code))
        assertEquals(KeyEvent.KEYCODE_BUTTON_A, GamepadManager.physicalKeyCode(KeyEvent.KEYCODE_BUTTON_A, 0x130))
        assertEquals(KeyEvent.KEYCODE_UNKNOWN, GamepadManager.physicalKeyCode(KeyEvent.KEYCODE_UNKNOWN, 0))
    }

    @Test
    fun axisButtonCodesDoNotCollideWithKeyOrScanCodes() {
        val code = GamepadManager.axisButtonCode(MotionEvent_AXIS_GENERIC_1)
        assertTrue(GamepadManager.isAxisButtonCode(code))
        assertTrue(!GamepadManager.isAxisButtonCode(KeyEvent.KEYCODE_BUTTON_16))
        assertTrue(!GamepadManager.isAxisButtonCode(-(0x10000 or 5)))
    }

    @Test
    fun systemKeysAreNeverBindable() {
        listOf(
            KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_HOME, KeyEvent.KEYCODE_POWER,
            KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_APP_SWITCH,
            KeyEvent.KEYCODE_ASSIST
        ).forEach { assertTrue(GamepadManager.isReservedSystemKey(it)) }
        assertTrue(!GamepadManager.isReservedSystemKey(KeyEvent.KEYCODE_BUTTON_MODE))
    }

    @Test
    fun syntheticAndTurboBindingsResolveToTheirActions() {
        val scan = GamepadManager.physicalKeyCode(KeyEvent.KEYCODE_UNKNOWN, 0x2c4)
        val axis = GamepadManager.axisButtonCode(MotionEvent_AXIS_GENERIC_1)
        val bindings = mapOf(
            "fast_forward" to scan,
            "turbo_cross" to axis,
            "pause" to KeyEvent.KEYCODE_BUTTON_MODE
        )
        assertEquals("fast_forward", GamepadManager.resolveMappedActionIdForKeyCode(scan, bindings))
        assertEquals("turbo_cross", GamepadManager.resolveMappedActionIdForKeyCode(axis, bindings))
        assertEquals("pause", GamepadManager.resolveMappedActionIdForKeyCode(KeyEvent.KEYCODE_BUTTON_MODE, bindings))
        assertTrue(GamepadManager.isTurboAction("turbo_cross"))
        assertEquals("cross", GamepadManager.turboBaseActionId("turbo_cross"))
    }

    @Test
    fun fastForwardHoldersEmitOnlyOnEmptyTransitions() {
        val holders = FastForwardHolders()
        assertEquals(true, holders.set("start:0", true))
        assertNull(holders.set("ff:7:99", true))
        assertNull(holders.set("start:0", false))
        assertEquals(false, holders.set("ff:7:99", false))
        assertNull(holders.set("ff:7:99", false))
    }

    @Test
    fun fastForwardHoldersDropADisconnectedDevice() {
        val holders = FastForwardHolders()
        holders.set("ff:7:99", true)
        holders.set("ff:8:99", true)
        assertNull(holders.removeWhere { it.startsWith("ff:7:") })
        assertEquals(false, holders.removeWhere { it.startsWith("ff:8:") })
    }

    @Test
    fun axisButtonsNeedARealPushFromARestingBaseline() {
        assertTrue(GamepadManager.axisButtonPressed(0f, 1f))
        assertTrue(GamepadManager.axisButtonPressed(-1f, 1f))
        assertTrue(!GamepadManager.axisButtonPressed(0f, 0.5f))
        assertTrue(!GamepadManager.axisButtonPressed(1f, 1f))
        assertTrue(!GamepadManager.axisButtonPressed(0.8f, 1f))
    }

    private companion object {
        // android.view.MotionEvent.AXIS_GENERIC_1
        const val MotionEvent_AXIS_GENERIC_1 = 32
    }
}

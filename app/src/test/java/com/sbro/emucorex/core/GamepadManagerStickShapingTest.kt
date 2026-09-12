package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class GamepadManagerStickShapingTest {
    private val epsilon = 1e-4f

    private fun shape(
        value: Float,
        deadzone: Float = 0f,
        sensitivity: Float = 1f,
        negativeDeadzone: Float = 0f,
        antiDeadzone: Float = 0f,
        curve: Float = 1f
    ): Float = GamepadManager.shapeStickAxis(
        value = value,
        deadzone = deadzone,
        sensitivity = sensitivity,
        negativeDeadzone = negativeDeadzone,
        antiDeadzone = antiDeadzone,
        curve = curve
    )

    @Test
    fun linearResponseIsIdentityWithoutShaping() {
        assertEquals(0.5f, shape(0.5f), epsilon)
        assertEquals(-0.5f, shape(-0.5f), epsilon)
    }

    @Test
    fun innerDeadzoneZeroesCenterNoise() {
        assertEquals(0f, shape(0.1f, deadzone = 0.15f), epsilon)
        assertEquals(0f, shape(-0.1f, deadzone = 0.15f), epsilon)
        assertEquals(0f, shape(0.15f, deadzone = 0.15f), epsilon)
    }

    @Test
    fun innerDeadzoneRenormalizesPastDeadzone() {
        assertEquals(0.5f, shape(0.575f, deadzone = 0.15f), epsilon)
        assertEquals(-0.5f, shape(-0.575f, deadzone = 0.15f), epsilon)
    }

    @Test
    fun innerDeadzoneIsClampedToSupportedRange() {
        assertEquals(0f, shape(0.3f, deadzone = 1f), epsilon)
        assertEquals(0.5f, shape(0.675f, deadzone = 1f), epsilon)
    }

    @Test
    fun negativeDeadzoneShrinksEffectiveDeadzone() {
        // With a 15% deadzone and 30% negative deadzone the effective inner
        // deadzone is 15% * (1 - 0.3) = 10.5%, so 11% deflection finally registers.
        assertEquals(0f, shape(0.1f, deadzone = 0.15f), epsilon)
        assertEquals(0f, shape(0.15f * 0.7f, deadzone = 0.15f, negativeDeadzone = 0.3f), epsilon)
        val justPastEffectiveDeadzone = shape(0.11f, deadzone = 0.15f, negativeDeadzone = 0.3f)
        assertEquals(true, justPastEffectiveDeadzone > 0f)
    }

    @Test
    fun negativeDeadzoneActsAsGainWithoutDeadzone() {
        assertEquals(0.3571f, shape(0.25f, negativeDeadzone = 0.3f), epsilon)
        assertEquals(1f, shape(0.75f, negativeDeadzone = 0.3f), epsilon)
        assertEquals(1f, shape(1f, negativeDeadzone = 0.3f), epsilon)
    }

    @Test
    fun negativeDeadzoneIsClampedToSupportedRange() {
        assertEquals(shape(0.25f, negativeDeadzone = 0.3f), shape(0.25f, negativeDeadzone = 1f), epsilon)
    }

    @Test
    fun antiDeadzoneLiftsOutputFloor() {
        // (0.2 - 0.15) / 0.85 = 0.0588 -> floor 0.5 + 0.0588 * 0.5.
        assertEquals(0.5294f, shape(0.2f, deadzone = 0.15f, antiDeadzone = 0.5f), epsilon)
        assertEquals(-0.5294f, shape(-0.2f, deadzone = 0.15f, antiDeadzone = 0.5f), epsilon)
    }

    @Test
    fun antiDeadzoneNeverMovesTrueCenter() {
        assertEquals(0f, shape(0.1f, deadzone = 0.15f, antiDeadzone = 0.5f), epsilon)
        assertEquals(0f, shape(0f, antiDeadzone = 0.5f), epsilon)
    }

    @Test
    fun curveAboveOneGivesFinerControlNearCenter() {
        assertEquals(0.25f, shape(0.5f, curve = 2f), epsilon)
        assertEquals(0.0625f, shape(0.25f, curve = 2f), epsilon)
    }

    @Test
    fun curveBelowOneReactsFasterNearCenter() {
        assertEquals(0.5f, shape(0.25f, curve = 0.5f), epsilon)
        assertEquals(0.7071f, shape(0.5f, curve = 0.5f), epsilon)
    }

    @Test
    fun sensitivityScalesLinearlyAndClamps() {
        assertEquals(0.5f, shape(0.25f, sensitivity = 2f), epsilon)
        assertEquals(1f, shape(0.75f, sensitivity = 2f), epsilon)
    }

    @Test
    fun fullPipelineKeepsNegativeSign() {
        // abs: (0.55 - 0.1) / 0.9 = 0.5; curve^2 -> 0.25; * 1.5 -> 0.375;
        // anti 0.2 + 0.375 * 0.8 = 0.5, restored as -0.5.
        assertEquals(-0.5f, shape(-0.55f, deadzone = 0.1f, sensitivity = 1.5f, antiDeadzone = 0.2f, curve = 2f), epsilon)
    }
}

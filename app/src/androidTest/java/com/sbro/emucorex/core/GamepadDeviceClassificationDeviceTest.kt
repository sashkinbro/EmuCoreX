package com.sbro.emucorex.core

import android.content.Context
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Opt-in: feed androidTest/assets/input/controller-classification.jsonl into `adb shell hid -`
 * and keep stdin open until the run ends. Pass -e hidDeviceFixtures true to opt in.
 * Use default controller mappings and assignments, with only these controller fixtures connected.
 * These are synthetic HID descriptors, not a claim to reproduce a specific Ipega firmware.
 */
@RunWith(AndroidJUnit4::class)
class GamepadDeviceClassificationDeviceTest {
    @Before
    fun requireFixtures() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("hidDeviceFixtures") == "true")
        onMain {
            GamepadManager.ensureInitialized(ApplicationProvider.getApplicationContext<Context>())
            assertFalse("Run controller routing tests without a game", NativeApp.hasValidVm())
        }
    }

    @Test
    fun bluetoothConsumerControlNameDoesNotRejectRealGamepadCapabilities() {
        assertController("EmuCoreX Test Consumer Control")
    }

    @Test
    fun bluetoothSystemControlNameDoesNotRejectRealGamepadCapabilities() {
        assertController("EmuCoreX Test System Control")
    }

    @Test
    fun usbControllerStillUsesTheGamepadPath() {
        assertController("EmuCoreX Test USB Gamepad")
    }

    @Test
    fun mediaKeyboardMouseAndFingerprintDevicesAreNotGamepads() {
        listOf("EmuCoreX Test Media Consumer Control", "EmuCoreX Test Keyboard",
            "EmuCoreX Test Mouse", "uinput-fpc EmuCoreX Test").forEach { name ->
            assertFalse("$name must remain outside the controller path", GamepadManager.isGameController(device(name)))
        }
        assertFalse(GamepadManager.isGameController(null))
    }

    @Test
    fun gamepadButtonsCannotBeCapturedAsKeyboardBindings() = onMain {
        var captured: Int? = null
        KeyboardMouseManager.startKeyboardBindingCapture(0, "cross") { captured = it }
        try {
            val event = key(device("EmuCoreX Test Consumer Control"), KeyEvent.ACTION_DOWN,
                KeyEvent.KEYCODE_BUTTON_A, InputDevice.SOURCE_GAMEPAD)
            assertFalse("Keyboard capture must leave the gamepad event alone",
                KeyboardMouseManager.handleKeyBindingCapture(event))
            assertNull(captured)
        } finally {
            KeyboardMouseManager.cancelBindingCapture()
        }
    }

    @Test
    fun actualKeyboardBindingsStillCaptureKeys() = onMain {
        var captured: Int? = null
        KeyboardMouseManager.startKeyboardBindingCapture(0, "cross") { captured = it }
        try {
            assertTrue(KeyboardMouseManager.handleKeyBindingCapture(
                key(device("EmuCoreX Test Keyboard"), KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_A, InputDevice.SOURCE_KEYBOARD)))
            assertEquals(KeyEvent.KEYCODE_A, captured)
        } finally {
            KeyboardMouseManager.cancelBindingCapture()
        }
    }

    @Test
    fun liveKeyboardMappingsCannotStealGamepadKeys() = onMain {
        val previousOverride = KeyboardMouseManager.gameOverride.value
        val previousEnabled = GamepadManager.isEmulationInputEnabled()
        KeyboardMouseManager.setPerGameOverride(KeyboardMouseManager.settings.value.copy(
            keyboardEnabled = true, keyboardTargetPad = 0,
            keyboardBindingsByPad = mapOf(0 to mapOf("cross" to KeyEvent.KEYCODE_BUTTON_A))))
        GamepadManager.setEmulationInputEnabled(true)
        try {
            assertFalse("Even a matching keyboard binding must leave controller keys alone",
                KeyboardMouseManager.handleKeyEvent(key(device("EmuCoreX Test Consumer Control"),
                    KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A, InputDevice.SOURCE_GAMEPAD)))
        } finally {
            KeyboardMouseManager.releaseAll()
            KeyboardMouseManager.setPerGameOverride(previousOverride)
            GamepadManager.setEmulationInputEnabled(previousEnabled)
        }
    }

    @Test
    fun mouseButtonsStillUseTheMousePath() = onMain {
        val previousOverride = KeyboardMouseManager.gameOverride.value
        val previousEnabled = GamepadManager.isEmulationInputEnabled()
        KeyboardMouseManager.setPerGameOverride(KeyboardMouseManager.settings.value.copy(
            mouseEnabled = true, mouseBindingsByPad = emptyMap()))
        GamepadManager.setEmulationInputEnabled(true)
        val event = MotionEvent.obtain(0L, SystemClock.uptimeMillis(), MotionEvent.ACTION_DOWN, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0; toolType = MotionEvent.TOOL_TYPE_MOUSE }),
            arrayOf(MotionEvent.PointerCoords()), 0, MotionEvent.BUTTON_PRIMARY, 1f, 1f,
            device("EmuCoreX Test Mouse").id, 0, InputDevice.SOURCE_MOUSE, 0)
        try {
            assertFalse("Mouse motion must not enter the gamepad path", GamepadManager.handleMotionEvent(event))
            assertTrue("Mouse buttons must still use their own mappings", KeyboardMouseManager.handleMouseButtonEvent(event))
        } finally {
            event.recycle()
            KeyboardMouseManager.releaseAll()
            KeyboardMouseManager.setPerGameOverride(previousOverride)
            GamepadManager.setEmulationInputEnabled(previousEnabled)
        }
    }

    @Test
    fun acceptedBluetoothGamepadRoutesButtonsAndJoystickMotion() = onMain {
        val gamepad = device("EmuCoreX Test Consumer Control")
        val previousEnabled = GamepadManager.isEmulationInputEnabled()
        GamepadManager.setEmulationInputEnabled(true)
        val event = MotionEvent.obtain(0L, SystemClock.uptimeMillis(), MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }),
            arrayOf(MotionEvent.PointerCoords().apply { setAxisValue(MotionEvent.AXIS_X, 0.75f) }),
            0, 0, 1f, 1f, gamepad.id, 0, InputDevice.SOURCE_JOYSTICK, 0)
        try {
            assertTrue("Bluetooth gamepad key down must reach its mapping",
                GamepadManager.handleKeyEvent(key(gamepad, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A, InputDevice.SOURCE_GAMEPAD)))
            assertTrue("Bluetooth gamepad key up must release the mapping",
                GamepadManager.handleKeyEvent(key(gamepad, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BUTTON_A, InputDevice.SOURCE_GAMEPAD)))
            assertTrue("Bluetooth joystick axes must remain on the gamepad path", GamepadManager.handleMotionEvent(event))
        } finally {
            event.recycle()
            GamepadManager.setEmulationInputEnabled(previousEnabled)
        }
    }

    private fun assertController(name: String) {
        val gamepad = device(name)
        assertFalse("UHID fixture must be a real Android input device", gamepad.isVirtual)
        assertTrue(gamepad.supportsSource(InputDevice.SOURCE_GAMEPAD))
        assertTrue(gamepad.motionRanges.any { it.axis == MotionEvent.AXIS_X })
        assertTrue("Capabilities must win over the device name: $name", GamepadManager.isGameController(gamepad))
    }

    private fun device(name: String): InputDevice = InputDevice.getDeviceIds().asSequence()
        .mapNotNull { InputDevice.getDevice(it) }.firstOrNull { it.name == name }
        ?: throw AssertionError("Missing required UHID fixture: $name")

    private fun key(device: InputDevice, action: Int, code: Int, source: Int) =
        KeyEvent(0L, SystemClock.uptimeMillis(), action, code, 0, 0, device.id, 0, 0, source)

    private fun onMain(action: () -> Unit) = InstrumentationRegistry.getInstrumentation().runOnMainSync(action)
}

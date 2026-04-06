package com.sbro.emucorex.core

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlin.math.abs

object GamepadManager {
    data class MappableButtonAction(
        val id: String,
        val padKey: Int,
        val defaultKeyCodes: List<Int>
    )

    private const val ANALOG_DEADZONE = 0.15f
    @Volatile
    private var emulationInputEnabled = false
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    @Volatile
    private var initialized = false
    @Volatile
    private var bindingCaptureListener: ((Int) -> Unit)? = null
    @Volatile
    private var customBindingsByAction: Map<String, Int> = emptyMap()
    @Volatile
    private var customBindingsByKeyCode: Map<Int, Int> = emptyMap()

    private object PadKey {
        const val Up = 19
        const val Right = 22
        const val Down = 20
        const val Left = 21
        const val Triangle = 100
        const val Circle = 97
        const val Cross = 96
        const val Square = 99
        const val Select = 109
        const val Start = 108
        const val L1 = 102
        const val L2 = 104
        const val R1 = 103
        const val R2 = 105
        const val L3 = 106
        const val R3 = 107
        const val LeftStickUp = 110
        const val LeftStickRight = 111
        const val LeftStickDown = 112
        const val LeftStickLeft = 113
        const val RightStickUp = 120
        const val RightStickRight = 121
        const val RightStickDown = 122
        const val RightStickLeft = 123
    }

    private val mappableActions = listOf(
        MappableButtonAction("cross", PadKey.Cross, listOf(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_1)),
        MappableButtonAction("circle", PadKey.Circle, listOf(KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_2)),
        MappableButtonAction("square", PadKey.Square, listOf(KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_3)),
        MappableButtonAction("triangle", PadKey.Triangle, listOf(KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_4)),
        MappableButtonAction("l1", PadKey.L1, listOf(KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_5)),
        MappableButtonAction("r1", PadKey.R1, listOf(KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_6)),
        MappableButtonAction("l2", PadKey.L2, listOf(KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_7)),
        MappableButtonAction("r2", PadKey.R2, listOf(KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_8)),
        MappableButtonAction("l3", PadKey.L3, listOf(KeyEvent.KEYCODE_BUTTON_THUMBL)),
        MappableButtonAction("r3", PadKey.R3, listOf(KeyEvent.KEYCODE_BUTTON_THUMBR)),
        MappableButtonAction("select", PadKey.Select, listOf(KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_9)),
        MappableButtonAction("start", PadKey.Start, listOf(KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_10)),
        MappableButtonAction("dpad_up", PadKey.Up, listOf(KeyEvent.KEYCODE_DPAD_UP)),
        MappableButtonAction("dpad_down", PadKey.Down, listOf(KeyEvent.KEYCODE_DPAD_DOWN)),
        MappableButtonAction("dpad_left", PadKey.Left, listOf(KeyEvent.KEYCODE_DPAD_LEFT)),
        MappableButtonAction("dpad_right", PadKey.Right, listOf(KeyEvent.KEYCODE_DPAD_RIGHT))
    )
    private val actionsById = mappableActions.associateBy { it.id }

    private var prevLeftX = 0f
    private var prevLeftY = 0f
    private var prevRightX = 0f
    private var prevRightY = 0f
    private var prevLT = 0f
    private var prevRT = 0f
    private var prevHatX = 0f
    private var prevHatY = 0f

    fun ensureInitialized(context: android.content.Context) {
        if (initialized) return
        initialized = true
        val preferences = AppPreferences(context.applicationContext)
        scope.launch {
            preferences.gamepadBindings.collectLatest { bindings ->
                customBindingsByAction = bindings
                customBindingsByKeyCode = bindings.entries.mapNotNull { (actionId, keyCode) ->
                    actionsById[actionId]?.let { keyCode to it.padKey }
                }.toMap()
            }
        }
    }

    fun mappableButtonActions(): List<MappableButtonAction> = mappableActions

    fun resolveBindingForAction(actionId: String, customBindings: Map<String, Int>): Int? {
        return customBindings[actionId] ?: actionsById[actionId]?.defaultKeyCodes?.firstOrNull()
    }

    fun keyCodeLabel(keyCode: Int): String {
        return when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_1 -> "A"
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_2 -> "B"
            KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_3 -> "X"
            KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_4 -> "Y"
            KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_5 -> "L1"
            KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_6 -> "R1"
            KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_7 -> "L2"
            KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_8 -> "R2"
            KeyEvent.KEYCODE_BUTTON_THUMBL -> "L3"
            KeyEvent.KEYCODE_BUTTON_THUMBR -> "R3"
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_9 -> "Select"
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_10 -> "Start"
            KeyEvent.KEYCODE_DPAD_UP -> "D-Pad Up"
            KeyEvent.KEYCODE_DPAD_DOWN -> "D-Pad Down"
            KeyEvent.KEYCODE_DPAD_LEFT -> "D-Pad Left"
            KeyEvent.KEYCODE_DPAD_RIGHT -> "D-Pad Right"
            else -> KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_").replace('_', ' ')
        }
    }

    fun firstConnectedControllerName(): String? {
        for (deviceId in InputDevice.getDeviceIds()) {
            val device = InputDevice.getDevice(deviceId)
            if (isGameController(device)) {
                return device?.name
            }
        }
        return null
    }

    fun startBindingCapture(onCaptured: (Int) -> Unit) {
        bindingCaptureListener = onCaptured
    }

    fun cancelBindingCapture() {
        bindingCaptureListener = null
    }

    fun handleBindingCapture(event: KeyEvent): Boolean {
        val listener = bindingCaptureListener ?: return false
        if (!isGameController(event.device)) return false
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount != 0) return false
        listener(event.keyCode)
        bindingCaptureListener = null
        return true
    }

    fun isGameController(device: InputDevice?): Boolean {
        if (device == null) return false
        val sources = device.sources
        return (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    fun isGamepadConnected(): Boolean {
        return InputDevice.getDeviceIds().any { id ->
            isGameController(InputDevice.getDevice(id))
        }
    }

    fun setEmulationInputEnabled(enabled: Boolean) {
        emulationInputEnabled = enabled
        if (!enabled) {
            resetAnalogState()
        }
    }

    fun isEmulationInputEnabled(): Boolean = emulationInputEnabled

    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (!emulationInputEnabled) return false
        if (!isGameController(event.device)) return false

        val padKey = mapKeyCodeToPadKey(event.keyCode) ?: return false
        val pressed = event.action == KeyEvent.ACTION_DOWN

        EmulatorBridge.setPadButton(padKey, 0, pressed)
        return true
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (!emulationInputEnabled) return false
        if (!isGameController(event.device)) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false

        // Left stick
        val leftX = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_X))
        val leftY = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_Y))

        if (leftX != prevLeftX || leftY != prevLeftY) {
            dispatchAnalogStick(
                x = leftX,
                y = leftY,
                upKey = PadKey.LeftStickUp,
                rightKey = PadKey.LeftStickRight,
                downKey = PadKey.LeftStickDown,
                leftKey = PadKey.LeftStickLeft
            )
            prevLeftX = leftX
            prevLeftY = leftY
        }

        // Right stick
        val rightX = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_Z))
        val rightY = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_RZ))

        if (rightX != prevRightX || rightY != prevRightY) {
            dispatchAnalogStick(
                x = rightX,
                y = rightY,
                upKey = PadKey.RightStickUp,
                rightKey = PadKey.RightStickRight,
                downKey = PadKey.RightStickDown,
                leftKey = PadKey.RightStickLeft
            )
            prevRightX = rightX
            prevRightY = rightY
        }

        // Triggers (L2/R2 as analog)
        val lt = event.getAxisValue(MotionEvent.AXIS_LTRIGGER)
        val rt = event.getAxisValue(MotionEvent.AXIS_RTRIGGER)

        if (lt != prevLT) {
            dispatchAnalogButton(PadKey.L2, lt)
            prevLT = lt
        }
        if (rt != prevRT) {
            dispatchAnalogButton(PadKey.R2, rt)
            prevRT = rt
        }

        // D-Pad via HAT axes
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)

        if (hatX != prevHatX || hatY != prevHatY) {
            EmulatorBridge.setPadButton(PadKey.Left, 0, hatX < -0.5f)
            EmulatorBridge.setPadButton(PadKey.Right, 0, hatX > 0.5f)
            EmulatorBridge.setPadButton(PadKey.Up, 0, hatY < -0.5f)
            EmulatorBridge.setPadButton(PadKey.Down, 0, hatY > 0.5f)
            prevHatX = hatX
            prevHatY = hatY
        }

        return true
    }

    private fun dispatchAnalogStick(
        x: Float, y: Float,
        upKey: Int, rightKey: Int, downKey: Int, leftKey: Int
    ) {
        dispatchAnalogButton(upKey, (-y).coerceAtLeast(0f))
        dispatchAnalogButton(rightKey, x.coerceAtLeast(0f))
        dispatchAnalogButton(downKey, y.coerceAtLeast(0f))
        dispatchAnalogButton(leftKey, (-x).coerceAtLeast(0f))
    }

    private fun dispatchAnalogButton(key: Int, value: Float) {
        val range = (value.coerceIn(0f, 1f) * 255f).toInt().coerceIn(0, 255)
        EmulatorBridge.setPadButton(key, range, range > 0)
    }

    private fun mapKeyCodeToPadKey(keyCode: Int): Int? {
        customBindingsByKeyCode[keyCode]?.let { return it }
        return when (keyCode) {
            // Standard button mapping (Xbox/PS layout)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_1 -> PadKey.Cross
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_2 -> PadKey.Circle
            KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_3 -> PadKey.Square
            KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_4 -> PadKey.Triangle
            KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_5 -> PadKey.L1
            KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_6 -> PadKey.R1
            KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_7 -> PadKey.L2
            KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_8 -> PadKey.R2
            KeyEvent.KEYCODE_BUTTON_THUMBL -> PadKey.L3
            KeyEvent.KEYCODE_BUTTON_THUMBR -> PadKey.R3
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_9 -> PadKey.Select
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_10 -> PadKey.Start
            // D-Pad via key events
            KeyEvent.KEYCODE_DPAD_UP -> PadKey.Up
            KeyEvent.KEYCODE_DPAD_DOWN -> PadKey.Down
            KeyEvent.KEYCODE_DPAD_LEFT -> PadKey.Left
            KeyEvent.KEYCODE_DPAD_RIGHT -> PadKey.Right
            else -> null
        }
    }

    private fun applyDeadzone(value: Float): Float {
        return if (abs(value) < ANALOG_DEADZONE) 0f else value
    }

    private fun resetAnalogState() {
        prevLeftX = 0f
        prevLeftY = 0f
        prevRightX = 0f
        prevRightY = 0f
        prevLT = 0f
        prevRT = 0f
        prevHatX = 0f
        prevHatY = 0f
    }

}

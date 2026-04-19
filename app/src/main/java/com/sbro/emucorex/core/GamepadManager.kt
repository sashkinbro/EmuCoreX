package com.sbro.emucorex.core

import android.os.Build
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
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
import kotlin.math.roundToInt

object GamepadManager {
    data class MappableButtonAction(
        val id: String,
        val padKey: Int,
        val defaultKeyCodes: List<Int>
    )

    data class ConnectedGamepad(
        val padIndex: Int,
        val deviceId: Int,
        val name: String
    )

    private data class BindingCaptureState(
        val padIndex: Int,
        val onCaptured: (Int) -> Unit
    )

    private data class AnalogState(
        var prevLeftX: Float = 0f,
        var prevLeftY: Float = 0f,
        var prevRightX: Float = 0f,
        var prevRightY: Float = 0f,
        var prevLT: Float = 0f,
        var prevRT: Float = 0f,
        var prevHatX: Float = 0f,
        var prevHatY: Float = 0f
    )

    private data class RumbleState(
        var lastAmplitude: Int = 0,
        var lastUpdateElapsedMs: Long = 0L
    )

    private const val ANALOG_DEADZONE = 0.15f
    private const val MAX_PAD_SLOTS = 2
    private const val RUMBLE_UPDATE_INTERVAL_MS = 40L
    private const val RUMBLE_PULSE_DURATION_MS = 80L

    @Volatile
    private var emulationInputEnabled = false
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    @Volatile
    private var initialized = false
    @Volatile
    private var bindingCaptureState: BindingCaptureState? = null
    @Volatile
    private var vibrationEnabled = true
    @Volatile
    private var customBindingsByPad: Map<Int, Map<String, Int>> = emptyMap()
    @Volatile
    private var customBindingsByPadAndKeyCode: Map<Int, Map<Int, Int>> = emptyMap()

    private val connectionLock = Any()
    private val deviceToPadIndex = linkedMapOf<Int, Int>()
    private val analogStatesByDeviceId = mutableMapOf<Int, AnalogState>()
    private val rumbleStatesByPad = mutableMapOf<Int, RumbleState>()

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

    fun ensureInitialized(context: android.content.Context) {
        if (initialized) return
        initialized = true
        val preferences = AppPreferences(context.applicationContext)
        scope.launch {
            preferences.gamepadBindingsByPad.collectLatest { bindingsByPad ->
                customBindingsByPad = bindingsByPad
                customBindingsByPadAndKeyCode = bindingsByPad.mapValues { (_, bindings) ->
                    bindings.entries.mapNotNull { (actionId, keyCode) ->
                        actionsById[actionId]?.let { keyCode to it.padKey }
                    }.toMap()
                }
            }
        }
        scope.launch {
            preferences.padVibration.collectLatest { enabled ->
                vibrationEnabled = enabled
                if (!enabled) {
                    stopAllGamepadVibrations()
                }
            }
        }
        refreshConnectedGamepads()
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

    fun connectedGamepads(): List<ConnectedGamepad> = refreshConnectedGamepads()

    fun connectedControllerName(padIndex: Int): String? {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        return connectedGamepads().firstOrNull { it.padIndex == normalizedPadIndex }?.name
    }

    fun firstConnectedControllerName(): String? = connectedGamepads().firstOrNull()?.name

    fun startBindingCapture(onCaptured: (Int) -> Unit) {
        startBindingCapture(0, onCaptured)
    }

    fun startBindingCapture(padIndex: Int, onCaptured: (Int) -> Unit) {
        bindingCaptureState = BindingCaptureState(normalizePadIndex(padIndex), onCaptured)
    }

    fun cancelBindingCapture() {
        bindingCaptureState = null
    }

    fun handleBindingCapture(event: KeyEvent): Boolean {
        val captureState = bindingCaptureState ?: return false
        if (!isGameController(event.device)) return false
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount != 0) return false
        captureState.onCaptured(event.keyCode)
        bindingCaptureState = null
        return true
    }

    fun isGameController(device: InputDevice?): Boolean {
        if (device == null) return false
        val sources = device.sources
        return (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    fun isGamepadConnected(): Boolean = connectedGamepads().isNotEmpty()

    fun setEmulationInputEnabled(enabled: Boolean) {
        emulationInputEnabled = enabled
        if (!enabled) {
            resetAnalogState()
            stopAllGamepadVibrations()
            EmulatorBridge.resetKeyStatus()
        }
    }

    fun isEmulationInputEnabled(): Boolean = emulationInputEnabled

    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (!emulationInputEnabled) return false
        if (!isGameController(event.device)) return false

        val padIndex = resolvePadIndexForDevice(event.deviceId) ?: return false
        val padKey = mapKeyCodeToPadKey(padIndex, event.keyCode) ?: return false
        val pressed = event.action == KeyEvent.ACTION_DOWN

        EmulatorBridge.setPadButton(padIndex, padKey, 0, pressed)
        return true
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (!emulationInputEnabled) return false
        if (!isGameController(event.device)) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false

        val padIndex = resolvePadIndexForDevice(event.deviceId) ?: return false
        val state = synchronized(connectionLock) {
            analogStatesByDeviceId.getOrPut(event.deviceId) { AnalogState() }
        }

        val leftX = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_X))
        val leftY = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_Y))
        if (leftX != state.prevLeftX || leftY != state.prevLeftY) {
            dispatchAnalogStick(
                padIndex = padIndex,
                x = leftX,
                y = leftY,
                upKey = PadKey.LeftStickUp,
                rightKey = PadKey.LeftStickRight,
                downKey = PadKey.LeftStickDown,
                leftKey = PadKey.LeftStickLeft
            )
            state.prevLeftX = leftX
            state.prevLeftY = leftY
        }

        val rightX = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_Z))
        val rightY = applyDeadzone(event.getAxisValue(MotionEvent.AXIS_RZ))
        if (rightX != state.prevRightX || rightY != state.prevRightY) {
            dispatchAnalogStick(
                padIndex = padIndex,
                x = rightX,
                y = rightY,
                upKey = PadKey.RightStickUp,
                rightKey = PadKey.RightStickRight,
                downKey = PadKey.RightStickDown,
                leftKey = PadKey.RightStickLeft
            )
            state.prevRightX = rightX
            state.prevRightY = rightY
        }

        val lt = event.getAxisValue(MotionEvent.AXIS_LTRIGGER)
        val rt = event.getAxisValue(MotionEvent.AXIS_RTRIGGER)
        if (lt != state.prevLT) {
            dispatchAnalogButton(padIndex, PadKey.L2, lt)
            state.prevLT = lt
        }
        if (rt != state.prevRT) {
            dispatchAnalogButton(padIndex, PadKey.R2, rt)
            state.prevRT = rt
        }

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        if (hatX != state.prevHatX || hatY != state.prevHatY) {
            EmulatorBridge.setPadButton(padIndex, PadKey.Left, 0, hatX < -0.5f)
            EmulatorBridge.setPadButton(padIndex, PadKey.Right, 0, hatX > 0.5f)
            EmulatorBridge.setPadButton(padIndex, PadKey.Up, 0, hatY < -0.5f)
            EmulatorBridge.setPadButton(padIndex, PadKey.Down, 0, hatY > 0.5f)
            state.prevHatX = hatX
            state.prevHatY = hatY
        }

        return true
    }

    fun onPadVibration(padIndex: Int, largeMotor: Float, smallMotor: Float) {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        if (!vibrationEnabled) {
            stopPadVibration(normalizedPadIndex)
            return
        }

        val connectedGamepad = connectedGamepads().firstOrNull { it.padIndex == normalizedPadIndex } ?: return
        val vibrator = getGamepadVibrator(connectedGamepad.deviceId) ?: return
        if (!vibrator.hasVibrator()) return

        val intensity = maxOf(largeMotor.coerceIn(0f, 1f), smallMotor.coerceIn(0f, 1f))
        val amplitude = (intensity * 255f).roundToInt().coerceIn(0, 255)
        if (amplitude <= 0) {
            stopPadVibration(normalizedPadIndex)
            return
        }

        val now = SystemClock.elapsedRealtime()
        val shouldUpdate = synchronized(connectionLock) {
            val state = rumbleStatesByPad.getOrPut(normalizedPadIndex) { RumbleState() }
            if (state.lastAmplitude == amplitude && (now - state.lastUpdateElapsedMs) < RUMBLE_UPDATE_INTERVAL_MS) {
                false
            } else {
                state.lastAmplitude = amplitude
                state.lastUpdateElapsedMs = now
                true
            }
        }
        if (!shouldUpdate) return

        vibrate(vibrator, amplitude)
    }

    private fun dispatchAnalogStick(
        padIndex: Int,
        x: Float,
        y: Float,
        upKey: Int,
        rightKey: Int,
        downKey: Int,
        leftKey: Int
    ) {
        dispatchAnalogButton(padIndex, upKey, (-y).coerceAtLeast(0f))
        dispatchAnalogButton(padIndex, rightKey, x.coerceAtLeast(0f))
        dispatchAnalogButton(padIndex, downKey, y.coerceAtLeast(0f))
        dispatchAnalogButton(padIndex, leftKey, (-x).coerceAtLeast(0f))
    }

    private fun dispatchAnalogButton(padIndex: Int, key: Int, value: Float) {
        val range = (value.coerceIn(0f, 1f) * 255f).roundToInt().coerceIn(0, 255)
        EmulatorBridge.setPadButton(padIndex, key, range, range > 0)
    }

    private fun mapKeyCodeToPadKey(padIndex: Int, keyCode: Int): Int? {
        customBindingsByPadAndKeyCode[normalizePadIndex(padIndex)]?.get(keyCode)?.let { return it }
        return when (keyCode) {
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

    private fun normalizePadIndex(padIndex: Int): Int = padIndex.coerceIn(0, MAX_PAD_SLOTS - 1)

    private fun resolvePadIndexForDevice(deviceId: Int): Int? {
        refreshConnectedGamepads()
        return synchronized(connectionLock) { deviceToPadIndex[deviceId] }
    }

    private fun refreshConnectedGamepads(): List<ConnectedGamepad> {
        val disconnectedAssignments = mutableListOf<Pair<Int, Int>>()
        val connectedSnapshot = synchronized(connectionLock) {
            val connectedDevices = buildList<InputDevice> {
                for (deviceId in InputDevice.getDeviceIds()) {
                    val device = InputDevice.getDevice(deviceId) ?: continue
                    if (isGameController(device)) {
                        add(device)
                    }
                }
            }
            val connectedDeviceIds = connectedDevices.map { it.id }.toSet()

            val staleDeviceIds = deviceToPadIndex.keys.filter { it !in connectedDeviceIds }
            staleDeviceIds.forEach { deviceId ->
                val padIndex = deviceToPadIndex.remove(deviceId) ?: return@forEach
                analogStatesByDeviceId.remove(deviceId)
                rumbleStatesByPad.remove(padIndex)
                disconnectedAssignments += (padIndex to deviceId)
            }

            val usedPadIndices = deviceToPadIndex.values.toMutableSet()
            connectedDevices.forEach { device ->
                if (deviceToPadIndex.containsKey(device.id)) return@forEach
                val freePadIndex = (0 until MAX_PAD_SLOTS).firstOrNull { it !in usedPadIndices } ?: return@forEach
                deviceToPadIndex[device.id] = freePadIndex
                usedPadIndices += freePadIndex
            }

            connectedDevices.mapNotNull { device ->
                val padIndex = deviceToPadIndex[device.id] ?: return@mapNotNull null
                ConnectedGamepad(
                    padIndex = padIndex,
                    deviceId = device.id,
                    name = device.name.ifBlank { "Controller ${padIndex + 1}" }
                )
            }.sortedBy { it.padIndex }
        }

        disconnectedAssignments.forEach { (padIndex, deviceId) ->
            stopGamepadVibrationForDevice(deviceId)
            if (emulationInputEnabled) {
                EmulatorBridge.resetPadState(padIndex)
            }
        }

        return connectedSnapshot
    }

    private fun resetAnalogState() {
        synchronized(connectionLock) {
            analogStatesByDeviceId.clear()
            rumbleStatesByPad.clear()
        }
    }

    private fun stopAllGamepadVibrations() {
        val deviceIds = connectedGamepads().map { it.deviceId }
        deviceIds.forEach(::stopGamepadVibrationForDevice)
        synchronized(connectionLock) {
            rumbleStatesByPad.clear()
        }
    }

    private fun stopPadVibration(padIndex: Int) {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        val deviceId = synchronized(connectionLock) {
            rumbleStatesByPad.remove(normalizedPadIndex)
            deviceToPadIndex.entries.firstOrNull { it.value == normalizedPadIndex }?.key
        } ?: return
        stopGamepadVibrationForDevice(deviceId)
    }

    @Suppress("DEPRECATION")
    private fun getGamepadVibrator(deviceId: Int): Vibrator? {
        val device = InputDevice.getDevice(deviceId) ?: return null
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            device.vibratorManager.defaultVibrator
        } else {
            device.vibrator
        }
    }

    @Suppress("DEPRECATION")
    private fun vibrate(vibrator: Vibrator, amplitude: Int) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val resolvedAmplitude = if (vibrator.hasAmplitudeControl()) amplitude else VibrationEffect.DEFAULT_AMPLITUDE
            vibrator.vibrate(VibrationEffect.createOneShot(RUMBLE_PULSE_DURATION_MS, resolvedAmplitude))
        } else {
            vibrator.vibrate(RUMBLE_PULSE_DURATION_MS)
        }
    }

    private fun stopGamepadVibrationForDevice(deviceId: Int) {
        val vibrator = getGamepadVibrator(deviceId) ?: return
        if (!vibrator.hasVibrator()) return
        vibrator.cancel()
    }
}

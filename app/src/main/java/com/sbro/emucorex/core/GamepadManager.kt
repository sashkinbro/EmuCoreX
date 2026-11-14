package com.sbro.emucorex.core

import android.annotation.SuppressLint
import android.content.Context
import android.os.Build
import android.os.CombinedVibration
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GamepadBindingRules
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.milliseconds
import java.util.Locale
import kotlin.math.abs
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sign

object GamepadManager {
    data class MappableButtonAction(
        val id: String,
        val padKey: Int?,
        val defaultKeyCodes: List<Int>
    )

    data class GamepadShortcutAction(
        val padIndex: Int,
        val actionId: String
    )

    data class ConnectedGamepad(
        val padIndex: Int,
        val deviceId: Int,
        val name: String
    )

    data class AvailableGamepad(
        val key: String,
        val deviceId: Int,
        val name: String,
        val isExternal: Boolean,
        val padIndex: Int? = null
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
        var prevLTPadKey: Int? = PadKey.L2,
        var prevRTPadKey: Int? = PadKey.R2,
        var prevRightStickYFromTriggers: Float = 0f,
        var prevHatX: Float = 0f,
        var prevHatY: Float = 0f
    )

    private data class HeldButton(val padIndex: Int, val padKey: Int)

    private data class RightStickTriggerReset(
        val padIndex: Int,
        val releaseRightStickY: Boolean,
        val releaseL2PadKey: Int?,
        val releaseR2PadKey: Int?
    )

    private data class RumbleState(
        var lastAmplitude: Int = 0,
        var lastUpdateElapsedMs: Long = 0L
    )

    private class StartTransportHoldState {
        var turboActive: Boolean = false
        var job: Job? = null
    }

    private data class FallbackRumbleState(
        val amplitude: Int,
        val durationMs: Long,
        val expiresAtElapsedMs: Long
    )

    private data class ResolvedVibrationTarget(
        val target: VibrationTarget,
        val isSystemFallback: Boolean
    )

    private sealed class VibrationTarget {
        data class Single(val vibrator: Vibrator) : VibrationTarget()
        data class Managed(val manager: VibratorManager) : VibrationTarget()
    }

private const val TAG = "GamepadManager"
private const val MAX_PAD_SLOTS = 2
    private const val PAD_INPUT_SOURCE_GAMEPAD = 3 // AndroidRuntime source slot.
    private const val START_TRANSPORT_HOLD_DELAY_MS = 360L
    private const val START_TAP_DURATION_MS = 70L
    private const val RUMBLE_UPDATE_INTERVAL_MS = 40L
    private const val RUMBLE_PULSE_DURATION_MS = 80L
    private const val PHONE_SMALL_MOTOR_WEIGHT = 0.35f
    private const val TEST_RUMBLE_DURATION_MS = 260L
    private const val MISSING_VIBRATOR_LOG_INTERVAL_MS = 1500L
    const val ACTION_QUICK_SAVE = "quick_save"
    const val ACTION_QUICK_LOAD = "quick_load"
    /** Hold-to-fast-forward; shares the gamepadFastForwardHolds path used by Start long-press. */
    const val ACTION_FAST_FORWARD = "fast_forward"
    const val ACTION_PAUSE = "pause"
    const val ACTION_OPEN_MENU = "open_menu"
    /** Bindable "turbo_<button>" actions: the bound physical button auto-fires that PS2 button. */
    private const val TURBO_ACTION_PREFIX = "turbo_"
    private const val TURBO_HALF_PERIOD_MS = 42L
    private val TURBO_BASE_ACTION_IDS = listOf("cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2")
    // Negative codes stand for scancode-only keys (KEYCODE_UNKNOWN); codes in
    // [AXIS_CODE_BASE, AXIS_CODE_BASE + 0xFFFF] stand for a generic axis pushed past a threshold.
    private const val SCAN_CODE_BASE = 0x10000
    private const val AXIS_CODE_BASE = 0x20000
    private const val AXIS_BUTTON_PRESS = 0.65f
    private const val AXIS_BUTTON_RELEASE = 0.35f
    private const val AXIS_BUTTON_MIN_DELTA = 0.5f
    private const val AXIS_REST_MAX = 0.35f
    private val BINDABLE_BUTTON_AXES = intArrayOf(
        MotionEvent.AXIS_GENERIC_1, MotionEvent.AXIS_GENERIC_2, MotionEvent.AXIS_GENERIC_3,
        MotionEvent.AXIS_GENERIC_4, MotionEvent.AXIS_GENERIC_5, MotionEvent.AXIS_GENERIC_6,
        MotionEvent.AXIS_GENERIC_7, MotionEvent.AXIS_GENERIC_8, MotionEvent.AXIS_GENERIC_9,
        MotionEvent.AXIS_GENERIC_10, MotionEvent.AXIS_GENERIC_11, MotionEvent.AXIS_GENERIC_12,
        MotionEvent.AXIS_GENERIC_13, MotionEvent.AXIS_GENERIC_14, MotionEvent.AXIS_GENERIC_15,
        MotionEvent.AXIS_GENERIC_16, MotionEvent.AXIS_THROTTLE, MotionEvent.AXIS_RUDDER
    )
    private val RESERVED_SYSTEM_KEY_CODES = setOf(
        KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_HOME, KeyEvent.KEYCODE_POWER,
        KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE,
        KeyEvent.KEYCODE_APP_SWITCH, KeyEvent.KEYCODE_ASSIST, KeyEvent.KEYCODE_SLEEP,
        KeyEvent.KEYCODE_WAKEUP
    )
    private val FINGERPRINT_UINPUT_DEVICE_TOKENS = setOf(
        "uinput-fpc",
        "uinput-goodix",
        "uinput-synaptics",
        "uinput-elan",
        "uinput-vfs",
        "uinput-atrus"
    )
    private val GAMEPAD_BUTTON_KEY_CODES = intArrayOf(
        KeyEvent.KEYCODE_BUTTON_A,
        KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_X,
        KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_BUTTON_L2,
        KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_BUTTON_THUMBL,
        KeyEvent.KEYCODE_BUTTON_THUMBR,
        KeyEvent.KEYCODE_BUTTON_SELECT,
        KeyEvent.KEYCODE_BUTTON_START,
        KeyEvent.KEYCODE_BUTTON_1,
        KeyEvent.KEYCODE_BUTTON_2,
        KeyEvent.KEYCODE_BUTTON_3,
        KeyEvent.KEYCODE_BUTTON_4,
        KeyEvent.KEYCODE_BUTTON_5,
        KeyEvent.KEYCODE_BUTTON_6,
        KeyEvent.KEYCODE_BUTTON_7,
        KeyEvent.KEYCODE_BUTTON_8,
        KeyEvent.KEYCODE_BUTTON_9,
        KeyEvent.KEYCODE_BUTTON_10,
        KeyEvent.KEYCODE_DPAD_UP,
        KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT
    )
    private val JOYSTICK_AXIS_CODES = intArrayOf(
        MotionEvent.AXIS_X,
        MotionEvent.AXIS_Y,
        MotionEvent.AXIS_Z,
        MotionEvent.AXIS_RX,
        MotionEvent.AXIS_RY,
        MotionEvent.AXIS_RZ,
        MotionEvent.AXIS_LTRIGGER,
        MotionEvent.AXIS_RTRIGGER,
        MotionEvent.AXIS_BRAKE,
        MotionEvent.AXIS_GAS,
        MotionEvent.AXIS_HAT_X,
        MotionEvent.AXIS_HAT_Y
    )
    private val ANALOG_STICK_DIRECTION_KEYS = setOf(
        PadKey.LeftStickUp,
        PadKey.LeftStickRight,
        PadKey.LeftStickDown,
        PadKey.LeftStickLeft,
        PadKey.RightStickUp,
        PadKey.RightStickRight,
        PadKey.RightStickDown,
        PadKey.RightStickLeft
    )

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
    private var vibrationStrength = AppPreferences.DEFAULT_PAD_VIBRATION_STRENGTH / 100f
    @Volatile
    private var vibrationFallbackEnabled = true
    @Volatile
    private var buttonHapticsEnabled = false
    @Volatile
    private var buttonHapticsStrength = AppPreferences.DEFAULT_TOUCH_HAPTICS_STRENGTH
    @Volatile
    private var buttonHapticsPreset = AppPreferences.DEFAULT_TOUCH_HAPTICS_PRESET
    @Volatile
    private var customBindingsByPad: Map<Int, Map<String, Int>> = emptyMap()
    @Volatile
    private var customShortcutBindingsByPadAndKeyCode: Map<Int, Map<Int, String>> = emptyMap()
    @Volatile
    private var analogDeadzone = AppPreferences.DEFAULT_GAMEPAD_STICK_DEADZONE / 100f
    @Volatile
    private var leftStickSensitivity = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY / 100f
    @Volatile
    private var rightStickSensitivity = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY / 100f
    @Volatile
    private var leftNegativeDeadzone = AppPreferences.DEFAULT_GAMEPAD_STICK_NEGATIVE_DEADZONE / 100f
    @Volatile
    private var rightNegativeDeadzone = AppPreferences.DEFAULT_GAMEPAD_STICK_NEGATIVE_DEADZONE / 100f
    @Volatile
    private var leftAntiDeadzone = AppPreferences.DEFAULT_GAMEPAD_STICK_ANTI_DEADZONE / 100f
    @Volatile
    private var rightAntiDeadzone = AppPreferences.DEFAULT_GAMEPAD_STICK_ANTI_DEADZONE / 100f
    @Volatile
    private var leftStickCurve = AppPreferences.DEFAULT_GAMEPAD_STICK_CURVE / 100f
    @Volatile
    private var rightStickCurve = AppPreferences.DEFAULT_GAMEPAD_STICK_CURVE / 100f
    @Volatile
    private var invertLeftStick = false
    @Volatile
    private var invertRightStick = false
    @Volatile
    private var invertLeftStickHorizontal = false
    @Volatile
    private var invertRightStickHorizontal = false
    @Volatile
    private var rightStickUpToR2 = false
    @Volatile
    private var rightStickDownToL2 = false
    @Volatile
    private var singleGamepadReplacesTouch = true
    @Volatile
    private var preferExternalGamepadAsPlayerOne = AppPreferences.DEFAULT_PREFER_EXTERNAL_GAMEPAD_PLAYER_ONE
    @Volatile
    private var deviceAssignmentKeysByPad: Map<Int, String> = emptyMap()
    @Volatile
    private var ignoredDeviceKeys: Set<String> = emptySet()

    @Volatile
    private var perGameBindingsActive = false
    @Volatile
    private var savedGlobalBindingsByPad: Map<Int, Map<String, Int>> = emptyMap()
    @Volatile
    private var activePerGameBindingsByPad: Map<Int, Map<String, Int>> = emptyMap()
    @Volatile
    private var savedGlobalAnalogDeadzone: Float = analogDeadzone
    @Volatile
    private var savedGlobalLeftStickSensitivity: Float = leftStickSensitivity
    @Volatile
    private var savedGlobalRightStickSensitivity: Float = rightStickSensitivity
    @Volatile
    private var savedGlobalLeftNegativeDeadzone: Float = leftNegativeDeadzone
    @Volatile
    private var savedGlobalRightNegativeDeadzone: Float = rightNegativeDeadzone
    @Volatile
    private var savedGlobalLeftAntiDeadzone: Float = leftAntiDeadzone
    @Volatile
    private var savedGlobalRightAntiDeadzone: Float = rightAntiDeadzone
    @Volatile
    private var savedGlobalLeftStickCurve: Float = leftStickCurve
    @Volatile
    private var savedGlobalRightStickCurve: Float = rightStickCurve
    @Volatile
    private var savedGlobalInvertLeftStick: Boolean = invertLeftStick
    @Volatile
    private var savedGlobalInvertRightStick: Boolean = invertRightStick
    @Volatile
    private var savedGlobalInvertLeftStickHorizontal: Boolean = invertLeftStickHorizontal
    @Volatile
    private var savedGlobalInvertRightStickHorizontal: Boolean = invertRightStickHorizontal

    private val connectionLock = Any()
    private var appContext: Context? = null
    private val deviceToPadIndex = linkedMapOf<Int, Int>()
    private val startTransportHolds = mutableMapOf<Int, StartTransportHoldState>()
    private val analogStatesByDeviceId = mutableMapOf<Int, AnalogState>()
    private val heldButtonsByDeviceAndKey = mutableMapOf<Pair<Int, Int>, HeldButton>()
    private val rumbleStatesByPad = mutableMapOf<Int, RumbleState>()
    private val fallbackRumbleStatesByPad = mutableMapOf<Int, FallbackRumbleState>()
    private var lastMissingVibratorLogElapsedMs = 0L

    @Suppress("ConstPropertyName")
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
        const val Pressure = 124
        const val GunTrigger = 125
        const val GunPedal = 126
        const val GunReload = 127
        const val GunRecalibrate = 128
        const val Coin = 129
        const val Service = 130
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
        MappableButtonAction("pressure", PadKey.Pressure, emptyList()),
        MappableButtonAction("gun_trigger", PadKey.GunTrigger, emptyList()),
        MappableButtonAction("gun_pedal", PadKey.GunPedal, emptyList()),
        MappableButtonAction("gun_reload", PadKey.GunReload, emptyList()),
        MappableButtonAction("gun_recalibrate", PadKey.GunRecalibrate, emptyList()),
        MappableButtonAction("coin", PadKey.Coin, emptyList()),
        MappableButtonAction("service", PadKey.Service, emptyList()),
        MappableButtonAction(ACTION_QUICK_SAVE, null, emptyList()),
        MappableButtonAction(ACTION_QUICK_LOAD, null, emptyList()),
        MappableButtonAction(ACTION_FAST_FORWARD, null, emptyList()),
        MappableButtonAction(ACTION_PAUSE, null, emptyList()),
        MappableButtonAction(ACTION_OPEN_MENU, null, emptyList()),
        MappableButtonAction("turbo_cross", PadKey.Cross, emptyList()),
        MappableButtonAction("turbo_circle", PadKey.Circle, emptyList()),
        MappableButtonAction("turbo_square", PadKey.Square, emptyList()),
        MappableButtonAction("turbo_triangle", PadKey.Triangle, emptyList()),
        MappableButtonAction("turbo_l1", PadKey.L1, emptyList()),
        MappableButtonAction("turbo_r1", PadKey.R1, emptyList()),
        MappableButtonAction("turbo_l2", PadKey.L2, emptyList()),
        MappableButtonAction("turbo_r2", PadKey.R2, emptyList()),
        MappableButtonAction("dpad_up", PadKey.Up, listOf(KeyEvent.KEYCODE_DPAD_UP)),
        MappableButtonAction("dpad_down", PadKey.Down, listOf(KeyEvent.KEYCODE_DPAD_DOWN)),
        MappableButtonAction("dpad_left", PadKey.Left, listOf(KeyEvent.KEYCODE_DPAD_LEFT)),
        MappableButtonAction("dpad_right", PadKey.Right, listOf(KeyEvent.KEYCODE_DPAD_RIGHT)),
        MappableButtonAction("left_stick_up", PadKey.LeftStickUp, emptyList()),
        MappableButtonAction("left_stick_down", PadKey.LeftStickDown, emptyList()),
        MappableButtonAction("left_stick_left", PadKey.LeftStickLeft, emptyList()),
        MappableButtonAction("left_stick_right", PadKey.LeftStickRight, emptyList()),
        MappableButtonAction("right_stick_up", PadKey.RightStickUp, emptyList()),
        MappableButtonAction("right_stick_down", PadKey.RightStickDown, emptyList()),
        MappableButtonAction("right_stick_left", PadKey.RightStickLeft, emptyList()),
        MappableButtonAction("right_stick_right", PadKey.RightStickRight, emptyList())
    )
    private val actionsById = mappableActions.associateBy { it.id }
    private val shortcutActionIds = setOf(ACTION_QUICK_SAVE, ACTION_QUICK_LOAD, ACTION_FAST_FORWARD, ACTION_PAUSE, ACTION_OPEN_MENU)
    // Gun cabinet actions must not permanently steal a controller button from the normal
    // pad mapping: their custom bindings only win while a light-gun context is active.
    private val gunActionIds = setOf(
        "gun_trigger",
        "gun_pedal",
        "gun_reload",
        "gun_recalibrate"
    )
    private val arcadeActionIds = setOf("coin", "service")
    @Volatile
    private var lightGunModeActive = false
    @Volatile
    private var arcadeModeActive = false
    @Volatile
    private var lightGunAimSource = AppPreferences.DEFAULT_LIGHT_GUN_AIM
    private val _lightGunStickByPad = MutableStateFlow<Map<Int, Pair<Float, Float>>>(emptyMap())
    val lightGunStickByPad: StateFlow<Map<Int, Pair<Float, Float>>> = _lightGunStickByPad

    fun setLightGunModeActive(active: Boolean, aimSource: Int = AppPreferences.DEFAULT_LIGHT_GUN_AIM) {
        val sourceChanged = lightGunAimSource != aimSource
        lightGunModeActive = active
        lightGunAimSource = aimSource
        if (!active || sourceChanged || aimSource == AppPreferences.LIGHT_GUN_AIM_GYRO) {
            _lightGunStickByPad.value = emptyMap()
        }
    }

    fun setArcadeModeActive(active: Boolean) {
        arcadeModeActive = active
    }
    private val _gamepadShortcutActions = MutableSharedFlow<GamepadShortcutAction>(extraBufferCapacity = 8)
    val gamepadShortcutActions: SharedFlow<GamepadShortcutAction> = _gamepadShortcutActions
    private val _gamepadFastForwardHolds = MutableSharedFlow<Boolean>(extraBufferCapacity = 8)
    val gamepadFastForwardHolds: SharedFlow<Boolean> = _gamepadFastForwardHolds
    private val _connectedGamepadCountState = MutableStateFlow(0)
    val connectedGamepadCountState: StateFlow<Int> = _connectedGamepadCountState
    private val _availableGamepadsState = MutableStateFlow<List<AvailableGamepad>>(emptyList())
    val availableGamepadsState: StateFlow<List<AvailableGamepad>> = _availableGamepadsState
    private val _gamepadDeviceAssignmentsState = MutableStateFlow<Map<Int, String>>(emptyMap())
    val gamepadDeviceAssignmentsState: StateFlow<Map<Int, String>> = _gamepadDeviceAssignmentsState
    private val _ignoredGamepadDevicesState = MutableStateFlow<Set<String>>(emptySet())
    val ignoredGamepadDevicesState: StateFlow<Set<String>> = _ignoredGamepadDevicesState
    private var connectedGamepadSnapshot: List<ConnectedGamepad> = emptyList()

    fun ensureInitialized(context: Context) {
        if (initialized) return
        initialized = true
        appContext = context.applicationContext
        val preferences = AppPreferences(context.applicationContext)
        scope.launch {
            preferences.gamepadBindingsByPad.collectLatest { bindingsByPad ->
                val previousBindingsByPad = customBindingsByPad
                savedGlobalBindingsByPad = bindingsByPad
                customBindingsByPad = if (perGameBindingsActive) {
                    GamepadBindingRules.merge(bindingsByPad, activePerGameBindingsByPad)
                } else bindingsByPad
                customShortcutBindingsByPadAndKeyCode = customBindingsByPad.mapValues { (_, bindings) ->
                    bindings.entries.mapNotNull { (actionId, keyCode) ->
                        actionId.takeIf { it in shortcutActionIds }?.let { keyCode to it }
                    }.toMap()
                }
                resetChangedBindingStates(previousBindingsByPad, customBindingsByPad)
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
        scope.launch {
            preferences.padVibrationStrength.collectLatest { value ->
                vibrationStrength = value.coerceIn(0, 150) / 100f
            }
        }
        scope.launch {
            preferences.padVibrationFallback.collectLatest { enabled ->
                vibrationFallbackEnabled = enabled
                if (!enabled) {
                    stopAllGamepadVibrations()
                }
            }
        }
        // Runtime-only gamepad controls are pushed by EmulationViewModel so per-game
        // profiles are not overwritten by unrelated global DataStore updates.
        scope.launch {
            preferences.touchHapticsStrength.collectLatest { value ->
                buttonHapticsStrength = value.coerceIn(10, 100)
            }
        }
        scope.launch {
            preferences.gamepadStickDeadzone.collectLatest { value ->
                val normalized = value.coerceIn(0, 35) / 100f
                if (perGameBindingsActive) savedGlobalAnalogDeadzone = normalized else analogDeadzone = normalized
            }
        }
        scope.launch {
            preferences.gamepadLeftStickSensitivity.collectLatest { value ->
                val normalized = value.coerceIn(50, 200) / 100f
                if (perGameBindingsActive) savedGlobalLeftStickSensitivity = normalized else leftStickSensitivity = normalized
            }
        }
        scope.launch {
            preferences.gamepadRightStickSensitivity.collectLatest { value ->
                val normalized = value.coerceIn(50, 200) / 100f
                if (perGameBindingsActive) savedGlobalRightStickSensitivity = normalized else rightStickSensitivity = normalized
            }
        }
        scope.launch {
            preferences.gamepadLeftStickNegativeDeadzone.collectLatest { value ->
                val normalized = value.coerceIn(0, 30) / 100f
                if (perGameBindingsActive) savedGlobalLeftNegativeDeadzone = normalized else leftNegativeDeadzone = normalized
            }
        }
        scope.launch {
            preferences.gamepadRightStickNegativeDeadzone.collectLatest { value ->
                val normalized = value.coerceIn(0, 30) / 100f
                if (perGameBindingsActive) savedGlobalRightNegativeDeadzone = normalized else rightNegativeDeadzone = normalized
            }
        }
        scope.launch {
            preferences.gamepadLeftStickAntiDeadzone.collectLatest { value ->
                val normalized = value.coerceIn(0, 50) / 100f
                if (perGameBindingsActive) savedGlobalLeftAntiDeadzone = normalized else leftAntiDeadzone = normalized
            }
        }
        scope.launch {
            preferences.gamepadRightStickAntiDeadzone.collectLatest { value ->
                val normalized = value.coerceIn(0, 50) / 100f
                if (perGameBindingsActive) savedGlobalRightAntiDeadzone = normalized else rightAntiDeadzone = normalized
            }
        }
        scope.launch {
            preferences.gamepadLeftStickCurve.collectLatest { value ->
                val normalized = value.coerceIn(50, 200) / 100f
                if (perGameBindingsActive) savedGlobalLeftStickCurve = normalized else leftStickCurve = normalized
            }
        }
        scope.launch {
            preferences.gamepadRightStickCurve.collectLatest { value ->
                val normalized = value.coerceIn(50, 200) / 100f
                if (perGameBindingsActive) savedGlobalRightStickCurve = normalized else rightStickCurve = normalized
            }
        }
        scope.launch {
            preferences.invertLeftStick.collectLatest { enabled ->
                if (perGameBindingsActive) savedGlobalInvertLeftStick = enabled else invertLeftStick = enabled
            }
        }
        scope.launch {
            preferences.invertRightStick.collectLatest { enabled ->
                if (perGameBindingsActive) savedGlobalInvertRightStick = enabled else invertRightStick = enabled
            }
        }
        scope.launch {
            preferences.invertLeftStickHorizontal.collectLatest { enabled ->
                if (perGameBindingsActive) savedGlobalInvertLeftStickHorizontal = enabled else invertLeftStickHorizontal = enabled
            }
        }
        scope.launch {
            preferences.invertRightStickHorizontal.collectLatest { enabled ->
                if (perGameBindingsActive) savedGlobalInvertRightStickHorizontal = enabled else invertRightStickHorizontal = enabled
            }
        }
        scope.launch {
            preferences.enableAutoGamepad.collectLatest { enabled ->
                singleGamepadReplacesTouch = enabled
                refreshConnectedGamepads()
            }
        }
        scope.launch {
            preferences.preferExternalGamepadPlayerOne.collectLatest { enabled ->
                preferExternalGamepadAsPlayerOne = enabled
                refreshConnectedGamepads()
            }
        }
        scope.launch {
            preferences.gamepadDeviceAssignments.collectLatest { assignments ->
                deviceAssignmentKeysByPad = assignments
                _gamepadDeviceAssignmentsState.value = assignments
                refreshConnectedGamepads()
            }
        }
        scope.launch {
            preferences.ignoredGamepadDevices.collectLatest { ignored ->
                ignoredDeviceKeys = ignored
                _ignoredGamepadDevicesState.value = ignored
                refreshConnectedGamepads()
            }
        }
        scope.launch {
            while (true) {
                refreshConnectedGamepads()
                delay(750.milliseconds)
            }
        }
        refreshConnectedGamepads()
    }

    fun mappableButtonActions(): List<MappableButtonAction> = mappableActions

    /** Actions that only make sense for a gamepad (hotkeys, turbo); hidden from keyboard/mouse mapping. */
    val gamepadOnlyActionIds: Set<String> = setOf(ACTION_FAST_FORWARD, ACTION_PAUSE, ACTION_OPEN_MENU) +
        TURBO_BASE_ACTION_IDS.map { TURBO_ACTION_PREFIX + it }

    fun isTurboAction(actionId: String): Boolean = actionId.startsWith(TURBO_ACTION_PREFIX)

    fun turboBaseActionId(actionId: String): String = actionId.removePrefix(TURBO_ACTION_PREFIX)

    /** Stored binding code for an event: scancode-only keys (KEYCODE_UNKNOWN) become negative synthetic codes. */
    internal fun physicalKeyCode(keyCode: Int, scanCode: Int): Int =
        if (keyCode == KeyEvent.KEYCODE_UNKNOWN && scanCode > 0) -(SCAN_CODE_BASE or (scanCode and 0xFFFF)) else keyCode

    internal fun physicalKeyCode(event: KeyEvent): Int = physicalKeyCode(event.keyCode, event.scanCode)

    internal fun axisButtonCode(axis: Int): Int = AXIS_CODE_BASE or axis

    internal fun isAxisButtonCode(code: Int): Boolean = code >= AXIS_CODE_BASE && code < AXIS_CODE_BASE + 0x10000

    internal fun isScanButtonCode(code: Int): Boolean = code <= -SCAN_CODE_BASE

    internal fun isReservedSystemKey(keyCode: Int): Boolean = keyCode in RESERVED_SYSTEM_KEY_CODES

    /** True when the key code is bound to some action for the pad that owns the event's device. */
    fun isKeyBound(event: KeyEvent): Boolean {
        val padIndex = resolvePadIndexForDevice(event.deviceId) ?: return false
        return isKeyBoundForPad(padIndex, physicalKeyCode(event))
    }

    private fun isKeyBoundForPad(padIndex: Int, code: Int): Boolean =
        customBindingsByPad[normalizePadIndex(padIndex)]?.values?.contains(code) == true

    /**
     * Wider than [isGameController]: also accepts secondary nodes of a controller (paddles, M1/M2,
     * Share often arrive on a keyboard-class node). Only for binding capture and mapped-key handling.
     */
    fun isBindableInputDevice(device: InputDevice?, keyCode: Int): Boolean {
        if (device == null || device.isVirtual || isFingerprintUinputDevice(device)) return false
        if (isReservedSystemKey(keyCode)) return false
        val sources = device.sources
        val hasSource =
            (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
                (sources and InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
        if (!hasSource) return false
        if (keyCode <= KeyEvent.KEYCODE_UNKNOWN) {
            // Scancode-only keys are accepted from controllers (or their sibling nodes), never plain keyboards.
            val controllerSource =
                (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                    (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
            if (!controllerSource && resolveSiblingPadIndex(device.id) == null) return false
        }
        return keyCode == KeyEvent.KEYCODE_UNKNOWN ||
            keyCode < 0 ||
            isAxisButtonCode(keyCode) ||
            KeyEvent.isGamepadButton(keyCode) ||
            keyCode in KeyEvent.KEYCODE_BUTTON_1..KeyEvent.KEYCODE_BUTTON_16 ||
            keyCode == KeyEvent.KEYCODE_BUTTON_MODE ||
            keyCode == KeyEvent.KEYCODE_BUTTON_C ||
            keyCode == KeyEvent.KEYCODE_BUTTON_Z
    }

    private fun isCapturableDevice(device: InputDevice?, keyCode: Int): Boolean {
        if (isReservedSystemKey(keyCode)) return false
        return isGameController(device) || isBindableInputDevice(device, keyCode)
    }

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
            KeyEvent.KEYCODE_BUTTON_11 -> "Button 11"
            KeyEvent.KEYCODE_BUTTON_12 -> "Button 12"
            KeyEvent.KEYCODE_BUTTON_13 -> "Button 13"
            KeyEvent.KEYCODE_BUTTON_14 -> "Button 14"
            KeyEvent.KEYCODE_BUTTON_15 -> "Button 15"
            KeyEvent.KEYCODE_BUTTON_16 -> "Button 16"
            KeyEvent.KEYCODE_BUTTON_C -> "C"
            KeyEvent.KEYCODE_BUTTON_Z -> "Z"
            KeyEvent.KEYCODE_BUTTON_MODE -> "Mode"
            in Int.MIN_VALUE..-SCAN_CODE_BASE -> "Key #${(-keyCode) and 0xFFFF}"
            in AXIS_CODE_BASE until AXIS_CODE_BASE + 0x10000 ->
                "Axis " + MotionEvent.axisToString(keyCode - AXIS_CODE_BASE).removePrefix("AXIS_").replace('_', ' ')
            else -> KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_").replace('_', ' ')
        }
    }

    fun connectedGamepads(): List<ConnectedGamepad> = synchronized(connectionLock) {
        connectedGamepadSnapshot
    }

    fun connectedControllerName(padIndex: Int): String? {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        return connectedGamepads().firstOrNull { it.padIndex == normalizedPadIndex }?.name
    }

    fun startBindingCapture(onCaptured: (Int) -> Unit) {
        startBindingCapture(0, onCaptured)
    }

    fun startBindingCapture(padIndex: Int, onCaptured: (Int) -> Unit) {
        axisCaptureBaselines.clear()
        bindingCaptureState = BindingCaptureState(normalizePadIndex(padIndex), onCaptured)
    }

    fun cancelBindingCapture() {
        bindingCaptureState = null
    }

    fun handleBindingCapture(event: KeyEvent): Boolean {
        val captureState = bindingCaptureState ?: return false
        val code = physicalKeyCode(event)
        // BACK/HOME/volume/etc. are never captured; BACK falls through so the dialog cancels.
        if (!isCapturableDevice(event.device, code)) return false
        val eventPadIndex = resolvePadIndexForDevice(event.deviceId)
            // Controllers without a slot stay swallowed; only secondary nodes fall through.
            ?: return isGameController(event.device)
        if (eventPadIndex != captureState.padIndex) {
            // Keep another player's input from leaking into the binding dialog or
            // overwriting the selected player's mapping.
            return true
        }
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount != 0) return true
        captureState.onCaptured(code)
        bindingCaptureState = null
        return true
    }

    fun handleMotionBindingCapture(event: MotionEvent): Boolean {
        val captureState = bindingCaptureState ?: return false
        if (!isGameController(event.device) || event.action != MotionEvent.ACTION_MOVE) return false
        if (resolvePadIndexForDevice(event.deviceId) != captureState.padIndex) return true
        val left = getAxisValueWithFallback(event, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE)
        val right = getAxisValueWithFallback(event, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS)
        val capturedKey = when {
            left > 0.65f && left >= right -> KeyEvent.KEYCODE_BUTTON_L2
            right > 0.65f -> KeyEvent.KEYCODE_BUTTON_R2
            else -> captureAxisButton(event)
        }
        if (capturedKey != null) {
            bindingCaptureState = null
            captureState.onCaptured(capturedKey)
        }
        return true
    }

    private val axisCaptureBaselines = mutableMapOf<Int, FloatArray>()

    /** Resting values are recorded on the first event per device, so only a real push counts. */
    private fun captureAxisButton(event: MotionEvent): Int? {
        val baseline = axisCaptureBaselines.getOrPut(event.deviceId) {
            FloatArray(BINDABLE_BUTTON_AXES.size) { event.getAxisValue(BINDABLE_BUTTON_AXES[it]) }
        }
        BINDABLE_BUTTON_AXES.forEachIndexed { index, axis ->
            if (axisButtonPressed(baseline[index], event.getAxisValue(axis))) return axisButtonCode(axis)
        }
        return null
    }

    internal fun axisButtonPressed(baseline: Float, value: Float): Boolean =
        baseline <= AXIS_REST_MAX && value > AXIS_BUTTON_PRESS && value - baseline > AXIS_BUTTON_MIN_DELTA

    fun isGameController(device: InputDevice?): Boolean {
        if (device == null) return false
        if (device.isVirtual) return false
        if (isFingerprintUinputDevice(device)) return false
        val sources = device.sources
        val hasControllerSource =
            (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        if (!hasControllerSource) return false

        return hasRecognizedGamepadButton(device) || hasRecognizedJoystickAxis(device)
    }

    fun isGamepadConnected(): Boolean = connectedGamepads().isNotEmpty()

    fun resolveTouchPadIndex(): Int? {
        val connectedCount = synchronized(connectionLock) { deviceToPadIndex.size }
        return when {
            connectedCount <= 0 -> 0
            connectedCount == 1 && !singleGamepadReplacesTouch -> 0
            else -> null
        }
    }

    fun setEmulationInputEnabled(enabled: Boolean) {
        emulationInputEnabled = enabled
        if (!enabled) {
            _lightGunStickByPad.value = emptyMap()
            synchronized(connectionLock) {
                heldButtonsByDeviceAndKey.clear()
                heldAxisButtons.clear()
                axisBaselines.clear()
            }
            removeFastForwardHolders { it.startsWith("ff:") }
            cancelStartTransportHolds()
            resetAnalogState()
            stopAllGamepadVibrations()
            EmulatorBridge.resetKeyStatus()
        }
        KeyboardMouseManager.setEmulationInputEnabled(enabled)
    }

    /** Allows keyboard/mouse mappings to reuse the existing quick save/load flow. */
    fun triggerShortcutAction(padIndex: Int, actionId: String) {
        if (!emulationInputEnabled) return
        if (actionId != ACTION_QUICK_SAVE && actionId != ACTION_QUICK_LOAD) return
        _gamepadShortcutActions.tryEmit(GamepadShortcutAction(normalizePadIndex(padIndex), actionId))
    }

    fun setRightStickTriggerMapping(upToR2: Boolean, downToL2: Boolean) {
        val changed = rightStickUpToR2 != upToR2 || rightStickDownToL2 != downToL2
        rightStickUpToR2 = upToR2
        rightStickDownToL2 = downToL2
        if (changed) {
            resetRightStickTriggerState()
        }
    }

    fun setButtonHapticsEnabled(
        enabled: Boolean,
        strengthPercent: Int = buttonHapticsStrength,
        preset: Int = buttonHapticsPreset
    ) {
        buttonHapticsEnabled = enabled
        buttonHapticsStrength = strengthPercent.coerceIn(10, 100)
        buttonHapticsPreset = preset.coerceIn(
            AppPreferences.TOUCH_HAPTICS_PRESET_SOFT,
            AppPreferences.TOUCH_HAPTICS_PRESET_STRONG
        )
    }

    fun applyPerGameOverrides(
        bindingsByPad: Map<Int, Map<String, Int>>?,
        deadzone: Int?,
        leftSensitivity: Int?,
        rightSensitivity: Int?,
        leftNegativeDeadzone: Int? = null,
        rightNegativeDeadzone: Int? = null,
        leftAntiDeadzone: Int? = null,
        rightAntiDeadzone: Int? = null,
        leftStickCurve: Int? = null,
        rightStickCurve: Int? = null,
        invertLeftStick: Boolean? = null,
        invertRightStick: Boolean? = null,
        invertLeftStickHorizontal: Boolean? = null,
        invertRightStickHorizontal: Boolean? = null
    ) {
        if (!perGameBindingsActive) {
            savedGlobalBindingsByPad = customBindingsByPad
            savedGlobalAnalogDeadzone = analogDeadzone
            savedGlobalLeftStickSensitivity = leftStickSensitivity
            savedGlobalRightStickSensitivity = rightStickSensitivity
            savedGlobalLeftNegativeDeadzone = this.leftNegativeDeadzone
            savedGlobalRightNegativeDeadzone = this.rightNegativeDeadzone
            savedGlobalLeftAntiDeadzone = this.leftAntiDeadzone
            savedGlobalRightAntiDeadzone = this.rightAntiDeadzone
            savedGlobalLeftStickCurve = this.leftStickCurve
            savedGlobalRightStickCurve = this.rightStickCurve
            savedGlobalInvertLeftStick = this.invertLeftStick
            savedGlobalInvertRightStick = this.invertRightStick
            savedGlobalInvertLeftStickHorizontal = this.invertLeftStickHorizontal
            savedGlobalInvertRightStickHorizontal = this.invertRightStickHorizontal
            perGameBindingsActive = true
        }
        if (bindingsByPad != null) {
            val previousBindingsByPad = customBindingsByPad
            activePerGameBindingsByPad = bindingsByPad
            customBindingsByPad = GamepadBindingRules.merge(savedGlobalBindingsByPad, bindingsByPad)
            customShortcutBindingsByPadAndKeyCode = customBindingsByPad.mapValues { (_, bindings) ->
                bindings.entries.mapNotNull { (actionId, keyCode) ->
                    actionId.takeIf { it in shortcutActionIds }?.let { keyCode to it }
                }.toMap()
            }
            resetChangedBindingStates(previousBindingsByPad, customBindingsByPad)
        }
        if (deadzone != null) {
            analogDeadzone = deadzone.coerceIn(0, 35) / 100f
        }
        if (leftSensitivity != null) {
            leftStickSensitivity = leftSensitivity.coerceIn(50, 200) / 100f
        }
        if (rightSensitivity != null) {
            rightStickSensitivity = rightSensitivity.coerceIn(50, 200) / 100f
        }
        if (leftNegativeDeadzone != null) {
            this.leftNegativeDeadzone = leftNegativeDeadzone.coerceIn(0, 30) / 100f
        }
        if (rightNegativeDeadzone != null) {
            this.rightNegativeDeadzone = rightNegativeDeadzone.coerceIn(0, 30) / 100f
        }
        if (leftAntiDeadzone != null) {
            this.leftAntiDeadzone = leftAntiDeadzone.coerceIn(0, 50) / 100f
        }
        if (rightAntiDeadzone != null) {
            this.rightAntiDeadzone = rightAntiDeadzone.coerceIn(0, 50) / 100f
        }
        if (leftStickCurve != null) {
            this.leftStickCurve = leftStickCurve.coerceIn(50, 200) / 100f
        }
        if (rightStickCurve != null) {
            this.rightStickCurve = rightStickCurve.coerceIn(50, 200) / 100f
        }
        invertLeftStick?.let { this.invertLeftStick = it }
        invertRightStick?.let { this.invertRightStick = it }
        invertLeftStickHorizontal?.let { this.invertLeftStickHorizontal = it }
        invertRightStickHorizontal?.let { this.invertRightStickHorizontal = it }
    }

    fun clearPerGameOverrides() {
        if (!perGameBindingsActive) return
        perGameBindingsActive = false
        activePerGameBindingsByPad = emptyMap()
        val previousBindingsByPad = customBindingsByPad
        customBindingsByPad = savedGlobalBindingsByPad
        customShortcutBindingsByPadAndKeyCode = savedGlobalBindingsByPad.mapValues { (_, bindings) ->
            bindings.entries.mapNotNull { (actionId, keyCode) ->
                actionId.takeIf { it in shortcutActionIds }?.let { keyCode to it }
            }.toMap()
        }
        analogDeadzone = savedGlobalAnalogDeadzone
        leftStickSensitivity = savedGlobalLeftStickSensitivity
        rightStickSensitivity = savedGlobalRightStickSensitivity
        leftNegativeDeadzone = savedGlobalLeftNegativeDeadzone
        rightNegativeDeadzone = savedGlobalRightNegativeDeadzone
        leftAntiDeadzone = savedGlobalLeftAntiDeadzone
        rightAntiDeadzone = savedGlobalRightAntiDeadzone
        leftStickCurve = savedGlobalLeftStickCurve
        rightStickCurve = savedGlobalRightStickCurve
        invertLeftStick = savedGlobalInvertLeftStick
        invertRightStick = savedGlobalInvertRightStick
        invertLeftStickHorizontal = savedGlobalInvertLeftStickHorizontal
        invertRightStickHorizontal = savedGlobalInvertRightStickHorizontal
        resetChangedBindingStates(previousBindingsByPad, savedGlobalBindingsByPad)
    }

    fun isEmulationInputEnabled(): Boolean = emulationInputEnabled

    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (!emulationInputEnabled) return false
        val code = physicalKeyCode(event)
        val isController = isGameController(event.device)
        val physicalKey = event.deviceId to code
        // Releases are processed before any gating, so a binding change mid-hold cannot strand a button.
        if (event.action == KeyEvent.ACTION_UP) {
            setFastForwardHolder("ff:${physicalKey.first}:${physicalKey.second}", false)
        }
        val heldButton = synchronized(connectionLock) { heldButtonsByDeviceAndKey[physicalKey] }
        if (event.action == KeyEvent.ACTION_UP && heldButton != null) {
            synchronized(connectionLock) { heldButtonsByDeviceAndKey.remove(physicalKey) }
            turboJobs.remove(physicalKey)?.cancel()
            if (heldButton.padKey == PadKey.Start) {
                return handleStartTransportEvent(heldButton.padIndex, event)
            }
            setGamepadButton(heldButton.padIndex, heldButton.padKey, 0, false)
            return true
        }
        if (!isController && !isBindableInputDevice(event.device, code)) return false
        val padIndex = resolvePadIndexForDevice(event.deviceId) ?: return false
        // Secondary/keyboard-class nodes only act on keys the user explicitly bound.
        if (!isController && !isKeyBoundForPad(padIndex, code)) return false
        if (event.action == KeyEvent.ACTION_DOWN && heldButton != null) return true
        val selectedActionId = resolveMappedActionIdForKeyCode(
            code, customBindingsByPad[normalizePadIndex(padIndex)].orEmpty()
        )
        val shortcutActionId = mapKeyCodeToShortcutActionId(padIndex, code)
            ?.takeUnless { selectedActionId != null && GamepadBindingRules.isArcadeAction(selectedActionId) }
        shortcutActionId?.let { actionId ->
            if (actionId == ACTION_FAST_FORWARD) {
                when (event.action) {
                    KeyEvent.ACTION_DOWN -> if (event.repeatCount == 0) {
                        playGamepadButtonHaptic()
                        setFastForwardHolder("ff:${physicalKey.first}:${physicalKey.second}", true)
                    }
                    KeyEvent.ACTION_UP -> setFastForwardHolder("ff:${physicalKey.first}:${physicalKey.second}", false)
                }
                return true
            }
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                playGamepadButtonHaptic()
                _gamepadShortcutActions.tryEmit(GamepadShortcutAction(padIndex, actionId))
            }
            return true
        }
        val rawPadKey = selectedActionId?.let { actionsById[it]?.padKey } ?: return false
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
            val normalizedPadIndex = normalizePadIndex(padIndex)
            val resolvedActionId = resolveMappedActionIdForKeyCode(
                keyCode = code,
                customBindings = customBindingsByPad[normalizedPadIndex].orEmpty()
            )
            if (resolvedActionId == "gun_recalibrate") {
                _gamepadShortcutActions.tryEmit(
                    GamepadShortcutAction(padIndex, "gun_recalibrate")
                )
            }
        }
        if (shouldUseAnalogTriggerAxisForRightStickMapping(event.device, rawPadKey)) {
            return true
        }
        val padKey = remapTriggerPadKeyForRightStick(rawPadKey)
        if (padKey == PadKey.Start) {
            if (event.action == KeyEvent.ACTION_DOWN) {
                synchronized(connectionLock) { heldButtonsByDeviceAndKey[physicalKey] = HeldButton(padIndex, padKey) }
            }
            return handleStartTransportEvent(padIndex, event)
        }
        val pressed = event.action == KeyEvent.ACTION_DOWN
        val range = if (pressed && padKey in ANALOG_STICK_DIRECTION_KEYS) 255 else 0

        if (pressed) {
            synchronized(connectionLock) { heldButtonsByDeviceAndKey[physicalKey] = HeldButton(padIndex, padKey) }
        }

        if (pressed && event.repeatCount == 0) {
            playGamepadButtonHaptic()
        }
        if (selectedActionId != null && isTurboAction(selectedActionId) && padKey !in ANALOG_STICK_DIRECTION_KEYS) {
            if (pressed && event.repeatCount == 0) startTurbo(physicalKey, padIndex, padKey)
            return true
        }
        setGamepadButton(padIndex, padKey, range, pressed)
        return true
    }

    private val turboJobs = mutableMapOf<Pair<Int, Int>, Job>()
    private val heldAxisButtons = mutableSetOf<Pair<Int, Int>>()
    private val axisBaselines = mutableMapOf<Pair<Int, Int>, Float>()
    private val fastForwardHolders = FastForwardHolders()
    private var boundAxisCodesByPad: Map<Int, Set<Int>> = emptyMap()

    private fun setFastForwardHolder(id: String, held: Boolean) {
        val changed = synchronized(connectionLock) { fastForwardHolders.set(id, held) }
        changed?.let { _gamepadFastForwardHolds.tryEmit(it) }
    }

    private fun removeFastForwardHolders(predicate: (String) -> Boolean) {
        val changed = synchronized(connectionLock) { fastForwardHolders.removeWhere(predicate) }
        changed?.let { _gamepadFastForwardHolds.tryEmit(it) }
    }

    private fun updateBoundAxisCodes() {
        boundAxisCodesByPad = customBindingsByPad
            .mapValues { (_, bindings) -> bindings.values.filter(::isAxisButtonCode).toSet() }
            .filterValues { it.isNotEmpty() }
    }

    private fun startTurbo(key: Pair<Int, Int>, padIndex: Int, padKey: Int) {
        turboJobs.remove(key)?.cancel()
        turboJobs[key] = scope.launch {
            while (synchronized(connectionLock) { heldButtonsByDeviceAndKey.containsKey(key) }) {
                setGamepadButton(padIndex, padKey, 0, true)
                delay(TURBO_HALF_PERIOD_MS.milliseconds)
                setGamepadButton(padIndex, padKey, 0, false)
                delay(TURBO_HALF_PERIOD_MS.milliseconds)
            }
        }
    }

    /** Feeds generic-axis "buttons" (see [axisButtonCode]) through the normal key path. */
    private fun dispatchBoundAxisButtons(event: MotionEvent, padIndex: Int) {
        val bound = boundAxisCodesByPad[normalizePadIndex(padIndex)].orEmpty()
        if (bound.isEmpty() && synchronized(connectionLock) { heldAxisButtons.isEmpty() }) return
        val deviceId = event.deviceId
        val stale = synchronized(connectionLock) {
            heldAxisButtons.filter { it.first == deviceId && it.second !in bound }
                .also { heldAxisButtons.removeAll(it.toSet()) }
        }
        stale.forEach { dispatchSyntheticKey(deviceId, it.second, false) }
        for (code in bound) {
            val value = event.getAxisValue(code - AXIS_CODE_BASE)
            val key = deviceId to code
            val baseline = synchronized(connectionLock) { axisBaselines.getOrPut(key) { value } }
            val held = synchronized(connectionLock) { key in heldAxisButtons }
            if (!held && axisButtonPressed(baseline, value)) {
                synchronized(connectionLock) { heldAxisButtons.add(key) }
                dispatchSyntheticKey(deviceId, code, true)
            } else if (held && value < AXIS_BUTTON_RELEASE) {
                synchronized(connectionLock) { heldAxisButtons.remove(key) }
                dispatchSyntheticKey(deviceId, code, false)
            }
        }
    }

    private fun dispatchSyntheticKey(deviceId: Int, code: Int, down: Boolean) {
        val now = SystemClock.uptimeMillis()
        handleKeyEvent(
            KeyEvent(now, now, if (down) KeyEvent.ACTION_DOWN else KeyEvent.ACTION_UP, code, 0, 0, deviceId, 0)
        )
    }

    private fun handleStartTransportEvent(padIndex: Int, event: KeyEvent): Boolean {
        return when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (event.repeatCount == 0) {
                    playGamepadButtonHaptic()
                    beginStartTransportHold(padIndex)
                }
                true
            }
            KeyEvent.ACTION_UP -> {
                val turboActive = endStartTransportHold(padIndex)
                if (!turboActive) {
                    scope.launch {
                        setGamepadButton(padIndex, PadKey.Start, 0, true)
                        delay(START_TAP_DURATION_MS)
                        setGamepadButton(padIndex, PadKey.Start, 0, false)
                    }
                }
                true
            }
            else -> false
        }
    }

    private fun beginStartTransportHold(padIndex: Int) {
        val state = StartTransportHoldState()
        state.job = scope.launch {
            delay(START_TRANSPORT_HOLD_DELAY_MS)
            val activated = synchronized(connectionLock) {
                if (startTransportHolds[padIndex] !== state) {
                    false
                } else {
                    state.turboActive = true
                    true
                }
            }
            if (activated) {
                setFastForwardHolder("start:$padIndex", true)
            }
        }
        synchronized(connectionLock) {
            startTransportHolds.remove(padIndex)?.job?.cancel()
            startTransportHolds[padIndex] = state
        }
    }

    private fun endStartTransportHold(padIndex: Int): Boolean {
        val state = synchronized(connectionLock) { startTransportHolds.remove(padIndex) } ?: return false
        state.job?.cancel()
        if (!state.turboActive) return false
        setFastForwardHolder("start:$padIndex", false)
        return true
    }

    private fun cancelStartTransportHolds() {
        val wasHolding = synchronized(connectionLock) {
            val active = startTransportHolds.values.any { it.turboActive }
            startTransportHolds.values.forEach { it.job?.cancel() }
            startTransportHolds.clear()
            active
        }
        if (wasHolding) {
            removeFastForwardHolders { it.startsWith("start:") }
        }
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (!emulationInputEnabled) return false
        if (!isGameController(event.device)) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false

        val padIndex = resolvePadIndexForDevice(event.deviceId) ?: return false
        val state = synchronized(connectionLock) {
            analogStatesByDeviceId.getOrPut(event.deviceId) { AnalogState() }
        }
        dispatchBoundAxisButtons(event, padIndex)

        val leftX = processStickAxis(
            value = event.getAxisValue(MotionEvent.AXIS_X),
            sensitivity = leftStickSensitivity,
            negativeDeadzone = leftNegativeDeadzone,
            antiDeadzone = leftAntiDeadzone,
            curve = leftStickCurve
        ).let { if (invertLeftStickHorizontal) -it else it }
        val leftY = processStickAxis(
            value = event.getAxisValue(MotionEvent.AXIS_Y),
            sensitivity = leftStickSensitivity,
            negativeDeadzone = leftNegativeDeadzone,
            antiDeadzone = leftAntiDeadzone,
            curve = leftStickCurve
        ).let { if (invertLeftStick) -it else it }
        if (leftX != state.prevLeftX || leftY != state.prevLeftY) {
            if (lightGunModeActive && lightGunAimSource == AppPreferences.LIGHT_GUN_AIM_LEFT_STICK) {
                _lightGunStickByPad.value = _lightGunStickByPad.value + (padIndex to (leftX to leftY))
            } else dispatchAnalogStick(
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

        val rightX = processStickAxis(
            value = getRightStickAxisValue(event, MotionEvent.AXIS_Z, MotionEvent.AXIS_RX),
            sensitivity = rightStickSensitivity,
            negativeDeadzone = rightNegativeDeadzone,
            antiDeadzone = rightAntiDeadzone,
            curve = rightStickCurve
        ).let { if (invertRightStickHorizontal) -it else it }
        val physicalRightY = processStickAxis(
            value = getRightStickAxisValue(event, MotionEvent.AXIS_RZ, MotionEvent.AXIS_RY),
            sensitivity = rightStickSensitivity,
            negativeDeadzone = rightNegativeDeadzone,
            antiDeadzone = rightAntiDeadzone,
            curve = rightStickCurve
        )
        val rightY = physicalRightY.let { if (invertRightStick) -it else it }
        val physicalLT = getAxisValueWithFallback(event, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE)
        val physicalRT = getAxisValueWithFallback(event, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS)
        val mappedLTPadKey = mapTriggerAxisToRawPadKey(padIndex, "l2")
            ?.let(::remapTriggerPadKeyForRightStick)
        val mappedRTPadKey = mapTriggerAxisToRawPadKey(padIndex, "r2")
            ?.let(::remapTriggerPadKeyForRightStick)
        val ltControlsRightStick = mappedLTPadKey == PadKey.RightStickDown
        val rtControlsRightStick = mappedRTPadKey == PadKey.RightStickUp
        val rightStickYFromTriggers = (
            (if (ltControlsRightStick) physicalLT else 0f) -
                (if (rtControlsRightStick) physicalRT else 0f)
            ).coerceIn(-1f, 1f)
        val rightStickY = (rightY + rightStickYFromTriggers).coerceIn(-1f, 1f)
        if (rightX != state.prevRightX || rightStickY != state.prevRightY) {
            if (lightGunModeActive && lightGunAimSource == AppPreferences.LIGHT_GUN_AIM_RIGHT_STICK) {
                _lightGunStickByPad.value = _lightGunStickByPad.value + (padIndex to (rightX to rightStickY))
            } else dispatchAnalogStick(
                padIndex = padIndex,
                x = rightX,
                y = rightStickY,
                upKey = PadKey.RightStickUp,
                rightKey = PadKey.RightStickRight,
                downKey = PadKey.RightStickDown,
                leftKey = PadKey.RightStickLeft
            )
            state.prevRightX = rightX
            state.prevRightY = rightStickY
        }

        val lt = if (ltControlsRightStick) 0f else physicalLT
        val rt = if (rtControlsRightStick) 0f else physicalRT
        if (mappedLTPadKey != state.prevLTPadKey) {
            state.prevLTPadKey?.let { dispatchAnalogButton(padIndex, it, 0f) }
            state.prevLTPadKey = mappedLTPadKey
            state.prevLT = 0f
        }
        if (mappedRTPadKey != state.prevRTPadKey) {
            state.prevRTPadKey?.let { dispatchAnalogButton(padIndex, it, 0f) }
            state.prevRTPadKey = mappedRTPadKey
            state.prevRT = 0f
        }
        if (lt != state.prevLT) {
            mappedLTPadKey?.let { dispatchAnalogButton(padIndex, it, lt) }
            state.prevLT = lt
        }
        if (rt != state.prevRT) {
            mappedRTPadKey?.let { dispatchAnalogButton(padIndex, it, rt) }
            state.prevRT = rt
        }
        state.prevRightStickYFromTriggers = rightStickYFromTriggers

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        if (hatX != state.prevHatX || hatY != state.prevHatY) {
            setGamepadButton(padIndex, PadKey.Left, 0, hatX < -0.5f)
            setGamepadButton(padIndex, PadKey.Right, 0, hatX > 0.5f)
            setGamepadButton(padIndex, PadKey.Up, 0, hatY < -0.5f)
            setGamepadButton(padIndex, PadKey.Down, 0, hatY > 0.5f)
            state.prevHatX = hatX
            state.prevHatY = hatY
        }

        return true
    }

    fun onPadVibration(padIndex: Int, largeMotor: Float, smallMotor: Float) {
        if (!emulationInputEnabled) {
            stopPadVibration(padIndex)
            return
        }
        playPadVibration(
            padIndex = padIndex,
            largeMotor = largeMotor,
            smallMotor = smallMotor,
            strengthOverride = null,
            durationMs = RUMBLE_PULSE_DURATION_MS,
            respectEnabledSetting = true
        )
    }

    fun testPadVibration(
        padIndex: Int = 0,
        strengthPercent: Int? = null,
        durationMs: Long = TEST_RUMBLE_DURATION_MS
    ) {
        playPadVibration(
            padIndex = padIndex,
            largeMotor = 1f,
            smallMotor = 1f,
            strengthOverride = strengthPercent?.coerceIn(0, 150)?.div(100f),
            durationMs = durationMs.coerceIn(40L, 600L),
            respectEnabledSetting = false
        )
    }

    private fun playGamepadButtonHaptic() {
        if (!buttonHapticsEnabled) return
        val now = SystemClock.elapsedRealtime()
        val systemFallbackRumbleActive = synchronized(connectionLock) {
            fallbackRumbleStatesByPad.values.any { it.expiresAtElapsedMs > now }
        }
        if (systemFallbackRumbleActive) return
        val context = appContext ?: return
        AndroidTouchHaptics.playButton(
            context = context,
            strengthPercent = buttonHapticsStrength,
            preset = buttonHapticsPreset,
            phase = AndroidTouchHaptics.ButtonPhase.PRESS
        )
    }

    private fun playPadVibration(
        padIndex: Int,
        largeMotor: Float,
        smallMotor: Float,
        strengthOverride: Float?,
        durationMs: Long,
        respectEnabledSetting: Boolean
    ) {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        if (respectEnabledSetting && !vibrationEnabled) {
            stopPadVibration(normalizedPadIndex)
            return
        }

        val largeIntensity = largeMotor.coerceIn(0f, 1f)
        val smallIntensity = smallMotor.coerceIn(0f, 1f)
        if (largeIntensity <= 0f && smallIntensity <= 0f) {
            stopPadVibration(normalizedPadIndex)
            return
        }

        val connectedGamepad = connectedGamepads().firstOrNull { it.padIndex == normalizedPadIndex }
        val resolvedTarget = resolveVibrationTarget(connectedGamepad, normalizedPadIndex)
        if (resolvedTarget == null) {
            stopPadVibration(normalizedPadIndex)
            logMissingVibrationTarget(normalizedPadIndex)
            return
        }

        val intensity = resolveRumbleIntensity(
            largeMotor = largeIntensity,
            smallMotor = smallIntensity,
            strength = (strengthOverride ?: vibrationStrength).coerceIn(0f, 1.5f),
            systemFallback = resolvedTarget.isSystemFallback
        )
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

        if (resolvedTarget.isSystemFallback) {
            updateFallbackPadVibration(normalizedPadIndex, amplitude, durationMs)
        } else {
            clearFallbackPadVibration(normalizedPadIndex)
            vibrate(resolvedTarget.target, amplitude, durationMs)
        }
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

    private fun setGamepadButton(padIndex: Int, key: Int, range: Int, pressed: Boolean) {
        EmulatorBridge.setPadButtonFromSource(padIndex, key, range, pressed, PAD_INPUT_SOURCE_GAMEPAD)
    }

    private fun resetGamepadPadState(padIndex: Int) {
        EmulatorBridge.resetPadStateFromSource(padIndex, PAD_INPUT_SOURCE_GAMEPAD)
    }

    private fun dispatchAnalogButton(padIndex: Int, key: Int, value: Float) {
        val range = (value.coerceIn(0f, 1f) * 255f).roundToInt().coerceIn(0, 255)
        setGamepadButton(padIndex, key, range, range > 0)
    }

    private fun resetChangedBindingStates(
        previousBindingsByPad: Map<Int, Map<String, Int>>,
        bindingsByPad: Map<Int, Map<String, Int>>
    ) {
        updateBoundAxisCodes()
        if (previousBindingsByPad == bindingsByPad) return
        val changedPadIndices = (previousBindingsByPad.keys + bindingsByPad.keys)
            .filterTo(mutableSetOf()) { padIndex ->
                previousBindingsByPad[padIndex] != bindingsByPad[padIndex]
            }
        if (changedPadIndices.isEmpty()) return

        synchronized(connectionLock) {
            val changedDeviceIds = deviceToPadIndex
                .filterValues { it in changedPadIndices }
                .keys
            changedDeviceIds.forEach(analogStatesByDeviceId::remove)
        }
        if (emulationInputEnabled) {
            changedPadIndices.forEach(::resetGamepadPadState)
        }
    }

    private fun resetRightStickTriggerState() {
        val resets = synchronized(connectionLock) {
            analogStatesByDeviceId.mapNotNull { (deviceId, state) ->
                val padIndex = deviceToPadIndex[deviceId] ?: return@mapNotNull null
                val reset = RightStickTriggerReset(
                    padIndex = padIndex,
                    releaseRightStickY = state.prevRightStickYFromTriggers != 0f,
                    releaseL2PadKey = state.prevLTPadKey.takeIf { state.prevLT != 0f },
                    releaseR2PadKey = state.prevRTPadKey.takeIf { state.prevRT != 0f }
                )
                state.prevRightY = 0f
                state.prevLT = 0f
                state.prevRT = 0f
                state.prevRightStickYFromTriggers = 0f
                reset
            }
        }
        if (!emulationInputEnabled) return

        resets.forEach { reset ->
            if (reset.releaseRightStickY) {
                setGamepadButton(reset.padIndex, PadKey.RightStickUp, 0, false)
                setGamepadButton(reset.padIndex, PadKey.RightStickDown, 0, false)
            }
            reset.releaseL2PadKey?.let { padKey ->
                setGamepadButton(reset.padIndex, padKey, 0, false)
            }
            reset.releaseR2PadKey?.let { padKey ->
                setGamepadButton(reset.padIndex, padKey, 0, false)
            }
        }
    }

    private fun mapKeyCodeToShortcutActionId(padIndex: Int, keyCode: Int): String? {
        return customShortcutBindingsByPadAndKeyCode[normalizePadIndex(padIndex)]?.get(keyCode)
    }

    private fun shouldUseAnalogTriggerAxisForRightStickMapping(device: InputDevice?, padKey: Int): Boolean {
        return when (padKey) {
            PadKey.L2 -> rightStickDownToL2 && hasAnyJoystickAxis(
                device = device,
                MotionEvent.AXIS_LTRIGGER,
                MotionEvent.AXIS_BRAKE
            )
            PadKey.R2 -> rightStickUpToR2 && hasAnyJoystickAxis(
                device = device,
                MotionEvent.AXIS_RTRIGGER,
                MotionEvent.AXIS_GAS
            )
            else -> false
        }
    }

    private fun mapTriggerAxisToRawPadKey(padIndex: Int, triggerActionId: String): Int? {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        val actionId = resolveMappedActionIdForTriggerAxis(
            triggerActionId = triggerActionId,
            customBindings = customBindingsByPad[normalizedPadIndex].orEmpty()
        ) ?: return null
        return actionsById[actionId]?.padKey
    }

    internal fun resolveMappedActionIdForKeyCode(
        keyCode: Int,
        customBindings: Map<String, Int>
    ): String? {
        val customActionId = customBindings.entries
            .filter { (actionId, mappedKeyCode) -> mappedKeyCode == keyCode && isActionActive(actionId) }
            .maxByOrNull { (actionId, _) -> if (GamepadBindingRules.isArcadeAction(actionId)) 1 else 0 }
            ?.key
        if (customActionId != null) {
            return customActionId
        }
        val defaultAction = mappableActions.firstOrNull { keyCode in it.defaultKeyCodes } ?: return null
        return defaultAction.id.takeUnless { it in customBindings && isActionActive(it) }
    }

    internal fun resolveMappedActionIdForTriggerAxis(
        triggerActionId: String,
        customBindings: Map<String, Int>
    ): String? {
        val triggerAction = actionsById[triggerActionId] ?: return null
        customBindings.entries
            .filter { (actionId, keyCode) ->
                keyCode in triggerAction.defaultKeyCodes && isActionActive(actionId)
            }
            .maxByOrNull { (actionId, _) -> if (GamepadBindingRules.isArcadeAction(actionId)) 1 else 0 }
            ?.let { return it.key }
        return triggerActionId.takeUnless { it in customBindings && isActionActive(it) }
    }

    private fun isActionActive(actionId: String): Boolean = when (actionId) {
        in gunActionIds -> lightGunModeActive
        in arcadeActionIds -> arcadeModeActive || lightGunModeActive
        else -> true
    }

    private fun remapTriggerPadKeyForRightStick(padKey: Int): Int {
        return when (padKey) {
            PadKey.L2 -> if (rightStickDownToL2) PadKey.RightStickDown else PadKey.L2
            PadKey.R2 -> if (rightStickUpToR2) PadKey.RightStickUp else PadKey.R2
            else -> padKey
        }
    }

    /**
     * Full per-stick shaping pipeline, in order:
     *  1. Negative deadzone: expands the raw input before the inner deadzone, so the
     *     stick leaves the deadzone sooner and reports movement earlier. This is the
     *     countermeasure for a game (or pad) whose built-in deadzone swallows small
     *     deflections; true center still reads 0 because the inner deadzone below
     *     re-zeroes it.
     *  2. Inner deadzone (existing): ignores center noise.
     *  3. Response curve: exponent applied to the post-deadzone magnitude. 1.0 is
     *     linear, above 1.0 gives finer control near center, below 1.0 reacts faster.
     *  4. Sensitivity: linear output scale (existing).
     *  5. Anti-deadzone: lifts any non-zero output to a minimum floor, so a game with
     *     its own large deadzone responds the instant the stick moves. True center
     *     (output == 0) always stays 0.
     */
    internal fun shapeStickAxis(
        value: Float,
        deadzone: Float,
        sensitivity: Float,
        negativeDeadzone: Float = 0f,
        antiDeadzone: Float = 0f,
        curve: Float = 1f
    ): Float {
        val raw = value.coerceIn(-1f, 1f)
        val negative = negativeDeadzone.coerceIn(0f, 0.3f)
        val expanded = if (negative > 0f) {
            (raw / (1f - negative)).coerceIn(-1f, 1f)
        } else {
            raw
        }
        val deadzoned = applyDeadzone(expanded, deadzone)
        if (deadzoned == 0f) return 0f
        val exponent = curve.coerceIn(0.5f, 2f)
        val curved = if (exponent != 1f) {
            sign(deadzoned) * abs(deadzoned).pow(exponent)
        } else {
            deadzoned
        }
        val scaled = (curved * sensitivity.coerceIn(0.5f, 2f)).coerceIn(-1f, 1f)
        val anti = antiDeadzone.coerceIn(0f, 0.5f)
        if (anti <= 0f || scaled == 0f) return scaled
        return (sign(scaled) * (anti + abs(scaled) * (1f - anti))).coerceIn(-1f, 1f)
    }

    private fun processStickAxis(
        value: Float,
        sensitivity: Float,
        negativeDeadzone: Float = 0f,
        antiDeadzone: Float = 0f,
        curve: Float = 1f
    ): Float = shapeStickAxis(
        value = value,
        deadzone = analogDeadzone,
        sensitivity = sensitivity,
        negativeDeadzone = negativeDeadzone,
        antiDeadzone = antiDeadzone,
        curve = curve
    )

    private fun getAxisValueWithFallback(event: MotionEvent, primaryAxis: Int, fallbackAxis: Int): Float {
        val primary = if (hasJoystickAxis(event.device, primaryAxis)) event.getAxisValue(primaryAxis) else 0f
        val fallback = if (hasJoystickAxis(event.device, fallbackAxis)) event.getAxisValue(fallbackAxis) else 0f
        // Handhelds may advertise both trigger families while only one carries input.
        return maxOf(primary, fallback)
    }

    private fun getRightStickAxisValue(event: MotionEvent, primaryAxis: Int, fallbackAxis: Int): Float {
        val device = event.device
        if (!hasJoystickAxis(device, primaryAxis)) return event.getAxisValue(fallbackAxis)
        if (!hasJoystickAxis(device, fallbackAxis)) return event.getAxisValue(primaryAxis)
        val primaryRange = device.getMotionRange(primaryAxis, InputDevice.SOURCE_JOYSTICK)
        val fallbackRange = device.getMotionRange(fallbackAxis, InputDevice.SOURCE_JOYSTICK)
        // Some handhelds expose Z/RZ as unipolar triggers and RX/RY as the right stick.
        // The old presence-only fallback silently selected the trigger axes there.
        if (primaryRange != null && fallbackRange != null &&
            primaryRange.min >= 0f && fallbackRange.min < 0f) {
            return event.getAxisValue(fallbackAxis)
        }
        val primary = event.getAxisValue(primaryAxis)
        val fallback = event.getAxisValue(fallbackAxis)
        return if (primary == 0f && fallback != 0f && fallbackRange?.min?.let { it < 0f } == true) {
            fallback
        } else primary
    }

    private fun applyDeadzone(value: Float, deadzoneFraction: Float): Float {
        val deadzone = deadzoneFraction.coerceIn(0f, 0.35f)
        val magnitude = abs(value)
        if (magnitude <= deadzone) return 0f
        val normalized = ((magnitude - deadzone) / (1f - deadzone)).coerceIn(0f, 1f)
        return normalized * sign(value)
    }

    private fun normalizePadIndex(padIndex: Int): Int = padIndex.coerceIn(0, MAX_PAD_SLOTS - 1)

    private fun isFingerprintUinputDevice(device: InputDevice): Boolean {
        val name = device.name.lowercase(Locale.ROOT)
        return FINGERPRINT_UINPUT_DEVICE_TOKENS.any { token ->
            name == token || name.startsWith("$token ") || name.startsWith("$token-") || name.startsWith("${token}_")
        }
    }

    private fun gamepadDeviceKey(device: InputDevice): String {
        val descriptor = runCatching { device.descriptor }.getOrNull()
        if (!descriptor.isNullOrBlank()) return descriptor
        return "${device.vendorId}:${device.productId}:${device.name}"
    }

    private fun hasRecognizedGamepadButton(device: InputDevice): Boolean {
        return try {
            device.hasKeys(*GAMEPAD_BUTTON_KEY_CODES).any { it }
        } catch (_: Exception) {
            false
        }
    }

    private fun hasRecognizedJoystickAxis(device: InputDevice): Boolean {
        return device.motionRanges.any { range ->
            (range.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK &&
                range.axis in JOYSTICK_AXIS_CODES
        }
    }

    private fun hasJoystickAxis(device: InputDevice?, axis: Int): Boolean {
        return device?.motionRanges?.any { range ->
            (range.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK &&
                range.axis == axis
        } == true
    }

    private fun hasAnyJoystickAxis(device: InputDevice?, vararg axes: Int): Boolean {
        if (device == null || axes.isEmpty()) return false
        return device.motionRanges.any { range ->
            (range.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK &&
                range.axis in axes
        }
    }

    private fun resolvePadIndexForDevice(deviceId: Int): Int? {
        refreshConnectedGamepads()
        return synchronized(connectionLock) { deviceToPadIndex[deviceId] } ?: resolveSiblingPadIndex(deviceId)
    }

    /** Secondary evdev nodes of an assigned controller share its pad (same descriptor, else same vendor:product). */
    private fun resolveSiblingPadIndex(deviceId: Int): Int? {
        val device = InputDevice.getDevice(deviceId) ?: return null
        if (device.isVirtual) return null
        val key = gamepadDeviceKey(device)
        val keyMatches = linkedSetOf<Int>()
        val candidates = linkedSetOf<Int>()
        synchronized(connectionLock) {
            for ((otherId, pad) in deviceToPadIndex) {
                val other = InputDevice.getDevice(otherId) ?: continue
                if (gamepadDeviceKey(other) == key) {
                    keyMatches.add(pad)
                } else if (device.vendorId != 0 && device.vendorId == other.vendorId &&
                    device.productId == other.productId
                ) candidates.add(pad)
            }
        }
        // Identical pads can share a key (blank descriptor): only a unique match is trustworthy.
        if (keyMatches.isNotEmpty()) return keyMatches.singleOrNull()
        return candidates.singleOrNull()
    }

    private fun refreshConnectedGamepads(): List<ConnectedGamepad> {
        val releasedAssignments = mutableListOf<Pair<Int, Int>>()
        val liveDeviceIds = InputDevice.getDeviceIds().toSet()
        val connectedSnapshot = synchronized(connectionLock) {
            heldButtonsByDeviceAndKey.keys.removeAll { it.first !in liveDeviceIds }
            heldAxisButtons.removeAll { it.first !in liveDeviceIds }
            axisBaselines.keys.removeAll { it.first !in liveDeviceIds }
            val connectedDevices = buildList {
                for (deviceId in InputDevice.getDeviceIds()) {
                    val device = InputDevice.getDevice(deviceId) ?: continue
                    if (isGameController(device)) {
                        add(device)
                    }
                }
            }
            val previousAssignments = deviceToPadIndex.toMap()
            val deviceKeysByDeviceId = connectedDevices.associate { it.id to gamepadDeviceKey(it) }
            val externalDeviceIds = connectedDevices
                .filter { it.isExternal }
                .mapTo(mutableSetOf()) { it.id }
            val updatedAssignments = assignConnectedGamepadSlots(
                previousAssignments = previousAssignments,
                connectedDeviceIds = connectedDevices.map { it.id },
                singleGamepadReplacesTouch = singleGamepadReplacesTouch,
                preferExternalGamepadAsPlayerOne = preferExternalGamepadAsPlayerOne,
                externalDeviceIds = externalDeviceIds,
                deviceKeysByDeviceId = deviceKeysByDeviceId,
                padDeviceKeys = deviceAssignmentKeysByPad,
                ignoredDeviceKeys = ignoredDeviceKeys
            )

            previousAssignments.forEach { (deviceId, padIndex) ->
                if (updatedAssignments[deviceId] != padIndex) {
                    analogStatesByDeviceId.remove(deviceId)
                    heldButtonsByDeviceAndKey.keys.removeAll { it.first == deviceId }
                    heldAxisButtons.removeAll { it.first == deviceId }
                    axisBaselines.keys.removeAll { it.first == deviceId }
                    rumbleStatesByPad.remove(padIndex)
                    releasedAssignments += (padIndex to deviceId)
                }
            }

            deviceToPadIndex.clear()
            deviceToPadIndex.putAll(updatedAssignments)

            val snapshot = connectedDevices.mapNotNull { device ->
                val padIndex = deviceToPadIndex[device.id] ?: return@mapNotNull null
                ConnectedGamepad(
                    padIndex = padIndex,
                    deviceId = device.id,
                    name = device.name.ifBlank { "Controller ${padIndex + 1}" }
                )
            }.sortedBy { it.padIndex }
            connectedGamepadSnapshot = snapshot
            _availableGamepadsState.value = connectedDevices.map { device ->
                AvailableGamepad(
                    key = deviceKeysByDeviceId.getValue(device.id),
                    deviceId = device.id,
                    name = device.name.ifBlank { "Controller" },
                    isExternal = device.isExternal,
                    padIndex = deviceToPadIndex[device.id]
                )
            }
            snapshot
        }

        removeFastForwardHolders { id ->
            id.startsWith("ff:") && id.split(":").getOrNull(1)?.toIntOrNull()?.let { it !in liveDeviceIds } == true
        }
        releasedAssignments.forEach { (padIndex, deviceId) ->
            removeFastForwardHolders { it.startsWith("ff:$deviceId:") }
            endStartTransportHold(padIndex)
            stopPhysicalGamepadVibrationForDevice(deviceId)
            clearFallbackPadVibration(padIndex)
            if (emulationInputEnabled) {
                resetGamepadPadState(padIndex)
            }
        }

        _connectedGamepadCountState.value = connectedSnapshot.size
        return connectedSnapshot
    }

    private fun resetAnalogState() {
        _lightGunStickByPad.value = emptyMap()
        synchronized(connectionLock) {
            analogStatesByDeviceId.clear()
            rumbleStatesByPad.clear()
        }
    }

    private fun stopAllGamepadVibrations() {
        val deviceIds = connectedGamepads().map { it.deviceId }
        deviceIds.forEach(::stopPhysicalGamepadVibrationForDevice)
        getSystemVibrationTarget()?.cancel()
        synchronized(connectionLock) {
            rumbleStatesByPad.clear()
            fallbackRumbleStatesByPad.clear()
        }
    }

    private fun stopPadVibration(padIndex: Int) {
        val normalizedPadIndex = normalizePadIndex(padIndex)
        val deviceId = synchronized(connectionLock) {
            rumbleStatesByPad.remove(normalizedPadIndex)
            deviceToPadIndex.entries.firstOrNull { it.value == normalizedPadIndex }?.key
        }
        if (deviceId != null) {
            stopPhysicalGamepadVibrationForDevice(deviceId)
        }
        clearFallbackPadVibration(normalizedPadIndex)
    }

    @Suppress("DEPRECATION")
    private fun getGamepadVibrationTarget(deviceId: Int): VibrationTarget? {
        val device = InputDevice.getDevice(deviceId) ?: return null
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            VibrationTarget.Managed(device.vibratorManager)
        } else {
            VibrationTarget.Single(device.vibrator)
        }
    }

    private fun resolveVibrationTarget(
        connectedGamepad: ConnectedGamepad?,
        padIndex: Int
    ): ResolvedVibrationTarget? {
        val gamepadTarget = connectedGamepad?.let { getGamepadVibrationTarget(it.deviceId) }
        if (gamepadTarget?.hasVibrator() == true) {
            return ResolvedVibrationTarget(gamepadTarget, isSystemFallback = false)
        }
        // Without a physical controller only the touch player's pad may use the phone.
        // Controllers without their own motors share the phone through the mixer below.
        if (!vibrationFallbackEnabled || (connectedGamepad == null && padIndex != 0)) {
            return null
        }
        val systemTarget = getSystemVibrationTarget()?.takeIf { it.hasVibrator() } ?: return null
        return ResolvedVibrationTarget(systemTarget, isSystemFallback = true)
    }

    @Suppress("DEPRECATION")
    private fun getSystemVibrationTarget(): VibrationTarget? {
        val context = appContext ?: return null
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            context.getSystemService(VibratorManager::class.java)?.let(VibrationTarget::Managed)
        } else {
            (context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator)?.let(VibrationTarget::Single)
        }
    }

    @SuppressLint("NewApi")
    private fun VibrationTarget.hasVibrator(): Boolean {
        return when (this) {
            is VibrationTarget.Single -> vibrator.hasVibrator()
            is VibrationTarget.Managed -> {
                manager.defaultVibrator.hasVibrator() || manager.vibratorIds.isNotEmpty()
            }
        }
    }

    private fun VibrationTarget.cancel() {
        when (this) {
            is VibrationTarget.Single -> vibrator.cancel()
            is VibrationTarget.Managed -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    manager.cancel()
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun vibrate(target: VibrationTarget, amplitude: Int, durationMs: Long) {
        when (target) {
            is VibrationTarget.Single -> {
                val resolvedAmplitude = if (target.vibrator.hasAmplitudeControl()) {
                    amplitude
                } else {
                    VibrationEffect.DEFAULT_AMPLITUDE
                }
                target.vibrator.vibrate(VibrationEffect.createOneShot(durationMs, resolvedAmplitude))
            }
            is VibrationTarget.Managed -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    val effect = VibrationEffect.createOneShot(durationMs, amplitude)
                    target.manager.vibrate(CombinedVibration.createParallel(effect))
                }
            }
        }
    }

    private fun updateFallbackPadVibration(padIndex: Int, amplitude: Int, durationMs: Long) {
        val now = SystemClock.elapsedRealtime()
        val mixedState = synchronized(connectionLock) {
            fallbackRumbleStatesByPad.entries.removeAll { it.value.expiresAtElapsedMs <= now }
            fallbackRumbleStatesByPad[padIndex] = FallbackRumbleState(
                amplitude = amplitude,
                durationMs = durationMs,
                expiresAtElapsedMs = now + durationMs
            )
            fallbackRumbleStatesByPad.values.maxByOrNull { it.amplitude }
        } ?: return
        val target = getSystemVibrationTarget()?.takeIf { it.hasVibrator() } ?: return
        vibrate(target, mixedState.amplitude, mixedState.durationMs)
    }

    private fun clearFallbackPadVibration(padIndex: Int) {
        val now = SystemClock.elapsedRealtime()
        val (removed, mixedState) = synchronized(connectionLock) {
            val removed = fallbackRumbleStatesByPad.remove(padIndex) != null
            fallbackRumbleStatesByPad.entries.removeAll { it.value.expiresAtElapsedMs <= now }
            removed to fallbackRumbleStatesByPad.values.maxByOrNull { it.amplitude }
        }
        if (!removed) return
        val target = getSystemVibrationTarget()?.takeIf { it.hasVibrator() } ?: return
        if (mixedState == null) {
            target.cancel()
        } else {
            vibrate(target, mixedState.amplitude, mixedState.durationMs)
        }
    }

    private fun stopPhysicalGamepadVibrationForDevice(deviceId: Int) {
        val target = getGamepadVibrationTarget(deviceId)?.takeIf { it.hasVibrator() } ?: return
        target.cancel()
    }

    private fun logMissingVibrationTarget(padIndex: Int) {
        val now = SystemClock.elapsedRealtime()
        if ((now - lastMissingVibratorLogElapsedMs) < MISSING_VIBRATOR_LOG_INTERVAL_MS) {
            return
        }
        lastMissingVibratorLogElapsedMs = now
        Log.w(TAG, "No vibration target available for pad $padIndex")
    }

    /**
     * A phone has one low-frequency actuator, while a DualShock 2 has separate large and small
     * motors. Mapping the binary small motor at full strength turns sustained road/engine effects
     * into an unnecessarily harsh continuous buzz. Physical controllers retain the original
     * two-motor intensity; only the phone fallback attenuates the small motor.
     */
    internal fun resolveRumbleIntensity(
        largeMotor: Float,
        smallMotor: Float,
        strength: Float,
        systemFallback: Boolean
    ): Float {
        val large = largeMotor.coerceIn(0f, 1f)
        val small = smallMotor.coerceIn(0f, 1f)
        val mixed = if (systemFallback) {
            maxOf(large, small * PHONE_SMALL_MOTOR_WEIGHT)
        } else {
            maxOf(large, small)
        }
        return (mixed * strength.coerceIn(0f, 1.5f)).coerceIn(0f, 1.5f)
    }

    internal fun assignConnectedGamepadSlots(
        previousAssignments: Map<Int, Int>,
        connectedDeviceIds: List<Int>,
        singleGamepadReplacesTouch: Boolean,
        preferExternalGamepadAsPlayerOne: Boolean = AppPreferences.DEFAULT_PREFER_EXTERNAL_GAMEPAD_PLAYER_ONE,
        externalDeviceIds: Set<Int> = emptySet(),
        deviceKeysByDeviceId: Map<Int, String> = emptyMap(),
        padDeviceKeys: Map<Int, String> = emptyMap(),
        ignoredDeviceKeys: Set<String> = emptySet()
    ): LinkedHashMap<Int, Int> {
        val connectedIds = connectedDeviceIds.distinct().filterNot { deviceId ->
            deviceKeysByDeviceId[deviceId]?.let { it in ignoredDeviceKeys } == true
        }
        val connectedIdSet = connectedIds.toSet()
        val stableDeviceIds = buildList {
            addAll(
                previousAssignments.entries
                    .sortedBy { it.value }
                    .map { it.key }
                    .filter { it in connectedIdSet }
            )
            connectedIds.forEach { deviceId ->
                if (deviceId !in previousAssignments) {
                    add(deviceId)
                }
            }
        }
        val orderedDeviceIds = if (preferExternalGamepadAsPlayerOne) {
            stableDeviceIds.filter { it in externalDeviceIds } +
                stableDeviceIds.filterNot { it in externalDeviceIds }
        } else {
            stableDeviceIds
        }
        val assignments = linkedMapOf<Int, Int>()
        val assignedDeviceIds = mutableSetOf<Int>()
        padDeviceKeys.toSortedMap().forEach { (padIndex, deviceKey) ->
            if (padIndex !in 0 until MAX_PAD_SLOTS || deviceKey.isBlank()) return@forEach
            val deviceId = orderedDeviceIds.firstOrNull { candidate ->
                candidate !in assignedDeviceIds && deviceKeysByDeviceId[candidate] == deviceKey
            } ?: return@forEach
            assignments[deviceId] = padIndex
            assignedDeviceIds += deviceId
        }
        val claimedPadIndices = assignments.values.toSet()
        val remainingPadIndices = desiredPadIndices(
            connectedGamepadCount = orderedDeviceIds.size,
            singleGamepadReplacesTouch = singleGamepadReplacesTouch
        ).filterNot { it in claimedPadIndices }
        orderedDeviceIds.asSequence()
            .filterNot { it in assignedDeviceIds }
            .zip(remainingPadIndices.asSequence())
            .forEach { (deviceId, padIndex) -> assignments[deviceId] = padIndex }
        return assignments
    }

    private fun desiredPadIndices(
        connectedGamepadCount: Int,
        singleGamepadReplacesTouch: Boolean = this.singleGamepadReplacesTouch
    ): List<Int> {
        val visibleGamepadCount = connectedGamepadCount.coerceIn(0, MAX_PAD_SLOTS)
        return when {
            visibleGamepadCount <= 0 -> emptyList()
            visibleGamepadCount == 1 && !singleGamepadReplacesTouch -> listOf(1)
            else -> List(visibleGamepadCount) { it }
        }
    }
}

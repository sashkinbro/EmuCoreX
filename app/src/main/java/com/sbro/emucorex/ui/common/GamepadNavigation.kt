package com.sbro.emucorex.ui.common

import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.focus.FocusDirection
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import kotlin.math.abs
import kotlin.math.hypot

enum class GamepadUiDirection {
    Up,
    Down,
    Left,
    Right
}

private data class GamepadNavigationRegistration(
    val token: Any,
    val onMove: (GamepadUiDirection) -> Boolean,
    val onBack: (() -> Boolean)?
)

private data class GamepadMenuRegistration(
    val token: Any,
    val onMenu: () -> Unit
)

private data class GamepadShoulderRegistration(
    val token: Any,
    val onPrevious: () -> Unit,
    val onNext: () -> Unit
)

object GamepadUiInputRouter {
    private const val AXIS_PRESS_THRESHOLD = 0.62f
    private const val AXIS_RELEASE_THRESHOLD = 0.34f
    private const val AXIS_REPEAT_MS = 170L

    private val lock = Any()
    private val navigationRegistrations = mutableListOf<GamepadNavigationRegistration>()
    private val menuRegistrations = mutableListOf<GamepadMenuRegistration>()
    private val shoulderRegistrations = mutableListOf<GamepadShoulderRegistration>()
    private var lastAxisDirection: GamepadUiDirection? = null
    private var lastAxisEventTimeMs: Long = 0L

    fun registerNavigation(
        token: Any,
        onMove: (GamepadUiDirection) -> Boolean,
        onBack: (() -> Boolean)? = null
    ) {
        synchronized(lock) {
            navigationRegistrations.removeAll { it.token === token }
            navigationRegistrations += GamepadNavigationRegistration(token, onMove, onBack)
        }
    }

    fun unregisterNavigation(token: Any) {
        synchronized(lock) {
            navigationRegistrations.removeAll { it.token === token }
            resetAxisStateLocked()
        }
    }

    fun registerMenuAction(token: Any, onMenu: () -> Unit) {
        synchronized(lock) {
            menuRegistrations.removeAll { it.token === token }
            menuRegistrations += GamepadMenuRegistration(token, onMenu)
        }
    }

    fun unregisterMenuAction(token: Any) {
        synchronized(lock) {
            menuRegistrations.removeAll { it.token === token }
        }
    }

    fun registerShoulderActions(
        token: Any,
        onPrevious: () -> Unit,
        onNext: () -> Unit
    ) {
        synchronized(lock) {
            shoulderRegistrations.removeAll { it.token === token }
            shoulderRegistrations += GamepadShoulderRegistration(token, onPrevious, onNext)
        }
    }

    fun unregisterShoulderActions(token: Any) {
        synchronized(lock) {
            shoulderRegistrations.removeAll { it.token === token }
        }
    }

    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN) return false

        keyDirection(event.keyCode)?.let { direction ->
            return activeNavigation()?.onMove?.invoke(direction) == true
        }

        if (event.repeatCount > 0) return false

        return when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_BUTTON_2,
            KeyEvent.KEYCODE_ESCAPE,
            KeyEvent.KEYCODE_BACK -> activeNavigation()?.onBack?.invoke() == true
            KeyEvent.KEYCODE_MENU,
            KeyEvent.KEYCODE_BUTTON_MODE,
            KeyEvent.KEYCODE_BUTTON_START,
            KeyEvent.KEYCODE_BUTTON_10 -> invokeMenuAction()
            KeyEvent.KEYCODE_BUTTON_L1,
            KeyEvent.KEYCODE_BUTTON_5 -> invokeShoulderAction(previous = true)
            KeyEvent.KEYCODE_BUTTON_R1,
            KeyEvent.KEYCODE_BUTTON_6 -> invokeShoulderAction(previous = false)
            else -> false
        }
    }

    fun handleEmulationOverlayKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount > 0) return false
        return when (event.keyCode) {
            KeyEvent.KEYCODE_MENU,
            KeyEvent.KEYCODE_BUTTON_MODE -> invokeMenuAction()
            else -> false
        }
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (event.actionMasked != MotionEvent.ACTION_MOVE) return false
        val direction = resolveAxisDirection(event)
        if (direction == null) {
            synchronized(lock) {
                resetAxisStateLocked()
            }
            return false
        }

        val now = SystemClock.elapsedRealtime()
        val shouldTrigger = synchronized(lock) {
            val trigger = direction != lastAxisDirection ||
                (now - lastAxisEventTimeMs) >= AXIS_REPEAT_MS
            if (trigger) {
                lastAxisDirection = direction
                lastAxisEventTimeMs = now
            }
            trigger
        }
        if (!shouldTrigger) return true

        return activeNavigation()?.onMove?.invoke(direction) == true
    }

    fun shouldMapToPrimaryClick(event: KeyEvent): Boolean {
        return event.keyCode == KeyEvent.KEYCODE_BUTTON_A ||
            event.keyCode == KeyEvent.KEYCODE_BUTTON_1
    }

    private fun keyDirection(keyCode: Int): GamepadUiDirection? {
        return when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> GamepadUiDirection.Up
            KeyEvent.KEYCODE_DPAD_DOWN -> GamepadUiDirection.Down
            KeyEvent.KEYCODE_DPAD_LEFT -> GamepadUiDirection.Left
            KeyEvent.KEYCODE_DPAD_RIGHT -> GamepadUiDirection.Right
            else -> null
        }
    }

    private fun resolveAxisDirection(event: MotionEvent): GamepadUiDirection? {
        val vectors = listOf(
            AxisVector(event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y)),
            AxisVector(event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y))
        )
        val strongest = vectors.maxByOrNull { hypot(it.x.toDouble(), it.y.toDouble()) } ?: return null
        if (abs(strongest.x) < AXIS_RELEASE_THRESHOLD && abs(strongest.y) < AXIS_RELEASE_THRESHOLD) {
            return null
        }
        if (abs(strongest.x) < AXIS_PRESS_THRESHOLD && abs(strongest.y) < AXIS_PRESS_THRESHOLD) {
            return lastAxisDirection
        }
        return if (abs(strongest.x) >= abs(strongest.y)) {
            if (strongest.x < 0f) GamepadUiDirection.Left else GamepadUiDirection.Right
        } else {
            if (strongest.y < 0f) GamepadUiDirection.Up else GamepadUiDirection.Down
        }
    }

    private fun activeNavigation(): GamepadNavigationRegistration? {
        return synchronized(lock) { navigationRegistrations.lastOrNull() }
    }

    private fun invokeMenuAction(): Boolean {
        val action = synchronized(lock) { menuRegistrations.lastOrNull()?.onMenu } ?: return false
        action()
        return true
    }

    private fun invokeShoulderAction(previous: Boolean): Boolean {
        val registration = synchronized(lock) { shoulderRegistrations.lastOrNull() } ?: return false
        if (previous) {
            registration.onPrevious()
        } else {
            registration.onNext()
        }
        return true
    }

    private fun resetAxisStateLocked() {
        lastAxisDirection = null
        lastAxisEventTimeMs = 0L
    }

    private data class AxisVector(val x: Float, val y: Float)
}

@Composable
fun ProvideGamepadUiNavigation(
    enabled: Boolean = true,
    onBack: (() -> Boolean)? = null
) {
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current
    val view = LocalView.current
    val token = remember { Any() }

    DisposableEffect(enabled, focusManager, keyboardController, view, onBack) {
        if (enabled) {
            GamepadUiInputRouter.registerNavigation(
                token = token,
                onMove = { direction ->
                    keyboardController?.hide()
                    val moved = focusManager.moveFocus(direction.toFocusDirection())
                    if (view.onCheckIsTextEditor()) {
                        // A focused text field keeps the system input session alive, and Android
                        // then forwards every gamepad event to the IME (InputEventSender). End the
                        // session so sticks stop flooding logcat and wasting CPU while navigating.
                        focusManager.clearFocus(force = true)
                        keyboardController?.hide()
                    }
                    moved
                },
                onBack = onBack
            )
        }
        onDispose {
            GamepadUiInputRouter.unregisterNavigation(token)
        }
    }
}

@Composable
fun ProvideGamepadMenuAction(
    enabled: Boolean = true,
    onMenu: () -> Unit
) {
    val token = remember { Any() }
    DisposableEffect(enabled, onMenu) {
        if (enabled) {
            GamepadUiInputRouter.registerMenuAction(token, onMenu)
        }
        onDispose {
            GamepadUiInputRouter.unregisterMenuAction(token)
        }
    }
}

@Composable
fun ProvideGamepadShoulderActions(
    enabled: Boolean = true,
    onPrevious: () -> Unit,
    onNext: () -> Unit
) {
    val token = remember { Any() }
    DisposableEffect(enabled, onPrevious, onNext) {
        if (enabled) {
            GamepadUiInputRouter.registerShoulderActions(token, onPrevious, onNext)
        }
        onDispose {
            GamepadUiInputRouter.unregisterShoulderActions(token)
        }
    }
}

fun Modifier.skipGamepadTextFieldFocus(): Modifier = composed {
    val focusRequester = remember { FocusRequester() }
    var touchFocusAllowed by remember { mutableStateOf(false) }

    LaunchedEffect(touchFocusAllowed) {
        if (touchFocusAllowed) {
            focusRequester.requestFocus()
        }
    }

    pointerInteropFilter { event ->
        if (
            event.actionMasked == MotionEvent.ACTION_DOWN &&
            (event.source and android.view.InputDevice.SOURCE_TOUCHSCREEN) == android.view.InputDevice.SOURCE_TOUCHSCREEN
        ) {
            touchFocusAllowed = true
        }
        false
    }
        .focusRequester(focusRequester)
        .onFocusChanged { state ->
            if (!state.isFocused) {
                touchFocusAllowed = false
            }
        }
        .focusProperties {
            canFocus = touchFocusAllowed
        }
}

private fun GamepadUiDirection.toFocusDirection(): FocusDirection {
    return when (this) {
        GamepadUiDirection.Up -> FocusDirection.Up
        GamepadUiDirection.Down -> FocusDirection.Down
        GamepadUiDirection.Left -> FocusDirection.Left
        GamepadUiDirection.Right -> FocusDirection.Right
    }
}

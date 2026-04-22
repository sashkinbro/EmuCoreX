package com.sbro.emucorex.core

import android.os.SystemClock
import android.view.MotionEvent
import kotlin.math.abs

object GamepadUiActions {
    @Volatile
    private var toggleDrawerAction: (() -> Unit)? = null
    @Volatile
    private var navigateLeftAction: (() -> Unit)? = null
    @Volatile
    private var navigateRightAction: (() -> Unit)? = null
    @Volatile
    private var lastHorizontalDirection: Int = 0
    @Volatile
    private var lastHorizontalEventTimeMs: Long = 0L

    private const val HORIZONTAL_NAVIGATION_THRESHOLD = 0.65f
    private const val HORIZONTAL_NAVIGATION_REPEAT_MS = 180L

    fun setToggleDrawerAction(action: (() -> Unit)?) {
        toggleDrawerAction = action
    }

    fun toggleDrawer(): Boolean {
        val action = toggleDrawerAction ?: return false
        action()
        return true
    }

    fun setHorizontalNavigationActions(
        onNavigateLeft: (() -> Unit)?,
        onNavigateRight: (() -> Unit)?
    ) {
        navigateLeftAction = onNavigateLeft
        navigateRightAction = onNavigateRight
        lastHorizontalDirection = 0
        lastHorizontalEventTimeMs = 0L
    }

    fun navigateLeft(): Boolean {
        val action = navigateLeftAction ?: return false
        action()
        return true
    }

    fun navigateRight(): Boolean {
        val action = navigateRightAction ?: return false
        action()
        return true
    }

    fun handleHorizontalMotion(event: MotionEvent): Boolean {
        val zAxis = event.getAxisValue(MotionEvent.AXIS_Z)
        val rxAxis = event.getAxisValue(MotionEvent.AXIS_RX)
        val hatAxis = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val horizontal = listOf(zAxis, rxAxis, hatAxis).maxByOrNull { abs(it) } ?: 0f

        val direction = when {
            horizontal <= -HORIZONTAL_NAVIGATION_THRESHOLD -> -1
            horizontal >= HORIZONTAL_NAVIGATION_THRESHOLD -> 1
            else -> 0
        }

        if (direction == 0) {
            lastHorizontalDirection = 0
            return false
        }

        val now = SystemClock.elapsedRealtime()
        val shouldTrigger = direction != lastHorizontalDirection ||
            (now - lastHorizontalEventTimeMs) >= HORIZONTAL_NAVIGATION_REPEAT_MS
        if (!shouldTrigger) return false

        lastHorizontalDirection = direction
        lastHorizontalEventTimeMs = now
        return if (direction < 0) navigateLeft() else navigateRight()
    }
}

package com.sbro.emucorex.core.utils

import android.util.Log

object SDLControllerManager {
    private const val TAG = "SDLControllerManager"

    @JvmStatic
    external fun nativeSetupJNI(): Int

    @JvmStatic
    external fun onNativePadDown(deviceId: Int, keycode: Int): Boolean

    @JvmStatic
    external fun onNativePadUp(deviceId: Int, keycode: Int): Boolean

    @JvmStatic
    external fun onNativeJoy(deviceId: Int, axis: Int, value: Float)

    @JvmStatic
    external fun onNativeHat(deviceId: Int, hatId: Int, x: Int, y: Int)

    @JvmStatic
    external fun nativeAddJoystick(
        deviceId: Int, name: String, desc: String, 
        vendorId: Int, productId: Int, buttonMask: Int, 
        nAxes: Int, axisMask: Int, nHats: Int, canRumble: Boolean
    )

    @JvmStatic
    external fun nativeRemoveJoystick(deviceId: Int)

    @JvmStatic
    external fun nativeAddHaptic(deviceId: Int, name: String)

    @JvmStatic
    external fun nativeRemoveHaptic(deviceId: Int)

    @JvmStatic
    fun pollInputDevices() {
    }

    @JvmStatic
    fun pollHapticDevices() {
    }

    @JvmStatic
    fun hapticRun(deviceId: Int, intensity: Float, length: Int) {
        Log.d(TAG, "hapticRun device=$deviceId intensity=$intensity length=$length")
    }

    @JvmStatic
    fun hapticRumble(deviceId: Int, lowFreq: Float, highFreq: Float, length: Int) {
        Log.d(TAG, "hapticRumble device=$deviceId low=$lowFreq high=$highFreq length=$length")
    }

    @JvmStatic
    fun hapticStop(deviceId: Int) {
        Log.d(TAG, "hapticStop device=$deviceId")
    }
}

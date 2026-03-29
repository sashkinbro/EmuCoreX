package com.sbro.emucorex.core.hid

import android.util.Log

object HIDDeviceManager {
    private const val TAG = "HIDDeviceManager"

    @JvmStatic
    external fun HIDDeviceRegisterCallback()

    @JvmStatic
    external fun HIDDeviceReleaseCallback()

    @JvmStatic
    external fun HIDDeviceConnected(
        deviceId: Int, identifier: String, vendorId: Int, productId: Int,
        serialNumber: String, releaseNumber: Int, manufacturer: String, product: String,
        interfaceNum: Int, interfaceClass: Int, interfaceSubclass: Int, interfaceProtocol: Int,
        bluetooth: Boolean
    )

    @JvmStatic
    external fun HIDDeviceOpenPending(deviceId: Int)

    @JvmStatic
    external fun HIDDeviceOpenResult(deviceId: Int, opened: Boolean)

    @JvmStatic
    external fun HIDDeviceDisconnected(deviceId: Int)

    @JvmStatic
    external fun HIDDeviceInputReport(deviceId: Int, value: ByteArray)

    @JvmStatic
    external fun HIDDeviceReportResponse(deviceId: Int, value: ByteArray)

    @JvmStatic
    fun initialize(p1: Boolean, p2: Boolean): Boolean {
        return true
    }

    @JvmStatic
    fun openDevice(deviceId: Int): Boolean {
        return true
    }

    @JvmStatic
    fun writeReport(deviceId: Int, value: ByteArray, p3: Boolean): Int {
        return value.size
    }

    @JvmStatic
    fun readReport(deviceId: Int, value: ByteArray, p3: Boolean): Boolean {
        return true
    }

    @JvmStatic
    fun closeDevice(deviceId: Int) {
    }
}

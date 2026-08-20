package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileDeviceIdentityTest {

    @Test
    fun `stable id survives app data recreation for the same install identity`() {
        val first = ProfileDeviceInfoProvider.stableDeviceId("android-id", "com.sbro.emucorex")
        val afterReinstall = ProfileDeviceInfoProvider.stableDeviceId("android-id", "com.sbro.emucorex")

        assertEquals(first, afterReinstall)
        assertTrue(first.startsWith("android_"))
        assertEquals(48, first.length)
    }

    @Test
    fun `stable id is scoped to device and application`() {
        val baseline = ProfileDeviceInfoProvider.stableDeviceId("device-a", "com.sbro.emucorex")

        assertNotEquals(baseline, ProfileDeviceInfoProvider.stableDeviceId("device-b", "com.sbro.emucorex"))
        assertNotEquals(baseline, ProfileDeviceInfoProvider.stableDeviceId("device-a", "com.example.other"))
    }
}

package com.sbro.emucorex.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EeFpuDivRoundingInstrumentedTest {
    @Test
    fun divUsesDedicatedNearestRoundingAndRestoresEeMode() {
        assertTrue("Native core must load for the ARM64 conformance test", NativeApp.hasNativeCore)
        assertEquals("ok", NativeApp.runEeFpuDivRoundingSelfTest())
    }
}

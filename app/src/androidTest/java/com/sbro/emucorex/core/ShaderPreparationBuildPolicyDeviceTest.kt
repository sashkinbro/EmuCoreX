package com.sbro.emucorex.core

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.BuildConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ShaderPreparationBuildPolicyDeviceTest {
    @Test
    fun ordinaryBuildCannotStartOrResumePreparationThroughJni() {
        assumeFalse("The explicit diagnostic core intentionally includes preparation",
            BuildConfig.TFX_PREPARATION_DIAGNOSTICS && AndroidNativeCoreSelector.runtimePageSizeBytes() == 4096L)
        assertTrue("The native core must load", NativeApp.hasNativeCore)
        assertFalse("No VM should be active", NativeApp.hasValidVm())
        repeat(3) {
            assertFalse(NativeApp.checkShaderPreparationReadiness())
            assertFalse(NativeApp.startShaderPreparation())
            assertFalse(NativeApp.resumeShaderPreparation(1))
            assertFalse(NativeApp.cancelShaderPreparation(1))
            val status = ShaderPreparationStatus.parseSnapshot(NativeApp.getShaderPreparationStatusSnapshot())
            assertNotNull("The disabled API must still return a valid snapshot", status)
            assertEquals(ShaderPreparationStatus.State.IDLE, status?.state)
            assertEquals(0L, status?.jobId)
            assertEquals(0, status?.total)
        }
        assertFalse("Rejected requests must never start a VM", NativeApp.hasValidVm())
    }
}

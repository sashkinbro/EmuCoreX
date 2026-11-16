package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in real-disc test. Does not create or overwrite save states. */
@RunWith(AndroidJUnit4::class)
class DiscSwapDeviceTest {
    @Test
    fun repeatedDiscChangesAndFailedOpenKeepTheVmRendering() = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        val game = args.getString("gamePath")
        val secondDisc = args.getString("swapDiscPath")
        val bios = args.getString("biosPath")
        assumeTrue("Supply gamePath, swapDiscPath and biosPath", !game.isNullOrBlank() &&
            !secondDisc.isNullOrBlank() && !bios.isNullOrBlank())
        val context = ApplicationProvider.getApplicationContext<Context>()
        val preferences = AppPreferences(context)
        val previousBios = preferences.biosPath.first()
        preferences.setBiosPath(requireNotNull(bios))
        val activity = instrumentation.startActivitySync(Intent("${context.packageName}.action.LAUNCH_GAME").apply {
            setClassName(context.packageName, "${context.packageName}.MainActivity")
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra("${context.packageName}.extra.GAME_PATH", game)
            putExtra("${context.packageName}.extra.RENDERER", RendererDefaults.VULKAN)
            putExtra("${context.packageName}.extra.AUTOTEST_MODE", true)
        })
        try {
            await("VM must start") { EmulatorBridge.hasValidVm() }
            awaitFreshFrames()
            // Switch while the guest may still have outstanding seeks/reads.
            repeat(3) {
                for (disc in listOf(requireNotNull(secondDisc), requireNotNull(game))) {
                    EmulatorBridge.pause()
                    assertTrue("Disc $disc must open", EmulatorBridge.changeDisc(disc))
                    EmulatorBridge.resume()
                    awaitFreshFrames()
                    Thread.sleep(1_000)
                    assertTrue("VM must survive each disc swap", EmulatorBridge.hasValidVm())
                }
            }
            EmulatorBridge.pause()
            assertFalse("An unavailable image must fail without losing the mounted disc",
                EmulatorBridge.changeDisc("/sdcard/PS2/Games/emucorex-missing-disc-test.iso"))
            EmulatorBridge.resume()
            Thread.sleep(9_000) // Eject, detect and seek back to the restored image.
            awaitFreshFrames()
            assertTrue("VM must survive rollback to the previous image", EmulatorBridge.hasValidVm())
        } finally {
            EmulatorBridge.shutdown()
            instrumentation.runOnMainSync { activity.finish() }
            preferences.setBiosPath(previousBios.orEmpty())
        }
    }

    private fun awaitFreshFrames() {
        NativeApp.setPerformanceMetricsEnabled(false, false, false)
        NativeApp.setPerformanceMetricsEnabled(true, true, false)
        await("VM must produce fresh frames") {
            NativeApp.getPerformanceMetricsSnapshot().orEmpty().lineSequence().firstOrNull()
                ?.toFloatOrNull()?.let { it > 0f } == true
        }
    }

    private fun await(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 45_000
        while (!condition() && SystemClock.elapsedRealtime() < deadline) Thread.sleep(50)
        assertTrue(message, condition())
    }
}

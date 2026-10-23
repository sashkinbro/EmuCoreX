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
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Opt-in lifecycle smoke test using an existing BIOS and game; no save states are written. */
@RunWith(AndroidJUnit4::class)
class RendererLifecycleDeviceTest {
    @Test
    fun gameKeepsRenderingAcrossPauseResumeAndRestart() = runBlocking {
        val arguments = InstrumentationRegistry.getArguments()
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val gamePath = arguments.getString("gamePath")
        val biosPath = arguments.getString("biosPath")
        assumeTrue("Supply gamePath and biosPath to run on a device", !gamePath.isNullOrBlank() && !biosPath.isNullOrBlank())
        val renderer = arguments.getString("renderer")?.toIntOrNull() ?: RendererDefaults.VULKAN
        require(renderer in listOf(RendererDefaults.OPENGL, RendererDefaults.VULKAN))
        val context = ApplicationProvider.getApplicationContext<Context>()
        val preferences = AppPreferences(context)
        val previousBios = preferences.biosPath.first()
        assertTrue("The supplied game must be accessible to the app", File(requireNotNull(gamePath)).isFile)
        assertTrue("The supplied BIOS directory must be accessible to the app", File(requireNotNull(biosPath)).isDirectory)
        try {
            preferences.setBiosPath(biosPath)
            repeat(2) { launch ->
                val intent = Intent("${context.packageName}.action.LAUNCH_GAME").apply {
                    setClassName(context.packageName, "${context.packageName}.MainActivity")
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    putExtra("${context.packageName}.extra.GAME_PATH", gamePath)
                    putExtra("${context.packageName}.extra.RENDERER", renderer)
                    putExtra("${context.packageName}.extra.ENABLE_EE_RECOMPILER", true)
                    putExtra("${context.packageName}.extra.ENABLE_IOP_RECOMPILER", true)
                    putExtra("${context.packageName}.extra.ENABLE_VU0_RECOMPILER", true)
                    putExtra("${context.packageName}.extra.ENABLE_VU1_RECOMPILER", true)
                }
                val activity = instrumentation.startActivitySync(intent)
                try {
                    await("Game must start on launch $launch") { EmulatorBridge.hasValidVm() }
                    assertEquals("Native configuration must use the requested renderer", renderer.toString(),
                        NativeApp.getSetting("EmuCore/GS", "Renderer", "int"))
                    awaitFreshFrames("Renderer $renderer must produce frames on launch $launch")
                    repeat(5) {
                        EmulatorBridge.pause()
                        Thread.sleep(150)
                        EmulatorBridge.resume()
                        Thread.sleep(500)
                        assertTrue("VM must survive pause/resume $it", EmulatorBridge.hasValidVm())
                        awaitFreshFrames("Rendering must continue after resume $it")
                    }
                    // Keep enough real rendering time to exercise on-demand TFX shaders.
                    Thread.sleep(10_000)
                    awaitFreshFrames("Rendering must remain live")
                    EmulatorBridge.shutdown()
                    assertFalse("Shutdown must release the VM", NativeApp.hasValidVm())
                } finally {
                    EmulatorBridge.shutdown()
                    instrumentation.runOnMainSync { activity.finish() }
                    await("Activity must close before the next launch") { activity.isDestroyed }
                }
                Thread.sleep(500)
            }
        } finally {
            EmulatorBridge.shutdown()
            preferences.setBiosPath(previousBios.orEmpty())
        }
    }

    private fun hasFrames(): Boolean = NativeApp.getPerformanceMetricsSnapshot()
        .orEmpty().lineSequence().firstOrNull()?.toFloatOrNull()?.let { it > 0f } == true

    private fun awaitFreshFrames(message: String) {
        // Disabling collection clears the cached native snapshot. A prior positive FPS
        // must never let a frozen renderer pass a later rendering checkpoint.
        NativeApp.setPerformanceMetricsEnabled(false, false, false)
        NativeApp.setPerformanceMetricsEnabled(true, true, false)
        await(message) { hasFrames() }
    }

    private fun await(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 45_000L
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            Thread.sleep(100)
        }
        assertTrue(message, condition())
    }
}

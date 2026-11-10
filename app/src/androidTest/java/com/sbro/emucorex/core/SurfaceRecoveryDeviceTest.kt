package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Uses the existing onboarding/SAF grants. Does not alter preferences or write save states. */
@RunWith(AndroidJUnit4::class)
class SurfaceRecoveryDeviceTest {
    @Test fun surfaceRecoveryPreservesPauseIntentAndRejectsStaleDetach() = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val game = InstrumentationRegistry.getArguments().getString("gameUri")
        assumeTrue(!game.isNullOrBlank())
        val context = ApplicationProvider.getApplicationContext<Context>()
        val activity = instrumentation.startActivitySync(Intent("${context.packageName}.action.LAUNCH_GAME").apply {
            setClassName(context.packageName, "${context.packageName}.MainActivity")
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra("${context.packageName}.extra.GAME_PATH", game)
            putExtra("${context.packageName}.extra.RENDERER", RendererDefaults.VULKAN)
            putExtra("${context.packageName}.extra.AUTOTEST_MODE", true)
        })
        try {
            await("VM must start") { EmulatorBridge.hasValidVm() }
            awaitFrames()
            var found: SurfaceView? = null
            instrumentation.runOnMainSync { found = findSurface(activity.window.decorView) }
            val surface = requireNotNull(found)

            instrumentation.runOnMainSync { surface.visibility = View.INVISIBLE }
            await("Surface must detach") { !surface.holder.surface.isValid }
            instrumentation.runOnMainSync { surface.visibility = View.VISIBLE }
            await("Surface must reattach") { surface.holder.surface.isValid }
            awaitFrames() // No explicit resume: only the surface-owned pause may be released.

            NativeApp.onNativeSurfaceDestroyed(1) // Already superseded by detach/reattach generations.
            awaitFrames()

            EmulatorBridge.pause()
            instrumentation.runOnMainSync { surface.visibility = View.INVISIBLE }
            await("Paused surface must detach") { !surface.holder.surface.isValid }
            instrumentation.runOnMainSync { surface.visibility = View.VISIBLE }
            await("Paused surface must reattach") { surface.holder.surface.isValid }
            clearMetrics()
            Thread.sleep(1_000)
            assertFalse("Surface recreation must not resume a user-paused VM", hasFrames())
            EmulatorBridge.resume()
            awaitFrames()
        } finally {
            EmulatorBridge.shutdown()
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    private fun clearMetrics() {
        NativeApp.setPerformanceMetricsEnabled(false, false, false)
        NativeApp.setPerformanceMetricsEnabled(true, true, false)
    }

    private fun hasFrames() = NativeApp.getPerformanceMetricsSnapshot().orEmpty()
        .lineSequence().firstOrNull()?.toFloatOrNull()?.let { it > 0f } == true

    private fun awaitFrames() {
        clearMetrics()
        await("VM must produce fresh frames") { hasFrames() }
    }

    private fun await(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 45_000
        while (!condition() && SystemClock.elapsedRealtime() < deadline) Thread.sleep(50)
        assertTrue(message, condition())
    }

    private fun findSurface(view: View): SurfaceView? = when (view) {
        is SurfaceView -> view
        is ViewGroup -> (0 until view.childCount).firstNotNullOfOrNull { findSurface(view.getChildAt(it)) }
        else -> null
    }
}

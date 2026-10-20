package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.os.Handler
import android.os.StrictMode
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.PixelCopy
import android.view.KeyEvent
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.sbro.emucorex.BuildConfig
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** Opt-in test: requires an existing game and save state, which it never overwrites. */
@RunWith(AndroidJUnit4::class)
class MenuResumeDeviceTest {
    @Test
    fun resumingAnUnchangedSurfaceDoesNotRecreateTheDisplay() = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val useUiMenu = args.getString("useUiMenu") == "true"
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val game = args.getString("gamePath")
        val statePath = args.getString("stateFile")
        assumeTrue(!game.isNullOrBlank() && !statePath.isNullOrBlank())
        assumeTrue("Native diagnostic assertions require a debug build", BuildConfig.DEBUG)
        val state = File(requireNotNull(statePath))
        val originalHash = hash(state)
        val context = ApplicationProvider.getApplicationContext<Context>()
        val token = "menu-resume-${SystemClock.elapsedRealtime()}"
        var mainPolicy: StrictMode.ThreadPolicy? = null
        val metadataDiskAccesses = AtomicInteger()
        try {
            context.startActivity(Intent("${context.packageName}.action.LAUNCH_GAME").apply {
                setClassName(context.packageName, "${context.packageName}.MainActivity")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra("${context.packageName}.extra.GAME_PATH", game)
            })
            val deadline = SystemClock.elapsedRealtime() + 45_000
            while (SystemClock.elapsedRealtime() < deadline) {
                if (NativeApp.hasValidVm() &&
                    NativeApp.getSaveStatePathForFile(requireNotNull(game), 1)?.let { File(it).canonicalPath } == state.canonicalPath
                ) break
                Thread.sleep(100)
            }
            assertTrue("The selected game must start", NativeApp.hasValidVm())
            assertEquals("The active game must resolve the supplied state", state.canonicalPath,
                NativeApp.getSaveStatePathForFile(requireNotNull(game), 1)?.let { File(it).canonicalPath })
            assertTrue("The existing gameplay state must load", NativeApp.loadStateFromSlot(1))
            NativeApp.setPerformanceMetricsEnabled(true, true, false)
            Thread.sleep(3_000)
            if (useUiMenu && args.getString("verifyMetadataOffMain") == "true") {
                instrumentation.runOnMainSync {
                    mainPolicy = StrictMode.getThreadPolicy()
                    StrictMode.setThreadPolicy(StrictMode.ThreadPolicy.Builder(mainPolicy)
                        .detectDiskReads().detectDiskWrites()
                        .penaltyListener({ it.run() }) { violation ->
                            if (violation.stackTrace.any {
                                    it.className.contains("EmulationViewModel") &&
                                        (it.methodName == "refreshSaveStateMetadata" ||
                                            it.className.contains("\$refreshSaveStateMetadata"))
                                }) metadataDiskAccesses.incrementAndGet()
                        }.build())
                }
            }
            Log.i("EmuCoreXMenuTest", "BEGIN $token")
            repeat(8) {
                if (useUiMenu) instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
                else EmulatorBridge.pause()
                // The real UI deliberately debounces menu actions for 550 ms.
                Thread.sleep(if (useUiMenu) 650 else if (it % 2 == 0) 50 else 500)
                if (useUiMenu) instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
                else EmulatorBridge.resume()
                Thread.sleep(if (useUiMenu) 650 else 500)
            }
            Log.i("EmuCoreXMenuTest", "END $token")
            mainPolicy?.let { policy -> instrumentation.runOnMainSync { StrictMode.setThreadPolicy(policy) } }
            mainPolicy = null
            assertEquals("Save-state metadata must not access disk on the UI thread", 0, metadataDiskAccesses.get())
            val log = instrumentation.uiAutomation.executeShellCommand(
                "logcat -d -v brief -s EmuCoreX:D EmuCoreXMenuTest:I"
            ).use { descriptor ->
                android.os.ParcelFileDescriptor.AutoCloseInputStream(descriptor).bufferedReader().use { it.readText() }
            }
            assertTrue("Diagnostic capture must contain this run", log.contains("BEGIN $token") && log.contains("END $token"))
            val transitions = log.substringAfter("BEGIN $token").substringBefore("END $token")
            assertEquals("Every menu cycle must pause the VM", 8,
                Regex("VM pause requested").findAll(transitions).count())
            assertEquals("Every menu cycle must resume the VM", 8,
                Regex("VM resume requested").findAll(transitions).count())
            assertEquals("Resuming the same Surface must not schedule a display recreation", 0,
                Regex("SetNativeSurface: scheduling MTGS::UpdateDisplayWindow").findAll(transitions).count())
            assertTrue("Rendering must continue after repeated resumes",
                NativeApp.getPerformanceMetricsSnapshot().orEmpty().lineSequence().firstOrNull()?.toFloatOrNull()?.let { it > 0 } == true)

            var gameSurface: SurfaceView? = null
            instrumentation.runOnMainSync {
                gameSurface = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                    .firstNotNullOfOrNull { findSurface(it.window.decorView) }
            }
            val surface = requireNotNull(gameSurface) { "The game SurfaceView must be attached" }
            assertRenderedSurface(surface)
            // A duplicate resume can be skipped, but a genuine detach/attach must recover.
            instrumentation.runOnMainSync { surface.visibility = View.INVISIBLE }
            Thread.sleep(500)
            assertTrue("Hiding the SurfaceView must destroy its surface", !surface.holder.surface.isValid)
            instrumentation.runOnMainSync { surface.visibility = View.VISIBLE }
            Thread.sleep(500)
            EmulatorBridge.resume()
            Thread.sleep(1_000)
            assertRenderedSurface(surface)
            // A size change on the same native window must also reach GS.
            instrumentation.runOnMainSync { surface.holder.setFixedSize(1280, 720) }
            Thread.sleep(1_000)
            assertRenderedSurface(surface)
            instrumentation.runOnMainSync { surface.holder.setSizeFromLayout() }
            Thread.sleep(1_000)
            assertRenderedSurface(surface)
        } finally {
            mainPolicy?.let { policy -> instrumentation.runOnMainSync { StrictMode.setThreadPolicy(policy) } }
            NativeApp.shutdown()
            assertEquals("The original save state must remain unchanged", originalHash, hash(state))
        }
    }

    private fun findSurface(view: View): SurfaceView? = when (view) {
        is SurfaceView -> view
        is ViewGroup -> (0 until view.childCount).firstNotNullOfOrNull { findSurface(view.getChildAt(it)) }
        else -> null
    }

    private fun assertRenderedSurface(surface: SurfaceView) {
        val bitmap = Bitmap.createBitmap(surface.width, surface.height, Bitmap.Config.ARGB_8888)
        val copied = CountDownLatch(1)
        var result = -1
        try {
            PixelCopy.request(surface, bitmap, { result = it; copied.countDown() }, Handler(Looper.getMainLooper()))
            assertTrue("Surface copy must finish", copied.await(5, TimeUnit.SECONDS))
            assertEquals("GS must present to the current Surface", PixelCopy.SUCCESS, result)
            val pixels = IntArray(bitmap.width * bitmap.height)
            bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
            assertTrue("The game surface must contain rendered color, not only black",
                pixels.count { (it and 0x00ffffff) != 0 } > pixels.size / 20)
        } finally {
            bitmap.recycle()
        }
    }

    private fun hash(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }
}

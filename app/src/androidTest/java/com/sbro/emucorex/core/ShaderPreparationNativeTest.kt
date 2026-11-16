package com.sbro.emucorex.core

import android.content.Context
import android.os.Build
import android.os.SystemClock
import com.sbro.emucorex.BuildConfig
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.Assume.assumeTrue
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class ShaderPreparationNativeTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun nativeJobCancelsResumesAndCompletesUsingPackagedShaderSource() {
        assumeTrue("Requires an explicitly enabled 4 KiB diagnostic core",
            BuildConfig.TFX_PREPARATION_DIAGNOSTICS && AndroidNativeCoreSelector.runtimePageSizeBytes() == 4096L)
        assertTrue("The native emulator core must load for this device integration test", NativeApp.hasNativeCore)
        assertFalse("The application must be quiescent before the headless test", NativeApp.hasValidVm())

        val originalDataRoot = NativeApp.dataRoot(context).canonicalFile
        val nativeLibraryDir = context.applicationInfo.nativeLibraryDir.orEmpty()
        assertTrue("Native library directory must be configured", nativeLibraryDir.isNotBlank())

        val oldRenderer = NativeApp.getSetting("EmuCore/GS", "Renderer", "int").orEmpty()
        val oldShaderCacheDisabled = NativeApp.getSetting("EmuCore/GS", "DisableShaderCache", "bool").orEmpty()
        val isolatedRoot = File(context.cacheDir, "shader-preparation-test-${System.nanoTime()}").canonicalFile
        val previousStatus = requireNotNull(readStatus())
        assertFalse("No shader preparation may already be active", previousStatus.isActive())

        var initializedForTest = false
        try {
            listOf("cache", "cache/achievement_images", "inis", "sstates", "memcards", "resources/shaders/vulkan").forEach { relative ->
                assertTrue("Could not create isolated data directory $relative", File(isolatedRoot, relative).mkdirs() || File(isolatedRoot, relative).isDirectory)
            }
            val shaderFile = File(isolatedRoot, "resources/shaders/vulkan/tfx.glsl")
            context.assets.open("resources/shaders/vulkan/tfx.glsl").use { source ->
                shaderFile.outputStream().use { output -> source.copyTo(output) }
            }
            assertTrue("Canonical packaged TFX source must be copied into the isolated root", shaderFile.isFile && shaderFile.length() > 0)

            NativeApp.setNativeLibraryDir(nativeLibraryDir)
            initializedForTest = true
            NativeApp.initialize(isolatedRoot.absolutePath, Build.VERSION.SDK_INT)
            NativeApp.setSetting("EmuCore/GS", "Renderer", "int", "14") // GSRendererType::VK
            NativeApp.setSetting("EmuCore/GS", "DisableShaderCache", "bool", "false")
            assertFalse("Zero is not a valid preparation job ID", NativeApp.cancelShaderPreparation(0L))
            assertFalse("Negative IDs cannot be converted to native job IDs", NativeApp.cancelShaderPreparation(-1L))
            assertFalse("Zero is not a valid preparation job ID", NativeApp.resumeShaderPreparation(0L))
            assertFalse("Negative IDs cannot be converted to native job IDs", NativeApp.resumeShaderPreparation(-1L))

            assertFalse("A fresh isolated cache has no prepared receipt", NativeApp.checkShaderPreparationReadiness())

            assertTrue("Native runtime should admit an asynchronous preparation job", NativeApp.startShaderPreparation())
            val first = awaitStatus(START_TIMEOUT_MS) { status -> status.jobId != previousStatus.jobId }
            assertTrue("Accepted job must have a positive bounded ID", first.jobId > 0L)
            assertEquals("A new job starts at attempt one", 1, first.attempt)

            // Cancellation can lose to a very fast completion. Only demand a resume
            // when native accepted cancellation for this attempt.
            val cancellationAccepted = NativeApp.cancelShaderPreparation(first.jobId)
            var terminal = awaitTerminal(first.jobId, first.attempt, JOB_TIMEOUT_MS)
            if (cancellationAccepted) {
                assertEquals("An accepted cancellation must finish cancelled", ShaderPreparationStatus.State.CANCELLED, terminal.state)
                assertTrue("Terminal cancellation retains the accepted request", terminal.cancelRequested)
                assertFalse("Accepted cancellation must not persist a prepared receipt", NativeApp.checkShaderPreparationReadiness())
                assertTrue("The cancelled job should resume under the same identity", NativeApp.resumeShaderPreparation(first.jobId))
                terminal = awaitTerminal(first.jobId, first.attempt + 1, JOB_TIMEOUT_MS)
                assertEquals("Resume increments the attempt without changing job identity", first.jobId, terminal.jobId)
                assertEquals(2, terminal.attempt)
            }

            assertEquals("Real Vulkan warmup must complete", ShaderPreparationStatus.State.COMPLETED, terminal.state)
            assertTrue("The current baseline plan has at least 164 selectors", terminal.total >= 164)
            assertEquals("Every dynamic selector should settle as available", terminal.total, terminal.settled)
            assertEquals(0, terminal.failed)
            assertEquals(0, terminal.cancelled)
            assertEquals(0, terminal.inFlight)
            assertTrue("Completed status must be finished", terminal.finished)
            assertTrue("Completion requires a durable Vulkan cache checkpoint", terminal.checkpointSaved)
            assertTrue("The Vulkan device should report its selected adapter", terminal.adapterName.isNotBlank())
            assertFalse("Headless preparation must not start a CPU VM", NativeApp.hasValidVm())
            assertNull("No VM draw rectangle is exposed without a running VM", NativeApp.getDisplayDrawRect())
            assertTrue("A completed checkpoint validates from persistent cache metadata", NativeApp.checkShaderPreparationReadiness())
            NativeApp.shutdown()
            assertTrue("A fresh device probe still validates after the preparation worker is joined", NativeApp.checkShaderPreparationReadiness())
            assertFalse("Readiness validation must not start a VM", NativeApp.hasValidVm())

            val originalShader = shaderFile.readBytes()
            shaderFile.appendText("\n// receipt identity invalidation test\n")
            assertFalse("Changed shader source invalidates the receipt", NativeApp.checkShaderPreparationReadiness())
            shaderFile.writeBytes(originalShader)
            assertTrue("Restoring matching source keeps a valid existing checkpoint", NativeApp.checkShaderPreparationReadiness())

            val receipt = File(isolatedRoot, "cache").walkTopDown().firstOrNull { it.isFile && it.name.endsWith(".tfxprep") }
            requireNotNull(receipt) { "The completed job must persist a bounded cache receipt" }
            val originalReceipt = receipt.readBytes()
            receipt.writeText("invalid receipt")
            assertFalse("Corrupted receipt must not suppress preparation", NativeApp.checkShaderPreparationReadiness())
            receipt.writeBytes(originalReceipt)
            assertTrue("Matching receipt and driver-accepted cache remain ready", NativeApp.checkShaderPreparationReadiness())
            val pipelineCache = File(receipt.absolutePath.removeSuffix(".tfxprep"))
            assertTrue("Receipt must reference a real pipeline checkpoint", pipelineCache.isFile)
            assertTrue("Only the isolated pipeline cache is deleted", pipelineCache.delete())
            assertFalse("Deleted pipeline cache invalidates readiness despite a completed old job", NativeApp.checkShaderPreparationReadiness())
            assertFalse("Cache recreation removes the stale receipt", receipt.exists())

        } finally {
            try {
                if (initializedForTest)
                    NativeApp.shutdown()
            } finally {
                try {
                    if (initializedForTest) {
                        NativeApp.initialize(originalDataRoot.absolutePath, Build.VERSION.SDK_INT)
                        NativeApp.setNativeLibraryDir(nativeLibraryDir)
                        // Settings are held in the runtime snapshot and are not persisted by
                        // setSetting. Empty values restore the native default when no prior
                        // override existed.
                        NativeApp.setSetting("EmuCore/GS", "Renderer", "int", oldRenderer)
                        NativeApp.setSetting("EmuCore/GS", "DisableShaderCache", "bool", oldShaderCacheDisabled)
                    }
                } finally {
                    isolatedRoot.deleteRecursively()
                }
            }
        }
    }

    private fun readStatus(): ShaderPreparationStatus? =
        ShaderPreparationStatus.parseSnapshot(NativeApp.getShaderPreparationStatusSnapshot())

    private fun awaitStatus(timeoutMs: Long, predicate: (ShaderPreparationStatus) -> Boolean): ShaderPreparationStatus {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        var latest: ShaderPreparationStatus? = null
        while (SystemClock.elapsedRealtime() < deadline) {
            latest = requireNotNull(readStatus()) { "Native status snapshot was null or malformed" }
            if (predicate(latest))
                return latest
            Thread.sleep(POLL_INTERVAL_MS)
        }
        throw AssertionError("Timed out waiting for shader preparation status; last snapshot=$latest")
    }

    private fun awaitTerminal(jobId: Long, attempt: Int, timeoutMs: Long): ShaderPreparationStatus =
        awaitStatus(timeoutMs) { status ->
            assertEquals("Job identity must remain stable", jobId, status.jobId)
            assertEquals("Attempt identity must remain stable", attempt, status.attempt)
            status.state in TERMINAL_STATES
        }

    private fun ShaderPreparationStatus.isActive(): Boolean =
        state in setOf(
            ShaderPreparationStatus.State.WAITING_FOR_LIFECYCLE,
            ShaderPreparationStatus.State.STARTING,
            ShaderPreparationStatus.State.RUNNING,
            ShaderPreparationStatus.State.CANCELLING
        )

    private companion object {
        const val POLL_INTERVAL_MS = 20L
        const val START_TIMEOUT_MS = 5_000L
        const val JOB_TIMEOUT_MS = 90_000L
        val TERMINAL_STATES = setOf(
            ShaderPreparationStatus.State.COMPLETED,
            ShaderPreparationStatus.State.CANCELLED,
            ShaderPreparationStatus.State.FAILED,
            ShaderPreparationStatus.State.UNSUPPORTED
        )
    }
}

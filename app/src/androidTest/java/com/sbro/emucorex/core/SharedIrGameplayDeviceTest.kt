package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.os.SystemClock
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.security.MessageDigest

@RunWith(AndroidJUnit4::class)
class SharedIrGameplayDeviceTest {
    @Test
    fun compareSavedGameplay() {
        val arguments = InstrumentationRegistry.getArguments()
        val gamePath = arguments.getString("gamePath")
        val stateFilePath = arguments.getString("stateFile")
        val label = arguments.getString("label")
        assumeTrue("gamePath, stateFile, and label opt this device test in", !gamePath.isNullOrBlank() && !stateFilePath.isNullOrBlank() && !label.isNullOrBlank())
        val requestedGamePath = requireNotNull(gamePath)
        val requestedStateFilePath = requireNotNull(stateFilePath)
        val requestedLabel = requireNotNull(label)

        require(requestedLabel.matches(Regex("[a-zA-Z0-9_-]+"))) { "label may contain only letters, numbers, underscores, and hyphens" }
        val slot = (arguments.getString("slot") ?: arguments.getString("slot1"))?.toIntOrNull() ?: 1
        require(slot in 1..10) { "slot must be between 1 and 10" }
        val captureSlot = arguments.getString("captureSlot")?.takeIf { it.isNotBlank() }?.let {
            requireNotNull(it.toIntOrNull()) { "captureSlot must be an integer" }
        }
        require(captureSlot == null || (captureSlot in 1..10 && captureSlot != slot)) {
            "captureSlot must be between 1 and 10 and differ from the loaded slot"
        }
        val sampleCount = arguments.getString("sampleCount")?.toIntOrNull() ?: 40
        val sampleIntervalMs = arguments.getString("sampleIntervalMs")?.toLongOrNull() ?: 500L
        val unlimited = arguments.getString("unlimited")?.toBooleanStrictOrNull() ?: false
        val inputScript = arguments.getString("inputScript") ?: "none"
        require(inputScript in setOf("none", "walk-camera", "combat", "drive")) { "Unknown inputScript: $inputScript" }
        require(sampleCount > 0) { "sampleCount must be positive" }
        require(sampleIntervalMs >= 0L) { "sampleIntervalMs must not be negative" }
        require(inputScript == "none" || sampleIntervalMs in 100L..500L) {
            "Active input scripts require sampling every 100 to 500ms"
        }

        val setupButtons = JSONArray(arguments.getString("setupButtons") ?: "[]")
        require(setupButtons.length() <= 32) { "At most 32 setup buttons are allowed" }
        for (index in 0 until setupButtons.length()) {
            val button = setupButtons.getJSONObject(index)
            require(button.getInt("key") in setOf(19, 20, 21, 22, 96, 97, 99, 100, 102, 103, 104, 105, 106, 107, 108, 109)) {
                "Setup buttons must use a supported digital pad key"
            }
            require(button.optLong("holdMs", 200L) in 1L..5_000L) { "Setup holdMs must be between 1 and 5000" }
            require(button.optLong("waitMs", 1_000L) in 0L..60_000L) { "Setup waitMs must be between 0 and 60000" }
        }

        val stateFile = File(requestedStateFilePath)
        assertTrue("State file must exist: $stateFilePath", stateFile.isFile)
        val canonicalRequestedStatePath = stateFile.canonicalPath
        val stateHashBefore = sha256(stateFile)
        val context = ApplicationProvider.getApplicationContext<Context>()
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val packageName = context.packageName
        val outputDirectory = File(
            requireNotNull(context.getExternalFilesDir(null)) { "External app storage is unavailable" },
            "logs"
        ).apply { mkdirs() }
        val samples = JSONArray()
        val inputEvents = JSONArray()
        val setupInputEvents = JSONArray()
        var activeInputs = emptyMap<Int, Int>()
        val screenshotPaths = JSONObject()
        val launchedIntent = Intent("$packageName.action.LAUNCH_GAME").apply {
            setClassName(packageName, "$packageName.MainActivity")
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra("$packageName.extra.GAME_PATH", requestedGamePath)
        }

        var launched = false
        var frameLimitChanged = false
        var stateHashAfter: String? = null
        var nativeResolvedStatePath: String? = null
        var capturedStatePath: String? = null
        var capturedStateSha256: String? = null
        var stateLoadSucceeded = false
        var positiveFpsSamples = 0
        var nonblankMetricSamples = 0
        try {
            context.startActivity(launchedIntent)
            launched = true

            val vmTimeout = SystemClock.elapsedRealtime() + VM_START_TIMEOUT_MS
            while (SystemClock.elapsedRealtime() < vmTimeout) {
                if (NativeApp.hasValidVm()) {
                    val resolvedPath = NativeApp.getSaveStatePathForFile(requestedGamePath, slot)
                    if (!resolvedPath.isNullOrBlank() && File(resolvedPath).canonicalPath == canonicalRequestedStatePath) {
                        nativeResolvedStatePath = resolvedPath
                        break
                    }
                }
                Thread.sleep(100)
            }
            assertTrue(
                "Active game state path must resolve to the supplied state file within 45 seconds; resolved=$nativeResolvedStatePath requested=$canonicalRequestedStatePath",
                nativeResolvedStatePath != null
            )
            stateLoadSucceeded = NativeApp.loadStateFromSlot(slot)
            assertTrue("Saved gameplay state must load from slot $slot", stateLoadSucceeded)
            NativeApp.setPerformanceMetricsEnabled(true, true, false)
            Thread.sleep(WARMUP_MS)
            val setupStartMs = SystemClock.elapsedRealtime()
            for (index in 0 until setupButtons.length()) {
                val button = setupButtons.getJSONObject(index)
                val key = button.getInt("key")
                NativeApp.setPadButton(0, key, 255, true)
                val pressedMs = SystemClock.elapsedRealtime() - setupStartMs
                try {
                    Thread.sleep(button.optLong("holdMs", 200L))
                } finally {
                    NativeApp.setPadButton(0, key, 0, false)
                    setupInputEvents.put(JSONObject().apply {
                        put("key", key)
                        put("pressedMs", pressedMs)
                        put("releasedMs", SystemClock.elapsedRealtime() - setupStartMs)
                    })
                }
                Thread.sleep(button.optLong("waitMs", 1_000L))
            }
            if (setupButtons.length() > 0) Thread.sleep(WARMUP_MS)
            NativeApp.setPerformanceMetricsEnabled(true, true, false)

            val metricsReadyTimeout = SystemClock.elapsedRealtime() + METRICS_READY_TIMEOUT_MS
            while (SystemClock.elapsedRealtime() < metricsReadyTimeout) {
                val currentSnapshot = NativeApp.getPerformanceMetricsSnapshot().orEmpty()
                val currentFps = currentSnapshot.lineSequence().firstOrNull()?.trim()?.toFloatOrNull()
                if (currentSnapshot.isNotBlank() && currentFps != null && currentFps > 0f) break
                Thread.sleep(METRICS_POLL_INTERVAL_MS)
            }
            val firstSnapshot = NativeApp.getPerformanceMetricsSnapshot().orEmpty()
            val firstFps = firstSnapshot.lineSequence().firstOrNull()?.trim()?.toFloatOrNull()
            assertTrue(
                "Live metrics must refresh with positive FPS within ${METRICS_READY_TIMEOUT_MS}ms after enabling",
                firstSnapshot.isNotBlank() && firstFps != null && firstFps > 0f
            )

            if (captureSlot != null) {
                val path = requireNotNull(NativeApp.getSaveStatePathForFile(requestedGamePath, captureSlot))
                val captureFile = File(path)
                check(!captureFile.exists()) { "Refusing to overwrite an existing checkpoint: $path" }
                check(NativeApp.saveStateToSlot(captureSlot)) { "Unable to capture checkpoint in slot $captureSlot" }
                NativeApp.waitForSaveStateFlush()
                check(captureFile.isFile && captureFile.length() > 0L) { "Checkpoint was not written: $path" }
                capturedStatePath = path
                capturedStateSha256 = sha256(captureFile)
            }

            if (unlimited) {
                frameLimitChanged = true
                NativeApp.setFrameLimitEnabled(false)
            }
            screenshotPaths.put(
                "before",
                captureScreenshot(instrumentation, File(outputDirectory, "shared-ir-$requestedLabel-before.png"))
            )

            val inputStartMs = SystemClock.elapsedRealtime()
            repeat(sampleCount) { index ->
                val elapsedMs = SystemClock.elapsedRealtime() - inputStartMs
                if (inputScript != "none") {
                    val desiredInputs = scriptedInputs(inputScript, elapsedMs)
                    (activeInputs.keys + desiredInputs.keys).forEach { key ->
                        val previous = activeInputs[key] ?: 0
                        val desired = desiredInputs[key] ?: 0
                        if (previous != desired) {
                            NativeApp.setPadButton(0, key, desired, desired > 0)
                            inputEvents.put(JSONObject().apply {
                                put("elapsedMs", elapsedMs)
                                put("key", key)
                                put("range", desired)
                            })
                        }
                    }
                    activeInputs = desiredInputs
                }
                val snapshot = NativeApp.getPerformanceMetricsSnapshot().orEmpty()
                val fps = snapshot.lineSequence().firstOrNull()?.trim()?.toFloatOrNull()
                if (snapshot.isNotBlank()) nonblankMetricSamples++
                if (fps != null && fps > 0f) positiveFpsSamples++

                val clocks = JSONObject()
                listOf(0, 4, 7).forEach { policy ->
                    clocks.put("policy$policy", runCatching {
                        File("/sys/devices/system/cpu/cpufreq/policy$policy/scaling_cur_freq").readText().trim()
                    }.getOrNull() ?: JSONObject.NULL)
                }
                samples.put(JSONObject().apply {
                    put("sample", index)
                    put("elapsedRealtimeMs", SystemClock.elapsedRealtime())
                    put("inputElapsedMs", elapsedMs)
                    put("snapshot", snapshot)
                    put("fps", fps ?: JSONObject.NULL)
                    put("clocks", clocks)
                })
                if (index + 1 < sampleCount && sampleIntervalMs > 0L) Thread.sleep(sampleIntervalMs)
            }

            screenshotPaths.put(
                "after",
                captureScreenshot(instrumentation, File(outputDirectory, "shared-ir-$requestedLabel-after.png"))
            )
        } finally {
            try {
                if (launched) {
                    try {
                        try {
                            NativeApp.resetPadState(0)
                        } finally {
                            if (frameLimitChanged) NativeApp.setFrameLimitEnabled(true)
                        }
                    } finally {
                        NativeApp.shutdown()
                    }
                }
            } finally {
                stateHashAfter = sha256(stateFile)
                val report = JSONObject().apply {
                    put("label", requestedLabel)
                    put("gamePath", requestedGamePath)
                    put("stateFile", requestedStateFilePath)
                    put("nativeResolvedStatePath", nativeResolvedStatePath ?: JSONObject.NULL)
                    put("inferredSerial", nativeResolvedStatePath?.let { File(it).name.substringBefore(" (") } ?: JSONObject.NULL)
                    put("stateLoadSucceeded", stateLoadSucceeded)
                    put("stateSha256Before", stateHashBefore)
                    put("stateSha256After", stateHashAfter)
                    put("stateUnchanged", stateHashBefore == stateHashAfter)
                    put("slot", slot)
                    put("capturedStatePath", capturedStatePath ?: JSONObject.NULL)
                    put("capturedStateSha256", capturedStateSha256 ?: JSONObject.NULL)
                    put("sampleCountRequested", sampleCount)
                    put("sampleIntervalMs", sampleIntervalMs)
                    put("unlimited", unlimited)
                    put("inputScript", inputScript)
                    put("setupButtons", setupButtons)
                    put("setupInputEvents", setupInputEvents)
                    put("inputEvents", inputEvents)
                    put("positiveFpsSamples", positiveFpsSamples)
                    put("nonblankMetricSamples", nonblankMetricSamples)
                    put("screenshots", screenshotPaths)
                    put("samples", samples)
                }
                File(outputDirectory, "shared-ir-$requestedLabel.json").writeText(report.toString(2), Charsets.UTF_8)
            }
        }

        assertEquals("The saved gameplay state file must remain unchanged", stateHashBefore, stateHashAfter)
        assertEquals("Every requested sample must be recorded", sampleCount, samples.length())
        assertEquals("Every requested sample must contain live performance metrics", sampleCount, nonblankMetricSamples)
        assertEquals("Every requested sample must contain positive FPS", sampleCount, positiveFpsSamples)
    }

    private fun scriptedInputs(script: String, elapsedMs: Long): Map<Int, Int> {
        if (script == "drive") {
            return buildMap {
                put(96, 255) // Cross: accelerate with the game's default controller layout.
                put(if ((elapsedMs / 4_000L) % 2 == 0L) 111 else 113, 48)
            }
        }
        val phase = ((elapsedMs / 4_000L) % 4).toInt()
        val walking = listOf(110, 111, 112, 113)[phase] // Left stick: up, right, down, left.
        val camera = if (phase % 2 == 0) 121 else 123 // Right stick: right/left.
        return buildMap {
            put(walking, 180)
            put(camera, 96)
            if (script == "combat" && (elapsedMs / 500L) % 2 == 0L) put(99, 255) // Square.
        }
    }

    private fun captureScreenshot(instrumentation: android.app.Instrumentation, file: File): String =
        run {
            val bitmap: Bitmap = requireNotNull(instrumentation.uiAutomation.takeScreenshot()) {
                "Unable to capture required gameplay screenshot: ${file.absolutePath}"
            }
            try {
                FileOutputStream(file).use { output ->
                    check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                        "Unable to write required gameplay screenshot: ${file.absolutePath}"
                    }
                }
            } finally {
                bitmap.recycle()
            }
            check(file.isFile && file.length() > 0L) {
                "Required gameplay screenshot was not written: ${file.absolutePath}"
            }
            file.absolutePath
        }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        FileInputStream(file).use { input ->
            val buffer = ByteArray(16 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { byte -> "%02x".format(byte) }
    }

    private companion object {
        const val VM_START_TIMEOUT_MS = 45_000L
        const val WARMUP_MS = 8_000L
        const val METRICS_READY_TIMEOUT_MS = 5_000L
        const val METRICS_POLL_INTERVAL_MS = 100L
    }
}

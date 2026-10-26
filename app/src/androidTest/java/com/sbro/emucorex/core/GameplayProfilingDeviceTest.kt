package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.os.SystemClock
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Opt-in interactive gameplay capture. Commands and artifacts stay in the target app's files. */
@RunWith(AndroidJUnit4::class)
class GameplayProfilingDeviceTest {
    private external fun readMetricsSample(): String

    @Test
    fun captureGameplayWindow() = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        val game = args.getString("gamePath")
        val bios = args.getString("biosPath")
        assumeTrue("Supply gamePath and biosPath", !game.isNullOrBlank() && !bios.isNullOrBlank())
        val context = ApplicationProvider.getApplicationContext<Context>()
        require(File(requireNotNull(game)).isFile && File(requireNotNull(bios)).isDirectory)
        val renderer = args.getString("renderer")?.toInt() ?: 14
        require(renderer == 12 || renderer == 14)
        val unlimited = args.getString("unlimited")?.toBooleanStrict() ?: true
        val gpuTiming = args.getString("gpuTiming")?.toBooleanStrict() ?: false
        val run = args.getString("runId") ?: "gameplay-${System.currentTimeMillis()}"
        require(run.matches(Regex("[A-Za-z0-9_-]{1,80}")))
        val output = File(context.filesDir, "gameplay-profiles/$run")
        require(!output.exists()) { "Use a fresh runId" }
        check(output.mkdirs())
        val timeout = (args.getString("runSeconds")?.toLong() ?: 900).also { require(it in 30..3600) } * 1000
        val preferences = AppPreferences(context)
        val oldBios = preferences.biosPath.first()
        val oldGames = preferences.gamePaths.first()
        val oldOnboarding = preferences.onboardingCompleted.first()
        val held = mutableMapOf<Int, Long>()
        val allowedButtons = setOf(19, 20, 21, 22, 96, 97, 99, 100, 102, 103, 104, 105, 106, 107, 108, 109,
            110, 111, 112, 113, 120, 121, 122, 123)
        var activity: android.app.Activity? = null
        var previousFrameLimit: Boolean? = null
        var phase = "navigation"
        var lastCommand = -1
        var gameplaySamples = 0
        var lastSample = 0L
        var lastSequence = -1L
        var settleUntil = 0L
        var stop = false
        fun screenshot(name: String) {
            val image = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            try {
                File(output, "$name.png").outputStream().use { check(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            } finally { image.recycle() }
        }
        fun status(state: String) {
            val temp = File(output, "status.tmp")
            temp.writeText(JSONObject().put("state", state).put("phase", phase).put("lastCommand", lastCommand)
                .put("gameplaySamples", gameplaySamples).put("elapsedMs", SystemClock.elapsedRealtime()).toString())
            check(temp.renameTo(File(output, "status.json")))
        }
        try {
            preferences.setBiosPath(bios)
            preferences.setGamePaths(listOf(File(game).parent!!))
            preferences.setOnboardingCompleted(true)
            val intent = Intent("${context.packageName}.action.LAUNCH_GAME").apply {
                setClassName(context.packageName, "${context.packageName}.MainActivity")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra("${context.packageName}.extra.GAME_PATH", game)
                putExtra("${context.packageName}.extra.RENDERER", renderer)
                for (key in listOf("EE", "IOP", "VU0", "VU1"))
                    putExtra("${context.packageName}.extra.ENABLE_${key}_RECOMPILER", true)
            }
            status("starting")
            activity = instrumentation.startActivitySync(intent)
            val launchDeadline = SystemClock.elapsedRealtime() + 60_000
            while (!EmulatorBridge.hasValidVm() && SystemClock.elapsedRealtime() < launchDeadline) Thread.sleep(100)
            check(EmulatorBridge.hasValidVm()) { "Game failed to start" }
            check(NativeApp.getSetting("EmuCore/GS", "Renderer", "int") == renderer.toString())
            previousFrameLimit = NativeApp.getSetting("EmuCore/GS", "FrameLimitEnable", "bool")?.toBooleanStrictOrNull()
            NativeApp.setFrameLimitEnabled(!unlimited)
            NativeApp.setPerformanceMetricsEnabled(true, true, gpuTiming)
            File(output, "metadata.json").writeText(JSONObject().put("game", game).put("bios", bios)
                .put("unlimited", unlimited).put("gpuTimingRequested", gpuTiming)
                .put("renderer", renderer).put("core", NativeApp.getCoreVersion()).put("model", android.os.Build.MODEL)
                .put("fingerprint", android.os.Build.FINGERPRINT).put("settings",
                    JSONObject().apply {
                        for (key in listOf("Renderer", "upscale_multiplier", "accurate_blending_unit", "HWDownloadMode"))
                            put(key, NativeApp.getSetting("EmuCore/GS", key, if (key == "upscale_multiplier") "float" else "int"))
                    }).toString(2))
            val deadline = SystemClock.elapsedRealtime() + timeout
            while (!stop && SystemClock.elapsedRealtime() < deadline) {
                var now = SystemClock.elapsedRealtime()
                check(EmulatorBridge.hasValidVm()) { "VM stopped during capture" }
                held.filterValues { it <= now }.keys.toList().forEach {
                    NativeApp.setPadButton(0, it, 0, false); held.remove(it)
                }
                val commandFile = File(output, "command.json")
                if (commandFile.isFile) {
                    val command = JSONObject(commandFile.readText())
                    val id = command.getInt("id")
                    if (id > lastCommand) {
                        val action = command.getString("action")
                        if (action in listOf("screenshot", "dump", "save", "load", "phase")) {
                            phase = "navigation"
                            settleUntil = now + 2_000
                        }
                        when (action) {
                            "press" -> {
                                val key = command.getInt("button")
                                val duration = command.optLong("durationMs", 150)
                                val pressure = command.optInt("pressure", 255)
                                require(key in allowedButtons && duration in 1..60_000 && pressure in 1..255)
                                NativeApp.setPadButton(0, key, pressure, true)
                                held[key] = now + duration
                            }
                            "release" -> held.keys.toList().forEach { NativeApp.setPadButton(0, it, 0, false); held.remove(it) }
                            "screenshot" -> screenshot("screen-$id")
                            "phase" -> {
                                val requested = command.getString("value").also { require(it in listOf("navigation", "gameplay")) }
                                // Operator must inspect the screenshot before selecting gameplay.
                                screenshot("phase-$id-$requested")
                                phase = requested
                            }
                            "dump" -> {
                                val frames = command.optInt("frames", 3).also { require(it in 1..120) }
                                NativeApp.queueGsDump(frames)
                            }
                            "save" -> {
                                val slot = command.getInt("slot").also { require(it in 0..9) }
                                val path = checkNotNull(NativeApp.getSaveStatePathForFile(game, slot))
                                check(!File(path).exists()) { "Refusing to overwrite existing save state" }
                                check(NativeApp.saveStateToSlot(slot))
                                NativeApp.waitForSaveStateFlush()
                            }
                            "load" -> {
                                held.keys.toList().forEach { NativeApp.setPadButton(0, it, 0, false); held.remove(it) }
                                check(NativeApp.loadStateFromSlot(command.getInt("slot").also { require(it in 0..9) }))
                                NativeApp.setFrameLimitEnabled(!unlimited)
                            }
                            "stop" -> stop = true
                            else -> error("Unknown gameplay command")
                        }
                        File(output, "commands.jsonl").appendText(JSONObject(command.toString())
                            .put("elapsedMs", now).put("phase", phase).toString() + "\n")
                        lastCommand = id
                        now = SystemClock.elapsedRealtime()
                        if (action in listOf("screenshot", "dump", "save", "load", "phase"))
                            settleUntil = now + 2_000
                    }
                }
                if (now - lastSample >= 500) {
                    val sample = readMetricsSample()
                    val sequence = sample.substringBefore('\n').toLong()
                    val snapshot = sample.substringAfter('\n')
                    val fps = snapshot.lineSequence().firstOrNull()?.toDoubleOrNull()
                    val fresh = sequence > lastSequence && snapshot.isNotEmpty()
                    val measuredPhase = if (now < settleUntil) "settling" else phase
                    if (fresh && measuredPhase == "gameplay" && unlimited)
                        check(snapshot.contains("Target: Max")) { "Uncapped gameplay requested but native limiter is active" }
                    if (fresh && measuredPhase == "gameplay" && fps != null && fps > 0) gameplaySamples++
                    File(output, "metrics.jsonl").appendText(JSONObject().put("elapsedMs", now).put("phase", measuredPhase)
                        .put("sequence", sequence).put("fresh", fresh).put("snapshot", snapshot).toString() + "\n")
                    lastSequence = sequence
                    status("running")
                    lastSample = now
                }
                Thread.sleep(50)
            }
            screenshot("final")
            assertTrue("No verified gameplay window: inspect screenshots and explicitly mark gameplay; menus are insufficient",
                gameplaySamples >= 20)
            status("complete")
        } catch (failure: Throwable) {
            File(output, "failure.txt").writeText(failure.stackTraceToString())
            status("failed")
            throw failure
        } finally {
            held.keys.forEach { NativeApp.setPadButton(0, it, 0, false) }
            NativeApp.setPerformanceMetricsEnabled(false, false, false)
            previousFrameLimit?.let { NativeApp.setFrameLimitEnabled(it) }
            EmulatorBridge.shutdown()
            activity?.let { instrumentation.runOnMainSync { it.finish() } }
            preferences.setBiosPath(oldBios.orEmpty())
            preferences.setGamePaths(oldGames)
            preferences.setOnboardingCompleted(oldOnboarding)
        }
    }
}

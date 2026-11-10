package com.sbro.emucorex.core

import android.content.Context
import android.content.Intent
import android.graphics.Rect
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import android.view.FrameMetrics
import android.view.KeyEvent
import android.view.InputDevice
import android.view.MotionEvent
import android.view.Window
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import java.util.Collections
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in measurement of the real menu; uses existing SAF grants and never changes settings or saves. */
@RunWith(AndroidJUnit4::class)
class GameMenuScrollDeviceTest {
    @Test fun fastScrollingAndTabSwitchingPreserveSettings() = runBlocking {
        Log.i("GameMenuScroll", "Starting real menu measurement")
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val game = InstrumentationRegistry.getArguments().getString("gameUri")
        assumeTrue(!game.isNullOrBlank())
        val context = ApplicationProvider.getApplicationContext<Context>()
        val preferences = AppPreferences(context)
        val settingsBefore = preferences.settingsSnapshot.first()
        val activity = instrumentation.startActivitySync(Intent("${context.packageName}.action.LAUNCH_GAME").apply {
            setClassName(context.packageName, "${context.packageName}.MainActivity")
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra("${context.packageName}.extra.GAME_PATH", game)
            putExtra("${context.packageName}.extra.RENDERER", RendererDefaults.VULKAN)
            putExtra("${context.packageName}.extra.AUTOTEST_MODE", true)
        })
        val thread = HandlerThread("menu-frame-metrics").apply { start() }
        val recording = AtomicBoolean(false)
        val frames = Collections.synchronizedList(mutableListOf<Long>())
        val layouts = Collections.synchronizedList(mutableListOf<Long>())
        val listener = Window.OnFrameMetricsAvailableListener { _, metrics, _ ->
            if (recording.get()) {
                frames += metrics.getMetric(FrameMetrics.TOTAL_DURATION)
                layouts += metrics.getMetric(FrameMetrics.LAYOUT_MEASURE_DURATION)
            }
        }
        try {
            await { EmulatorBridge.hasValidVm() }
            assertTrue("Game must start", EmulatorBridge.hasValidVm())
            Thread.sleep(1_000)
            val rendererBefore = NativeApp.getSetting("EmuCore/GS", "Renderer", "int")
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            await { findNode { it.contentDescription?.toString() == activity.getString(R.string.settings_graphics_tab) } != null }
            instrumentation.runOnMainSync { activity.window.addOnFrameMetricsAvailableListener(listener, Handler(thread.looper)) }
            for (tab in listOf(R.string.settings_graphics_tab, R.string.settings_fixes_tab, R.string.settings_controls_tab)) {
                var node = requireNotNull(findNode { it.contentDescription?.toString() == activity.getString(tab) })
                while (!node.isClickable) node = requireNotNull(node.parent)
                assertTrue(node.performAction(AccessibilityNodeInfo.ACTION_CLICK))
                Thread.sleep(400)
                val list = requireNotNull(findNode { it.isScrollable && bounds(it).height() > 300 && bounds(it).width() > 300 })
                val area = bounds(list)
                // Warm the tab once, then measure the same up/down flings in both builds.
                swipe(area, up = true)
                swipe(area, up = false)
                Thread.sleep(650)
                frames.clear()
                layouts.clear()
                recording.set(true)
                repeat(4) { swipe(area, up = true) }
                repeat(4) { swipe(area, up = false) }
                Thread.sleep(650)
                recording.set(false)
                val times = synchronized(frames) { frames.sorted() }
                val layoutTimes = synchronized(layouts) { layouts.sorted() }
                assertTrue("Scrolling must render measured frames", times.isNotEmpty())
                fun percentile(values: List<Long>, p: Double) = values[((values.size - 1) * p).toInt()] / 1_000_000.0
                val report = "tab=${activity.getString(tab)} frames=${times.size} p50_ms=${percentile(times, .50)} p95_ms=${percentile(times, .95)} max_ms=${times.last() / 1_000_000.0} over33ms=${times.count { it > 33_333_333 }} layout_p95_ms=${percentile(layoutTimes, .95)}"
                Log.i("GameMenuScroll", report)
                instrumentation.sendStatus(2, Bundle().apply { putString("stream", "\nMENU_SCROLL $report\n") })
            }
            assertEquals("Scrolling and tabs must not write preferences", settingsBefore, preferences.settingsSnapshot.first())
            assertEquals("Scrolling and tabs must not change the native renderer", rendererBefore,
                NativeApp.getSetting("EmuCore/GS", "Renderer", "int"))
            assertTrue("Menu must preserve the running VM", EmulatorBridge.hasValidVm())
        } finally {
            recording.set(false)
            instrumentation.runOnMainSync { activity.window.removeOnFrameMetricsAvailableListener(listener) }
            thread.quitSafely()
            EmulatorBridge.shutdown()
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    private fun await(condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 45_000
        while (!condition() && SystemClock.elapsedRealtime() < deadline) Thread.sleep(50)
        assertTrue("Timed out waiting for the game/menu", condition())
    }

    private fun bounds(node: AccessibilityNodeInfo) = Rect().also(node::getBoundsInScreen)

    private fun findNode(predicate: (AccessibilityNodeInfo) -> Boolean): AccessibilityNodeInfo? {
        fun search(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
            if (predicate(node)) return node
            return (0 until node.childCount).firstNotNullOfOrNull { node.getChild(it)?.let(::search) }
        }
        return InstrumentationRegistry.getInstrumentation().uiAutomation.rootInActiveWindow?.let(::search)
    }

    private fun swipe(area: Rect, up: Boolean) {
        val automation = InstrumentationRegistry.getInstrumentation().uiAutomation
        val started = SystemClock.uptimeMillis()
        val x = area.exactCenterX()
        val from = area.top + area.height() * if (up) .8f else .2f
        val to = area.top + area.height() * if (up) .2f else .8f
        for (step in 0..10) {
            val action = when (step) { 0 -> MotionEvent.ACTION_DOWN; 10 -> MotionEvent.ACTION_UP; else -> MotionEvent.ACTION_MOVE }
            val event = MotionEvent.obtain(started, SystemClock.uptimeMillis(), action, x, from + (to - from) * step / 10, 0)
            event.source = InputDevice.SOURCE_TOUCHSCREEN
            assertTrue(automation.injectInputEvent(event, true))
            event.recycle()
            Thread.sleep(10)
        }
        Thread.sleep(150)
    }
}

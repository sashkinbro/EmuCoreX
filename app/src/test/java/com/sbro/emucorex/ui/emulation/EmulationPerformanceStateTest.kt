package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class EmulationPerformanceStateTest {
    @Test
    fun detailedMonitoringRetainsNativeRatesAndAllOverlayLines() {
        val overlay = "FPS: 44.00 [P] | VPS: 50.00\nLSFG: 87.50\nCPU:Unknown | 31.5%\nFrame: 15 / 20 / 80 ms"
        val state = requireNotNull(parseEmulationPerformanceSnapshot("44.00\n88.0\n$overlay", FPS_OVERLAY_MODE_DETAILED, "Unisoc T820"))

        assertEquals("%.1f".format(44f), state.fps)
        assertEquals(88f, state.speedPercent)
        assertEquals(overlay.replace("CPU:Unknown", "CPU:Unisoc T820"), state.overlayText)
    }

    @Test
    fun simpleMonitoringStillUsesNumericGeneratedRate() {
        val raw = "44.00\n88.0\nFPS: 44.00 [P]\nLSFG: 87.50"
        val state = requireNotNull(parseEmulationPerformanceSnapshot(raw, FPS_OVERLAY_MODE_SIMPLE, "Unknown"))

        assertEquals("%.1f".format(87.5f), state.fps)
        assertEquals(88f, state.speedPercent)
        val waiting = requireNotNull(parseEmulationPerformanceSnapshot(raw.replace("87.50", "starting"), FPS_OVERLAY_MODE_SIMPLE, "Unknown"))
        assertEquals("%.1f".format(44f), waiting.fps)
    }

    @Test
    fun incompleteSnapshotsDoNotReplaceLiveMonitoring() {
        for (raw in listOf("", "44.0", "44.0\n88.0", "invalid\n88.0\nFPS", "44.0\ninvalid\nFPS")) {
            assertNull(raw, parseEmulationPerformanceSnapshot(raw, FPS_OVERLAY_MODE_DETAILED, "Unisoc T820"))
        }
    }
}

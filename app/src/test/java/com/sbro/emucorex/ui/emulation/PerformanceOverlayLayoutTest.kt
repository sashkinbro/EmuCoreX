package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.PerformanceOverlayMetrics
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PerformanceOverlayLayoutTest {
    private val fullSnapshot = """
        FPS: 59.94 [P] | VPS: 60.00
        Speed: 100% | Target: 100%
        Vulkan HW
        VRAM: 24 MB | TGT: 8 MB | SRC: 4 MB
        Frame: 15.20 / 16.67 / 18.40 ms
        Queue: 2
        Res: 1280x896 NTSC Progressive
        CPU:Snapdragon 8 Gen 3 | 31.5%
        GPU:Adreno 750 | 82.4% (13.73ms)
        EE: 55.2% (11.04 ms)
        GS: 57.7% (11.53 ms)
        VU: 57.4% (11.49 ms)
        SW-0: 20.0% (4.00 ms)
        Audio:OpenSL ES
    """.trimIndent()

    @Test
    fun defaultMaskShowsHostHardwareAndKeepsQueueHidden() {
        val layout = buildPerformanceOverlayLayout(
            fullSnapshot,
            PerformanceOverlayMetrics.DEFAULT,
            "EmuCoreX-0.2.6 | 119 | v2.7.316"
        )

        assertEquals("EmuCoreX-0.2.6|119|v2.7.316", layout.mainLines[0])
        assertEquals("FPS:59.94[P]|VPS:60.00", layout.mainLines[1])
        assertEquals("Speed:100%|Target:100%", layout.mainLines[2])
        assertTrue(layout.mainLines.any { it.startsWith("CPU:") })
        assertTrue(layout.mainLines.any { it.startsWith("GPU:") })
        assertTrue(layout.mainLines.any { it.startsWith("EE:") })
        assertTrue(layout.mainLines.any { it.startsWith("SW-0:") })
        assertFalse(layout.mainLines.any { it.startsWith("Audio:") })
        assertEquals("Vulkan HW|VRAM:24MB|TGT:8MB|SRC:4MB", layout.bottomLines[0])
        assertFalse(layout.bottomLines.any { it.startsWith("GS Queue:") })
    }

    @Test
    fun compositeLinesAllowIndependentMetricSelection() {
        val mask = PerformanceOverlayMetrics.VPS or PerformanceOverlayMetrics.TARGET
        val layout = buildPerformanceOverlayLayout(fullSnapshot, mask)

        assertEquals(listOf("VPS:60.00", "Target:100%"), layout.mainLines)
        assertTrue(layout.bottomLines.isEmpty())
    }

    @Test
    fun rendererAndVramCanBeSelectedIndependently() {
        val rendererOnly = buildPerformanceOverlayLayout(fullSnapshot, PerformanceOverlayMetrics.RENDERER)
        val vramOnly = buildPerformanceOverlayLayout(fullSnapshot, PerformanceOverlayMetrics.VRAM)

        assertEquals(listOf("Vulkan HW"), rendererOnly.bottomLines)
        assertEquals(listOf("VRAM:24MB|TGT:8MB|SRC:4MB"), vramOnly.bottomLines)
    }

    @Test
    fun queueCanBeEnabledExplicitly() {
        val layout = buildPerformanceOverlayLayout(fullSnapshot, PerformanceOverlayMetrics.QUEUE)

        assertEquals(listOf("GS Queue:2"), layout.bottomLines)
        assertTrue(layout.mainLines.isEmpty())
    }

    @Test
    fun audioBackendIsOptIn() {
        val hidden = buildPerformanceOverlayLayout(fullSnapshot, PerformanceOverlayMetrics.DEFAULT)
        val visible = buildPerformanceOverlayLayout(fullSnapshot, PerformanceOverlayMetrics.AUDIO)

        assertFalse(hidden.mainLines.any { it.startsWith("Audio:") })
        assertEquals(listOf("Audio:OpenSL ES"), visible.mainLines)
        assertEquals(0, PerformanceOverlayMetrics.DEFAULT and PerformanceOverlayMetrics.AUDIO)
    }

    @Test
    fun hostHardwareMetricsAreOptInAndHeaderCannotBeMaskedOut() {
        val layout = buildPerformanceOverlayLayout(
            fullSnapshot,
            PerformanceOverlayMetrics.HOST_CPU or PerformanceOverlayMetrics.HOST_GPU,
            "EmuCoreX-0.2.6 | 119 | v2.7.316"
        )

        assertEquals(
            listOf(
                "EmuCoreX-0.2.6|119|v2.7.316",
                "CPU:Snapdragon 8 Gen 3|31.5%",
                "GPU:Adreno 750|82.4%(13.73ms)"
            ),
            layout.mainLines
        )
        assertTrue(layout.bottomLines.isEmpty())
    }

    @Test
    fun hostHardwareMetricsArePlacedDirectlyBelowVu() {
        val layout = buildPerformanceOverlayLayout(fullSnapshot, PerformanceOverlayMetrics.ALL)

        val vuIndex = layout.mainLines.indexOfFirst { it.startsWith("VU:") }
        assertEquals(vuIndex + 1, layout.mainLines.indexOfFirst { it.startsWith("CPU:") })
        assertEquals(vuIndex + 2, layout.mainLines.indexOfFirst { it.startsWith("GPU:") })
        assertEquals(vuIndex + 3, layout.mainLines.indexOfFirst { it.startsWith("SW-0:") })
        assertEquals(vuIndex + 4, layout.mainLines.indexOfFirst { it.startsWith("Audio:") })
    }

    @Test
    fun cpuPlatformCodeCanBeReplacedWithoutChangingItsLoad() {
        val text = replacePerformanceCpuName("CPU:SM8850 | 4.9%", "Snapdragon 8 Elite Gen 5")

        assertEquals("CPU:Snapdragon 8 Elite Gen 5 | 4.9%", text)
    }

    @Test
    fun allFormattingWhitespaceIsCompactedWithoutDamagingNames() {
        assertEquals(
            "CPU:Snapdragon 8 Elite Gen 5|4.7%(2.15ms)",
            compactPerformanceOverlayLine(" CPU: Snapdragon 8 Elite Gen 5 | 4.7% (2.15 ms) ")
        )
        assertEquals(
            "Res:640x512 PAL Interlaced(Field)",
            compactPerformanceOverlayLine("Res: 640x512 PAL Interlaced (Field)")
        )
        assertEquals(
            "Frame:17.89/20.00/22.73ms",
            compactPerformanceOverlayLine("Frame: 17.89 / 20.00 / 22.73 ms")
        )
    }

    @Test
    fun unsupportedMaskBitsAreDiscarded() {
        assertEquals(0, PerformanceOverlayMetrics.sanitize(Int.MIN_VALUE))
        assertEquals(
            PerformanceOverlayMetrics.FPS,
            PerformanceOverlayMetrics.sanitize(Int.MIN_VALUE or PerformanceOverlayMetrics.FPS)
        )
    }
}

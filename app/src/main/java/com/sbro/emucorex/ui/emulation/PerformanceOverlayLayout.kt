package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.PerformanceOverlayMetrics

internal data class PerformanceOverlayLayout(
    val mainLines: List<String>,
    val bottomLines: List<String>
)

private val overlayColonSpacing = Regex(":\\s+")
private val overlayPipeSpacing = Regex("\\s*\\|\\s*")
private val overlaySlashSpacing = Regex("\\s*/\\s*")
private val overlayGroupingSpacing = Regex("\\s+(?=[\\[(])")
private val overlayUnitSpacing = Regex("(?<=\\d)\\s+(?=(?:ms|MB)\\b)")
private val lsfgDisplayRate = Regex("(?m)^LSFG:\\s*([0-9]+(?:\\.[0-9]+)?)\\s*$")

internal fun effectivePerformanceFps(baseFps: Float, overlayText: String): Float {
    return lsfgDisplayRate.find(overlayText)
        ?.groupValues
        ?.getOrNull(1)
        ?.toFloatOrNull()
        ?: baseFps
}

internal fun compactPerformanceOverlayLine(line: String): String {
    return line
        .replace(overlayColonSpacing, ":")
        .replace(overlayPipeSpacing, "|")
        .replace(overlaySlashSpacing, "/")
        .replace(overlayGroupingSpacing, "")
        .replace(overlayUnitSpacing, "")
        .trim()
}

internal fun buildPerformanceOverlayLayout(
    text: String,
    metricsMask: Int,
    fixedHeaderLine: String = ""
): PerformanceOverlayLayout {
    fun isRendererLine(line: String): Boolean {
        return line.endsWith(" HW") ||
            line.endsWith(" SW") ||
            line.endsWith(" Null") ||
            line.contains(" HW |") ||
            line.contains(" SW |") ||
            line.contains(" Null |")
    }

    fun metricForSegment(segment: String): Int = when {
        segment.startsWith("FPS:") -> PerformanceOverlayMetrics.FPS
        segment.startsWith("VPS:") -> PerformanceOverlayMetrics.VPS
        segment.startsWith("Speed:") -> PerformanceOverlayMetrics.SPEED
        segment.startsWith("Target:") -> PerformanceOverlayMetrics.TARGET
        else -> 0
    }

    fun filterLine(line: String): String? {
        if (line.startsWith("LSFG:")) {
            return line.takeIf {
                PerformanceOverlayMetrics.isEnabled(metricsMask, PerformanceOverlayMetrics.FPS)
            }
        }
        if (line.startsWith("FPS:") || line.startsWith("VPS:") ||
            line.startsWith("Speed:") || line.startsWith("Target:")
        ) {
            return line.split(" | ")
                .filter { segment ->
                    val metric = metricForSegment(segment)
                    metric == 0 || PerformanceOverlayMetrics.isEnabled(metricsMask, metric)
                }
                .joinToString(" | ")
                .ifBlank { null }
        }

        val metric = when {
            isRendererLine(line) -> PerformanceOverlayMetrics.RENDERER
            line.startsWith("VRAM:") -> PerformanceOverlayMetrics.VRAM
            line.startsWith("Frame:") -> PerformanceOverlayMetrics.FRAME_TIME
            line.startsWith("Queue:") -> PerformanceOverlayMetrics.QUEUE
            line.startsWith("Res:") -> PerformanceOverlayMetrics.RESOLUTION
            line.startsWith("EE:") -> PerformanceOverlayMetrics.EE
            line.startsWith("GS:") -> PerformanceOverlayMetrics.GS
            line.startsWith("VU:") -> PerformanceOverlayMetrics.VU
            line.startsWith("SW-") -> PerformanceOverlayMetrics.SOFTWARE_THREADS
            line.startsWith("CPU:") -> PerformanceOverlayMetrics.HOST_CPU
            line.startsWith("GPU:") -> PerformanceOverlayMetrics.HOST_GPU
            line.startsWith("Audio:") -> PerformanceOverlayMetrics.AUDIO
            else -> 0
        }
        if (metric != 0 && !PerformanceOverlayMetrics.isEnabled(metricsMask, metric)) return null
        return if (line.startsWith("Queue:")) {
            line.replaceFirst("Queue:", "GS Queue:")
        } else {
            line
        }
    }

    val filtered = text.lineSequence()
        .map { it.trim() }
        .filter { it.isNotEmpty() }
        .mapNotNull(::filterLine)
        .toList()

    val displayLines = if (PerformanceOverlayMetrics.sanitize(metricsMask) == PerformanceOverlayMetrics.FPS) {
        val generatedRate = filtered.firstOrNull { it.startsWith("LSFG:") }
            ?.removePrefix("LSFG:")
            ?.trim()
            ?.takeIf { it.toFloatOrNull() != null }
        if (generatedRate != null) {
            filtered
                .filterNot { it.startsWith("LSFG:") }
                .map { line -> if (line.startsWith("FPS:")) "FPS: $generatedRate [LSFG]" else line }
        } else {
            filtered.filterNot { it.startsWith("LSFG:") }
        }
    } else {
        filtered
    }

    val topLines = displayLines.filter { line ->
        line.startsWith("FPS:") || line.startsWith("VPS:") ||
            line.startsWith("Speed:") || line.startsWith("Target:") || line.startsWith("LSFG:")
    }
    val processorLines = displayLines.filter { line ->
        line.startsWith("EE:") || line.startsWith("GS:") || line.startsWith("VU:")
    }
    val hardwareLines = displayLines.filter { line ->
        line.startsWith("CPU:") || line.startsWith("GPU:")
    }
    val audioLines = displayLines.filter { line -> line.startsWith("Audio:") }
    val softwareThreadLines = displayLines.filter { line -> line.startsWith("SW-") }
    val rendererLine = displayLines.firstOrNull(::isRendererLine)
    val vramLine = displayLines.firstOrNull { it.startsWith("VRAM:") }
    val bottomLines = buildList {
        when {
            rendererLine != null && vramLine != null -> add("$rendererLine | $vramLine")
            rendererLine != null -> add(rendererLine)
            vramLine != null -> add(vramLine)
        }
        addAll(displayLines.filter { line ->
            line.startsWith("Frame:") || line.startsWith("GS Queue:") || line.startsWith("Res:")
        })
    }
    val knownLines = (topLines + processorLines + hardwareLines + audioLines + softwareThreadLines + bottomLines).toSet()
    val unknownLines = displayLines.filterNot { line ->
        line in knownLines || line == rendererLine || line == vramLine
    }

    return PerformanceOverlayLayout(
        mainLines = (
            listOf(fixedHeaderLine).filter(String::isNotBlank) +
                topLines + processorLines + hardwareLines + softwareThreadLines + audioLines + unknownLines
            ).map(::compactPerformanceOverlayLine),
        bottomLines = bottomLines.map(::compactPerformanceOverlayLine)
    )
}

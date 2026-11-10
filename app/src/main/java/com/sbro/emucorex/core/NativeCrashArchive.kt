package com.sbro.emucorex.core

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import com.sbro.emucorex.BuildConfig
import java.io.File
import java.io.InputStream
import java.io.OutputStream

/** Collect outside the dying process; Android owns unwinding and the original register context. */
internal object NativeCrashArchive {
    private const val MAX_TRACE_BYTES = 4 * 1024 * 1024
    private const val KEEP_REPORTS = 4

    fun collect(context: Context) {
        if (Build.VERSION.SDK_INT < 31) return
        runCatching {
            val manager = context.getSystemService(ActivityManager::class.java) ?: return
            val directory = File(context.getExternalFilesDir(null) ?: context.filesDir, "logs/native-exits")
            val exits = manager.getHistoricalProcessExitReasons(context.packageName, 0, 16)
                .filter { it.processName == context.packageName && it.reason == ApplicationExitInfo.REASON_CRASH_NATIVE }
                .sortedByDescending { it.timestamp }.take(KEEP_REPORTS)
            if (exits.isEmpty()) return
            check(directory.isDirectory || directory.mkdirs())
            for (exit in exits) {
                runCatching archive@{
                    val name = "native-exit-${exit.timestamp}-${exit.pid}"
                    val trace = File(directory, "$name.pb")
                    if (trace.isFile) return@archive
                    val metadata = File(directory, "$name.txt")
                    val alreadyRecorded = metadata.isFile
                    val partial = File(directory, "$name.part")
                    try {
                        exit.traceInputStream?.use { input ->
                            partial.outputStream().use { output -> copyBoundedTrace(input, output, MAX_TRACE_BYTES) }
                            check(partial.renameTo(trace))
                        }
                    } finally {
                        partial.delete()
                    }
                    metadata.writeText(buildString {
                        appendLine("timestamp_ms=${exit.timestamp}")
                        appendLine("pid=${exit.pid}")
                        appendLine("process=${exit.processName}")
                        appendLine("reason=${exit.reason} status=${exit.status}")
                        appendLine("description=${exit.description.orEmpty()}")
                        appendLine("collector_build=${BuildConfig.VERSION_CODE}")
                        appendLine("system_tombstone=${if (trace.isFile) trace.name else "unavailable"}")
                    })
                    if (!alreadyRecorded) CrashLogger.logError("NativeExit", "Recovered native exit: ${metadata.name}")
                }.onFailure { CrashLogger.logError("NativeExit", "Could not archive native tombstone", it) }
            }
            val keepNames = exits.map { "native-exit-${it.timestamp}-${it.pid}" }.toSet()
            directory.listFiles()?.filter { it.name.startsWith("native-exit-") && it.nameWithoutExtension !in keepNames }
                ?.forEach { it.delete() }
        }.onFailure { CrashLogger.logError("NativeExit", "Could not read historical native exits", it) }
    }
}

internal fun copyBoundedTrace(input: InputStream, output: OutputStream, maxBytes: Int) {
    require(maxBytes > 0)
    val buffer = ByteArray(8192)
    var remaining = maxBytes
    while (remaining > 0) {
        val count = input.read(buffer, 0, minOf(buffer.size, remaining))
        if (count < 0) return
        if (count == 0) continue
        output.write(buffer, 0, count)
        remaining -= count
    }
    check(input.read() == -1) { "Native tombstone exceeds $maxBytes bytes" }
}

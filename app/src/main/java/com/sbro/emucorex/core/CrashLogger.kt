package com.sbro.emucorex.core

import android.content.Context
import android.os.Build
import com.sbro.emucorex.BuildConfig
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale


object CrashLogger {

    private const val TAG = "CrashLogger"
    private const val LOG_DIR = "logs"
    private const val LOG_FILE = "crash.log"
    private const val MAX_LOG_SIZE_BYTES = 5 * 1024 * 1024L  // 5 MB
    private const val KEEP_BYTES_AFTER_ROTATION = 4 * 1024 * 1024  // keep last 4 MB

    @Volatile
    private var logFile: File? = null

    @Volatile
    private var previousHandler: Thread.UncaughtExceptionHandler? = null

    private val dateFmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)

    fun init(context: Context) {
        logFile = resolveLogFile(context)
        installUncaughtExceptionHandler()
        if (BuildConfig.DEBUG) writeStartupLine(context)
    }

    fun logError(tag: String, message: String, throwable: Throwable? = null) {
        val body = buildString {
            append("[$tag] $message")
            if (throwable != null) {
                append("\n")
                append(throwable.stackTraceString())
            }
        }
        writeEntry("ERROR", body)
    }

    fun logInfo(tag: String, message: String) {
        if (!BuildConfig.DEBUG) return
        writeEntry("INFO", "[$tag] $message")
    }

    fun logContext(key: String, value: Any?) {
        if (!BuildConfig.DEBUG) return
        writeEntry("CTX", "$key = $value")
    }

    // ─── Private helpers ──────────────────────────────────────────────────────

    private fun resolveLogFile(context: Context): File? {
        return try {
            val dir = File(context.getExternalFilesDir(null) ?: context.filesDir, LOG_DIR)
            if (!dir.exists()) dir.mkdirs()
            File(dir, LOG_FILE)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to resolve log file", e)
            null
        }
    }

    private fun writeStartupLine(context: Context) {
        val appVersion = runCatching {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        }.getOrDefault("?")

        val device = "${Build.MANUFACTURER} ${Build.MODEL}"
        val androidVer = Build.VERSION.RELEASE
        val abi = Build.SUPPORTED_ABIS.firstOrNull() ?: "?"

        writeEntry("START", "APP START — v$appVersion | Android $androidVer | $device | $abi")
    }

    private fun installUncaughtExceptionHandler() {
        previousHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                writeEntry(
                    "CRASH",
                    "Thread: ${thread.name}\n${throwable.stackTraceString()}"
                )
            } catch (_: Exception) {
                // Don't let logger crash prevent the original crash from propagating
            }
            previousHandler?.uncaughtException(thread, throwable)
        }
    }

    private fun writeEntry(level: String, body: String) {
        val file = logFile ?: return
        val timestamp = dateFmt.format(Date())
        val line = "[$timestamp] $level: $body\n"

        try {
            rotateIfNeeded(file)
            FileWriter(file, /* append = */ true).use { it.write(line) }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to write log entry", e)
        }
    }

    private fun rotateIfNeeded(file: File) {
        if (!file.exists() || file.length() < MAX_LOG_SIZE_BYTES) return
        try {
            val content = file.readBytes()
            // Keep only the last KEEP_BYTES_AFTER_ROTATION bytes to avoid trimming mid-line chaos
            val trimStart = (content.size - KEEP_BYTES_AFTER_ROTATION).coerceAtLeast(0)
            // Find the next newline after trimStart so we start on a clean line
            var safeStart = trimStart
            while (safeStart < content.size && content[safeStart] != '\n'.code.toByte()) safeStart++
            safeStart++ // skip the newline itself

            val rotationHeader = "=== LOG ROTATED at ${dateFmt.format(Date())} (old entries trimmed) ===\n"
            file.writeBytes(rotationHeader.toByteArray() + content.copyOfRange(safeStart.coerceAtMost(content.size), content.size))
        } catch (e: Exception) {
            Log.e(TAG, "Failed to rotate log", e)
        }
    }

    private fun Throwable.stackTraceString(): String {
        val sw = StringWriter()
        printStackTrace(PrintWriter(sw))
        return sw.toString().trimEnd()
    }
}

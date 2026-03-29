package com.sbro.emucorex.data

import android.content.Context
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

class CoverArtRepository(private val context: Context) {

    companion object {
        private const val TAG = "CoverArtRepository"
        private const val COVER_BASE_URL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default"
        private const val CONNECT_TIMEOUT_MS = 10000
        private const val READ_TIMEOUT_MS = 15000
        private const val MISS_TTL_MS = 7L * 24L * 60L * 60L * 1000L // 7 days
    }

    private val cacheDirectory by lazy {
        File(context.cacheDir, "game-covers").apply {
            if (!exists()) mkdirs()
            Log.d(TAG, "Cover cache directory created: $absolutePath")
        }
    }

    fun findCachedCoverPath(serial: String?): String? {
        val normalizedSerial = normalizeSerial(serial)
        if (normalizedSerial == null) {
            Log.d(TAG, "No serial provided")
            return null
        }
        val coverFile = File(cacheDirectory, "$normalizedSerial.jpg")
        val exists = coverFile.exists()
        Log.d(TAG, "Cached cover for $normalizedSerial: ${if (exists) "FOUND" else "NOT FOUND"}")
        return coverFile.takeIf(File::exists)?.absolutePath
    }

    fun findCachedCoverUri(serial: String?): String? {
        return findCachedCoverPath(serial)
    }

    fun downloadCover(serial: String?): String? {
        val normalizedSerial = normalizeSerial(serial)
        if (normalizedSerial == null) {
            Log.w(TAG, "Cannot download cover: invalid serial '$serial'")
            return null
        }

        Log.d(TAG, "========== COVER DOWNLOAD START ==========")
        Log.d(TAG, "Original serial: $serial")
        Log.d(TAG, "Normalized serial: $normalizedSerial")

        val coverFile = File(cacheDirectory, "$normalizedSerial.jpg")
        if (coverFile.exists()) {
            Log.d(TAG, "Cover already exists: ${coverFile.absolutePath}")
            Log.d(TAG, "========== COVER DOWNLOAD END (CACHED) ==========")
            return coverFile.absolutePath
        }

        val missFile = File(cacheDirectory, "$normalizedSerial.miss")
        if (missFile.exists() && System.currentTimeMillis() - missFile.lastModified() < MISS_TTL_MS) {
            Log.d(TAG, "Recent miss marker found, skipping (age: ${System.currentTimeMillis() - missFile.lastModified()}ms)")
            Log.d(TAG, "========== COVER DOWNLOAD END (MISS CACHED) ==========")
            return null
        }

        Log.d(TAG, "Trying primary source: $COVER_BASE_URL/$normalizedSerial.jpg")
        var result = downloadFromUrl("$COVER_BASE_URL/$normalizedSerial.jpg", coverFile, missFile, "Primary")
        
        if (result == null) {
            val alternativeSerial = normalizedSerial.replace("-", "")
            if (alternativeSerial != normalizedSerial) {
                Log.d(TAG, "Trying alternative serial format: $alternativeSerial")
                val altCoverFile = File(cacheDirectory, "$alternativeSerial.jpg")
                result = downloadFromUrl("$COVER_BASE_URL/$alternativeSerial.jpg", altCoverFile, missFile, "Alternative")
                if (result != null) {
                    altCoverFile.renameTo(coverFile)
                    result = coverFile.absolutePath
                }
            }
        }

        Log.d(TAG, "Download result: ${if (result != null) "SUCCESS" else "FAILED"}")
        Log.d(TAG, "========== COVER DOWNLOAD END ==========")
        return result
    }

    private fun downloadFromUrl(
        urlString: String,
        coverFile: File,
        missFile: File,
        sourceName: String
    ): String? {
        if (coverFile.exists()) {
            return coverFile.absolutePath
        }

        val connection = (URL(urlString).openConnection() as? HttpURLConnection)
            ?: run {
                Log.e(TAG, "$sourceName: Failed to create HTTP connection")
                return null
            }

        return try {
            connection.requestMethod = "GET"
            connection.instanceFollowRedirects = true
            connection.connectTimeout = CONNECT_TIMEOUT_MS
            connection.readTimeout = READ_TIMEOUT_MS
            connection.setRequestProperty("User-Agent", "Mozilla/5.0 (EmuCoreX)")
            connection.connect()

            val responseCode = connection.responseCode
            Log.d(TAG, "$sourceName: HTTP response code: $responseCode")

            if (responseCode != HttpURLConnection.HTTP_OK) {
                Log.w(TAG, "$sourceName: Cover not found (HTTP $responseCode)")
                if (responseCode == 404) {
                    missFile.writeText("$responseCode at ${System.currentTimeMillis()}")
                }
                return null
            }

            val contentLength = connection.contentLength
            Log.d(TAG, "$sourceName: Content length: $contentLength bytes")

            if (contentLength <= 0) {
                Log.w(TAG, "$sourceName: Invalid content length")
                return null
            }

            val tempFile = File(cacheDirectory, "${coverFile.name}.tmp")
            connection.inputStream.use { input ->
                tempFile.outputStream().use { output ->
                    val copied = input.copyTo(output)
                    Log.d(TAG, "$sourceName: Copied $copied bytes")
                }
            }

            if (tempFile.length() == 0L) {
                Log.w(TAG, "$sourceName: Downloaded file is empty")
                tempFile.delete()
                return null
            }

            if (coverFile.exists()) {
                coverFile.delete()
            }
            tempFile.renameTo(coverFile)
            missFile.delete()

            Log.d(TAG, "$sourceName: SUCCESS - ${coverFile.absolutePath}")
            coverFile.absolutePath
        } catch (e: Exception) {
            Log.e(TAG, "$sourceName: Error: ${e.message}", e)
            null
        } finally {
            connection.disconnect()
        }
    }

    private fun normalizeSerial(serial: String?): String? {
        if (serial.isNullOrBlank()) return null

        val regex = Regex("([A-Za-z]{4})[^a-zA-Z0-9]*([0-9]{3})[^a-zA-Z0-9]*([0-9]{2})")
        val altRegex = Regex("([A-Za-z]{4})[^a-zA-Z0-9]*([0-9]{5})")
        
        val cleanSerial = serial.trim().uppercase(Locale.ROOT)
        var formatted: String? = null
        
        val match = regex.find(cleanSerial)
        if (match != null) {
            formatted = "${match.groupValues[1]}-${match.groupValues[2]}${match.groupValues[3]}"
        } else {
            val altMatch = altRegex.find(cleanSerial)
            if (altMatch != null) {
                formatted = "${altMatch.groupValues[1]}-${altMatch.groupValues[2]}"
            }
        }
        
        return (formatted ?: cleanSerial.replace(Regex("[^A-Z0-9_-]"), ""))
            .also { Log.d(TAG, "Normalized: '$serial' -> '$it'") }
    }

}

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
        const val DEFAULT_COVER_BASE_URL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default"
        const val DEFAULT_COVER_3D_BASE_URL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/3d"
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

    fun clearCache() {
        cacheDirectory.listFiles().orEmpty().forEach { file ->
            runCatching { file.delete() }
        }
    }

    fun findCachedCoverPath(serial: String?): String? {
        if (resolveCoverArtStyle() == AppPreferences.COVER_ART_STYLE_DISABLED) {
            return null
        }
        val normalizedSerial = normalizeSerial(serial)
        if (normalizedSerial == null) {
            Log.d(TAG, "No serial provided")
            return null
        }
        val style = resolveCoverArtStyle()
        val preferredFiles = if (style == AppPreferences.COVER_ART_STYLE_3D) {
            listOf(
                File(cacheDirectory, "${normalizedSerial}_3d.png"),
                File(cacheDirectory, "${normalizedSerial}_3d.jpg")
            )
        } else {
            listOf(
                File(cacheDirectory, "$normalizedSerial.jpg"),
                File(cacheDirectory, "$normalizedSerial.png")
            )
        }
        val found = preferredFiles.firstOrNull(File::exists)
        Log.d(TAG, "Cached cover for $normalizedSerial (style=$style): ${if (found != null) "FOUND" else "NOT FOUND"}")
        return found?.absolutePath
    }

    fun findCachedCoverUri(serial: String?): String? {
        return findCachedCoverPath(serial)
    }

    fun downloadCover(serial: String?): String? {
        if (resolveCoverArtStyle() == AppPreferences.COVER_ART_STYLE_DISABLED) {
            Log.d(TAG, "Cover download skipped: cover art style is disabled")
            return null
        }
        val normalizedSerial = normalizeSerial(serial)
        if (normalizedSerial == null) {
            Log.w(TAG, "Cannot download cover: invalid serial '$serial'")
            return null
        }
        val coverBaseUrl = resolveCoverBaseUrl()
        val style = resolveCoverArtStyle()
        val targetExtension = if (style == AppPreferences.COVER_ART_STYLE_3D) "png" else "jpg"

        Log.d(TAG, "========== COVER DOWNLOAD START ==========")
        Log.d(TAG, "Original serial: $serial")
        Log.d(TAG, "Normalized serial: $normalizedSerial")
        Log.d(TAG, "Cover base URL: $coverBaseUrl")
        Log.d(TAG, "Cover style: $style")

        val coverFile = File(cacheDirectory, cacheFileName(normalizedSerial, style, targetExtension))
        if (coverFile.exists()) {
            Log.d(TAG, "Cover already exists: ${coverFile.absolutePath}")
            Log.d(TAG, "========== COVER DOWNLOAD END (CACHED) ==========")
            return coverFile.absolutePath
        }

        val missFile = File(cacheDirectory, cacheMissFileName(normalizedSerial, style))
        if (missFile.exists() && System.currentTimeMillis() - missFile.lastModified() < MISS_TTL_MS) {
            Log.d(TAG, "Recent miss marker found, skipping (age: ${System.currentTimeMillis() - missFile.lastModified()}ms)")
            Log.d(TAG, "========== COVER DOWNLOAD END (MISS CACHED) ==========")
            return null
        }

        val extensionsToTry = if (style == AppPreferences.COVER_ART_STYLE_3D) listOf("png", "jpg") else listOf("jpg", "png")
        var result: String? = null
        for (extension in extensionsToTry) {
            val targetFile = File(cacheDirectory, cacheFileName(normalizedSerial, style, extension))
            Log.d(TAG, "Trying primary source: $coverBaseUrl/$normalizedSerial.$extension")
            result = downloadFromUrl("$coverBaseUrl/$normalizedSerial.$extension", targetFile, missFile, "Primary")
            if (result != null) {
                break
            }
        }
        
        if (result == null) {
            val alternativeSerial = normalizedSerial.replace("-", "")
            if (alternativeSerial != normalizedSerial) {
                Log.d(TAG, "Trying alternative serial format: $alternativeSerial")
                for (extension in extensionsToTry) {
                    val altCoverFile = File(cacheDirectory, cacheFileName(alternativeSerial, style, extension))
                    result = downloadFromUrl("$coverBaseUrl/$alternativeSerial.$extension", altCoverFile, missFile, "Alternative")
                    if (result != null) {
                        val finalFile = File(cacheDirectory, cacheFileName(normalizedSerial, style, extension))
                        if (altCoverFile.absolutePath != finalFile.absolutePath) {
                            if (finalFile.exists()) finalFile.delete()
                            altCoverFile.renameTo(finalFile)
                        }
                        result = finalFile.absolutePath
                        break
                    }
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

    private fun resolveCoverBaseUrl(): String {
        val preferences = AppPreferences(context)
        val configuredUrls = preferences.getCoverDownloadBaseUrlSync()
            ?.split(Regex("\\s+"))
            ?.map { it.trim().trimEnd('/') }
            ?.filter { it.isNotBlank() }
            .orEmpty()
        val style = preferences.getCoverArtStyleSync()
        if (configuredUrls.isNotEmpty()) {
            return if (style == AppPreferences.COVER_ART_STYLE_3D) {
                configuredUrls.getOrNull(1) ?: configuredUrls.first()
            } else {
                configuredUrls.first()
            }
        }
        return if (style == AppPreferences.COVER_ART_STYLE_3D) {
            DEFAULT_COVER_3D_BASE_URL
        } else {
            DEFAULT_COVER_BASE_URL
        }
    }

    private fun resolveCoverArtStyle(): Int {
        return AppPreferences(context).getCoverArtStyleSync()
    }

    private fun cacheFileName(serial: String, style: Int, extension: String): String {
        return if (style == AppPreferences.COVER_ART_STYLE_3D) {
            "${serial}_3d.$extension"
        } else {
            "$serial.$extension"
        }
    }

    private fun cacheMissFileName(serial: String, style: Int): String {
        return if (style == AppPreferences.COVER_ART_STYLE_3D) {
            "${serial}_3d.miss"
        } else {
            "$serial.miss"
        }
    }

}

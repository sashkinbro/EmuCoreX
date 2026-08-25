package com.sbro.emucorex.data

import android.content.Context
import android.graphics.BitmapFactory
import android.net.Uri
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

data class CoverCacheClearResult(
    val deletedFiles: Int,
    val freedBytes: Long,
    val failedFiles: Int
) {
    val fullyCleared: Boolean
        get() = failedFiles == 0
}

class CoverArtRepository(context: Context) {

    companion object {
        private const val TAG = "CoverArtRepository"
        const val DEFAULT_COVER_BASE_URL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default"
        const val DEFAULT_COVER_3D_BASE_URL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/3d"
        const val DEFAULT_ARCADE_COVER_BASE_URL = "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-Arcade-Covers/main/covers"
        private const val CONNECT_TIMEOUT_MS = 10000
        private const val READ_TIMEOUT_MS = 15000
        private const val MISS_TTL_MS = 7L * 24L * 60L * 60L * 1000L // 7 days
        private const val ARCADE_COVER_SOURCE_REVISION = 5

        private data class ArcadeCoverAsset(val baseUrl: String, val fileName: String)

        private fun arcadeCover(fileName: String) = ArcadeCoverAsset(DEFAULT_ARCADE_COVER_BASE_URL, fileName)

        // All Namco System 246/256 titles are hosted as 600x900 PNG in EmuCoreX-Arcade-Covers.
        private val ARCADE_COVER_ASSETS = mapOf(
            "NM00001" to arcadeCover("NM00001.png"),
            "NM00002" to arcadeCover("NM00002.png"),
            "NM00003" to arcadeCover("NM00003.png"),
            "NM00004" to arcadeCover("NM00004.png"),
            "NM00005" to arcadeCover("NM00005.png"),
            "NM00006" to arcadeCover("NM00006.png"),
            "NM00007" to arcadeCover("NM00007.png"),
            "NM00008" to arcadeCover("NM00008.png"),
            "NM00009" to arcadeCover("NM00009.png"),
            "NM00010" to arcadeCover("NM00010.png"),
            "NM00011" to arcadeCover("NM00011.png"),
            "NM00012" to arcadeCover("NM00012.png"),
            "NM00013" to arcadeCover("NM00013.png"),
            "NM00014" to arcadeCover("NM00014.png"),
            "NM00015" to arcadeCover("NM00015.png"),
            "NM00016" to arcadeCover("NM00016.png"),
            "NM00017" to arcadeCover("NM00017.png"),
            "NM00018" to arcadeCover("NM00018.png"),
            "NM00019" to arcadeCover("NM00019.png"),
            "NM00020" to arcadeCover("NM00020.png"),
            "NM00021" to arcadeCover("NM00021.png"),
            "NM00022" to arcadeCover("NM00022.png"),
            "NM00023" to arcadeCover("NM00023.png"),
            "NM00024" to arcadeCover("NM00024.png"),
            "NM00025" to arcadeCover("NM00025.png"),
            "NM00026" to arcadeCover("NM00026.png"),
            "NM00027" to arcadeCover("NM00027.png"),
            "NM00028" to arcadeCover("NM00028.png"),
            "NM00029" to arcadeCover("NM00029.png"),
            "NM00030" to arcadeCover("NM00030.png"),
            "NM00031" to arcadeCover("NM00031.png"),
            "NM00032" to arcadeCover("NM00032.png"),
            "NM00033" to arcadeCover("NM00033.png"),
            "NM00034" to arcadeCover("NM00034.png"),
            "NM00035" to arcadeCover("NM00035.png"),
            "NM00036" to arcadeCover("NM00036.png"),
            "NM00037" to arcadeCover("NM00037.png"),
            "NM00038" to arcadeCover("NM00038.png"),
            "NM00039" to arcadeCover("NM00039.png"),
            "NM00040" to arcadeCover("NM00040.png"),
            "NM00041" to arcadeCover("NM00041.png"),
            "NM00042" to arcadeCover("NM00042.png"),
            "NM00043" to arcadeCover("NM00043.png"),
            "NM00044" to arcadeCover("NM00044.png"),
            "NM00045" to arcadeCover("NM00045.png"),
            "NM00046" to arcadeCover("NM00046.png"),
            "NM00047" to arcadeCover("NM00047.png"),
            "NM00048" to arcadeCover("NM00048.png"),
            "NM00051" to arcadeCover("NM00051.png"),
            "NM00052" to arcadeCover("NM00052.png"),
            "NM00053" to arcadeCover("NM00053.png"),
            "NM00054" to arcadeCover("NM00054.png"),
            "NM00056" to arcadeCover("NM00056.png"),
            "NM00057" to arcadeCover("NM00057.png"),
            "NM10003" to arcadeCover("NM10003.png")
        )

        internal fun hasDefaultArcadeCover(serial: String): Boolean =
            ARCADE_COVER_ASSETS.containsKey(serial.uppercase(Locale.ROOT))
    }

    private val context = context.applicationContext

    private val coverCacheDirectory = File(this.context.cacheDir, "game-covers")
    private val remoteImageCacheDirectory = File(this.context.cacheDir, "remote-image-cache")

    private val cacheDirectory by lazy {
        coverCacheDirectory.apply {
            if (!exists()) mkdirs()
            Log.d(TAG, "Cover cache directory created: $absolutePath")
        }
    }

    /** Clears only automatically downloaded covers. User-selected covers live in filesDir and are preserved. */
    fun clearCache(): CoverCacheClearResult = clearDirectories(listOf(coverCacheDirectory))

    /** Clears all temporary cover/image downloads without touching library metadata or custom artwork. */
    fun clearAllTemporaryImageCaches(): CoverCacheClearResult =
        clearDirectories(listOf(coverCacheDirectory, remoteImageCacheDirectory))

    fun isManagedCoverCachePath(path: String?): Boolean =
        CoverCachePolicy.isPathInside(path, coverCacheDirectory)

    fun isMissingManagedCover(path: String?): Boolean =
        isManagedCoverCachePath(path) && !path.isNullOrBlank() && !isUsableCoverFile(File(path))

    fun findCachedCoverPath(
        serial: String?,
        styleOverride: Int? = null,
        ignoreDisabled: Boolean = false
    ): String? {
        val style = resolveCoverArtStyle(styleOverride)
        if (!ignoreDisabled && style == AppPreferences.COVER_ART_STYLE_DISABLED) {
            return null
        }
        val normalizedSerial = normalizeSerial(serial)
        if (normalizedSerial == null) {
            Log.d(TAG, "No serial provided")
            return null
        }
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
        preferredFiles.filter(File::exists).forEach { file ->
            if (!isUsableCoverFile(file)) {
                Log.w(TAG, "Removing invalid cached cover: ${file.absolutePath}")
                runCatching { file.delete() }
            }
        }
        val found = preferredFiles.firstOrNull(::isUsableCoverFile)
        Log.d(TAG, "Cached cover for $normalizedSerial (style=$style): ${if (found != null) "FOUND" else "NOT FOUND"}")
        return found?.absolutePath
    }

    fun findCachedCoverUri(
        serial: String?,
        styleOverride: Int? = null,
        ignoreDisabled: Boolean = false
    ): String? {
        return findCachedCoverPath(serial, styleOverride, ignoreDisabled)
    }

    fun downloadCover(
        serial: String?,
        styleOverride: Int? = null,
        ignoreDisabled: Boolean = false
    ): String? {
        val style = resolveCoverArtStyle(styleOverride)
        if (!ignoreDisabled && style == AppPreferences.COVER_ART_STYLE_DISABLED) {
            Log.d(TAG, "Cover download skipped: cover art style is disabled")
            return null
        }
        val normalizedSerial = normalizeSerial(serial)
        if (normalizedSerial == null) {
            Log.w(TAG, "Cannot download cover: invalid serial '$serial'")
            return null
        }
        val coverBaseUrl = resolveCoverBaseUrl(style)
        val targetExtension = if (style == AppPreferences.COVER_ART_STYLE_3D) "png" else "jpg"

        Log.d(TAG, "========== COVER DOWNLOAD START ==========")
        Log.d(TAG, "Original serial: $serial")
        Log.d(TAG, "Normalized serial: $normalizedSerial")
        Log.d(TAG, "Cover base URL: $coverBaseUrl")
        Log.d(TAG, "Cover style: $style")

        val coverFile = File(cacheDirectory, cacheFileName(normalizedSerial, style, targetExtension))
        if (isUsableCoverFile(coverFile)) {
            Log.d(TAG, "Cover already exists: ${coverFile.absolutePath}")
            Log.d(TAG, "========== COVER DOWNLOAD END (CACHED) ==========")
            return coverFile.absolutePath
        }
        if (coverFile.exists()) coverFile.delete()

        val missFile = File(cacheDirectory, cacheMissFileName(normalizedSerial, style))
        if (missFile.exists() && System.currentTimeMillis() - missFile.lastModified() < MISS_TTL_MS) {
            Log.d(TAG, "Recent miss marker found, skipping (age: ${System.currentTimeMillis() - missFile.lastModified()}ms)")
            Log.d(TAG, "========== COVER DOWNLOAD END (MISS CACHED) ==========")
            return null
        }

        if (normalizedSerial.startsWith("NM") && normalizedSerial.length == 7) {
            return downloadArcadeCover(normalizedSerial, style, missFile)
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
                            if (!altCoverFile.renameTo(finalFile)) {
                                runCatching { altCoverFile.copyTo(finalFile, overwrite = true) }
                                altCoverFile.delete()
                            }
                        }
                        result = finalFile.takeIf(::isUsableCoverFile)?.absolutePath
                        break
                    }
                }
            }
        }

        Log.d(TAG, "Download result: ${if (result != null) "SUCCESS" else "FAILED"}")
        Log.d(TAG, "========== COVER DOWNLOAD END ==========")
        return result
    }

    private fun downloadArcadeCover(serial: String, style: Int, missFile: File): String? {
        val configuredBase = AppPreferences(context).getArcadeCoverDownloadBaseUrlSync()
            ?.trim()?.trimEnd('/')?.takeIf { it.isNotBlank() }
        val baseUrl = configuredBase ?: DEFAULT_ARCADE_COVER_BASE_URL
        val candidates = buildList {
            // Try stable NM ID directly from default/custom arcade repo first, fallback to mapped asset.
            add("$baseUrl/$serial.png")
            add("$baseUrl/$serial.jpg")
            ARCADE_COVER_ASSETS[serial]?.let { asset ->
                val fallbackUrl = "${asset.baseUrl}/${Uri.encode(asset.fileName)}"
                if (fallbackUrl !in this) add(fallbackUrl)
            }
        }.distinct()

        for (url in candidates) {
            val extension = url.substringBefore('?').substringAfterLast('.', "png").lowercase()
            val target = File(cacheDirectory, cacheFileName(serial, style, extension))
            downloadFromUrl(url, target, missFile, "Arcade")?.let { return it }
        }
        return null
    }

    fun buildPublicCoverUrl(
        serial: String?,
        styleOverride: Int? = AppPreferences.COVER_ART_STYLE_DEFAULT
    ): String? {
        val normalizedSerial = normalizeSerial(serial) ?: return null
        if (normalizedSerial.startsWith("NM") && normalizedSerial.length == 7) {
            val configuredBase = AppPreferences(context).getArcadeCoverDownloadBaseUrlSync()
                ?.trim()?.trimEnd('/')?.takeIf { it.isNotBlank() }
            val baseUrl = configuredBase ?: DEFAULT_ARCADE_COVER_BASE_URL
            return "$baseUrl/$normalizedSerial.png"
        }
        val style = resolveCoverArtStyle(styleOverride)
        val baseUrl = if (style == AppPreferences.COVER_ART_STYLE_3D) {
            DEFAULT_COVER_3D_BASE_URL
        } else {
            DEFAULT_COVER_BASE_URL
        }
        val extension = if (style == AppPreferences.COVER_ART_STYLE_3D) "png" else "jpg"
        return "$baseUrl/$normalizedSerial.$extension"
    }

    private fun downloadFromUrl(
        urlString: String,
        coverFile: File,
        missFile: File,
        sourceName: String
    ): String? {
        if (isUsableCoverFile(coverFile)) {
            return coverFile.absolutePath
        }
        if (coverFile.exists()) coverFile.delete()

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

            // Chunked HTTP responses legitimately report -1. Only an explicitly empty body is invalid.
            if (contentLength == 0) {
                Log.w(TAG, "$sourceName: Invalid content length")
                return null
            }

            val tempFile = File.createTempFile("${coverFile.name}.", ".tmp", cacheDirectory)
            try {
                connection.inputStream.use { input ->
                    tempFile.outputStream().use { output ->
                        val copied = input.copyTo(output)
                        Log.d(TAG, "$sourceName: Copied $copied bytes")
                    }
                }

                if (!isUsableCoverFile(tempFile)) {
                    Log.w(TAG, "$sourceName: Downloaded file is not a valid image")
                    return null
                }

                if (coverFile.exists()) coverFile.delete()
                if (!tempFile.renameTo(coverFile)) {
                    tempFile.copyTo(coverFile, overwrite = true)
                }
                if (!isUsableCoverFile(coverFile)) {
                    coverFile.delete()
                    return null
                }
                missFile.delete()

                Log.d(TAG, "$sourceName: SUCCESS - ${coverFile.absolutePath}")
                coverFile.absolutePath
            } finally {
                tempFile.delete()
            }
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

    private fun resolveCoverBaseUrl(style: Int = resolveCoverArtStyle()): String {
        val preferences = AppPreferences(context)
        val configuredUrls = preferences.getCoverDownloadBaseUrlSync()
            ?.split(Regex("\\s+"))
            ?.map { it.trim().trimEnd('/') }
            ?.filter { it.isNotBlank() }
            .orEmpty()
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

    private fun resolveCoverArtStyle(styleOverride: Int? = null): Int {
        return styleOverride ?: AppPreferences(context).getCoverArtStyleSync()
    }

    private fun cacheFileName(serial: String, style: Int, extension: String): String {
        return if (style == AppPreferences.COVER_ART_STYLE_3D) {
            "${serial}_3d.$extension"
        } else {
            "$serial.$extension"
        }
    }

    private fun cacheMissFileName(serial: String, style: Int): String {
        val versionedSerial = if (serial.startsWith("NM") && serial.length == 7) {
            "${serial}_arcade_v$ARCADE_COVER_SOURCE_REVISION"
        } else {
            serial
        }
        return if (style == AppPreferences.COVER_ART_STYLE_3D) {
            "${versionedSerial}_3d.miss"
        } else {
            "$versionedSerial.miss"
        }
    }

    private fun isUsableCoverFile(file: File): Boolean {
        if (!file.isFile || file.length() <= 0L) return false
        return runCatching {
            val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, options)
            options.outWidth > 0 && options.outHeight > 0
        }.getOrDefault(false)
    }

    private fun clearDirectories(directories: List<File>): CoverCacheClearResult {
        var deletedFiles = 0
        var freedBytes = 0L
        var failedFiles = 0

        directories.distinctBy { it.absolutePath }.forEach { directory ->
            if (!directory.exists()) return@forEach
            val root = runCatching { directory.canonicalFile }.getOrNull() ?: run {
                failedFiles++
                return@forEach
            }
            directory.walkBottomUp().forEach { entry ->
                if (entry == directory) return@forEach
                val safeEntry = runCatching { entry.canonicalFile }.getOrNull()
                if (safeEntry == null || !CoverCachePolicy.isFileInside(safeEntry, root)) {
                    failedFiles++
                    return@forEach
                }
                if (entry.isFile) {
                    val size = entry.length()
                    if (runCatching { entry.delete() }.getOrDefault(false)) {
                        deletedFiles++
                        freedBytes += size
                    } else {
                        failedFiles++
                    }
                } else if (entry.isDirectory) {
                    // Remove only now-empty cache subdirectories; the two cache roots remain available.
                    runCatching { entry.delete() }
                }
            }
        }
        return CoverCacheClearResult(deletedFiles, freedBytes, failedFiles)
    }

}

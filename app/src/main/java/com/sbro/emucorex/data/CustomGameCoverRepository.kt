package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import android.webkit.MimeTypeMap
import java.io.File
import java.security.MessageDigest
import java.util.Locale

class CustomGameCoverRepository(context: Context) {

    private val appContext = context.applicationContext
    private val directory by lazy {
        File(appContext.filesDir, "library/custom-game-covers").apply { mkdirs() }
    }

    fun findCustomCoverPath(gamePath: String): String? {
        val key = stableKey(gamePath)
        return directory
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.name.startsWith("$key.") && it.length() > 0L }
            .maxByOrNull(File::lastModified)
            ?.absolutePath
    }

    fun saveCustomCover(gamePath: String, sourceUri: Uri): String? {
        val key = stableKey(gamePath)
        val extension = resolveExtension(sourceUri)
        val targetFile = File(directory, "$key.$extension")
        val tempFile = File(directory, "$key.$extension.tmp")

        return runCatching {
            directory.mkdirs()
            appContext.contentResolver.openInputStream(sourceUri)?.use { input ->
                tempFile.outputStream().use { output ->
                    input.copyTo(output)
                }
            } ?: return null

            if (tempFile.length() <= 0L) {
                tempFile.delete()
                return null
            }

            existingFiles(key).forEach { existing ->
                if (existing.absolutePath != targetFile.absolutePath) {
                    existing.delete()
                }
            }
            if (targetFile.exists()) {
                targetFile.delete()
            }
            if (!tempFile.renameTo(targetFile)) {
                tempFile.copyTo(targetFile, overwrite = true)
                tempFile.delete()
            }

            targetFile.absolutePath
        }.getOrNull()
    }

    fun isCustomCoverPath(coverPath: String?): Boolean {
        if (coverPath.isNullOrBlank()) return false
        return runCatching {
            File(coverPath).absolutePath.startsWith(directory.absolutePath, ignoreCase = true)
        }.getOrDefault(false)
    }

    private fun existingFiles(key: String): List<File> {
        return directory
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.name.startsWith("$key.") }
    }

    private fun resolveExtension(sourceUri: Uri): String {
        val mimeType = appContext.contentResolver.getType(sourceUri)?.lowercase(Locale.ROOT)
        val mimeExtension = mimeType
            ?.substringAfter('/', "")
            ?.substringBefore('+')
            ?.let { MimeTypeMap.getSingleton().getExtensionFromMimeType("image/$it") ?: it }
            ?.takeIf { it.matches(Regex("[a-z0-9]{2,5}")) }
        if (mimeExtension != null) {
            return if (mimeExtension == "jpeg") "jpg" else mimeExtension
        }

        val pathExtension = sourceUri.lastPathSegment
            ?.substringAfterLast('.', "")
            ?.lowercase(Locale.ROOT)
            ?.takeIf { it.matches(Regex("[a-z0-9]{2,5}")) }
        return if (pathExtension == "jpeg") "jpg" else pathExtension ?: "img"
    }

    private fun stableKey(gamePath: String): String {
        val digest = MessageDigest.getInstance("SHA-1")
        return digest.digest(gamePath.toByteArray()).joinToString("") { "%02x".format(it) }
    }
}

package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import android.webkit.MimeTypeMap
import java.io.File
import java.security.MessageDigest
import java.util.Locale

class CustomGameCoverRepository(context: Context) {

    private companion object {
        // Home and Settings use separate repository instances for the same files.
        val filesLock = Any()
        val savedCoverName = Regex("[a-f0-9]{40}\\..+")
    }

    private val appContext = context.applicationContext
    private val directory by lazy {
        File(appContext.filesDir, "library/custom-game-covers").apply { mkdirs() }
    }

    fun findCustomCoverPath(gamePath: String): String? = synchronized(filesLock) {
        val key = stableKey(gamePath)
        directory
            .listFiles()
            .orEmpty()
            .filter {
                it.isFile &&
                    it.name.startsWith("$key.") &&
                    !it.name.endsWith(".tmp") &&
                    it.length() > 0L
            }
            .maxByOrNull(File::lastModified)
            ?.absolutePath
    }

    fun saveCustomCover(gamePath: String, sourceUri: Uri): String? = synchronized(filesLock) {
        val key = stableKey(gamePath)
        val extension = resolveExtension(sourceUri)
        val targetFile = File(directory, "$key.$extension")
        val tempFile = File(directory, "$key.$extension.tmp")

        runCatching {
            directory.mkdirs()
            if (tempFile.exists()) {
                tempFile.delete()
            }
            openSourceStream(sourceUri)?.use { input ->
                tempFile.outputStream().use { output ->
                    input.copyTo(output)
                }
            } ?: return@runCatching null

            if (tempFile.length() <= 0L) {
                tempFile.delete()
                return@runCatching null
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
        }.onFailure {
            tempFile.delete()
        }.getOrNull()
    }

    fun removeCustomCover(gamePath: String): Boolean = synchronized(filesLock) {
        val files = directory.listFiles() ?: return@synchronized !directory.exists()
        val prefix = "${stableKey(gamePath)}."
        deleteCoverFiles(files.filter { it.isFile && it.name.startsWith(prefix) && !it.name.endsWith(".tmp") })
    }

    fun resetAllCustomCovers(): Boolean = synchronized(filesLock) {
        val files = directory.listFiles() ?: return@synchronized !directory.exists()
        deleteCoverFiles(files.filter {
            it.isFile && savedCoverName.matches(it.name) && !it.name.endsWith(".tmp")
        })
    }

    private fun deleteCoverFiles(files: List<File>): Boolean =
        files.map { file -> runCatching { !file.exists() || file.delete() }.getOrDefault(false) }.all { it }

    fun isCustomCoverPath(coverPath: String?): Boolean {
        if (coverPath.isNullOrBlank()) return false
        return runCatching {
            File(coverPath).absolutePath.startsWith(directory.absolutePath, ignoreCase = true)
        }.getOrDefault(false)
    }

    fun isMissingCustomCover(coverPath: String?): Boolean =
        isCustomCoverPath(coverPath) && !File(coverPath!!).isFile

    private fun existingFiles(key: String): List<File> {
        return directory
            .listFiles()
            .orEmpty()
            .filter { it.isFile && it.name.startsWith("$key.") && !it.name.endsWith(".tmp") }
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

    private fun openSourceStream(sourceUri: Uri) =
        appContext.contentResolver.openInputStream(sourceUri)
            ?: appContext.contentResolver.openAssetFileDescriptor(sourceUri, "r")?.createInputStream()

    private fun stableKey(gamePath: String): String {
        val digest = MessageDigest.getInstance("SHA-1")
        return digest.digest(gamePath.toByteArray()).joinToString("") { "%02x".format(it) }
    }
}

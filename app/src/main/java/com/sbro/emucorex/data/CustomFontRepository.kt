package com.sbro.emucorex.data

import android.content.Context
import android.graphics.Typeface
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import kotlin.math.max
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

data class InstalledCustomFont(
    val file: File,
    val displayName: String
)

class CustomFontRepository(context: Context) {
    private val appContext = context.applicationContext
    private val customizationDirectory = File(appContext.filesDir, DIRECTORY_NAME)
    private val fontFile = File(customizationDirectory, FILE_NAME)

    suspend fun install(uri: Uri): Result<InstalledCustomFont> = withContext(Dispatchers.IO) {
        runCatching {
            val displayName = readDisplayName(uri)
            require(isSupported(uri, displayName)) { "Unsupported font format" }
            validateDeclaredSize(uri)
            customizationDirectory.mkdirs()

            val previousModified = fontFile.lastModified()
            val temporaryFile = File(customizationDirectory, "$FILE_NAME.tmp")
            temporaryFile.delete()
            try {
                appContext.contentResolver.openInputStream(uri).use { input ->
                    requireNotNull(input) { "Unable to open selected font" }
                    copyValidated(input, temporaryFile)
                }
                validateFont(temporaryFile)
                if (fontFile.exists()) check(fontFile.delete()) { "Unable to replace custom font" }
                check(temporaryFile.renameTo(fontFile)) { "Unable to save custom font" }
                fontFile.setLastModified(max(System.currentTimeMillis(), previousModified + 1L))
                InstalledCustomFont(fontFile, displayName.ifBlank { DEFAULT_DISPLAY_NAME })
            } finally {
                temporaryFile.delete()
            }
        }
    }

    /** Installs an already validated local font file (a Store font) into the same slot as [install]. */
    suspend fun installFromFile(source: File, displayName: String): Result<InstalledCustomFont> = withContext(Dispatchers.IO) {
        runCatching {
            require(source.isFile && source.length() in 1..MAX_FONT_BYTES) { "Font file is missing or too large" }
            customizationDirectory.mkdirs()

            val previousModified = fontFile.lastModified()
            val temporaryFile = File(customizationDirectory, "$FILE_NAME.tmp")
            temporaryFile.delete()
            try {
                source.inputStream().use { copyValidated(it, temporaryFile) }
                validateFont(temporaryFile)
                if (fontFile.exists()) check(fontFile.delete()) { "Unable to replace custom font" }
                check(temporaryFile.renameTo(fontFile)) { "Unable to save custom font" }
                fontFile.setLastModified(max(System.currentTimeMillis(), previousModified + 1L))
                InstalledCustomFont(fontFile, displayName.trim().ifBlank { DEFAULT_DISPLAY_NAME })
            } finally {
                temporaryFile.delete()
            }
        }
    }

    suspend fun restoreFromBackup(input: InputStream): Result<File> = withContext(Dispatchers.IO) {
        runCatching {
            customizationDirectory.mkdirs()
            val previousModified = fontFile.lastModified()
            val temporaryFile = File(customizationDirectory, "$FILE_NAME.restore.tmp")
            temporaryFile.delete()
            try {
                copyValidated(input, temporaryFile)
                validateFont(temporaryFile)
                if (fontFile.exists()) check(fontFile.delete()) { "Unable to replace custom font" }
                check(temporaryFile.renameTo(fontFile)) { "Unable to restore custom font" }
                fontFile.setLastModified(max(System.currentTimeMillis(), previousModified + 1L))
                fontFile
            } finally {
                temporaryFile.delete()
            }
        }
    }

    fun installedFile(): File? = fontFile.takeIf { it.isFile && it.length() > 0L }

    fun clear() {
        fontFile.delete()
        File(customizationDirectory, "$FILE_NAME.tmp").delete()
        File(customizationDirectory, "$FILE_NAME.restore.tmp").delete()
    }

    private fun copyValidated(input: InputStream, target: File) {
        FileOutputStream(target).use { output ->
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var totalBytes = 0L
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                totalBytes += read
                require(totalBytes <= MAX_FONT_BYTES) { "Font file is too large" }
                output.write(buffer, 0, read)
            }
            output.fd.sync()
        }
        require(target.length() > 0L) { "Font file is empty" }
    }

    private fun validateFont(file: File) {
        val typeface = Typeface.Builder(file).build()
        require(typeface != null) { "Selected font cannot be decoded" }
    }

    private fun isSupported(uri: Uri, displayName: String): Boolean {
        val mime = appContext.contentResolver.getType(uri).orEmpty().lowercase()
        val name = displayName.lowercase()
        return mime.startsWith("font/") ||
            mime == "application/x-font-ttf" ||
            mime == "application/x-font-opentype" ||
            mime == "application/vnd.ms-opentype" ||
            name.endsWith(".ttf") ||
            name.endsWith(".otf")
    }

    private fun readDisplayName(uri: Uri): String = appContext.contentResolver.query(
        uri,
        arrayOf(OpenableColumns.DISPLAY_NAME),
        null,
        null,
        null
    )?.use { cursor ->
        if (cursor.moveToFirst()) cursor.getString(0).orEmpty() else ""
    }.orEmpty()

    private fun validateDeclaredSize(uri: Uri) {
        appContext.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.SIZE),
            null,
            null,
            null
        )?.use { cursor ->
            if (cursor.moveToFirst() && !cursor.isNull(0)) {
                require(cursor.getLong(0) in 1..MAX_FONT_BYTES) { "Font file is too large or empty" }
            }
        }
    }

    companion object {
        const val BACKUP_ENTRY = "customization/app-font"
        const val DEFAULT_DISPLAY_NAME = "Custom font"
        private const val DIRECTORY_NAME = "customization"
        private const val FILE_NAME = "app_font"
        private const val MAX_FONT_BYTES = 20L * 1024L * 1024L
    }
}

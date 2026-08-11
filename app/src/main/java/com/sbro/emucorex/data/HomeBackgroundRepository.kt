package com.sbro.emucorex.data

import android.content.Context
import android.graphics.ImageDecoder
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import kotlin.math.max
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class HomeBackgroundRepository(context: Context) {
    private val appContext = context.applicationContext
    private val backgroundDirectory = File(appContext.filesDir, DIRECTORY_NAME)

    suspend fun install(uri: Uri): Result<HomeBackgroundType> = withContext(Dispatchers.IO) {
        runCatching {
            val type = resolveType(uri)
            require(type != HomeBackgroundType.NONE) { "Unsupported background format" }
            validateDeclaredSize(uri)

            backgroundDirectory.mkdirs()
            val previousModified = installedBackground()?.second?.lastModified() ?: 0L
            val temporaryFile = File(backgroundDirectory, "$FILE_NAME.tmp")
            temporaryFile.delete()
            try {
                appContext.contentResolver.openInputStream(uri).use { input ->
                    requireNotNull(input) { "Unable to open selected background" }
                    FileOutputStream(temporaryFile).use { output ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                        var totalBytes = 0L
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            totalBytes += read
                            require(totalBytes <= MAX_BACKGROUND_BYTES) { "Background file is too large" }
                            output.write(buffer, 0, read)
                        }
                        output.fd.sync()
                    }
                }
                validateContent(temporaryFile, type)
                val targetFile = fileFor(type)
                backgroundDirectory.listFiles()?.forEach { file ->
                    if (file != temporaryFile && file != targetFile) file.delete()
                }
                if (targetFile.exists()) targetFile.delete()
                check(temporaryFile.renameTo(targetFile)) { "Unable to save selected background" }
                // Fixed private file names are intentional; force a monotonic identity for UI caches.
                targetFile.setLastModified(max(System.currentTimeMillis(), previousModified + 1L))
                type
            } finally {
                temporaryFile.delete()
            }
        }
    }

    fun fileFor(type: HomeBackgroundType): File =
        File(backgroundDirectory, "$FILE_NAME.${type.fileExtension}")

    fun existingFile(type: HomeBackgroundType): File? =
        fileFor(type).takeIf { type.isCustomMedia && it.isFile && it.length() > 0L }

    fun installedBackground(): Pair<HomeBackgroundType, File>? =
        listOf(HomeBackgroundType.IMAGE, HomeBackgroundType.GIF, HomeBackgroundType.VIDEO)
            .firstNotNullOfOrNull { type -> existingFile(type)?.let { type to it } }

    suspend fun restoreFromBackup(
        type: HomeBackgroundType,
        input: InputStream
    ): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            require(type.isCustomMedia) { "Invalid background backup type" }
            backgroundDirectory.mkdirs()
            val temporaryFile = File(backgroundDirectory, "$FILE_NAME.restore.tmp")
            temporaryFile.delete()
            try {
                FileOutputStream(temporaryFile).use { output ->
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                    var totalBytes = 0L
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        totalBytes += read
                        require(totalBytes <= MAX_BACKGROUND_BYTES) { "Background file is too large" }
                        output.write(buffer, 0, read)
                    }
                    output.fd.sync()
                }
                validateContent(temporaryFile, type)
                clear()
                val targetFile = fileFor(type)
                check(temporaryFile.renameTo(targetFile)) { "Unable to restore background" }
            } finally {
                temporaryFile.delete()
            }
        }
    }

    fun clear() {
        backgroundDirectory.listFiles()?.forEach(File::delete)
    }

    private fun resolveType(uri: Uri): HomeBackgroundType {
        val mimeType = appContext.contentResolver.getType(uri).orEmpty().lowercase()
        val displayName = appContext.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.DISPLAY_NAME),
            null,
            null,
            null
        )?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0).orEmpty() else ""
        }.orEmpty().lowercase()

        return when {
            mimeType == "image/gif" || displayName.endsWith(".gif") -> HomeBackgroundType.GIF
            mimeType.startsWith("image/") -> HomeBackgroundType.IMAGE
            mimeType.startsWith("video/") -> HomeBackgroundType.VIDEO
            else -> HomeBackgroundType.NONE
        }
    }

    private fun validateDeclaredSize(uri: Uri) {
        appContext.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.SIZE),
            null,
            null,
            null
        )?.use { cursor ->
            if (cursor.moveToFirst() && !cursor.isNull(0)) {
                require(cursor.getLong(0) <= MAX_BACKGROUND_BYTES) { "Background file is too large" }
            }
        }
    }

    private fun validateContent(file: File, type: HomeBackgroundType) {
        when (type) {
            HomeBackgroundType.IMAGE,
            HomeBackgroundType.GIF -> {
                val drawable = ImageDecoder.decodeDrawable(ImageDecoder.createSource(file)) { decoder, info, _ ->
                    var sampleSize = 1
                    while (max(info.size.width, info.size.height) / sampleSize > MAX_VALIDATION_DIMENSION) {
                        sampleSize *= 2
                    }
                    decoder.setTargetSampleSize(sampleSize)
                }
                require(drawable.intrinsicWidth > 0 && drawable.intrinsicHeight > 0) {
                    "Selected image cannot be decoded"
                }
            }
            HomeBackgroundType.VIDEO -> {
                val retriever = MediaMetadataRetriever()
                try {
                    retriever.setDataSource(file.absolutePath)
                    val hasVideo = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO)
                    require(hasVideo == "yes") { "Selected file does not contain video" }
                    val previewFrame = retriever.getFrameAtTime(
                        0L,
                        MediaMetadataRetriever.OPTION_CLOSEST_SYNC
                    )
                    requireNotNull(previewFrame) { "Selected video cannot be decoded" }
                    previewFrame.recycle()
                } finally {
                    retriever.release()
                }
            }
            HomeBackgroundType.NONE,
            HomeBackgroundType.BUILT_IN -> error("Unsupported background format")
        }
    }

    private val HomeBackgroundType.fileExtension: String
        get() = when (this) {
            HomeBackgroundType.IMAGE -> "image"
            HomeBackgroundType.GIF -> "gif"
            HomeBackgroundType.VIDEO -> "video"
            HomeBackgroundType.NONE -> "none"
            HomeBackgroundType.BUILT_IN -> "built_in"
        }

    private val HomeBackgroundType.isCustomMedia: Boolean
        get() = this == HomeBackgroundType.IMAGE ||
            this == HomeBackgroundType.GIF ||
            this == HomeBackgroundType.VIDEO

    private companion object {
        const val DIRECTORY_NAME = "customization"
        const val FILE_NAME = "home_background"
        const val MAX_BACKGROUND_BYTES = 250L * 1024L * 1024L
        const val MAX_VALIDATION_DIMENSION = 2048
    }
}

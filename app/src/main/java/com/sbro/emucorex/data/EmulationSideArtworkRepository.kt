package com.sbro.emucorex.data

import android.content.Context
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import kotlin.math.max
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class EmulationSideArtworkRepository(context: Context) {
    private val appContext = context.applicationContext
    private val directory = File(appContext.filesDir, DIRECTORY_NAME)

    fun customFile(): File = File(directory, CUSTOM_FILE_NAME)

    fun existingCustomFile(): File? = customFile().takeIf { it.isFile && it.length() > 0L }

    suspend fun install(uri: Uri): Result<File> = withContext(Dispatchers.IO) {
        runCatching {
            require(appContext.contentResolver.getType(uri).orEmpty().startsWith("image/")) {
                "Selected file is not an image"
            }
            validateDeclaredSize(uri)
            directory.mkdirs()
            val temporary = File(directory, "$CUSTOM_FILE_NAME.tmp")
            temporary.delete()
            try {
                appContext.contentResolver.openInputStream(uri).use { input ->
                    requireNotNull(input) { "Unable to open selected image" }
                    FileOutputStream(temporary).use { output ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                        var total = 0L
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            total += read
                            require(total <= MAX_BYTES) { "Selected image is too large" }
                            output.write(buffer, 0, read)
                        }
                        output.fd.sync()
                    }
                }
                val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(temporary.absolutePath, bounds)
                require(bounds.outWidth > 0 && bounds.outHeight > 0) { "Selected image cannot be decoded" }
                require(max(bounds.outWidth, bounds.outHeight) <= MAX_DIMENSION) {
                    "Selected image dimensions are too large"
                }
                require(bounds.outWidth.toFloat() / bounds.outHeight >= MIN_ASPECT_RATIO) {
                    "A wide landscape image is required"
                }
                val target = customFile()
                if (target.exists()) check(target.delete()) { "Unable to replace custom artwork" }
                check(temporary.renameTo(target)) { "Unable to save custom artwork" }
                target
            } finally {
                temporary.delete()
            }
        }
    }

    fun clear() {
        directory.listFiles()?.forEach(File::delete)
    }

    private fun validateDeclaredSize(uri: Uri) {
        appContext.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst() && !cursor.isNull(0)) {
                    require(cursor.getLong(0) <= MAX_BYTES) { "Selected image is too large" }
                }
            }
    }

    private companion object {
        const val DIRECTORY_NAME = "emulation_side_artwork"
        const val CUSTOM_FILE_NAME = "custom_side_artwork.image"
        const val MAX_BYTES = 32L * 1024L * 1024L
        const val MAX_DIMENSION = 8192
        const val MIN_ASPECT_RATIO = 1.25f
    }
}

package com.sbro.emucorex.data

import android.content.Context
import android.media.MediaMetadataRetriever
import android.media.SoundPool
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

data class InstalledUiSound(
    val file: File,
    /** Null when the provider gave no usable name; the UI then shows a localized generic label. */
    val displayName: String?
)

/**
 * Stores user-picked audio files privately in app storage. The selected document is copied once, so
 * playback keeps working after reboots or when the source provider revokes access; no persistable
 * URI permission has to be held.
 */
class UiSoundRepository(context: Context) {
    private val appContext = context.applicationContext
    // Own directory: HomeBackgroundRepository/CustomFontRepository manage "customization".
    private val directory = File(appContext.filesDir, DIRECTORY_NAME)

    fun installedFile(kind: UiSoundKind): File? = fileFor(kind).takeIf { it.isFile && it.length() > 0L }

    suspend fun install(kind: UiSoundKind, uri: Uri): Result<InstalledUiSound> = withContext(Dispatchers.IO) {
        runCatchingNonCancellation {
            val displayName = readDisplayName(uri)
            val maxBytes = maxBytes(kind)
            validateDeclaredSize(uri, maxBytes)
            directory.mkdirs()
            val target = fileFor(kind)
            val temporary = File(directory, "${target.name}.tmp")
            temporary.delete()
            try {
                appContext.contentResolver.openInputStream(uri).use { input ->
                    requireNotNull(input) { "Unable to open selected audio" }
                    copyValidated(input, temporary, maxBytes)
                }
                validateAudio(kind, temporary)
                if (target.exists()) check(target.delete()) { "Unable to replace custom sound" }
                check(temporary.renameTo(target)) { "Unable to save custom sound" }
                InstalledUiSound(target, displayName.trim().takeIf(String::isNotEmpty))
            } finally {
                temporary.delete()
            }
        }
    }

    suspend fun restoreFromBackup(kind: UiSoundKind, input: InputStream): Result<File> = withContext(Dispatchers.IO) {
        runCatchingNonCancellation {
            directory.mkdirs()
            val target = fileFor(kind)
            val temporary = File(directory, "${target.name}.restore.tmp")
            temporary.delete()
            try {
                copyValidated(input, temporary, maxBytes(kind))
                validateAudio(kind, temporary)
                if (target.exists()) check(target.delete()) { "Unable to replace custom sound" }
                check(temporary.renameTo(target)) { "Unable to restore custom sound" }
                target
            } finally {
                temporary.delete()
            }
        }
    }

    fun clear(kind: UiSoundKind) {
        val target = fileFor(kind)
        target.delete()
        File(directory, "${target.name}.tmp").delete()
        File(directory, "${target.name}.restore.tmp").delete()
    }

    fun clearAll() = UiSoundKind.entries.forEach(::clear)

    private fun fileFor(kind: UiSoundKind) = File(directory, "ui_sound_${kind.id}")

    private fun maxBytes(kind: UiSoundKind) = if (kind.isShortSample) MAX_SHORT_BYTES else MAX_MUSIC_BYTES

    private fun copyValidated(input: InputStream, target: File, maxBytes: Long) {
        FileOutputStream(target).use { output ->
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var total = 0L
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                total += read
                require(total <= maxBytes) { "Audio file is too large" }
                output.write(buffer, 0, read)
            }
            output.fd.sync()
        }
        require(target.length() > 0L) { "Audio file is empty" }
    }

    private fun validateAudio(kind: UiSoundKind, file: File) {
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(file.absolutePath)
            require(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO) == "yes") {
                "Selected file has no audio track"
            }
            val durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                ?.toLongOrNull()
            if (kind.isShortSample) {
                if (durationMs != null) {
                    require(durationMs <= MAX_SHORT_DURATION_MS) { "Sound effect is too long" }
                }
                requireSoundPoolCanLoad(file)
            }
        } finally {
            runCatching { retriever.release() }
        }
    }

    /** SoundPool silently drops samples it cannot decode (or that exceed its memory limit). */
    private fun requireSoundPoolCanLoad(file: File) {
        val pool = SoundPool.Builder().setMaxStreams(1).build()
        try {
            val latch = CountDownLatch(1)
            var status = -1
            pool.setOnLoadCompleteListener { _, _, loadStatus ->
                status = loadStatus
                latch.countDown()
            }
            check(pool.load(file.absolutePath, 1) != 0) { "Sound effect cannot be loaded" }
            check(latch.await(LOAD_TIMEOUT_SECONDS, TimeUnit.SECONDS)) { "Sound effect load timed out" }
            check(status == 0) { "Sound effect cannot be decoded" }
        } finally {
            pool.release()
        }
    }

    private inline fun <T> runCatchingNonCancellation(block: () -> T): Result<T> = try {
        Result.success(block())
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (error: Exception) {
        Result.failure(error)
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

    private fun validateDeclaredSize(uri: Uri, maxBytes: Long) {
        appContext.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.SIZE),
            null,
            null,
            null
        )?.use { cursor ->
            if (cursor.moveToFirst() && !cursor.isNull(0)) {
                require(cursor.getLong(0) in 1..maxBytes) { "Audio file is too large or empty" }
            }
        }
    }

    companion object {
        fun backupEntry(kind: UiSoundKind) = "customization/ui-sound-${kind.id}"
        fun kindForBackupEntry(name: String): UiSoundKind? =
            UiSoundKind.entries.firstOrNull { backupEntry(it) == name }

        private const val DIRECTORY_NAME = "ui_sounds"
        private const val LOAD_TIMEOUT_SECONDS = 5L
        private const val MAX_SHORT_BYTES = 2L * 1024L * 1024L
        private const val MAX_MUSIC_BYTES = 40L * 1024L * 1024L
        private const val MAX_SHORT_DURATION_MS = 5_000L
    }
}

package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.RandomAccessFile
import java.security.MessageDigest
import java.util.Locale
import kotlin.math.min

object ImageConversionManager {
    private const val CHD_HEADER_SIZE = 124
    private const val CHD_VERSION_V5 = 5
    private const val DEFAULT_HUNK_BYTES = 0x80000
    private const val DEFAULT_UNIT_BYTES = 2048
    private const val IO_BUFFER_SIZE = 64 * 1024
    private const val MAP_ENTRY_BYTES = 4L

    val libraryFormats = listOf("ISO", "BIN", "IMG", "MDF", "GZ", "CSO", "ZSO", "CHD", "ELF", "ACGAME")
    val recommendedFormats = listOf("ISO", "CHD", "BIN")
    val isoMimeTypes = arrayOf(
        "application/octet-stream",
        "application/x-iso9660-image",
        "application/x-cd-image",
        "application/x-raw-disk-image"
    )

    fun isIsoToChdAvailable(): Boolean = true

    fun buildOutputName(sourceDisplayName: String): String {
        val trimmed = sourceDisplayName.trim().ifBlank { "game.iso" }
        return trimmed.replace(Regex("\\.iso$", RegexOption.IGNORE_CASE), ".chd")
            .let { if (it.equals(trimmed, ignoreCase = true)) "$trimmed.chd" else it }
    }

    suspend fun convertIsoToChd(
        context: Context,
        sourceUri: Uri
    ): ConversionResult = withContext(Dispatchers.IO) {
        val displayName = DocumentFile.fromSingleUri(context, sourceUri)?.name
            ?: sourceUri.lastPathSegment
            ?: "game.iso"

        if (!displayName.lowercase(Locale.US).endsWith(".iso")) {
            return@withContext ConversionResult(success = false, message = "not_iso")
        }

        val cacheDir = File(context.cacheDir, "image-converter").apply { mkdirs() }
        val stagedIso = File(cacheDir, sanitizeFileName(displayName))
        val outputPath = stagedIso.absolutePath.replace(
            Regex("\\.iso$", RegexOption.IGNORE_CASE),
            ".chd"
        )
        val outputFile = File(outputPath)

        try {
            if (stagedIso.exists()) {
                stagedIso.delete()
            }
            if (outputFile.exists()) {
                outputFile.delete()
            }

            context.contentResolver.openInputStream(sourceUri)?.use { input ->
                FileOutputStream(stagedIso).use { output ->
                    input.copyTo(output)
                }
            } ?: return@withContext ConversionResult(success = false, message = "input_open_failed")

            val resultCode = convertWithFallback(stagedIso, outputFile)

            if (resultCode != 0 || !outputFile.exists() || outputFile.length() <= 0L) {
                stagedIso.delete()
                outputFile.delete()
                return@withContext ConversionResult(
                    success = false,
                    message = mapErrorMessage(resultCode)
                )
            }

            stagedIso.delete()

            ConversionResult(
                success = true,
                outputFile = outputFile,
                suggestedFileName = buildOutputName(displayName)
            )
        } catch (_: Exception) {
            stagedIso.delete()
            outputFile.delete()
            ConversionResult(success = false, message = "unexpected_failure")
        }
    }

    suspend fun saveConvertedFile(
        context: Context,
        sourceFile: File,
        destinationUri: Uri
    ): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            FileInputStream(sourceFile).use { input ->
                context.contentResolver.openOutputStream(destinationUri, "w")?.use { output ->
                    input.copyTo(output)
                } ?: return@runCatching false
            }
            true
        }.getOrDefault(false)
    }

    fun cleanupTempFile(file: File?) {
        if (file == null) return
        runCatching {
            if (file.exists() && file.parentFile?.name == "image-converter") {
                file.delete()
            }
        }
    }

    private fun sanitizeFileName(name: String): String {
        return buildString(name.length) {
            name.forEach { char ->
                append(
                    when {
                        char.isLetterOrDigit() || char == '.' || char == '-' || char == '_' -> char
                        else -> '_'
                    }
                )
            }
        }
    }

    private fun convertWithFallback(stagedIso: File, outputFile: File): Int {
        if (NativeApp.hasNativeTools) {
            val nativeResult = runCatching { NativeApp.convertIsoToChd(stagedIso.absolutePath) }
                .getOrDefault(Int.MIN_VALUE)
            if (nativeResult == 0 && outputFile.exists() && outputFile.length() > 0L) {
                return 0
            }
            outputFile.delete()
        }

        return runCatching {
            convertIsoToChdPortable(stagedIso, outputFile)
        }.getOrElse { error ->
            outputFile.delete()
            when (error) {
                is ConversionException -> error.code
                else -> -100
            }
        }
    }

    private fun convertIsoToChdPortable(inputFile: File, outputFile: File): Int {
        if (!inputFile.exists()) {
            return -3
        }
        if (!inputFile.isFile) {
            return -4
        }

        val isoSize = inputFile.length()
        val hunkBytes = DEFAULT_HUNK_BYTES.toLong()
        val hunkCount = if (isoSize == 0L) 0L else (isoSize + hunkBytes - 1L) / hunkBytes
        if (hunkCount > UInt.MAX_VALUE.toLong()) {
            return -7
        }

        val mapSize = safeMultiplyByMapEntrySize(hunkCount) ?: return -8
        val mapOffset = CHD_HEADER_SIZE.toLong()
        val mapEnd = safeAdd(mapOffset, mapSize) ?: return -8
        val dataOffset = if (hunkCount == 0L) {
            mapEnd
        } else {
            alignUpToHunk(mapEnd) ?: return -8
        }
        val baseHunkIndex = if (hunkCount == 0L) 0L else dataOffset / hunkBytes
        if (baseHunkIndex > UInt.MAX_VALUE.toLong()) {
            return -8
        }

        return try {
            FileInputStream(inputFile).use { input ->
                RandomAccessFile(outputFile, "rw").use { output ->
                    output.setLength(0L)
                    output.write(ByteArray(CHD_HEADER_SIZE))
                    repeat(hunkCount.toInt()) { index ->
                        output.writeInt((baseHunkIndex + index).toInt())
                    }

                    writeZeroPadding(output, dataOffset - mapEnd)
                    if (hunkCount > 0L) {
                        output.seek(dataOffset)
                    }

                    val sha1 = MessageDigest.getInstance("SHA-1")
                    val hunkBuffer = ByteArray(DEFAULT_HUNK_BYTES)
                    val copyBuffer = ByteArray(IO_BUFFER_SIZE)
                    var bytesRemaining = isoSize

                    repeat(hunkCount.toInt()) {
                        val chunkSize = min(bytesRemaining, hunkBuffer.size.toLong()).toInt()
                        readChunkExact(input, hunkBuffer, copyBuffer, chunkSize)
                        if (chunkSize > 0) {
                            sha1.update(hunkBuffer, 0, chunkSize)
                        }
                        if (chunkSize < hunkBuffer.size) {
                            hunkBuffer.fill(0, chunkSize, hunkBuffer.size)
                        }
                        output.write(hunkBuffer)
                        bytesRemaining -= chunkSize.toLong()
                    }

                    if (bytesRemaining != 0L) {
                        throw ConversionException(-9)
                    }

                    val digest = sha1.digest()
                    output.seek(0L)
                    output.write(chdHeader(digest, isoSize))
                }
            }
            0
        } catch (error: ConversionException) {
            error.code
        } catch (_: java.io.FileNotFoundException) {
            -5
        } catch (_: java.io.IOException) {
            -6
        } catch (_: Exception) {
            -100
        }
    }

    private fun chdHeader(sha1: ByteArray, logicalBytes: Long): ByteArray {
        val header = ByteArray(CHD_HEADER_SIZE)
        val magic = "MComprHD".encodeToByteArray()
        magic.copyInto(header, endIndex = magic.size)

        writeIntBe(header, 8, CHD_HEADER_SIZE)
        writeIntBe(header, 12, CHD_VERSION_V5)
        writeIntBe(header, 16, 0)
        writeIntBe(header, 20, 0)
        writeIntBe(header, 24, 0)
        writeIntBe(header, 28, 0)
        writeLongBe(header, 32, logicalBytes)
        writeLongBe(header, 40, CHD_HEADER_SIZE.toLong())
        writeLongBe(header, 48, 0L)
        writeIntBe(header, 56, DEFAULT_HUNK_BYTES)
        writeIntBe(header, 60, DEFAULT_UNIT_BYTES)
        sha1.copyInto(header, destinationOffset = 64)
        sha1.copyInto(header, destinationOffset = 84)
        return header
    }

    private fun writeZeroPadding(output: RandomAccessFile, byteCount: Long) {
        if (byteCount <= 0L) return
        val zeros = ByteArray(min(IO_BUFFER_SIZE.toLong(), byteCount).toInt())
        var remaining = byteCount
        while (remaining > 0L) {
            val chunkSize = min(remaining, zeros.size.toLong()).toInt()
            output.write(zeros, 0, chunkSize)
            remaining -= chunkSize.toLong()
        }
    }

    private fun readChunkExact(
        input: FileInputStream,
        target: ByteArray,
        buffer: ByteArray,
        byteCount: Int
    ) {
        var offset = 0
        while (offset < byteCount) {
            val bytesToRead = min(buffer.size, byteCount - offset)
            val read = input.read(buffer, 0, bytesToRead)
            if (read <= 0) {
                throw ConversionException(-9)
            }
            buffer.copyInto(target, destinationOffset = offset, endIndex = read)
            offset += read
        }
    }

    private fun writeIntBe(target: ByteArray, offset: Int, value: Int) {
        target[offset] = (value ushr 24).toByte()
        target[offset + 1] = (value ushr 16).toByte()
        target[offset + 2] = (value ushr 8).toByte()
        target[offset + 3] = value.toByte()
    }

    private fun writeLongBe(target: ByteArray, offset: Int, value: Long) {
        target[offset] = (value ushr 56).toByte()
        target[offset + 1] = (value ushr 48).toByte()
        target[offset + 2] = (value ushr 40).toByte()
        target[offset + 3] = (value ushr 32).toByte()
        target[offset + 4] = (value ushr 24).toByte()
        target[offset + 5] = (value ushr 16).toByte()
        target[offset + 6] = (value ushr 8).toByte()
        target[offset + 7] = value.toByte()
    }

    private fun safeAdd(left: Long, right: Long): Long? {
        return if (Long.MAX_VALUE - left < right) null else left + right
    }

    private fun safeMultiplyByMapEntrySize(value: Long): Long? {
        return if (value == 0L) {
            0L
        } else if (value > Long.MAX_VALUE / MAP_ENTRY_BYTES) {
            null
        } else {
            value * MAP_ENTRY_BYTES
        }
    }

    private fun alignUpToHunk(value: Long): Long? {
        val alignment = DEFAULT_HUNK_BYTES.toLong()
        val remainder = value % alignment
        if (remainder == 0L) return value
        return safeAdd(value, alignment - remainder)
    }

    private fun mapErrorMessage(code: Int): String {
        return when (code) {
            -1 -> "null_pointer"
            -2 -> "invalid_utf8"
            -3 -> "input_missing"
            -4 -> "input_not_regular_file"
            -5 -> "output_create_failed"
            -6 -> "io_failure"
            -7 -> "too_many_hunks"
            -8 -> "numeric_overflow"
            -9 -> "unexpected_end_of_data"
            -100 -> "internal_failure"
            else -> "unknown_error:$code"
        }
    }
}

private class ConversionException(val code: Int) : RuntimeException()

data class ConversionResult(
    val success: Boolean,
    val outputFile: File? = null,
    val suggestedFileName: String? = null,
    val message: String? = null
)

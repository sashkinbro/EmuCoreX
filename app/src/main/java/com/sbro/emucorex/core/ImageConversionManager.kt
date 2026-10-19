package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.io.RandomAccessFile
import java.security.MessageDigest
import java.util.Locale
import kotlin.coroutines.coroutineContext
import kotlin.math.min

object ImageConversionManager {
    private const val CHD_HEADER_SIZE = 124
    private const val CHD_VERSION_V5 = 5
    private const val DEFAULT_HUNK_BYTES = 0x80000
    private const val DEFAULT_UNIT_BYTES = 2048
    private const val IO_BUFFER_SIZE = 64 * 1024
    private const val MAP_ENTRY_BYTES = 4L
    private const val PROGRESS_HUNK_INTERVAL = 64
    private const val CD_SECTOR_BYTES = 2352
    private const val ISO_SECTOR_BYTES = 2048
    private const val CD_MODE2_FORM2_FLAG = 0x20

    /** [isoDataOffsetForSector] result for CD audio (or any sector without a data header). */
    internal const val SECTOR_AUDIO = -1

    /** [isoDataOffsetForSector] result for sector layouts that cannot become ISO data. */
    internal const val SECTOR_UNSUPPORTED = -2

    val libraryFormats = listOf("ISO", "BIN", "IMG", "MDF", "GZ", "CSO", "ZSO", "CHD", "ELF", "ACGAME")
    val recommendedFormats = listOf("ISO", "CHD", "BIN")
    val isoMimeTypes = arrayOf(
        "application/octet-stream",
        "application/x-iso9660-image",
        "application/x-cd-image",
        "application/x-raw-disk-image"
    )
    val chdMimeTypes = arrayOf(
        "application/octet-stream",
        "application/x-chd",
        "application/x-mame-chd"
    )
    val binMimeTypes = arrayOf(
        "application/octet-stream",
        "application/x-binary",
        "application/x-cd-image"
    )

    enum class ConversionStage {
        STAGING,
        CONVERTING,
        SAVING
    }

    data class ConversionProgress(
        val stage: ConversionStage,
        val processedBytes: Long,
        val totalBytes: Long
    ) {
        val fraction: Float
            get() = if (totalBytes > 0L) {
                (processedBytes.toDouble() / totalBytes.toDouble()).toFloat().coerceIn(0f, 1f)
            } else {
                0f
            }
    }

    fun isIsoToChdAvailable(): Boolean = true

    fun isChdToIsoAvailable(): Boolean = NativeApp.hasNativeCore

    fun buildOutputName(sourceDisplayName: String): String {
        val trimmed = sourceDisplayName.trim().ifBlank { "game.iso" }
        return trimmed.replace(Regex("\\.iso$", RegexOption.IGNORE_CASE), ".chd")
            .let { if (it.equals(trimmed, ignoreCase = true)) "$trimmed.chd" else it }
    }

    fun buildIsoOutputName(sourceDisplayName: String): String {
        val trimmed = sourceDisplayName.trim().ifBlank { "game.chd" }
        return trimmed.replace(Regex("\\.chd$", RegexOption.IGNORE_CASE), ".iso")
            .let { if (it.equals(trimmed, ignoreCase = true)) "$trimmed.iso" else it }
    }

    fun buildIsoFromBinOutputName(sourceDisplayName: String): String {
        val trimmed = sourceDisplayName.trim().ifBlank { "game.bin" }
        return trimmed.replace(Regex("\\.bin$", RegexOption.IGNORE_CASE), ".iso")
            .let { if (it.equals(trimmed, ignoreCase = true)) "$trimmed.iso" else it }
    }

    suspend fun convertIsoToChd(
        context: Context,
        sourceUri: Uri,
        onProgress: (ConversionProgress) -> Unit = {}
    ): ConversionResult = withContext(Dispatchers.IO) {
        val displayName = sourceDisplayName(context, sourceUri, "game.iso")
        if (!displayName.lowercase(Locale.US).endsWith(".iso")) {
            return@withContext ConversionResult(success = false, message = "not_iso")
        }

        val cacheDir = converterCacheDir(context)
        val stagedIso = File(cacheDir, sanitizeFileName(displayName))
        val outputFile = File(cacheDir, sanitizeFileName(buildOutputName(displayName)))

        try {
            stagedIso.delete()
            outputFile.delete()

            val activeContext = coroutineContext
            val sourceBytes = sourceByteCount(context, sourceUri)
            val copied = copyUriToFile(
                context = context,
                sourceUri = sourceUri,
                targetFile = stagedIso,
                totalBytes = sourceBytes,
                checkCancelled = { activeContext.ensureActive() },
                onProgress = onProgress
            )
            if (!copied) {
                stagedIso.delete()
                return@withContext ConversionResult(success = false, message = "input_open_failed")
            }
            activeContext.ensureActive()

            val resultCode = convertIsoToChdPortable(
                inputFile = stagedIso,
                outputFile = outputFile,
                checkCancelled = { activeContext.ensureActive() },
                onProgress = { processedBytes, totalBytes ->
                    onProgress(
                        ConversionProgress(
                            stage = ConversionStage.CONVERTING,
                            processedBytes = processedBytes,
                            totalBytes = totalBytes
                        )
                    )
                }
            )

            if (resultCode != 0 || !outputFile.exists() || outputFile.length() <= 0L) {
                stagedIso.delete()
                outputFile.delete()
                return@withContext ConversionResult(
                    success = false,
                    message = mapErrorMessage(resultCode)
                )
            }

            stagedIso.delete()
            onProgress(
                ConversionProgress(
                    stage = ConversionStage.CONVERTING,
                    processedBytes = outputFile.length(),
                    totalBytes = outputFile.length()
                )
            )

            ConversionResult(
                success = true,
                outputFile = outputFile,
                suggestedFileName = buildOutputName(displayName)
            )
        } catch (cancelled: CancellationException) {
            stagedIso.delete()
            outputFile.delete()
            throw cancelled
        } catch (_: Exception) {
            stagedIso.delete()
            outputFile.delete()
            ConversionResult(success = false, message = "unexpected_failure")
        }
    }

    suspend fun convertChdToIso(
        context: Context,
        sourceUri: Uri,
        onProgress: (ConversionProgress) -> Unit = {}
    ): ConversionResult = withContext(Dispatchers.IO) {
        if (!isChdToIsoAvailable()) {
            return@withContext ConversionResult(success = false, message = "native_unavailable")
        }

        val displayName = sourceDisplayName(context, sourceUri, "game.chd")
        if (!displayName.lowercase(Locale.US).endsWith(".chd")) {
            return@withContext ConversionResult(success = false, message = "not_chd")
        }

        val cacheDir = converterCacheDir(context)
        val stagedChd = File(cacheDir, sanitizeFileName(displayName))
        val outputFile = File(cacheDir, sanitizeFileName(buildIsoOutputName(displayName)))

        try {
            stagedChd.delete()
            outputFile.delete()

            val activeContext = coroutineContext
            val sourceBytes = sourceByteCount(context, sourceUri)
            val copied = copyUriToFile(
                context = context,
                sourceUri = sourceUri,
                targetFile = stagedChd,
                totalBytes = sourceBytes,
                checkCancelled = { activeContext.ensureActive() },
                onProgress = onProgress
            )
            if (!copied) {
                stagedChd.delete()
                return@withContext ConversionResult(success = false, message = "input_open_failed")
            }
            activeContext.ensureActive()

            val callback = ConversionProgressCallback { processedBytes, totalBytes ->
                onProgress(
                    ConversionProgress(
                        stage = ConversionStage.CONVERTING,
                        processedBytes = processedBytes,
                        totalBytes = totalBytes
                    )
                )
                !activeContext.isActive
            }
            val resultCode = runCatching {
                NativeApp.convertChdToIso(stagedChd.absolutePath, outputFile.absolutePath, callback)
            }.getOrDefault(Int.MIN_VALUE)

            stagedChd.delete()
            activeContext.ensureActive()

            if (resultCode != 0 || !outputFile.exists() || outputFile.length() <= 0L) {
                outputFile.delete()
                return@withContext ConversionResult(
                    success = false,
                    message = mapChdErrorMessage(resultCode)
                )
            }

            onProgress(
                ConversionProgress(
                    stage = ConversionStage.CONVERTING,
                    processedBytes = outputFile.length(),
                    totalBytes = outputFile.length()
                )
            )

            ConversionResult(
                success = true,
                outputFile = outputFile,
                suggestedFileName = buildIsoOutputName(displayName)
            )
        } catch (cancelled: CancellationException) {
            stagedChd.delete()
            outputFile.delete()
            throw cancelled
        } catch (_: Exception) {
            stagedChd.delete()
            outputFile.delete()
            ConversionResult(success = false, message = "unexpected_failure")
        }
    }

    suspend fun convertBinToIso(
        context: Context,
        sourceUri: Uri,
        cueUri: Uri? = null,
        onProgress: (ConversionProgress) -> Unit = {}
    ): ConversionResult = withContext(Dispatchers.IO) {
        val displayName = sourceDisplayName(context, sourceUri, "game.bin")
        if (!displayName.lowercase(Locale.US).endsWith(".bin")) {
            return@withContext ConversionResult(success = false, message = "not_bin")
        }

        val cacheDir = converterCacheDir(context)
        val outputFile = File(cacheDir, sanitizeFileName(buildIsoFromBinOutputName(displayName)))

        try {
            outputFile.delete()
            val activeContext = coroutineContext
            val totalBytes = sourceByteCount(context, sourceUri)
            val cueSheet = cueUri?.let { readCueSheet(context, it) }
            val input = context.contentResolver.openInputStream(sourceUri)
                ?: return@withContext ConversionResult(success = false, message = "input_open_failed")

            var audioTracksSkipped = false
            input.use { source ->
                FileOutputStream(outputFile).use { output ->
                    audioTracksSkipped = convertBinStreamToIso(
                        source = source,
                        output = output,
                        cueSheet = cueSheet,
                        binName = displayName,
                        totalBytes = totalBytes,
                        checkCancelled = { activeContext.ensureActive() },
                        onProgress = { processed, total ->
                            onProgress(
                                ConversionProgress(
                                    stage = ConversionStage.CONVERTING,
                                    processedBytes = processed,
                                    totalBytes = total
                                )
                            )
                        }
                    )
                }
            }

            if (!outputFile.exists() || outputFile.length() <= 0L) {
                outputFile.delete()
                return@withContext ConversionResult(success = false, message = "invalid_bin")
            }

            onProgress(
                ConversionProgress(
                    stage = ConversionStage.CONVERTING,
                    processedBytes = outputFile.length(),
                    totalBytes = outputFile.length()
                )
            )

            ConversionResult(
                success = true,
                outputFile = outputFile,
                suggestedFileName = buildIsoFromBinOutputName(displayName),
                warningMessage = if (audioTracksSkipped) "bin_audio_skipped" else null
            )
        } catch (cancelled: CancellationException) {
            outputFile.delete()
            throw cancelled
        } catch (rawError: RawConversionException) {
            outputFile.delete()
            ConversionResult(success = false, message = rawError.resultCode)
        } catch (_: Exception) {
            outputFile.delete()
            ConversionResult(success = false, message = "unexpected_failure")
        }
    }

    /**
     * Streams a raw disc image into a plain 2048-byte-per-sector ISO. When a CUE sheet is
     * available, exactly its data track is converted; otherwise the layout is detected from
     * the first sector. Returns true when non-data (audio) sectors had to be skipped.
     */
    internal fun convertBinStreamToIso(
        source: InputStream,
        output: OutputStream,
        cueSheet: CueSheet?,
        binName: String,
        totalBytes: Long,
        checkCancelled: () -> Unit,
        onProgress: (processedBytes: Long, totalBytes: Long) -> Unit
    ): Boolean {
        val dataBlock = cueSheet?.dataBlockFor(binName)
        val dataTrack = dataBlock?.tracks?.firstOrNull(CueTrackInfo::isDataTrack)
        val sheetAudioTracks = cueSheet?.hasAudioTracks == true

        if (dataTrack != null) {
            val sectorBytes = dataTrack.sectorBytes()
            val startOffset = dataTrack.startFrame * sectorBytes
            val endOffset = dataBlock?.trackEndFrame(dataTrack)?.times(sectorBytes)
            skipFully(source, startOffset)
            val skippedAudioSectors = if (sectorBytes == ISO_SECTOR_BYTES) {
                copyPlainRange(source, output, startOffset, endOffset, totalBytes, checkCancelled, onProgress)
                false
            } else {
                convertRawCdSectors(
                    source = source,
                    output = output,
                    totalBytes = totalBytes,
                    endOffset = endOffset,
                    processedBytes = startOffset,
                    firstSector = null,
                    checkCancelled = checkCancelled,
                    onProgress = onProgress
                )
            }
            return sheetAudioTracks || skippedAudioSectors
        }

        val firstSector = ByteArray(CD_SECTOR_BYTES)
        val firstRead = readUpTo(source, firstSector, firstSector.size)
        val isRawCd = firstRead >= CD_SECTOR_BYTES && hasCdSectorSync(firstSector)
        val skippedAudioSectors = if (isRawCd) {
            convertRawCdSectors(
                source = source,
                output = output,
                totalBytes = totalBytes,
                endOffset = null,
                processedBytes = 0L,
                firstSector = firstSector,
                checkCancelled = checkCancelled,
                onProgress = onProgress
            )
        } else {
            if (totalBytes > 0L && totalBytes % ISO_SECTOR_BYTES != 0L) {
                throw RawConversionException("invalid_bin")
            }
            output.write(firstSector, 0, firstRead)
            copyPlainRange(source, output, firstRead.toLong(), null, totalBytes, checkCancelled, onProgress)
            false
        }
        return sheetAudioTracks || skippedAudioSectors
    }

    internal fun convertRawCdSectors(
        source: InputStream,
        output: OutputStream,
        totalBytes: Long,
        endOffset: Long?,
        processedBytes: Long,
        firstSector: ByteArray?,
        checkCancelled: () -> Unit,
        onProgress: (processedBytes: Long, totalBytes: Long) -> Unit
    ): Boolean {
        val sector = firstSector ?: ByteArray(CD_SECTOR_BYTES)
        var sectorRead = if (firstSector != null) {
            firstSector.size
        } else {
            readUpTo(source, sector, CD_SECTOR_BYTES)
        }
        if (sectorRead < CD_SECTOR_BYTES) return false

        val firstOffset = isoDataOffsetForSector(sector)
        if (firstOffset == SECTOR_UNSUPPORTED) throw RawConversionException("unsupported_sector_mode")
        if (firstOffset == SECTOR_AUDIO) return true
        val firstMode = sector[15].toInt() and 0xFF

        var processed = processedBytes
        while (sectorRead >= CD_SECTOR_BYTES && (endOffset == null || processed < endOffset)) {
            checkCancelled()
            val offset = isoDataOffsetForSector(sector)
            when (offset) {
                SECTOR_AUDIO -> {
                    // Sector without a data header: CD audio (or another media). The remaining
                    // data cannot be stored in an ISO image, so only the data track is converted.
                    return true
                }
                SECTOR_UNSUPPORTED -> throw RawConversionException("unsupported_sector_mode")
            }
            if ((sector[15].toInt() and 0xFF) != firstMode) {
                return true
            }
            output.write(sector, offset, ISO_SECTOR_BYTES)
            processed += CD_SECTOR_BYTES
            onProgress(processed, totalBytes)
            if (endOffset != null && processed >= endOffset) break
            sectorRead = readUpTo(source, sector, CD_SECTOR_BYTES)
        }
        return false
    }

    internal fun copyPlainRange(
        source: InputStream,
        output: OutputStream,
        processedBytes: Long,
        endOffset: Long?,
        totalBytes: Long,
        checkCancelled: () -> Unit,
        onProgress: (processedBytes: Long, totalBytes: Long) -> Unit
    ) {
        var processed = processedBytes
        val buffer = ByteArray(IO_BUFFER_SIZE)
        while (endOffset == null || processed < endOffset) {
            checkCancelled()
            val allowed = if (endOffset != null) {
                min(buffer.size.toLong(), endOffset - processed).toInt()
            } else {
                buffer.size
            }
            if (allowed <= 0) break
            val read = source.read(buffer, 0, allowed)
            if (read <= 0) break
            output.write(buffer, 0, read)
            processed += read
            onProgress(processed, totalBytes)
        }
    }

    internal fun skipFully(source: InputStream, byteCount: Long) {
        var remaining = byteCount
        val buffer = ByteArray(IO_BUFFER_SIZE)
        while (remaining > 0L) {
            val read = source.read(buffer, 0, min(buffer.size.toLong(), remaining).toInt())
            if (read <= 0) throw RawConversionException("invalid_bin")
            remaining -= read
        }
    }

    internal fun readCueSheet(context: Context, cueUri: Uri): CueSheet? {
        return runCatching {
            context.contentResolver.openInputStream(cueUri)?.use { input ->
                val bytes = ByteArray(512 * 1024)
                var offset = 0
                while (offset < bytes.size) {
                    val read = input.read(bytes, offset, bytes.size - offset)
                    if (read <= 0) break
                    offset += read
                }
                val utf8 = parseCueSheet(String(bytes, 0, offset, Charsets.UTF_8))
                utf8 ?: parseCueSheet(String(bytes, 0, offset, Charsets.ISO_8859_1))
            }
        }.getOrNull()
    }

    internal fun hasCdSectorSync(sector: ByteArray): Boolean {
        if (sector.size < 12) return false
        if (sector[0].toInt() != 0x00 || sector[11].toInt() != 0x00) return false
        for (index in 1..10) {
            if ((sector[index].toInt() and 0xFF) != 0xFF) return false
        }
        return true
    }

    /**
     * Byte offset of the 2048-byte user payload inside a raw CD sector, [SECTOR_AUDIO] for
     * sectors without a data header, or [SECTOR_UNSUPPORTED] for layouts ISO cannot hold.
     */
    internal fun isoDataOffsetForSector(sector: ByteArray): Int {
        if (sector.size < CD_SECTOR_BYTES) return SECTOR_UNSUPPORTED
        if (!hasCdSectorSync(sector)) return SECTOR_AUDIO
        return when (sector[15].toInt() and 0xFF) {
            1 -> 16
            2 -> if ((sector[18].toInt() and CD_MODE2_FORM2_FLAG) != 0) SECTOR_UNSUPPORTED else 24
            else -> SECTOR_UNSUPPORTED
        }
    }

    internal fun readUpTo(input: InputStream, target: ByteArray, byteCount: Int): Int {
        var offset = 0
        while (offset < byteCount) {
            val read = input.read(target, offset, byteCount - offset)
            if (read <= 0) break
            offset += read
        }
        return offset
    }

    suspend fun saveConvertedFile(
        context: Context,
        sourceFile: File,
        destinationUri: Uri,
        onProgress: (ConversionProgress) -> Unit = {}
    ): Boolean = withContext(Dispatchers.IO) {
        val totalBytes = sourceFile.length().coerceAtLeast(0L)
        runCatching {
            FileInputStream(sourceFile).use { input ->
                context.contentResolver.openOutputStream(destinationUri, "w")?.use { output ->
                    val buffer = ByteArray(IO_BUFFER_SIZE)
                    var copied = 0L
                    while (true) {
                        coroutineContext.ensureActive()
                        val read = input.read(buffer)
                        if (read <= 0) break
                        output.write(buffer, 0, read)
                        copied += read
                        onProgress(
                            ConversionProgress(
                                stage = ConversionStage.SAVING,
                                processedBytes = copied,
                                totalBytes = totalBytes
                            )
                        )
                    }
                    output.flush()
                } ?: return@runCatching false
            }
            true
        }.getOrDefault(false)
    }

    fun cleanupTempFile(file: File?) {
        if (file == null) return
        runCatching {
            if (file.exists() && file.parentFile?.name == CONVERTER_CACHE_DIR_NAME) {
                file.delete()
            }
        }
    }

    private const val CONVERTER_CACHE_DIR_NAME = "image-converter"

    private fun converterCacheDir(context: Context): File =
        File(context.cacheDir, CONVERTER_CACHE_DIR_NAME).apply { mkdirs() }

    private fun sourceDisplayName(context: Context, uri: Uri, fallback: String): String {
        return DocumentFile.fromSingleUri(context, uri)?.name
            ?: uri.lastPathSegment
            ?: fallback
    }

    private fun sourceByteCount(context: Context, uri: Uri): Long {
        val documentLength = runCatching {
            DocumentFile.fromSingleUri(context, uri)?.length() ?: 0L
        }.getOrDefault(0L)
        if (documentLength > 0L) return documentLength
        return runCatching {
            context.contentResolver.openAssetFileDescriptor(uri, "r")?.use { descriptor ->
                descriptor.length
            } ?: 0L
        }.getOrDefault(0L)
    }

    private fun copyUriToFile(
        context: Context,
        sourceUri: Uri,
        targetFile: File,
        totalBytes: Long,
        checkCancelled: () -> Unit,
        onProgress: (ConversionProgress) -> Unit
    ): Boolean {
        val input: InputStream = context.contentResolver.openInputStream(sourceUri) ?: return false
        input.use { source ->
            FileOutputStream(targetFile).use { output ->
                val buffer = ByteArray(IO_BUFFER_SIZE)
                var copied = 0L
                while (true) {
                    checkCancelled()
                    val read = source.read(buffer)
                    if (read <= 0) break
                    output.write(buffer, 0, read)
                    copied += read
                    onProgress(
                        ConversionProgress(
                            stage = ConversionStage.STAGING,
                            processedBytes = copied,
                            totalBytes = totalBytes
                        )
                    )
                }
            }
        }
        return true
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

    private fun convertIsoToChdPortable(
        inputFile: File,
        outputFile: File,
        checkCancelled: () -> Unit,
        onProgress: (processedBytes: Long, totalBytes: Long) -> Unit
    ): Int {
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
                    var processedBytes = 0L

                    repeat(hunkCount.toInt()) { index ->
                        checkCancelled()
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
                        processedBytes += chunkSize.toLong()
                        if (index % PROGRESS_HUNK_INTERVAL == 0 || bytesRemaining == 0L) {
                            onProgress(processedBytes, isoSize)
                        }
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
        } catch (cancelled: CancellationException) {
            throw cancelled
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

    private fun mapChdErrorMessage(code: Int): String {
        return when (code) {
            -2 -> "chd_unreadable"
            -3 -> "chd_unreadable"
            -6 -> "chd_unreadable"
            -5, -7 -> "output_create_failed"
            -8 -> "internal_failure"
            -9 -> "cancelled"
            -10 -> "chd_requires_parent"
            Int.MIN_VALUE -> "native_unavailable"
            else -> "unknown_error:$code"
        }
    }
}

private class ConversionException(val code: Int) : RuntimeException()

internal class RawConversionException(val resultCode: String) : RuntimeException()

data class ConversionResult(
    val success: Boolean,
    val outputFile: File? = null,
    val suggestedFileName: String? = null,
    val message: String? = null,
    val warningMessage: String? = null
)

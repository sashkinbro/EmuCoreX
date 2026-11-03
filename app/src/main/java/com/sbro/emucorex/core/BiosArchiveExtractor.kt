package com.sbro.emucorex.core

import com.github.junrar.Archive
import org.apache.commons.compress.archivers.sevenz.SevenZFile
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.OutputStream
import java.util.zip.ZipFile

/**
 * Unpacks BIOS images from user-supplied archives during import.
 *
 * The emulator core only understands a plain BIOS file, so archives never reach it: they are
 * expanded once into the normal `imported-bios` staging directory and afterwards validated by
 * the existing BIOS pipeline. Everything here is read-only and strictly bounded, so a broken,
 * encrypted or hostile archive can never damage an existing BIOS selection - it just yields no
 * usable image and the previous selection stays in place.
 *
 * Deliberately free of Android APIs so the extraction rules are covered by plain JVM tests.
 */
object BiosArchiveExtractor {

    /** Archive file types the BIOS picker accepts. */
    val archiveExtensions: Set<String> = setOf("zip", "7z", "rar")

    /** Archive types shown in the BIOS hint; kept in sync with the extraction above. */
    val displayArchiveFormats: String = archiveExtensions.joinToString(" · ") { ".$it" }

    /** Entry names inside an archive that may hold a main BIOS image or a Namco ROM. */
    private val biosImageExtensions: Set<String> = setOf("bin", "rom", "7d", "8g")

    private enum class Format { ZIP, SEVEN_ZIP, RAR }

    fun isArchiveName(name: String?): Boolean {
        val fileName = name?.trim().orEmpty()
        if (fileName.isEmpty()) return false
        return fileName.substringAfterLast('.', "").lowercase() in archiveExtensions
    }

    /**
     * Extracts every BIOS-looking image from [archiveFile] into [targetDir].
     * Returns the files that were written, or an empty list when nothing usable was found.
     * Never throws: unreadable, encrypted or unsupported archives simply produce no files.
     */
    fun extractBiosFiles(archiveFile: File, targetDir: File): List<File> {
        if (!archiveFile.isFile || archiveFile.length() <= 0L) return emptyList()
        if (!targetDir.isDirectory && !targetDir.mkdirs()) return emptyList()

        return try {
            when (detectFormat(archiveFile)) {
                Format.ZIP -> extractZip(archiveFile, targetDir)
                Format.SEVEN_ZIP -> extractSevenZip(archiveFile, targetDir)
                Format.RAR -> extractRar(archiveFile, targetDir)
                null -> emptyList()
            }
        } catch (_: Exception) {
            // Encrypted, multi-volume, truncated or otherwise unsupported archives are a normal
            // user mistake; the caller reports "no BIOS found" and keeps the old selection.
            emptyList()
        }
    }

    private fun extractZip(archiveFile: File, targetDir: File): List<File> {
        val budget = ExtractionBudget(targetDir)
        ZipFile(archiveFile).use { zip ->
            var scanned = 0
            for (entry in zip.entries()) {
                if (++scanned > MAX_ENTRIES_SCANNED || !budget.canAccept()) break
                if (entry.isDirectory || !isBiosImageName(entry.name)) continue
                if (entry.size > MAX_ENTRY_BYTES) continue
                if (!budget.tryBeginFile()) break

                val target = budget.nextTarget(entry.name)
                val written = runCatching {
                    zip.getInputStream(entry).use { input ->
                        copyBounded(target, budget) { buffer -> input.read(buffer) }
                    }
                }.getOrNull()
                budget.accept(target, written)
            }
        }
        return budget.extracted
    }

    private fun extractSevenZip(archiveFile: File, targetDir: File): List<File> {
        val budget = ExtractionBudget(targetDir)
        SevenZFile.builder().setFile(archiveFile).get().use { sevenZip ->
            var scanned = 0
            var entry = sevenZip.nextEntry
            while (entry != null) {
                scanned++
                if (scanned > MAX_ENTRIES_SCANNED || !budget.canAccept()) break
                if (entry.isDirectory || !isBiosImageName(entry.name)) {
                    entry = sevenZip.nextEntry
                    continue
                }
                if (entry.size > MAX_ENTRY_BYTES) {
                    entry = sevenZip.nextEntry
                    continue
                }
                if (!budget.tryBeginFile()) break

                val target = budget.nextTarget(entry.name)
                val written = runCatching {
                    copyBounded(target, budget) { buffer -> sevenZip.read(buffer, 0, buffer.size) }
                }.getOrNull()
                budget.accept(target, written)
                entry = sevenZip.nextEntry
            }
        }
        return budget.extracted
    }

    private fun extractRar(archiveFile: File, targetDir: File): List<File> {
        val budget = ExtractionBudget(targetDir)
        Archive(archiveFile).use { archive ->
            if (archive.isEncrypted) return emptyList()

            var scanned = 0
            for (header in archive.fileHeaders) {
                if (++scanned > MAX_ENTRIES_SCANNED || !budget.canAccept()) break
                if (header.isDirectory || header.isEncrypted) continue
                val entryName = header.fileName ?: continue
                if (!isBiosImageName(entryName)) continue
                if (header.fullUnpackSize > MAX_ENTRY_BYTES) continue
                if (!budget.tryBeginFile()) break

                val target = budget.nextTarget(entryName)
                val limit = minOf(MAX_ENTRY_BYTES, MAX_TOTAL_BYTES - budget.totalBytes)
                val written = runCatching {
                    FileOutputStream(target).use { output ->
                        val limited = LimitedOutputStream(output, limit)
                        archive.extractFile(header, limited)
                        limited.written
                    }
                }.getOrNull()
                budget.accept(target, written)
            }
        }
        return budget.extracted
    }

    /**
     * Copies the current entry into [target], refusing to write more than the per-entry or total
     * budget. Returns the number of bytes written, or null when the entry had to be discarded.
     */
    private fun copyBounded(
        target: File,
        budget: ExtractionBudget,
        readChunk: (ByteArray) -> Int
    ): Long? {
        val limit = minOf(MAX_ENTRY_BYTES, MAX_TOTAL_BYTES - budget.totalBytes)
        if (limit <= 0L) return null

        var written = 0L
        try {
            FileOutputStream(target).use { output ->
                val buffer = ByteArray(COPY_BUFFER_BYTES)
                while (true) {
                    val read = readChunk(buffer)
                    if (read < 0) break
                    if (read == 0) continue
                    written += read
                    if (written > limit) throw IOException("BIOS archive entry is too large")
                    output.write(buffer, 0, read)
                }
            }
        } catch (error: Exception) {
            target.delete()
            if (error is IOException) throw error
            return null
        }
        return written
    }

    private fun detectFormat(file: File): Format? {
        val header = ByteArray(8)
        val read = runCatching {
            file.inputStream().use { it.read(header) }
        }.getOrDefault(-1)

        if (read >= 4 && header[0] == 0x50.toByte() && header[1] == 0x4B.toByte()) return Format.ZIP
        if (read >= 6 &&
            header[0] == 0x37.toByte() && header[1] == 0x7A.toByte() &&
            header[2] == 0xBC.toByte() && header[3] == 0xAF.toByte() &&
            header[4] == 0x27.toByte() && header[5] == 0x1C.toByte()
        ) {
            return Format.SEVEN_ZIP
        }
        if (read >= 7 &&
            header[0] == 0x52.toByte() && header[1] == 0x61.toByte() && header[2] == 0x72.toByte() &&
            header[3] == 0x21.toByte() && header[4] == 0x1A.toByte() && header[5] == 0x07.toByte()
        ) {
            return Format.RAR
        }

        // Some DocumentsProviders stream archives without a readable signature; fall back to the
        // file extension so those selections still work.
        return when (file.extension.lowercase()) {
            "zip" -> Format.ZIP
            "7z" -> Format.SEVEN_ZIP
            "rar" -> Format.RAR
            else -> null
        }
    }

    private fun isBiosImageName(name: String?): Boolean {
        val fileName = name?.replace('\\', '/')?.substringAfterLast('/').orEmpty()
        if (fileName.isEmpty() || fileName.startsWith(".")) return false
        return fileName.substringAfterLast('.', "").lowercase() in biosImageExtensions
    }

    /** Wraps a stream and refuses writes past [limit], protecting against a lying RAR header. */
    private class LimitedOutputStream(
        private val delegate: OutputStream,
        private val limit: Long
    ) : OutputStream() {
        var written = 0L
            private set

        override fun write(byte: Int) {
            if (written + 1 > limit) throw IOException("BIOS archive entry is too large")
            delegate.write(byte)
            written += 1
        }

        override fun write(buffer: ByteArray, offset: Int, length: Int) {
            if (written + length > limit) throw IOException("BIOS archive entry is too large")
            delegate.write(buffer, offset, length)
            written += length
        }

        override fun flush() = delegate.flush()
    }
}

// A real PS2 BIOS is ~4 MiB and the Namco ROMs are 2 MiB, so these bounds only reject junk.
private const val MAX_ENTRY_BYTES = 8L * 1024L * 1024L
private const val MAX_TOTAL_BYTES = 24L * 1024L * 1024L
private const val MAX_ENTRIES_SCANNED = 2048
private const val MAX_FILES_WRITTEN = 8
private const val MAX_FILES_ATTEMPTED = 12
private const val COPY_BUFFER_BYTES = 64 * 1024
private const val MAX_FILE_NAME_LENGTH = 96

private fun sanitizeEntryName(name: String): String {
    val baseName = name.replace('\\', '/').substringAfterLast('/')
    val sanitized = buildString(baseName.length) {
        baseName.forEach { character ->
            append(
                if (character.isLetterOrDigit() || character == '.' || character == '-' || character == '_') {
                    character
                } else {
                    '_'
                }
            )
        }
    }
    return sanitized.take(MAX_FILE_NAME_LENGTH).ifBlank { "bios.bin" }
}

private class ExtractionBudget(private val targetDir: File) {
    val extracted = mutableListOf<File>()
    var totalBytes = 0L
        private set
    private var attemptedFiles = 0

    fun canAccept(): Boolean =
        extracted.size < MAX_FILES_WRITTEN && totalBytes < MAX_TOTAL_BYTES

    /** Counts every entry we try to write, so a corrupt archive cannot loop over huge entries. */
    fun tryBeginFile(): Boolean {
        if (attemptedFiles >= MAX_FILES_ATTEMPTED) return false
        attemptedFiles++
        return true
    }

    fun nextTarget(entryName: String): File {
        val baseName = sanitizeEntryName(entryName)
        val extension = baseName.substringAfterLast('.', "")
        val stem = baseName.substringBeforeLast('.', baseName)
        var candidate = File(targetDir, baseName)
        var suffix = 1
        while (candidate.exists()) {
            candidate = File(
                targetDir,
                if (extension.isEmpty()) "${stem}_${suffix}" else "${stem}_${suffix}.$extension"
            )
            suffix++
        }
        return candidate
    }

    fun accept(target: File, written: Long?) {
        if (written == null || written <= 0L) {
            target.delete()
            return
        }
        totalBytes += written
        extracted += target
    }
}

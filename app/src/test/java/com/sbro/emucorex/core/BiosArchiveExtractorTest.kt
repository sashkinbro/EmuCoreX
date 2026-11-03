package com.sbro.emucorex.core

import org.apache.commons.compress.archivers.sevenz.SevenZArchiveEntry
import org.apache.commons.compress.archivers.sevenz.SevenZOutputFile
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class BiosArchiveExtractorTest {

    private val tempDirs = mutableListOf<File>()

    @After
    fun tearDown() {
        tempDirs.forEach { it.deleteRecursively() }
        tempDirs.clear()
    }

    @Test
    fun `archive names are recognized case insensitively`() {
        assertTrue(BiosArchiveExtractor.isArchiveName("bios.zip"))
        assertTrue(BiosArchiveExtractor.isArchiveName("SCPH-39001.7Z"))
        assertTrue(BiosArchiveExtractor.isArchiveName("ps2 bios.rar"))
        assertFalse(BiosArchiveExtractor.isArchiveName("bios.bin"))
        assertFalse(BiosArchiveExtractor.isArchiveName("bios.gz"))
        assertFalse(BiosArchiveExtractor.isArchiveName(null))
        assertFalse(BiosArchiveExtractor.isArchiveName(""))
    }

    @Test
    fun `zip archive is unpacked with its BIOS image`() {
        val archive = createZip("bios.zip", "SCPH-39001.bin" to ByteArray(4096) { (it % 251).toByte() })
        val target = tempDir("extract-zip")

        val extracted = BiosArchiveExtractor.extractBiosFiles(archive, target)

        assertEquals(1, extracted.size)
        assertEquals("SCPH-39001.bin", extracted.single().name)
        assertEquals(4096L, extracted.single().length())
    }

    @Test
    fun `nested archive folders are flattened to the file name`() {
        val archive = createZip("bios.zip", "PS2/BIOS/SCPH-70012.bin" to ByteArray(1024))
        val target = tempDir("extract-nested")

        val extracted = BiosArchiveExtractor.extractBiosFiles(archive, target)

        assertEquals("SCPH-70012.bin", extracted.single().name)
        assertTrue(extracted.single().parentFile?.absolutePath == target.absolutePath)
    }

    @Test
    fun `seven zip archive is unpacked`() {
        val archive = createSevenZip("bios.7z", "r27v1602f.7d" to ByteArray(2048) { 0x5A })
        val target = tempDir("extract-7z")

        val extracted = BiosArchiveExtractor.extractBiosFiles(archive, target)

        assertEquals(1, extracted.size)
        assertEquals("r27v1602f.7d", extracted.single().name)
        assertEquals(2048L, extracted.single().length())
    }

    @Test
    fun `archive without BIOS images extracts nothing`() {
        val archive = createZip("documents.zip", "readme.txt" to "hello".toByteArray())
        val target = tempDir("extract-empty")

        assertTrue(BiosArchiveExtractor.extractBiosFiles(archive, target).isEmpty())
    }

    @Test
    fun `entry larger than the BIOS budget is rejected`() {
        val oversized = ByteArray(9 * 1024 * 1024)
        val archive = createZip("huge.zip", "bios.bin" to oversized)
        val target = tempDir("extract-huge")

        val extracted = BiosArchiveExtractor.extractBiosFiles(archive, target)

        assertTrue(extracted.isEmpty())
        assertTrue(target.listFiles().orEmpty().none { it.name == "bios.bin" })
    }

    @Test
    fun `path traversal entry names stay inside the target directory`() {
        val archive = createZip("evil.zip", "../../escape.bin" to ByteArray(64))
        val target = tempDir("extract-traversal")

        val extracted = BiosArchiveExtractor.extractBiosFiles(archive, target)

        assertEquals(1, extracted.size)
        assertEquals(target.absolutePath, extracted.single().parentFile?.absolutePath)
        assertTrue(extracted.single().name.startsWith("escape.bin"))
    }

    @Test
    fun `broken archive produces no files and does not throw`() {
        val broken = File(tempDir("broken"), "bios.rar")
        broken.writeBytes("this is definitely not a rar archive".toByteArray())

        assertTrue(BiosArchiveExtractor.extractBiosFiles(broken, tempDir("broken-out")).isEmpty())
    }

    @Test
    fun `unsupported extension produces no files`() {
        val archive = File(tempDir("unknown"), "bios.img")
        archive.writeBytes(ByteArray(16))

        assertTrue(BiosArchiveExtractor.extractBiosFiles(archive, tempDir("unknown-out")).isEmpty())
    }

    @Test
    fun `extracted bytes match the archive entry`() {
        val payload = ByteArray(777) { ((it * 7) % 253).toByte() }
        val archive = createZip("bytes.zip", "bios.bin" to payload)
        val target = tempDir("extract-bytes")

        val extracted = BiosArchiveExtractor.extractBiosFiles(archive, target).single()

        assertArrayEquals(payload, extracted.readBytes())
    }

    private fun tempDir(name: String): File {
        val dir = Files.createTempDirectory("emucorex-$name").toFile()
        tempDirs += dir
        return dir
    }

    private fun createZip(name: String, vararg entries: Pair<String, ByteArray>): File {
        val archive = File(tempDir("zip-source"), name)
        ZipOutputStream(archive.outputStream()).use { zip ->
            entries.forEach { (entryName, bytes) ->
                zip.putNextEntry(ZipEntry(entryName))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
        return archive
    }

    private fun createSevenZip(name: String, vararg entries: Pair<String, ByteArray>): File {
        val archive = File(tempDir("sevenzip-source"), name)
        SevenZOutputFile(archive).use { sevenZip ->
            entries.forEach { (entryName, bytes) ->
                val entry = SevenZArchiveEntry().apply { this.name = entryName }
                sevenZip.putArchiveEntry(entry)
                sevenZip.write(bytes)
                sevenZip.closeArchiveEntry()
            }
        }
        return archive
    }
}

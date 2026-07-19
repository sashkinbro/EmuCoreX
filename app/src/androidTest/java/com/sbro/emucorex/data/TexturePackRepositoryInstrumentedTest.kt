package com.sbro.emucorex.data

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.core.EmulatorStorage
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

@RunWith(AndroidJUnit4::class)
class TexturePackRepositoryInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    private val preferences = AppPreferences(context)
    private val repository = TexturePackRepository(context, preferences)

    @Test
    fun commonArchiveLayoutsImportWithoutDuplicatingSerialPath() {
        val serial = uniqueSerial()
        val root = packRoot(serial)
        try {
            val archive = zipOf(
                "$serial/replacements/body.png" to byteArrayOf(1, 2, 3),
                "replacements/$serial/wheels.dds" to byteArrayOf(4, 5, 6)
            )

            val result = repository.importPackZip(ByteArrayInputStream(archive), "pack.zip")

            assertTrue(result.success)
            assertEquals(2, result.importedFiles)
            assertEquals(setOf(serial), result.importedSerials)
            assertArrayEquals(byteArrayOf(1, 2, 3), File(root, "replacements/body.png").readBytes())
            assertArrayEquals(byteArrayOf(4, 5, 6), File(root, "replacements/wheels.dds").readBytes())
            assertFalse(File(root, "replacements/$serial").exists())
        } finally {
            repository.deletePack(serial)
        }
    }

    @Test
    fun reimportUpdatesFilesAndManagerOperationsKeepReplacementsWhenClearingDumps() = runBlocking {
        val serial = uniqueSerial()
        val root = packRoot(serial)
        try {
            val first = repository.importPackZip(
                ByteArrayInputStream(zipOf("$serial/replacements/body.png" to byteArrayOf(1))),
                "pack.zip"
            )
            val second = repository.importPackZip(
                ByteArrayInputStream(zipOf("$serial/replacements/body.png" to byteArrayOf(9, 8))),
                "pack.zip"
            )
            val dump = File(root, "dumps/dump.png").apply {
                parentFile?.mkdirs()
                writeBytes(byteArrayOf(7))
            }

            assertTrue(first.success)
            assertTrue(second.success)
            assertArrayEquals(byteArrayOf(9, 8), File(root, "replacements/body.png").readBytes())
            assertTrue(repository.listPacks().packs.any {
                it.serial == serial && it.replacementCount == 1 && it.dumpCount == 1
            })

            assertTrue(repository.clearDumps(serial))
            assertFalse(dump.exists())
            assertTrue(File(root, "replacements/body.png").exists())
            assertTrue(repository.deletePack(serial))
            assertFalse(root.exists())
        } finally {
            repository.deletePack(serial)
        }
    }

    @Test
    fun invalidOrCorruptArchiveFailsWithoutInstallingPartialFiles() {
        val serial = uniqueSerial()
        val root = packRoot(serial)
        try {
            val traversal = repository.importPackZip(
                ByteArrayInputStream(zipOf("../$serial/replacements/body.png" to byteArrayOf(1))),
                "pack.zip"
            )
            val corrupt = repository.importPackZip(
                ByteArrayInputStream("not a zip".toByteArray()),
                "$serial.zip"
            )

            assertFalse(traversal.success)
            assertFalse(corrupt.success)
            assertFalse(root.exists())
        } finally {
            repository.deletePack(serial)
        }
    }

    private fun packRoot(serial: String): File {
        return File(
            EmulatorStorage.texturesDir(context, preferences.getEmulatorDataPathSync()),
            serial
        )
    }

    private fun uniqueSerial(): String {
        val digits = (System.nanoTime() % 100_000).toString().padStart(5, '0')
        return "TTST-$digits"
    }

    private fun zipOf(vararg entries: Pair<String, ByteArray>): ByteArray {
        val output = ByteArrayOutputStream()
        ZipOutputStream(output).use { zip ->
            entries.forEach { (name, contents) ->
                zip.putNextEntry(ZipEntry(name))
                zip.write(contents)
                zip.closeEntry()
            }
        }
        return output.toByteArray()
    }
}

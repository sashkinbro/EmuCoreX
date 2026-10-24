package com.sbro.emucorex.data

import java.io.File
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.io.path.createTempDirectory
import org.junit.Assert.*
import org.junit.Test

class ShaderPackStoreTest {
    @Test fun multipleReferencesMayIncludeParameterOnlyFiles() = fixture { f ->
        f.store.installArchive(f.archive(mapOf(
            "base.slangp" to preset,
            "shared.slang" to shader,
            "values.params" to "BRIGHTNESS = 1.0\n",
            "alias.slangp" to "#reference \"base.slangp\"\n#reference \"values.params\"\n"
        )), official = true)
        assertEquals(setOf("base.slangp", "alias.slangp"), f.names())
    }

    @Test fun inheritedChainStillValidatesLocalPassAndTextureOverrides() = fixture { f ->
        f.store.installArchive(f.archive(mapOf(
            "base.slangp" to "$preset\ntextures = LUT\nLUT = texture.png\n",
            "shared.slang" to shader,
            "texture.png" to "texture",
            "bad-pass.slangp" to "#reference \"base.slangp\"\nshader0 = missing.slang\n",
            "bad-texture.slangp" to "#reference \"base.slangp\"\nLUT = ../outside.png\n",
            "good-alias.slangp" to "#reference \"base.slangp\"\n"
        )), official = true)
        assertEquals(setOf("base.slangp", "good-alias.slangp"), f.names())
    }

    @Test fun interruptedSwapRestoresPreviousPackWhenReopened() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        assertTrue(f.root.renameTo(File(f.root.parentFile, "${f.root.name}.backup")))
        val reopened = ShaderPackStore(f.root, f.cache)
        assertTrue(reopened.hasInstalledPack())
        assertEquals(2, reopened.listPresets().size)
    }

    @Test fun officialArchiveCompatibilityWhenDownloadedForIntegrationCheck() = fixture { f ->
        val archive = File("../build/shader-official-validation.zip")
        org.junit.Assume.assumeTrue("Optional downloaded upstream archive", archive.isFile)
        val count = f.store.installArchive(archive, official = true)
        assertTrue("Only $count official presets were usable", count > 2400)
        assertTrue(f.store.hasInstalledPack())
    }

    @Test fun importedPresetsDoNotMarkTheOfficialDownloadInstalled() = fixture { f ->
        assertEquals(2, f.store.installArchive(f.archive(), official = false))
        assertEquals(2, f.store.listPresets().size)
        assertFalse(f.store.hasInstalledPack())
        f.store.installArchive(f.archive(mapOf("official.slangp" to preset, "shared.slang" to shader)), official = true)
        assertTrue(f.store.hasInstalledPack())
        assertEquals(setOf("official.slangp"), f.names())
    }

    @Test fun officialMarkerAndPresetsSurviveANewStoreInstance() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        val reopened = ShaderPackStore(f.root, f.cache)
        assertTrue(reopened.hasInstalledPack())
        assertEquals(2, reopened.listPresets().size)
    }

    @Test fun customImportReplacesThePackAndClearsItsPreviousOfficialStatus() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        f.store.installArchive(f.archive(mapOf("custom.slangp" to preset, "shared.slang" to shader)), official = false)
        assertFalse(f.store.hasInstalledPack())
        assertEquals(setOf("custom.slangp"), f.names())
    }

    @Test fun filesWithoutAValidPresetDoNotCountAsAnInstalledPack() = fixture { f ->
        f.root.mkdirs()
        File(f.root, "invalid.slangp").writeText("not a shader preset")
        assertFalse(f.store.hasInstalledPack())
        assertTrue(f.store.listPresets().isEmpty())
    }

    @Test fun missingDependencyInvalidatesAnExistingOfficialPack() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        File(f.root, "shared.slang").delete()
        assertTrue(f.store.listPresets().isEmpty())
        assertFalse(f.store.hasInstalledPack())
    }

    @Test fun failedZipDownloadPreservesThePreviousOfficialPack() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        val bad = File(f.directory, "network-error.zip").apply { writeText("<html>download failed</html>") }
        f.assertRejectedWithoutReplacingOfficial(bad)
    }

    @Test fun truncatedZipPreservesThePreviousOfficialPack() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        val bad = f.archive().apply {
            val bytes = readBytes()
            writeBytes(bytes.copyOf(bytes.size / 2))
        }
        f.assertRejectedWithoutReplacingOfficial(bad)
    }

    @Test fun crcMismatchPreservesThePreviousOfficialPack() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        val bad = f.archive(stored = true)
        val bytes = bad.readBytes()
        val payload = shader.toByteArray()
        val offset = (0..bytes.size - payload.size).first { start ->
            payload.indices.all { bytes[start + it] == payload[it] }
        }
        // Keep the ZIP headers, sizes and central-directory CRC intact while
        // corrupting an uncompressed pass payload. Merely opening ZipFile is insufficient.
        bytes[offset] = (bytes[offset].toInt() xor 1).toByte()
        bad.writeBytes(bytes)
        f.assertRejectedWithoutReplacingOfficial(bad)
    }

    @Test fun zipWithoutAnyPresetPreservesThePreviousOfficialPack() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        f.assertRejectedWithoutReplacingOfficial(f.archive(mapOf("shared.slang" to shader)))
    }

    @Test fun presetMissingItsDeclaredPassPreservesThePreviousOfficialPack() = fixture { f ->
        f.store.installArchive(f.archive(), official = true)
        f.assertRejectedWithoutReplacingOfficial(f.archive(mapOf("missing.slangp" to preset)))
    }

    @Test fun everyDeclaredPassMustExist() = fixture { f ->
        val twoPasses = "shaders = 2\nshader0 = \"shared.slang\"\nshader1 = \"missing.slang\"\n"
        assertTrue(runCatching {
            f.store.installArchive(f.archive(mapOf("two.slangp" to twoPasses, "shared.slang" to shader)), true)
        }.isFailure)
        assertFalse(f.store.hasInstalledPack())
    }

    @Test fun presetCannotReferenceAPassOutsideThePack() = fixture { f ->
        val external = File(f.directory, "outside.slang").apply { writeText(shader) }
        val outside = "shaders = 1\nshader0 = \"${external.invariantSeparatorsPath}\"\n"
        assertTrue(runCatching { f.store.installArchive(f.archive(mapOf("bad.slangp" to outside)), true) }.isFailure)
        assertTrue(f.store.listPresets().isEmpty())
    }

    @Test fun nestedPresetReferencesRemainValid() = fixture { f ->
        val entries = linkedMapOf(
            "base.slangp" to preset,
            "alias.slangp" to "#reference \"base.slangp\"\n",
            "shared.slang" to shader
        )
        assertEquals(2, f.store.installArchive(f.archive(entries), true))
        assertEquals(setOf("base.slangp", "alias.slangp"), f.names())
    }

    @Test fun referenceCyclesAreRejected() = fixture { f ->
        val entries = mapOf(
            "one.slangp" to "#reference \"two.slangp\"\n",
            "two.slangp" to "#reference \"one.slangp\"\n"
        )
        assertTrue(runCatching { f.store.installArchive(f.archive(entries), true) }.isFailure)
        assertTrue(f.store.listPresets().isEmpty())
    }

    @Test fun deletingASelectionHidesOnlyThosePresetsAndPersistsAcrossReopening() = fixture { f ->
        f.store.installArchive(f.archive(), official = false)
        val selected = f.store.listPresets().single { File(it.absolutePath).name == "one.slangp" }
        assertEquals(1, f.store.removePresets(setOf(selected.absolutePath)).getOrThrow())
        assertEquals(setOf("two.slangp"), f.names())
        assertTrue(File(selected.absolutePath).isFile)
        assertTrue(File(f.root, "shared.slang").isFile)
        assertEquals(setOf("two.slangp"), ShaderPackStore(f.root, f.cache).listPresets().map { File(it.absolutePath).name }.toSet())
    }

    @Test fun hidingAReferencedPresetKeepsItsDependentPresetWorking() = fixture { f ->
        f.store.installArchive(f.archive(mapOf(
            "base.slangp" to preset,
            "alias.slangp" to "#reference \"base.slangp\"\n",
            "shared.slang" to shader
        )), official = false)
        val base = f.store.listPresets().single { File(it.absolutePath).name == "base.slangp" }
        f.store.removePresets(setOf(base.absolutePath)).getOrThrow()
        assertEquals(setOf("alias.slangp"), f.names())
        assertTrue(File(base.absolutePath).isFile)
        assertTrue(File(f.root, "shared.slang").isFile)
    }

    @Test fun reinstallingResetsHiddenPresetSelections() = fixture { f ->
        val archive = f.archive()
        f.store.installArchive(archive, official = false)
        f.store.removePresets(f.store.listPresets().map { it.absolutePath }.toSet()).getOrThrow()
        assertTrue(f.store.listPresets().isEmpty())
        f.store.installArchive(archive, official = false)
        assertEquals(setOf("one.slangp", "two.slangp"), f.names())
    }

    @Test fun deleteRejectsOutsideAndNonPresetPathsWithoutChangingTheSelection() = fixture { f ->
        f.store.installArchive(f.archive(), official = false)
        val selected = f.store.listPresets().first().absolutePath
        val outside = File(f.directory, "outside.slangp").apply { writeText(preset) }
        assertTrue(f.store.removePresets(setOf(selected, outside.absolutePath)).isFailure)
        assertEquals(2, f.store.listPresets().size)
        assertTrue(outside.isFile)
        assertTrue(f.store.removePresets(setOf(File(f.root, "shared.slang").absolutePath)).isFailure)
        assertEquals(2, f.store.listPresets().size)
    }

    private class Fixture {
        val directory = createTempDirectory("shader-store-test").toFile()
        val root = File(directory, "installed")
        val cache = File(directory, "cache").apply { mkdirs() }
        val store = ShaderPackStore(root, cache)
        private var nextArchive = 0

        fun names(): Set<String> = store.listPresets().map { File(it.absolutePath).name }.toSet()

        fun archive(entries: Map<String, String> = mapOf("one.slangp" to preset, "two.slangp" to preset, "shared.slang" to shader), stored: Boolean = false): File {
            val file = File(directory, "pack-${nextArchive++}.zip")
            ZipOutputStream(file.outputStream()).use { zip ->
                entries.forEach { (path, content) ->
                    val bytes = content.toByteArray()
                    val entry = ZipEntry(path)
                    if (stored) {
                        entry.method = ZipEntry.STORED
                        entry.size = bytes.size.toLong()
                        entry.crc = CRC32().apply { update(bytes) }.value
                    }
                    zip.putNextEntry(entry)
                    zip.write(bytes)
                    zip.closeEntry()
                }
            }
            return file
        }

        fun assertRejectedWithoutReplacingOfficial(archive: File) {
            val before = names()
            assertTrue("Invalid archive was accepted", runCatching { store.installArchive(archive, official = true) }.isFailure)
            assertEquals(before, names())
            assertTrue(store.hasInstalledPack())
        }
    }

    private fun fixture(block: (Fixture) -> Unit) {
        val fixture = Fixture()
        try { block(fixture) } finally { fixture.directory.deleteRecursively() }
    }

    companion object {
        private const val preset = "shaders = 1\nshader0 = \"shared.slang\"\n"
        private const val shader = "#version 450\n#pragma stage fragment\nvoid main() {}\n"
    }
}

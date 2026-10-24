package com.sbro.emucorex.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ShaderPackStoreInstrumentedTest {
    @Test fun repairAndRemovalSurviveAndroidFilesystemReopening() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val directory = File(context.cacheDir, "shader-device-test-${UUID.randomUUID()}").apply { mkdirs() }
        try {
            val root = File(directory, "installed")
            val archive = File(directory, "pack.zip")
            ZipOutputStream(archive.outputStream()).use { zip ->
                mapOf(
                    "base.slangp" to "shaders = 1\nshader0 = \"shared.slang\"\n",
                    "dependent.slangp" to "#reference \"base.slangp\"\n",
                    "shared.slang" to "#version 450\nvoid main() {}\n"
                ).forEach { (name, content) ->
                    zip.putNextEntry(ZipEntry(name)); zip.write(content.toByteArray()); zip.closeEntry()
                }
            }
            val store = ShaderPackStore(root, directory)
            assertEquals(2, store.installArchive(archive, official = true))
            val base = store.listPresets().single { it.label == "base" }.absolutePath
            assertEquals(1, store.removePresets(setOf(base)).getOrThrow())
            assertTrue(File(base).isFile)
            assertEquals(listOf("dependent"), ShaderPackStore(root, directory).listPresets().map { it.label })
            val failed = File(directory, "failed.zip").apply { writeText("network failure") }
            assertTrue(runCatching { store.installArchive(failed, official = true) }.isFailure)
            assertTrue(store.hasInstalledPack())
            assertEquals(listOf("dependent"), store.listPresets().map { it.label })
            assertEquals(2, store.installArchive(archive, official = true))
            assertEquals(2, ShaderPackStore(root, directory).listPresets().size)
        } finally { directory.deleteRecursively() }
    }
}

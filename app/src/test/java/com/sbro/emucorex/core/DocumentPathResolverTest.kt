package com.sbro.emucorex.core

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DocumentPathResolverTest {
    @Test
    fun atomicBiosDirectoryReplacementPromotesCompleteStagingDirectory() {
        val parent = Files.createTempDirectory("bios-replace").toFile()
        try {
            val target = File(parent, "imported-bios").apply { mkdirs() }
            File(target, "old-bios.bin").writeText("old")
            val staging = File(parent, "imported-bios-staging").apply { mkdirs() }
            File(staging, "[SCPH39001].bin").writeText("new")
            File(staging, ".source-uri").writeText("content://bios")

            assertTrue(DocumentPathResolver.replaceImportedBiosDirectory(target, staging))
            assertTrue(File(target, "[SCPH39001].bin").isFile)
            assertTrue(File(target, ".source-uri").isFile)
            assertFalse(File(target, "old-bios.bin").exists())
            assertFalse(staging.exists())
        } finally {
            parent.deleteRecursively()
        }
    }

    @Test
    fun failedBiosDirectoryReplacementRestoresPreviousDirectory() {
        val parent = Files.createTempDirectory("bios-restore").toFile()
        try {
            val target = File(parent, "imported-bios").apply { mkdirs() }
            File(target, "working-bios.bin").writeText("working")
            val missingStaging = File(parent, "missing-staging")

            assertFalse(DocumentPathResolver.replaceImportedBiosDirectory(target, missingStaging))
            assertTrue(File(target, "working-bios.bin").isFile)
        } finally {
            parent.deleteRecursively()
        }
    }
}

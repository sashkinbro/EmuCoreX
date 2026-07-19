package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File
import java.nio.file.Files

class SaveStateColdStartResolverTest {
    @Test
    fun resolvesExistingSlotByLibrarySerialBeforeNativeMetadataIsReady() {
        val directory = Files.createTempDirectory("save-state-cold-start").toFile()
        try {
            val expected = File(directory, "SLES-53045 (18C101A7).01.p2s").apply {
                writeBytes(byteArrayOf(1))
            }
            File(directory, "SLES-53045 (18C101A7).02.p2s").writeBytes(byteArrayOf(2))
            File(directory, "SLUS-20928 (12345678).01.p2s").writeBytes(byteArrayOf(3))
            File(directory, "SLES-53045 (18C101A7).01.p2s.backup").writeBytes(byteArrayOf(4))

            assertEquals(
                expected.absolutePath,
                findFallbackSaveStateFile(directory.listFiles().orEmpty(), "SLES 53045", 1)?.absolutePath
            )
            assertNull(findFallbackSaveStateFile(directory.listFiles().orEmpty(), "SLES-53045", 3))
            assertNull(findFallbackSaveStateFile(directory.listFiles().orEmpty(), null, 1))
        } finally {
            directory.deleteRecursively()
        }
    }
}

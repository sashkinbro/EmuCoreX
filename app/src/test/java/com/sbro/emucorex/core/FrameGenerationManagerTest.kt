package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class FrameGenerationManagerTest {
    @Test
    fun `frame generation requires Adreno 7xx or newer`() {
        assertFalse(FrameGenerationManager.supportsAdrenoFamily(null))
        assertFalse(FrameGenerationManager.supportsAdrenoFamily(AdrenoFamily.A6XX))
        assertTrue(FrameGenerationManager.supportsAdrenoFamily(AdrenoFamily.A7XX))
        assertTrue(FrameGenerationManager.supportsAdrenoFamily(AdrenoFamily.A8XX))
    }

    @Test
    fun `flow scale is clamped and rounded to supported quarter steps`() {
        assertEquals(25, FrameGenerationManager.normalizeFlowScale(-1))
        assertEquals(25, FrameGenerationManager.normalizeFlowScale(36))
        assertEquals(50, FrameGenerationManager.normalizeFlowScale(38))
        assertEquals(75, FrameGenerationManager.normalizeFlowScale(74))
        assertEquals(100, FrameGenerationManager.normalizeFlowScale(101))
    }

    @Test
    fun `PE validation rejects a file with only an MZ prefix`() {
        withTempFile(ByteArray(128).also {
            it[0] = 'M'.code.toByte()
            it[1] = 'Z'.code.toByte()
        }) { file ->
            assertFalse(FrameGenerationManager.hasPeSignature(file))
        }
    }

    @Test
    fun `PE validation accepts a bounded PE signature`() {
        val bytes = ByteArray(256)
        bytes[0] = 'M'.code.toByte()
        bytes[1] = 'Z'.code.toByte()
        bytes[0x3c] = 0x80.toByte()
        bytes[0x80] = 'P'.code.toByte()
        bytes[0x81] = 'E'.code.toByte()
        withTempFile(bytes) { file ->
            assertTrue(FrameGenerationManager.hasPeSignature(file))
        }
    }

    @Test
    fun `sha256 is stable for imported content`() {
        withTempFile("EmuCoreX".encodeToByteArray()) { file ->
            assertEquals(
                "4a73640cc302936bdd30141ec030bddbdaa7f38817b58690b82904564cfa0185",
                FrameGenerationManager.sha256(file)
            )
        }
    }

    private fun withTempFile(bytes: ByteArray, block: (File) -> Unit) {
        val file = kotlin.io.path.createTempFile("emucorex-framegen", ".bin").toFile()
        try {
            file.writeBytes(bytes)
            block(file)
        } finally {
            file.delete()
        }
    }
}

package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class FrameGenerationManagerTest {
    @Test
    fun `display rate follows frame generation settings without device clamping`() {
        assertEquals(0f, FrameGenerationRefreshRate.requested(FrameGenerationSettings()), 0f)
        assertEquals(
            120f,
            FrameGenerationRefreshRate.requested(FrameGenerationSettings(enabled = true, multiplier = 2)),
            0f
        )
        assertEquals(
            180f,
            FrameGenerationRefreshRate.requested(FrameGenerationSettings(enabled = true, multiplier = 3)),
            0f
        )
        assertEquals(
            240f,
            FrameGenerationRefreshRate.requested(FrameGenerationSettings(enabled = true, multiplier = 4)),
            0f
        )
        assertEquals(
            144f,
            FrameGenerationRefreshRate.requested(
                FrameGenerationSettings(enabled = true, multiplier = 2, targetRefreshRate = 144)
            ),
            0f
        )
    }

    @Test
    fun `android presentation mode follows request without clamping generator`() {
        val modes = listOf(
            FrameGenerationPresentationMode(1, 1272, 2772, 60f),
            FrameGenerationPresentationMode(2, 1272, 2772, 120f),
            FrameGenerationPresentationMode(3, 1272, 2772, 144f),
            FrameGenerationPresentationMode(4, 1272, 2772, 165f),
            FrameGenerationPresentationMode(5, 1080, 2354, 144f)
        )

        assertEquals(3, FrameGenerationRefreshRate.preferredDisplayModeId(144f, 1272, 2772, modes))
        assertEquals(4, FrameGenerationRefreshRate.preferredDisplayModeId(180f, 1272, 2772, modes))
        assertEquals(0, FrameGenerationRefreshRate.preferredDisplayModeId(0f, 1272, 2772, modes))
        assertEquals(180f, FrameGenerationRefreshRate.requested(FrameGenerationSettings(true, 3)), 0f)
    }

    @Test
    fun `low latency chooses highest clean multiple for ntsc`() {
        val modes = listOf(
            FrameGenerationPresentationMode(1, 1272, 2772, 60f),
            FrameGenerationPresentationMode(2, 1272, 2772, 90f),
            FrameGenerationPresentationMode(3, 1272, 2772, 120f),
            FrameGenerationPresentationMode(4, 1080, 2354, 165f)
        )

        assertEquals(120f, LowLatencyRefreshRate.requested(59.94f, 1272, 2772, modes), 0.01f)
    }

    @Test
    fun `low latency chooses clean pal multiple and ignores other resolutions`() {
        val modes = listOf(
            FrameGenerationPresentationMode(1, 1272, 2772, 60f),
            FrameGenerationPresentationMode(2, 1272, 2772, 100f),
            FrameGenerationPresentationMode(3, 1272, 2772, 120f),
            FrameGenerationPresentationMode(4, 1080, 2354, 150f)
        )

        assertEquals(100f, LowLatencyRefreshRate.requested(50f, 1272, 2772, modes), 0.01f)
    }

    @Test
    fun `low latency falls back near native cadence when no clean multiple exists`() {
        val modes = listOf(
            FrameGenerationPresentationMode(1, 1272, 2772, 60f),
            FrameGenerationPresentationMode(2, 1272, 2772, 90f)
        )

        assertEquals(60f, LowLatencyRefreshRate.requested(59.94f, 1272, 2772, modes), 0.01f)
        assertEquals(0f, LowLatencyRefreshRate.requested(0f, 1272, 2772, modes), 0f)
    }

    @Test
    fun `frame generation requires Adreno 7xx or newer`() {
        assertFalse(FrameGenerationManager.supportsAdrenoFamily(null))
        assertFalse(FrameGenerationManager.supportsAdrenoFamily(AdrenoFamily.A6XX))
        assertTrue(FrameGenerationManager.supportsAdrenoFamily(AdrenoFamily.A7XX))
        assertTrue(FrameGenerationManager.supportsAdrenoFamily(AdrenoFamily.A8XX))
    }

    @Test
    fun `installed setup stays configured independently of runtime gpu detection`() {
        withTempFile(ByteArray(1)) { dll ->
            val setup = FrameGenerationSetup(
                hardwareSupported = false,
                componentInstalled = true,
                componentVersion = 1,
                dllPath = dll.absolutePath,
                dllSha256 = null,
                settings = FrameGenerationSettings(enabled = true)
            )

            assertTrue(setup.isConfigured)
            assertFalse(setup.isReady)
        }
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

package com.sbro.emucorex.core

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class BinToIsoConversionTest {

    @Test
    fun `mode 1 raw cd sectors convert to 2048 byte iso payloads`() {
        val input = sectorsOf(mode1Sector(0x11), mode1Sector(0x22), mode1Sector(0x33))
        val output = ByteArrayOutputStream()
        val progress = mutableListOf<Pair<Long, Long>>()

        val skipped = ImageConversionManager.convertBinStreamToIso(
            source = ByteArrayInputStream(input),
            output = output,
            cueSheet = null,
            binName = "disc.bin",
            totalBytes = input.size.toLong(),
            checkCancelled = {},
            onProgress = { processed, total -> progress += processed to total }
        )

        assertFalse(skipped)
        val iso = output.toByteArray()
        assertEquals(3 * 2048, iso.size)
        assertPayload(iso, sectorIndex = 0, expected = 0x11)
        assertPayload(iso, sectorIndex = 1, expected = 0x22)
        assertPayload(iso, sectorIndex = 2, expected = 0x33)
        assertEquals(input.size.toLong() to input.size.toLong(), progress.last())
    }

    @Test
    fun `mode 2 form 1 uses the 24 byte payload offset`() {
        val input = sectorsOf(mode2Form1Sector(0x44))
        val output = ByteArrayOutputStream()

        val skipped = ImageConversionManager.convertBinStreamToIso(
            source = ByteArrayInputStream(input),
            output = output,
            cueSheet = null,
            binName = "disc.bin",
            totalBytes = input.size.toLong(),
            checkCancelled = {},
            onProgress = { _, _ -> }
        )

        assertFalse(skipped)
        assertPayload(output.toByteArray(), sectorIndex = 0, expected = 0x44)
    }

    @Test
    fun `mode 2 form 2 sectors are rejected as unsupported`() {
        val input = sectorsOf(mode2Form2Sector(0x55))

        try {
            ImageConversionManager.convertBinStreamToIso(
                source = ByteArrayInputStream(input),
                output = ByteArrayOutputStream(),
                cueSheet = null,
                binName = "disc.bin",
                totalBytes = input.size.toLong(),
                checkCancelled = {},
                onProgress = { _, _ -> }
            )
            fail("Expected unsupported_sector_mode")
        } catch (error: RawConversionException) {
            assertEquals("unsupported_sector_mode", error.resultCode)
        }
    }

    @Test
    fun `audio sectors after the data track are skipped and reported`() {
        val input = sectorsOf(mode1Sector(0x66), audioSector())
        val output = ByteArrayOutputStream()

        val skipped = ImageConversionManager.convertBinStreamToIso(
            source = ByteArrayInputStream(input),
            output = output,
            cueSheet = null,
            binName = "disc.bin",
            totalBytes = input.size.toLong(),
            checkCancelled = {},
            onProgress = { _, _ -> }
        )

        assertTrue(skipped)
        assertEquals(2048, output.size())
        assertPayload(output.toByteArray(), sectorIndex = 0, expected = 0x66)
    }

    @Test
    fun `plain 2048 images are copied through untouched`() {
        val payload = ByteArray(4096) { index -> (index % 251).toByte() }
        val output = ByteArrayOutputStream()

        val skipped = ImageConversionManager.convertBinStreamToIso(
            source = ByteArrayInputStream(payload),
            output = output,
            cueSheet = null,
            binName = "image.bin",
            totalBytes = payload.size.toLong(),
            checkCancelled = {},
            onProgress = { _, _ -> }
        )

        assertFalse(skipped)
        assertArrayEquals(payload, output.toByteArray())
    }

    @Test
    fun `plain images with a partial sector are rejected`() {
        val payload = ByteArray(3000)
        try {
            ImageConversionManager.convertBinStreamToIso(
                source = ByteArrayInputStream(payload),
                output = ByteArrayOutputStream(),
                cueSheet = null,
                binName = "image.bin",
                totalBytes = payload.size.toLong(),
                checkCancelled = {},
                onProgress = { _, _ -> }
            )
            fail("Expected invalid_bin")
        } catch (error: RawConversionException) {
            assertEquals("invalid_bin", error.resultCode)
        }
    }

    @Test
    fun `cue data track range start and end are honoured`() {
        val input = sectorsOf(
            mode1Sector(0x01),
            mode1Sector(0x02),
            mode1Sector(0x03),
            mode1Sector(0x04)
        )
        val cue = parseCueSheet(
            """
            FILE "disc.bin" BINARY
              TRACK 01 MODE1/2352
                INDEX 01 00:00:01
              TRACK 02 AUDIO
                INDEX 01 00:00:03
            """.trimIndent()
        )
        val output = ByteArrayOutputStream()

        val skipped = ImageConversionManager.convertBinStreamToIso(
            source = ByteArrayInputStream(input),
            output = output,
            cueSheet = cue,
            binName = "disc.bin",
            totalBytes = input.size.toLong(),
            checkCancelled = {},
            onProgress = { _, _ -> }
        )

        assertTrue(skipped)
        val iso = output.toByteArray()
        assertEquals(2 * 2048, iso.size)
        assertPayload(iso, sectorIndex = 0, expected = 0x02)
        assertPayload(iso, sectorIndex = 1, expected = 0x03)
    }

    @Test
    fun `cue with a 2048 sector data track copies only that range`() {
        val payload = ByteArray(3 * 2048) { index -> (index / 2048 + 1).toByte() }
        val cue = parseCueSheet(
            """
            FILE "image.bin" BINARY
              TRACK 01 MODE1/2048
                INDEX 01 00:00:01
              TRACK 02 AUDIO
                INDEX 01 00:00:02
            """.trimIndent()
        )
        val output = ByteArrayOutputStream()

        val skipped = ImageConversionManager.convertBinStreamToIso(
            source = ByteArrayInputStream(payload),
            output = output,
            cueSheet = cue,
            binName = "image.bin",
            totalBytes = payload.size.toLong(),
            checkCancelled = {},
            onProgress = { _, _ -> }
        )

        assertTrue(skipped)
        assertArrayEquals(payload.copyOfRange(2048, 4096), output.toByteArray())
    }

    @Test
    fun `cancellation stops the conversion`() {
        val input = sectorsOf(mode1Sector(0x77), mode1Sector(0x78))
        var calls = 0
        try {
            ImageConversionManager.convertBinStreamToIso(
                source = ByteArrayInputStream(input),
                output = ByteArrayOutputStream(),
                cueSheet = null,
                binName = "disc.bin",
                totalBytes = input.size.toLong(),
                checkCancelled = { calls++; if (calls > 1) throw kotlinx.coroutines.CancellationException() },
                onProgress = { _, _ -> }
            )
            fail("Expected cancellation")
        } catch (_: kotlinx.coroutines.CancellationException) {
            assertTrue(calls > 1)
        }
    }

    @Test
    fun `sector layout detection maps modes audio and form 2`() {
        assertEquals(16, ImageConversionManager.isoDataOffsetForSector(mode1Sector(0x00)))
        assertEquals(24, ImageConversionManager.isoDataOffsetForSector(mode2Form1Sector(0x00)))
        assertEquals(
            ImageConversionManager.SECTOR_UNSUPPORTED,
            ImageConversionManager.isoDataOffsetForSector(mode2Form2Sector(0x00))
        )
        assertEquals(
            ImageConversionManager.SECTOR_AUDIO,
            ImageConversionManager.isoDataOffsetForSector(audioSector())
        )
        assertEquals(
            ImageConversionManager.SECTOR_UNSUPPORTED,
            ImageConversionManager.isoDataOffsetForSector(ByteArray(8))
        )
    }

    @Test
    fun `cd sync detection accepts only the ff sync header`() {
        assertTrue(ImageConversionManager.hasCdSectorSync(mode1Sector(0x00)))
        val broken = mode1Sector(0x00).also { it[5] = 0x7F }
        assertFalse(ImageConversionManager.hasCdSectorSync(broken))
        assertFalse(ImageConversionManager.hasCdSectorSync(ByteArray(2352)))
    }

    @Test
    fun `skip fully fails on a truncated stream`() {
        try {
            ImageConversionManager.skipFully(ByteArrayInputStream(ByteArray(16)), 64L)
            fail("Expected invalid_bin")
        } catch (error: RawConversionException) {
            assertEquals("invalid_bin", error.resultCode)
        }
    }

    @Test
    fun `copy plain range respects the end offset`() {
        val payload = ByteArray(100) { index -> index.toByte() }
        val output = ByteArrayOutputStream()
        val source = ByteArrayInputStream(payload)
        source.skip(10)

        ImageConversionManager.copyPlainRange(
            source = source,
            output = output,
            processedBytes = 10L,
            endOffset = 40L,
            totalBytes = payload.size.toLong(),
            checkCancelled = {},
            onProgress = { _, _ -> }
        )

        assertArrayEquals(payload.copyOfRange(10, 40), output.toByteArray())
    }

    private fun sectorsOf(vararg sectors: ByteArray): ByteArray =
        sectors.reduce { acc, sector -> acc + sector }

    private fun assertPayload(iso: ByteArray, sectorIndex: Int, expected: Int) {
        val offset = sectorIndex * 2048
        for (index in 0 until 2048) {
            assertEquals(
                "byte $index of ISO sector $sectorIndex",
                expected.toByte(),
                iso[offset + index]
            )
        }
    }

    private fun mode1Sector(payload: Int): ByteArray = cdSector(mode = 1, form2 = false, payload = payload)

    private fun mode2Form1Sector(payload: Int): ByteArray = cdSector(mode = 2, form2 = false, payload = payload)

    private fun mode2Form2Sector(payload: Int): ByteArray = cdSector(mode = 2, form2 = true, payload = payload)

    private fun cdSector(mode: Int, form2: Boolean, payload: Int): ByteArray {
        val sector = ByteArray(2352)
        sector[0] = 0x00
        for (index in 1..10) sector[index] = 0xFF.toByte()
        sector[11] = 0x00
        sector[15] = mode.toByte()
        if (mode == 2) {
            sector[18] = if (form2) 0x20 else 0x00
        }
        val payloadOffset = if (mode == 1) 16 else 24
        for (index in 0 until 2048) {
            sector[payloadOffset + index] = payload.toByte()
        }
        return sector
    }

    private fun audioSector(): ByteArray = ByteArray(2352) { 0x11 }
}

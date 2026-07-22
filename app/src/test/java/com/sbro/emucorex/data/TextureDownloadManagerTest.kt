package com.sbro.emucorex.data

import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Test

class TextureDownloadManagerTest {
    @Test
    fun `download progress is clamped to a valid range`() {
        assertEquals(0f, task(downloaded = 10L, total = 0L).progress)
        assertEquals(0.5f, task(downloaded = 50L, total = 100L).progress)
        assertEquals(1f, task(downloaded = 150L, total = 100L).progress)
    }

    @Test
    fun `estimated duration uses compact clock formatting`() {
        assertEquals("0:00", formatDownloadDuration(-10L))
        assertEquals("1:05", formatDownloadDuration(65L))
        assertEquals("1:01:01", formatDownloadDuration(3_661L))
    }

    @Test
    fun `download byte formatting keeps useful precision`() {
        val previous = Locale.getDefault()
        try {
            Locale.setDefault(Locale.US)
            assertEquals("512 B", formatDownloadBytes(512L))
            assertEquals("1.5 KB", formatDownloadBytes(1_536L))
            assertEquals("2.00 GB", formatDownloadBytes(2L * 1024L * 1024L * 1024L))
        } finally {
            Locale.setDefault(previous)
        }
    }

    private fun task(downloaded: Long, total: Long) = TextureDownloadTask(
        key = "0123456789abcdef0123456789abcdef",
        packId = "pack",
        packName = "Pack",
        version = "1",
        serial = "SLUS-00000",
        downloadUrl = "https://example.com/pack.zip",
        sha256 = "00",
        totalBytes = total,
        downloadedBytes = downloaded,
        bytesPerSecond = 0L,
        etaSeconds = 0L,
        status = TextureDownloadStatus.DOWNLOADING,
        error = "",
        updatedAt = 0L
    )
}

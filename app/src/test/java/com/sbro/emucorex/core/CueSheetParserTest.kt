package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CueSheetParserTest {

    @Test
    fun `parses a single data track with its start frame`() {
        val sheet = parseCueSheet(
            """
            FILE "Game.bin" BINARY
              TRACK 01 MODE1/2352
                INDEX 01 00:00:00
            """.trimIndent()
        )

        val track = requireNotNull(sheet).dataBlockFor("game.bin")?.tracks?.single()
        assertEquals(1, track?.number)
        assertEquals("MODE1/2352", track?.mode)
        assertEquals(0L, track?.startFrame)
        assertTrue(track != null && track.isDataTrack())
        assertFalse(sheet.hasAudioTracks)
    }

    @Test
    fun `detects audio tracks and track boundaries`() {
        val sheet = parseCueSheet(
            """
            FILE "disc.bin" BINARY
              TRACK 01 MODE2/2352
                INDEX 01 00:00:00
              TRACK 02 AUDIO
                INDEX 01 02:30:00
            """.trimIndent()
        )

        requireNotNull(sheet)
        assertTrue(sheet.hasAudioTracks)
        val block = requireNotNull(sheet.dataBlockFor("disc.bin"))
        val dataTrack = requireNotNull(block.tracks.firstOrNull(CueTrackInfo::isDataTrack))
        assertEquals("MODE2/2352", dataTrack.mode)
        assertEquals(2352, dataTrack.sectorBytes())
        assertEquals((2 * 60 + 30) * 75L, block.trackEndFrame(dataTrack))
    }

    @Test
    fun `falls back to the first data block for a different file spelling`() {
        val sheet = parseCueSheet(
            """
            FILE "SomeLongName.bin" BINARY
              TRACK 01 MODE1/2048
                INDEX 01 00:00:00
            """.trimIndent()
        )

        val track = requireNotNull(sheet).dataBlockFor("other.bin")?.tracks?.single()
        assertEquals(2048, requireNotNull(track).sectorBytes())
    }

    @Test
    fun `parses mm ss ff cue frames at 75 frames per second`() {
        assertEquals(0L, parseCueFrame("00:00:00"))
        assertEquals(74L, parseCueFrame("00:00:74"))
        assertEquals(75L, parseCueFrame("00:01:00"))
        assertEquals(9000L, parseCueFrame("02:00:00"))
        assertNull(parseCueFrame("00:99:00"))
        assertNull(parseCueFrame("nonsense"))
    }

    @Test
    fun `ignores empty and comment-only sheets`() {
        assertNull(parseCueSheet(""))
        assertNull(parseCueSheet("REM just a comment"))
    }
}

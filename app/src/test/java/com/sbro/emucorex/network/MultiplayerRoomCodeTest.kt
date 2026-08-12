package com.sbro.emucorex.network

import java.security.SecureRandom
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MultiplayerRoomCodeTest {
    @Test
    fun sanitize_removesAmbiguousAndUnsupportedCharacters() {
        assertEquals("AB3D7K9P", sanitizeMultiplayerRoomCode(" ab-3d7k9p0io1 "))
    }

    @Test
    fun sanitize_limitsRoomCodeLength() {
        assertEquals("ABCDEFGH", sanitizeMultiplayerRoomCode("abcdefghjkmn"))
    }

    @Test
    fun generatedCodesAreValidAndUnambiguous() {
        repeat(100) {
            val code = generateMultiplayerRoomCode(SecureRandom())
            assertEquals(MULTIPLAYER_ROOM_CODE_LENGTH, code.length)
            assertTrue(code.all { it in MULTIPLAYER_ROOM_ALPHABET })
            assertFalse(code.any { it in "01IO" })
        }
    }

    @Test
    fun digitalTouchPressUsesFullNetworkRange() {
        assertEquals(255, encodeNetworkButtonRange(range = 0, pressed = true))
    }

    @Test
    fun analogPressureIsPreservedAndReleaseIsZero() {
        assertEquals(127, encodeNetworkButtonRange(range = 127, pressed = true))
        assertEquals(255, encodeNetworkButtonRange(range = 999, pressed = true))
        assertEquals(0, encodeNetworkButtonRange(range = 127, pressed = false))
    }
}

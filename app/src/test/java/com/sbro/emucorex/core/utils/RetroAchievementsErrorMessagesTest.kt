package com.sbro.emucorex.core.utils

import com.sbro.emucorex.core.utils.RetroAchievementsErrorMessages.Kind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class RetroAchievementsErrorMessagesTest {
    @Test
    fun numericRcClientCodesMap() {
        assertEquals(Kind.CREDENTIALS, RetroAchievementsErrorMessages.classify("-34"))
        assertEquals(Kind.TOKEN_EXPIRED, RetroAchievementsErrorMessages.classify("-35"))
        assertEquals(Kind.NETWORK, RetroAchievementsErrorMessages.classify("-32"))
        assertEquals(Kind.LOGIN_REQUIRED, RetroAchievementsErrorMessages.classify(" -28 "))
        assertEquals(Kind.NO_GAME, RetroAchievementsErrorMessages.classify("-29"))
        assertEquals(Kind.UNKNOWN, RetroAchievementsErrorMessages.classify("-999"))
    }

    @Test
    fun nativeTextsMap() {
        assertEquals(Kind.CREDENTIALS, RetroAchievementsErrorMessages.classify("Invalid credentials"))
        assertEquals(Kind.TOKEN_EXPIRED, RetroAchievementsErrorMessages.classify("The token has expired"))
        assertEquals(Kind.TIMEOUT, RetroAchievementsErrorMessages.classify("Request timed out"))
        assertEquals(Kind.NETWORK, RetroAchievementsErrorMessages.classify("Could not resolve host"))
    }

    @Test
    fun unknownAndBlankFallBackToNativeText() {
        assertEquals(Kind.UNKNOWN, RetroAchievementsErrorMessages.classify(null))
        assertEquals(Kind.UNKNOWN, RetroAchievementsErrorMessages.classify("something odd"))
        assertNull(RetroAchievementsErrorMessages.stringRes(Kind.UNKNOWN))
        assertNotNull(RetroAchievementsErrorMessages.stringRes(Kind.CREDENTIALS))
        // No context: native text wins, then the English default.
        assertEquals("native text", RetroAchievementsErrorMessages.resolve(null, "something odd", "native text"))
        assertEquals("RetroAchievements is unavailable right now.", RetroAchievementsErrorMessages.resolve(null, "-1", null))
    }
}

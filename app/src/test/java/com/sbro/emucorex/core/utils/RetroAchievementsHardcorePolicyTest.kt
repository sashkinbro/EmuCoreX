package com.sbro.emucorex.core.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RetroAchievementsHardcorePolicyTest {
    @Test fun inheritedRequestTracksTheGlobalDefaultInBothDirections() {
        assertFalse(RetroAchievementsHardcorePolicy.resolve(false, null, false))
        assertTrue(RetroAchievementsHardcorePolicy.resolve(true, null, false))
    }

    @Test fun explicitGameChoicesOverrideEitherGlobalDefault() {
        assertTrue(RetroAchievementsHardcorePolicy.resolve(false, true, false))
        assertTrue(RetroAchievementsHardcorePolicy.resolve(true, true, false))
        assertFalse(RetroAchievementsHardcorePolicy.resolve(true, false, false))
        assertFalse(RetroAchievementsHardcorePolicy.resolve(false, false, false))
    }

    @Test fun temporaryTestHostSuppressesEveryEffectiveRequest() {
        for (global in listOf(false, true)) {
            for (override in listOf(null, false, true)) {
                assertFalse(RetroAchievementsHardcorePolicy.resolve(global, override, true))
            }
        }
    }

    @Test fun pauseCooldownRoundsUpToWholeSecondsAndNeverShowsZero() {
        assertEquals(0, RetroAchievementsHardcorePolicy.pauseCooldownSeconds(0))
        assertEquals(1, RetroAchievementsHardcorePolicy.pauseCooldownSeconds(1))
        assertEquals(1, RetroAchievementsHardcorePolicy.pauseCooldownSeconds(60))
        assertEquals(2, RetroAchievementsHardcorePolicy.pauseCooldownSeconds(61))
        assertEquals(5, RetroAchievementsHardcorePolicy.pauseCooldownSeconds(300))
    }

    @Test fun pauseScrimIsAtLeastSeventyPercentOpaque() {
        assertTrue(RetroAchievementsHardcorePolicy.HARDCORE_PAUSE_SCRIM_ALPHA >= 0.7f)
    }

    @Test fun userAgentFollowsProductSlashNumericVersionSystemExtensions() {
        assertEquals(
            "EmuCoreX/0.6.1 (Android 14) pcsx2/2.8.0",
            RetroAchievementsHardcorePolicy.userAgent("0.6.1", "14")
        )
        // No "v" prefix, no build suffix, padded to three components.
        assertEquals("EmuCoreX/1.2.0 (Android) pcsx2/2.8.0", RetroAchievementsHardcorePolicy.userAgent("v1.2-debug", null))
        assertEquals("EmuCoreX/0.0.0 (Android) pcsx2/2.8.0", RetroAchievementsHardcorePolicy.userAgent("", ""))
    }

    @Test fun numericVersionRejectsGarbage() {
        assertEquals("0.0.0", RetroAchievementsHardcorePolicy.numericVersion(null))
        assertEquals("0.0.0", RetroAchievementsHardcorePolicy.numericVersion("beta"))
        assertEquals("0.0.0", RetroAchievementsHardcorePolicy.numericVersion("..1"))
        assertEquals("3.0.0", RetroAchievementsHardcorePolicy.numericVersion("3"))
        assertEquals("0.6.1", RetroAchievementsHardcorePolicy.numericVersion("0.6.1.0".substringBeforeLast(".0")))
    }
}

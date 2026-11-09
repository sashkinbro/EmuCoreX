package com.sbro.emucorex.core.utils

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
}

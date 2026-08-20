package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class EmuCoreXAchievementCatalogTest {
    @Test
    fun catalog_hasAtLeastOneHundredUniqueAchievementsAcrossDifferentMetrics() {
        val definitions = EmuCoreXAchievementCatalog.definitions
        assertTrue(definitions.size >= 100)
        assertEquals(definitions.size, definitions.map { it.id }.distinct().size)
        assertTrue(definitions.map { it.metric }.distinct().size >= 10)
        assertTrue(definitions.count { it.hidden } >= 30)
    }

    @Test
    fun completedProgressUnlocksLocallyWhenCloudWriteIsUnavailable() {
        val definition = EmuAchievementDefinition(
            id = "test",
            titleRes = 0,
            descriptionRes = 0,
            metric = EmuAchievementMetric.TotalPlayTimeMinutes,
            target = 30,
            points = 10
        )

        val state = resolveAchievementState(
            definition = definition,
            progress = 30,
            cloudUnlockedAtMs = null,
            localCompletedAtMs = 1234L
        )

        assertTrue(state.unlocked)
        assertEquals(30L, state.progress)
        assertEquals(1234L, state.unlockedAtMs)
    }
}

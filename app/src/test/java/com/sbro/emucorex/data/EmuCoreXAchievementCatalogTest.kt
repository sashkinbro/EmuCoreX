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
}

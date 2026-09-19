package com.sbro.emucorex.ui.cheats

import com.sbro.emucorex.data.CheatBlock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CheatCategoriesTest {
    private fun block(id: String, title: String, enabled: Boolean = false) = CheatBlock(
        id = id,
        title = title,
        lines = listOf("patch=1,EE,00000000,word,00000000"),
        enabled = enabled
    )

    @Test
    fun groupsAreEmittedInCategoryOrderAndKeepTheirCheats() {
        val blocks = listOf(
            block("money", "Infinite Money"),
            block("weapon", "All Weapons"),
            block("misc", "Debug Camera"),
            block("health", "Max Health")
        )

        val groups = groupCheatBlocks(blocks)

        assertEquals(listOf(CheatCategory.PLAYER, CheatCategory.ITEMS, CheatCategory.OTHER), groups.map { it.first })
        assertEquals(listOf("money", "health"), groups[0].second.map { it.id })
        assertEquals(listOf("weapon"), groups[1].second.map { it.id })
        assertEquals(listOf("misc"), groups[2].second.map { it.id })
    }

    @Test
    fun hotkeyCheatsTakePriorityOverOtherKeywords() {
        val blocks = listOf(block("press", "Press L1 For Infinite Health"))

        val groups = groupCheatBlocks(blocks)

        assertEquals(CheatCategory.HOTKEYS, groups.single().first)
    }

    @Test
    fun emptyInputProducesNoGroups() {
        assertTrue(groupCheatBlocks(emptyList()).isEmpty())
    }
}

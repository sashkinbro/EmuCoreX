package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MemoryCardRepositoryTest {

    @Test
    fun `standard default card is a classic 8 MB PS2 file`() {
        assertEquals(MEMORY_CARD_TYPE_FILE, STANDARD_DEFAULT_MEMORY_CARD_SPEC.type)
        assertEquals(MEMORY_CARD_FILE_TYPE_PS2_8_MB, STANDARD_DEFAULT_MEMORY_CARD_SPEC.fileType)
    }

    @Test
    fun `card names with spaces are used exactly and default cards are not substituted`() {
        val existing = setOf("Mcd001 .ps2", "Mcd001.ps2", "Mcd002.ps2")
        val resolved = resolveMemoryCardAssignments(
            MemoryCardAssignments("Mcd001 .ps2", null),
            existing
        )
        assertEquals("Mcd001 .ps2", resolved.slot1)
        assertNull(resolved.slot2)
    }

    @Test
    fun `empty slots stay empty even when other cards exist`() {
        val existing = setOf("Mcd001.ps2", "Mcd002.ps2")
        val resolved = resolveMemoryCardAssignments(MemoryCardAssignments(null, null), existing)
        assertNull(resolved.slot1)
        assertNull(resolved.slot2)
    }

    @Test
    fun `missing card file resolves to an empty slot`() {
        val resolved = resolveMemoryCardAssignments(
            MemoryCardAssignments("Gone.ps2", "Mcd002.ps2"),
            setOf("Mcd002.ps2")
        )
        assertNull(resolved.slot1)
        assertEquals("Mcd002.ps2", resolved.slot2)
    }

    @Test
    fun `slot two never mounts the same card as slot one`() {
        val resolved = resolveMemoryCardAssignments(
            MemoryCardAssignments("A.ps2", "a.ps2"),
            setOf("A.ps2")
        )
        assertEquals("A.ps2", resolved.slot1)
        assertNull(resolved.slot2)
    }

    @Test
    fun `custom assignments are preserved when both are valid`() {
        val resolved = resolveMemoryCardAssignments(
            MemoryCardAssignments("Mcd002.ps2", "Mcd001.ps2"),
            setOf("Mcd001.ps2", "Mcd002.ps2")
        )
        assertEquals("Mcd002.ps2", resolved.slot1)
        assertEquals("Mcd001.ps2", resolved.slot2)
    }
}

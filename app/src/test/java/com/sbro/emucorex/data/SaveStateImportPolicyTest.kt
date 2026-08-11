package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SaveStateImportPolicyTest {
    @Test
    fun parsesExternalNamesAndNormalizesSerials() {
        assertEquals(
            ParsedExternalSaveStateName("SLES-53045", "18C101A7", 2),
            parseExternalSaveStateName("files/sstates/SLES 53045 (18c101a7).02.p2s")
        )
        assertNull(parseExternalSaveStateName("../../not-a-state.p2s"))
        assertNull(parseExternalSaveStateName("SLES-53045 (18C101A7).99.p2s"))
    }

    @Test
    fun recognizesCurrentAndLegacyStateVersions() {
        assertEquals(SaveStateImportFormat.CURRENT, saveStateFormatForVersion(0x9A590000.toInt()))
        assertEquals(SaveStateImportFormat.AETHERSX2, saveStateFormatForVersion(0x9A2C0000.toInt()))
        assertEquals(SaveStateImportFormat.NETHERSX2, saveStateFormatForVersion(0x9A340000.toInt()))
        assertEquals(SaveStateImportFormat.UNKNOWN, saveStateFormatForVersion(0x9A590001.toInt()))
        assertEquals(SaveStateImportFormat.UNKNOWN, saveStateFormatForVersion(null))
    }

    @Test
    fun preservesFreeSlotAndFallsBackWithoutUsingAutosave() {
        assertEquals(4, allocateSaveStateSlot(preferred = 4, occupied = setOf(0, 1)))
        assertEquals(2, allocateSaveStateSlot(preferred = 1, occupied = setOf(0, 1)))
        assertNull(allocateSaveStateSlot(preferred = 1, occupied = (0..10).toSet()))
    }
}

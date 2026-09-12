package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeAppArcadePathTest {
    @Test
    fun parentOfNestedDocumentUsesSlashSeparator() {
        assertEquals(
            "primary:Games/tekken5",
            ArcadeDocumentIds.parentDocumentId("primary:Games/tekken5/tekken5.acgame")
        )
    }

    @Test
    fun parentOfRootLevelDocumentFallsBackToVolumeRoot() {
        assertEquals("primary:", ArcadeDocumentIds.parentDocumentId("primary:tekken5.acgame"))
        assertEquals("1234-5678:", ArcadeDocumentIds.parentDocumentId("1234-5678:tekken5.acgame"))
    }

    @Test
    fun parentOfOpaqueDocumentIsUnknown() {
        assertNull(ArcadeDocumentIds.parentDocumentId("12345"))
    }

    @Test
    fun appendKeepsVolumeRootWithoutExtraSeparator() {
        assertEquals("primary:tekken5.acgame", ArcadeDocumentIds.appendDocumentId("primary:", "tekken5.acgame"))
        assertEquals("primary:Games/data", ArcadeDocumentIds.appendDocumentId("primary:Games", "data"))
    }

    @Test
    fun ascendStopsAtVolumeRoot() {
        assertEquals("primary:Games", ArcadeDocumentIds.ascendDocumentId("primary:Games/tekken5"))
        assertEquals("primary:", ArcadeDocumentIds.ascendDocumentId("primary:tekken5.acgame"))
        assertNull(ArcadeDocumentIds.ascendDocumentId("primary:"))
        assertNull(ArcadeDocumentIds.ascendDocumentId("12345"))
    }

    @Test
    fun ascendClampsAtGrantedTreeRoot() {
        val floor = "primary:Arcade Namco"
        assertEquals(
            "primary:Arcade Namco",
            ArcadeDocumentIds.ascendDocumentId("primary:Arcade Namco/tekken4", floor)
        )
        assertEquals(
            "primary:Arcade Namco",
            ArcadeDocumentIds.ascendDocumentId("primary:Arcade Namco", floor)
        )
    }

    @Test
    fun sharedMemcardCandidateClampsAtGrantedRoot() {
        val floor = "primary:Arcade Namco"
        var id = "primary:Arcade Namco/tekken4"
        for (part in listOf("..", "..", "memcards", "NM00004.ps2")) {
            id = if (part == "..") {
                ArcadeDocumentIds.ascendDocumentId(id, floor)!!
            } else {
                ArcadeDocumentIds.appendDocumentId(id, part)
            }
        }
        assertEquals("primary:Arcade Namco/memcards/NM00004.ps2", id)
    }
}

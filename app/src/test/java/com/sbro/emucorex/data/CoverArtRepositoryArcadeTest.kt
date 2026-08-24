package com.sbro.emucorex.data

import org.junit.Assert.assertTrue
import org.junit.Test

class CoverArtRepositoryArcadeTest {

    @Test
    fun everyBundledNamcoGameIdHasDefaultArtwork() {
        val bundledSerials = setOf(
            "NM00001", "NM00002", "NM00003", "NM00004", "NM00005",
            "NM00006", "NM00007", "NM00008", "NM00010", "NM00011",
            "NM00012", "NM00015", "NM00016", "NM00018", "NM00019",
            "NM00021", "NM00025", "NM00026", "NM00027", "NM00031",
            "NM00032", "NM00039", "NM00042", "NM00047", "NM00048"
        )

        bundledSerials.forEach { serial ->
            assertTrue("Missing default artwork for $serial", CoverArtRepository.hasDefaultArcadeCover(serial))
        }
    }
}

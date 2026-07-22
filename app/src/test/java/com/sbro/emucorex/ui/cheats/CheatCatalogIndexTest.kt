package com.sbro.emucorex.ui.cheats

import com.sbro.emucorex.data.RemoteCheatPack
import org.junit.Assert.assertEquals
import org.junit.Test

class CheatCatalogIndexTest {
    @Test
    fun `exact crc keeps matching and serialless packs only`() {
        val exact = pack("exact", "Bully", listOf("SLUS-21269"), "28703748")
        val serialless = pack("serialless", "Bully", emptyList(), "28703748")
        val wrongRegion = pack("wrong-region", "Bully", listOf("SLES-53561"), "28703748")
        val index = indexRemoteCheatCatalog(listOf(exact, serialless, wrongRegion))

        val selected = selectRemoteCheatPacks(index, "SLUS-21269", "28703748", "Bully")

        assertEquals(listOf("exact", "serialless"), selected.compatible.map { it.id })
        assertEquals(listOf("wrong-region"), selected.otherVersions.map { it.id })
    }

    @Test
    fun `serial matching also includes title-only catalog entries`() {
        val serial = pack("serial", "Gran Turismo 3 - A-Spec", listOf("SCES-50294"), "B590CE04")
        val titleOnly = pack("title-only", "Gran Turismo 3 A-Spec", emptyList(), "11111111")
        val otherVersion = pack("other", "Gran Turismo 3 A-Spec", listOf("SCUS-97102"), "85AE91B3")
        val index = indexRemoteCheatCatalog(listOf(serial, titleOnly, otherVersion))

        val selected = selectRemoteCheatPacks(index, "SCES-50294", null, "Gran Turismo 3: A-Spec")

        assertEquals(setOf("serial", "title-only"), selected.compatible.map { it.id }.toSet())
        assertEquals(listOf("other"), selected.otherVersions.map { it.id })
    }

    private fun pack(id: String, title: String, serials: List<String>, crc: String) = RemoteCheatPack(
        id = id,
        title = title,
        serials = serials,
        crc = crc,
        authors = listOf("Author"),
        description = "Cheats",
        downloadUrl = "https://example.com/$id.pnach",
        sourceUrl = "https://example.com/$id",
        sourceName = "Test",
        license = "Test",
        blockCount = 1
    )
}

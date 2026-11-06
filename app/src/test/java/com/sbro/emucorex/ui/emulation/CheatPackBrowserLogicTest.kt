package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.RemoteCheatPack
import com.sbro.emucorex.ui.cheats.RemoteCheatSelection
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CheatPackBrowserLogicTest {

    // --- PNACH install key -------------------------------------------------

    @Test
    fun `crc qualified key is preferred`() {
        assertEquals("SLUS-21269_28703748", resolveCheatPackGameKey(" SLUS-21269 ", "28703748"))
    }

    @Test
    fun `crc is normalized to uppercase`() {
        assertEquals("SLES-12345_ABCDEF01", resolveCheatPackGameKey("SLES-12345", "abcdef01"))
    }

    @Test
    fun `crc only is used when the serial is unknown`() {
        assertEquals("28703748", resolveCheatPackGameKey("", "28703748"))
    }

    @Test
    fun `serial only is used when the crc is unknown`() {
        assertEquals("SLUS-21269", resolveCheatPackGameKey("SLUS-21269", " "))
    }

    @Test
    fun `fallback file name is used without any identity`() {
        assertEquals("my_cheats", resolveCheatPackGameKey("", "", " my_cheats "))
    }

    @Test
    fun `blank identity and fallback resolve to null`() {
        assertNull(resolveCheatPackGameKey(" ", " ", "  "))
        assertNull(resolveCheatPackGameKey("", ""))
    }

    // --- Mirror dedupe ----------------------------------------------------

    @Test
    fun `mirrored packs collapse into a single card`() {
        val first = pack(id = "first")
        val mirror = pack(id = "mirror", title = "  bully ")

        assertEquals(listOf("first"), distinctCheatPacksForGame(listOf(first, mirror)).map { it.id })
    }

    @Test
    fun `packs with a different block count stay distinct`() {
        val small = pack(id = "small", blockCount = 1)
        val large = pack(id = "large", blockCount = 486)

        assertEquals(
            listOf("small", "large"),
            distinctCheatPacksForGame(listOf(small, large)).map { it.id }
        )
    }

    @Test
    fun `packs for another crc stay distinct`() {
        val ntsc = pack(id = "ntsc", crc = "07652DD9")
        val pal = pack(id = "pal", crc = "B5A7735B")

        assertEquals(listOf("ntsc", "pal"), distinctCheatPacksForGame(listOf(ntsc, pal)).map { it.id })
    }

    // --- Catalog selection for the running game ---------------------------

    @Test
    fun `selection prefers the running crc and collapses mirrors`() {
        val selection = selectInGameCheatPacks(
            entries = catalogFixture(),
            serial = "SLUS-21269",
            crc = "28703748",
            gameTitle = "Bully"
        )

        assertEquals(listOf("mirror-a", "small"), selection.compatible.map(RemoteCheatPack::id))
        assertEquals(listOf("pal-revision"), selection.otherVersions.map(RemoteCheatPack::id))
    }

    @Test
    fun `selection falls back to the serial when the crc is unknown`() {
        val selection = selectInGameCheatPacks(
            entries = catalogFixture(),
            serial = "SLUS-21269",
            crc = "",
            gameTitle = "Bully"
        )

        assertEquals(listOf("mirror-a", "small"), selection.compatible.map(RemoteCheatPack::id))
        assertEquals(listOf("pal-revision"), selection.otherVersions.map(RemoteCheatPack::id))
    }

    @Test
    fun `selection keeps crc matches without serial metadata`() {
        val free = pack(id = "free", serials = emptyList(), blockCount = 3)
        val wrongSerial = pack(id = "wrong-serial", serials = listOf("SLES-53561"), blockCount = 3)

        val selection = selectInGameCheatPacks(
            entries = listOf(free, wrongSerial),
            serial = "SLUS-21269",
            crc = "28703748",
            gameTitle = "Bully"
        )

        assertEquals(listOf("free"), selection.compatible.map(RemoteCheatPack::id))
    }

    @Test
    fun `selection is empty for an empty catalog`() {
        val selection = selectInGameCheatPacks(
            entries = emptyList(),
            serial = "SLUS-21269",
            crc = "28703748",
            gameTitle = "Bully"
        )

        assertTrue(selection.compatible.isEmpty())
        assertTrue(selection.otherVersions.isEmpty())
    }

    // --- Browser state machine -------------------------------------------

    @Test
    fun `beginning a load resets results and keeps badges for the same game`() {
        val previous = CheatPackBrowserState(
            phase = CheatPackBrowserPhase.Content,
            identityKey = "SLUS-21269|28703748|Bully",
            compatiblePacks = listOf(pack(id = "a")),
            installedPackIds = setOf("a"),
            failedPackId = "a",
            importSucceeded = true
        )

        val loading = previous.beginCatalogLoad(
            identityKey = "SLUS-21269|28703748|Bully",
            serial = "SLUS-21269",
            crc = "28703748"
        )

        assertEquals(CheatPackBrowserPhase.Loading, loading.phase)
        assertTrue(loading.compatiblePacks.isEmpty())
        assertEquals(setOf("a"), loading.installedPackIds)
        assertNull(loading.failedPackId)
        assertNull(loading.importSucceeded)
        assertFalse(loading.isBusy)
    }

    @Test
    fun `beginning a load for another game drops install badges`() {
        val loading = CheatPackBrowserState(identityKey = "old", installedPackIds = setOf("a"))
            .beginCatalogLoad(identityKey = "new", serial = "SLUS-1", crc = "AABBCCDD")

        assertTrue(loading.installedPackIds.isEmpty())
    }

    @Test
    fun `catalog failure shows the error phase`() {
        val failed = CheatPackBrowserState(phase = CheatPackBrowserPhase.Loading)
            .applyCatalogSelection(selection = null)

        assertEquals(CheatPackBrowserPhase.Error, failed.phase)
    }

    @Test
    fun `empty catalog selection shows the empty phase`() {
        val empty = CheatPackBrowserState(phase = CheatPackBrowserPhase.Loading)
            .applyCatalogSelection(selection = RemoteCheatSelection(emptyList(), emptyList()))

        assertEquals(CheatPackBrowserPhase.Empty, empty.phase)
    }

    @Test
    fun `non-empty selection becomes content and replaces both lists`() {
        val compatible = listOf(pack(id = "a"))
        val otherVersions = listOf(pack(id = "b", crc = "B5A7735B"))

        val content = CheatPackBrowserState(phase = CheatPackBrowserPhase.Loading)
            .applyCatalogSelection(RemoteCheatSelection(compatible, otherVersions))

        assertEquals(CheatPackBrowserPhase.Content, content.phase)
        assertEquals(compatible, content.compatiblePacks)
        assertEquals(otherVersions, content.otherVersionPacks)
    }

    @Test
    fun `starting a pack install clears older feedback`() {
        val working = CheatPackBrowserState(failedPackId = "old", importSucceeded = true)
            .startPackInstall("new")

        assertEquals("new", working.workingPackId)
        assertNull(working.failedPackId)
        assertNull(working.importSucceeded)
        assertTrue(working.isBusy)
    }

    @Test
    fun `pack install failure stops the spinner and records the pack`() {
        val failed = CheatPackBrowserState(workingPackId = "a").failPackInstall("a")

        assertNull(failed.workingPackId)
        assertEquals("a", failed.failedPackId)
        assertFalse(failed.isBusy)
    }

    @Test
    fun `successful installs accumulate and keep earlier badges`() {
        val first = CheatPackBrowserState().finishInstall("a")
        val second = first.finishInstall("b")

        assertEquals(setOf("a", "b"), second.installedPackIds)
        assertNull(second.workingPackId)
        assertNull(second.failedPackId)
    }

    @Test
    fun `starting an import clears prior pack badges and failures`() {
        val started = CheatPackBrowserState(
            installedPackIds = setOf("a"),
            failedPackId = "b",
            importSucceeded = true
        ).startImport()

        assertTrue(started.importing)
        assertTrue(started.installedPackIds.isEmpty())
        assertNull(started.failedPackId)
        assertNull(started.importSucceeded)
        assertTrue(started.isBusy)
    }

    @Test
    fun `import failure stops the spinner and reports failure`() {
        val failed = CheatPackBrowserState(importing = true).failImport()

        assertFalse(failed.importing)
        assertEquals(false, failed.importSucceeded)
    }

    @Test
    fun `finishing an import reports success without a catalog badge`() {
        val finished = CheatPackBrowserState(installedPackIds = setOf("a"))
            .startImport()
            .finishInstall(packId = null)

        assertFalse(finished.importing)
        assertTrue(finished.importSucceeded == true)
        assertTrue(finished.installedPackIds.isEmpty())
    }

    // --- Fixtures ---------------------------------------------------------

    private fun catalogFixture(): List<RemoteCheatPack> = listOf(
        pack(id = "mirror-a", blockCount = 486),
        pack(id = "mirror-b", blockCount = 486),
        pack(id = "small", blockCount = 12),
        pack(
            id = "pal-revision",
            crc = "B5A7735B",
            serials = listOf("SLES-53561"),
            blockCount = 5
        ),
        pack(
            id = "other-game",
            title = "God of War",
            crc = "BBBBBBBB",
            serials = listOf("SLUS-20312"),
            blockCount = 9
        )
    )

    private fun pack(
        id: String,
        title: String = "Bully",
        crc: String = "28703748",
        serials: List<String> = listOf("SLUS-21269"),
        blockCount: Int = 1
    ) = RemoteCheatPack(
        id = id,
        title = title,
        serials = serials,
        crc = crc,
        authors = listOf("Original patch authors"),
        description = "",
        downloadUrl = "https://example.com/pack.pnach",
        sourceUrl = "https://example.com/source",
        sourceName = "Test collection",
        license = "",
        blockCount = blockCount
    )
}

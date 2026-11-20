package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PerGameKeyMatcherTest {
    private val saf = "content://com.android.externalstorage.documents/tree/primary%3AGames/document/primary%3AGames%2FGoW2.iso"
    private val real = "/storage/emulated/0/Games/GoW2.iso"
    private val noReal: (String) -> String? = { null }
    private fun launch(raw: String, resolved: String? = null, name: String? = null, size: Long? = null, serial: String? = null) =
        PerGameKeyLaunchInfo(raw, resolved, name, size, serial)

    @Test
    fun exactKeyWins() {
        val profiles = listOf(PerGameKeyProfileRef(real, null))
        assertEquals(real, PerGameKeyMatcher.match(launch(real), profiles, emptyList(), noReal))
    }

    @Test
    fun fileUriMatchesPlainPath() {
        val profiles = listOf(PerGameKeyProfileRef("/storage/emulated/0/Games/My Game.iso", null))
        val raw = "file:///storage/emulated/0/Games/My%20Game.iso"
        assertEquals(profiles[0].gameKey, PerGameKeyMatcher.match(launch(raw), profiles, emptyList(), noReal))
    }

    @Test
    fun contentUriMatchesProfileViaRealPath() {
        val profiles = listOf(PerGameKeyProfileRef(real, null))
        val other = "content://other/doc/1"
        assertEquals(real, PerGameKeyMatcher.match(launch(other, resolved = real), profiles, emptyList(), noReal))
    }

    @Test
    fun libraryEntryRealPathMapsToSafProfile() {
        val profiles = listOf(PerGameKeyProfileRef(saf, null))
        val library = listOf(PerGameKeyLibraryRef(saf, "GoW2.iso", 100))
        val result = PerGameKeyMatcher.match(
            launch("file://$real", resolved = real), profiles, library
        ) { if (it == saf) real else null }
        assertEquals(saf, result)
    }

    @Test
    fun libraryMatchWithoutProfileReturnsCanonicalLibraryKey() {
        val library = listOf(PerGameKeyLibraryRef(saf, "GoW2.iso", 100))
        val result = PerGameKeyMatcher.match(launch(real, resolved = real), emptyList(), library) { if (it == saf) real else null }
        assertEquals(saf, result)
    }

    @Test
    fun nameAndSizeFallbackRequiresUniqueMatch() {
        val library = listOf(PerGameKeyLibraryRef(saf, "GoW2.iso", 100))
        assertEquals(saf, PerGameKeyMatcher.match(launch("content://x/1", name = "gow2.ISO", size = 100), emptyList(), library, noReal))
        val dup = library + PerGameKeyLibraryRef("/a/GoW2.iso", "GoW2.iso", 100)
        assertNull(PerGameKeyMatcher.match(launch("content://x/1", name = "GoW2.iso", size = 100), emptyList(), dup, noReal))
    }

    @Test
    fun serialFallbackOnlyWhenExactlyOneProfileMatches() {
        val one = listOf(PerGameKeyProfileRef("/a.iso", "SCUS-97481"), PerGameKeyProfileRef("/b.iso", "SLUS-21050"))
        assertEquals("/a.iso", PerGameKeyMatcher.match(launch("content://x/1", serial = "SCUS_97481"), one, emptyList(), noReal))
        val two = one + PerGameKeyProfileRef("/c.iso", "SCUS-97481")
        assertNull(PerGameKeyMatcher.match(launch("content://x/1", serial = "SCUS-97481"), two, emptyList(), noReal))
    }

    @Test
    fun documentIdsFromLauncherPaths() {
        val docUri = "content://com.android.externalstorage.documents/document/primary%3APS2%2FGames%2FX.iso"
        assertEquals("primary:PS2/Games/X.iso", PerGameKeyMatcher.externalStorageDocumentId(docUri))
        assertEquals("primary:PS2/Games/X.iso", PerGameKeyMatcher.externalStorageDocumentId("file:///storage/emulated/0/PS2/Games/X.iso"))
        assertEquals("primary:PS2/My Game.iso", PerGameKeyMatcher.externalStorageDocumentId("file:///storage/emulated/0/PS2/My%20Game.iso"))
        assertEquals("1A2B-3C4D:Games/X.iso", PerGameKeyMatcher.externalStorageDocumentId("/storage/1A2B-3C4D/Games/X.iso"))
        assertEquals("primary:PS2/Games/X.iso", PerGameKeyMatcher.externalStorageDocumentId(saf.replace("primary%3AGames/document/primary%3AGames%2FGoW2.iso", "primary%3APS2%2FGames/document/primary%3APS2%2FGames%2FX.iso")))
        assertNull(PerGameKeyMatcher.externalStorageDocumentId("content://other/document/1"))
        assertNull(PerGameKeyMatcher.externalStorageDocumentId("/data/user/0/app/x.iso"))
        assertEquals("primary:PS2/Games", PerGameKeyMatcher.treeDocumentId("content://com.android.externalstorage.documents/tree/primary%3APS2%2FGames"))
    }

    @Test
    fun launcherDocumentUriMapsToHeldTreeWithoutPermission() {
        val tree = "content://com.android.externalstorage.documents/tree/primary%3APS2%2FGames"
        val entry = "$tree/document/primary%3APS2%2FGames%2FX.iso"
        val doc = "content://com.android.externalstorage.documents/document/primary%3APS2%2FGames%2FX.iso"
        assertEquals(entry, PerGameKeyMatcher.mapToTreeDocumentUri(listOf(doc), listOf(tree), listOf(entry)))
        assertEquals(entry, PerGameKeyMatcher.mapToTreeDocumentUri(listOf("file:///storage/emulated/0/PS2/Games/X.iso"), listOf(tree), emptyList()))
        // Subfolder, spaces and a tree whose id is only a prefix of another folder.
        assertEquals(
            "$tree/document/primary%3APS2%2FGames%2FSub%20Dir%2FY.iso",
            PerGameKeyMatcher.mapToTreeDocumentUri(listOf("/storage/emulated/0/PS2/Games/Sub Dir/Y.iso"), listOf(tree), emptyList())
        )
        assertNull(PerGameKeyMatcher.mapToTreeDocumentUri(listOf("/storage/emulated/0/PS2/Games2/Z.iso"), listOf(tree), emptyList()))
        assertNull(PerGameKeyMatcher.mapToTreeDocumentUri(listOf("/storage/emulated/0/Other/Z.iso"), listOf(tree), emptyList()))
    }

    @Test
    fun matchWithTreesReturnsLibraryEntryForLauncherUri() {
        val tree = "content://com.android.externalstorage.documents/tree/primary%3APS2%2FGames"
        val entry = "$tree/document/primary%3APS2%2FGames%2FX.iso"
        val raw = "file:///storage/emulated/0/PS2/Games/X.iso"
        val result = PerGameKeyMatcher.matchWithTrees(
            launch(raw), emptyList(), listOf(PerGameKeyLibraryRef(entry, "X.iso", 1)), listOf(tree), noReal
        )
        assertEquals(entry, result)
    }

    @Test
    fun nothingMatchesReturnsNull() {
        assertNull(PerGameKeyMatcher.match(launch("content://x/1"), listOf(PerGameKeyProfileRef("/a.iso", null)), emptyList(), noReal))
    }
}

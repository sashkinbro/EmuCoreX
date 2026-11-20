package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GameLibraryMergeTest {

    private fun game(path: String, title: String, size: Long = 1L) =
        GameItem(title = title, path = path, fileName = path.substringAfterLast('/'), fileSize = size, lastModified = 1L)

    private val root = "/storage/games"
    private val a = game("$root/a.iso", "Alpha")
    private val b = game("$root/b.iso", "Beta")

    @Test
    fun unchangedScanReportsNoChange() {
        val result = GameLibraryMerge.merge(listOf(a, b), listOf(LibraryRootScan(root, listOf(b, a), true)))
        assertFalse(result.changed)
        assertEquals(listOf(a, b), result.games)
    }

    @Test
    fun addsNewAndKeepsSortOrder() {
        val c = game("$root/c.iso", "alpha two")
        val result = GameLibraryMerge.merge(listOf(a, b), listOf(LibraryRootScan(root, listOf(a, b, c), true)))
        assertTrue(result.changed)
        assertEquals(listOf("Alpha", "alpha two", "Beta"), result.games.map { it.title })
    }

    @Test
    fun dropsMissingFromCompleteRoot() {
        val result = GameLibraryMerge.merge(listOf(a, b), listOf(LibraryRootScan(root, listOf(a), true)))
        assertTrue(result.changed)
        assertEquals(listOf(a), result.games)
    }

    @Test
    fun updatesChangedEntry() {
        val a2 = a.copy(fileSize = 99L)
        val result = GameLibraryMerge.merge(listOf(a, b), listOf(LibraryRootScan(root, listOf(a2, b), true)))
        assertTrue(result.changed)
        assertEquals(99L, result.games.first { it.path == a.path }.fileSize)
    }

    @Test
    fun failedRootNeverDropsItsGames() {
        val result = GameLibraryMerge.merge(listOf(a, b), listOf(LibraryRootScan(root, emptyList(), false)))
        assertFalse(result.changed)
        assertEquals(listOf(a, b), result.games)
    }

    @Test
    fun incompleteRootStillAddsNewGames() {
        val c = game("$root/c.iso", "Gamma")
        val result = GameLibraryMerge.merge(listOf(a, b), listOf(LibraryRootScan(root, listOf(c), false)))
        assertEquals(listOf("Alpha", "Beta", "Gamma"), result.games.map { it.title })
    }

    @Test
    fun onlyFailedRootIsProtectedWhenMultipleRoots() {
        val sd = "/storage/sdcard"
        val s = game("$sd/s.iso", "Sigma")
        val scans = listOf(
            LibraryRootScan(root, listOf(a), true),
            LibraryRootScan(sd, emptyList(), false)
        )
        val result = GameLibraryMerge.merge(listOf(a, b, s), scans)
        assertEquals(listOf(a, s), result.games)
    }

    @Test
    fun rootPrefixDoesNotMatchSiblingFolder() {
        assertFalse(GameLibraryMerge.isUnderRoot("/storage/games", "/storage/games2/x.iso"))
        assertTrue(GameLibraryMerge.isUnderRoot("/storage/games/", "/storage/games/sub/x.iso"))
    }

    @Test
    fun safTreeAttribution() {
        val tree = "content://com.android.externalstorage.documents/tree/primary%3AGames"
        val doc = "$tree/document/primary%3AGames%2Fa.iso"
        assertTrue(GameLibraryMerge.isUnderRoot(tree, doc))
        assertFalse(GameLibraryMerge.isUnderRoot(tree, "content://other/tree/x/document/y"))
        val g = game(doc, "Alpha")
        val failed = GameLibraryMerge.merge(listOf(g), listOf(LibraryRootScan(tree, emptyList(), false)))
        assertEquals(listOf(g), failed.games)
        val removed = GameLibraryMerge.merge(listOf(g), listOf(LibraryRootScan(tree, emptyList(), true)))
        assertTrue(removed.games.isEmpty())
    }
}

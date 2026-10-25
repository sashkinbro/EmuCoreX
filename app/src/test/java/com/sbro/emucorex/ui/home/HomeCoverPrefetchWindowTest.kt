package com.sbro.emucorex.ui.home

import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.LibraryPlatform
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HomeCoverPrefetchWindowTest {
    private fun rows(count: Int, columns: Int = 2) = (0 until count).map { row ->
        HomeCoverPrefetchRow("row_$row", (0 until columns).map { "cover_${row}_$it" })
    }

    @Test fun forwardScrollWarmsTwoNearestRowsBeforeTheTrailingRow() {
        val window = HomeCoverPrefetchWindow(rows(10))
        assertEquals(
            listOf("cover_5_0", "cover_5_1", "cover_6_0", "cover_6_1", "cover_2_0", "cover_2_1"),
            window.paths(listOf("search", "row_3", "row_4"), forward = true)
        )
    }

    @Test fun reversingScrollPrioritizesTheNewDirectionAndNeverRequestsTheViewport() {
        val window = HomeCoverPrefetchWindow(rows(10))
        assertEquals(
            listOf("cover_2_0", "cover_2_1", "cover_1_0", "cover_1_1", "cover_5_0", "cover_5_1"),
            window.paths(listOf("row_4", "row_3"), forward = false)
        )
    }

    @Test fun wideGridsHaveAtMostEightDistinctSpeculativeImagesNearestFirst() {
        val window = HomeCoverPrefetchWindow(rows(20, columns = 7))
        val paths = window.paths(listOf("row_5", "row_6"), forward = true)
        assertEquals((0..6).map { "cover_7_$it" } + "cover_8_0", paths)
        assertEquals(8, paths.distinct().size)
    }

    @Test fun headerOnlyViewportWarmsInitialRowsWithoutTreatingHeadersAsGames() {
        val window = HomeCoverPrefetchWindow(rows(8))
        assertEquals(
            listOf("cover_0_0", "cover_0_1", "cover_1_0", "cover_1_1"),
            window.paths(listOf("header", "platform_tabs", "recent"), forward = true)
        )
    }

    @Test fun listAndPartialLastRowsStayWithinBoundsInBothDirections() {
        val window = HomeCoverPrefetchWindow(rows(4, columns = 1))
        assertEquals(listOf("cover_3_0", "cover_1_0"), window.paths(listOf("row_2"), true))
        assertEquals(listOf("cover_2_0", "cover_1_0"), window.paths(listOf("row_3"), false))
        assertTrue(HomeCoverPrefetchWindow(emptyList()).paths(listOf("header"), true).isEmpty())
    }

    @Test fun missingDisabledDuplicateAndVisibleArtworkNeverConsumesPrefetchSlots() {
        val window = HomeCoverPrefetchWindow(listOf(
            HomeCoverPrefetchRow("row_0", listOf("visible", "same")),
            HomeCoverPrefetchRow("row_1", listOf(null, "", " ", "same", "new", "new")),
            HomeCoverPrefetchRow("row_2", listOf("another"))
        ))
        assertEquals(listOf("new", "another"), window.paths(listOf("row_0"), true))
    }

    @Test fun sectionBoundariesKeepTheSamePartialRowsAsTheActualLibrary() {
        val first = List(3) { game("ps2_$it") }
        val second = List(3) { game("arcade_$it") }
        val sections = listOf(HomeLibrarySection(LibraryPlatform.PS2, first),
            HomeLibrarySection(LibraryPlatform.ARCADE, second))
        val rows = buildHomeCoverPrefetchRows(sections, columns = 2, listView = false)
        assertEquals(listOf(first[0], first[2], second[0], second[2]).map(::homeLibraryRowKey), rows.map { it.key })
        assertEquals(listOf("cover_arcade_0", "cover_arcade_1", "cover_arcade_2", "cover_ps2_0", "cover_ps2_1"),
            HomeCoverPrefetchWindow(rows).paths(listOf(homeLibraryRowKey(first[2]), "platform_ARCADE"), true))
    }

    @Test fun listViewAndDisabledCoverProjectionUseOnlyEligibleImagesWithoutMutatingGames() {
        val games = listOf(game("first"), game("custom"), game("last"))
        val sections = listOf(HomeLibrarySection(null, games))
        val rows = buildHomeCoverPrefetchRows(sections, columns = 6, listView = true) { game ->
            game.coverArtPath.takeIf { game.path == "custom" }
        }
        assertEquals(3, rows.size)
        assertEquals(listOf("cover_custom"), HomeCoverPrefetchWindow(rows).paths(listOf("header"), true))
        assertEquals(listOf("cover_first", "cover_custom", "cover_last"), games.map { it.coverArtPath })
    }

    private fun game(id: String) = GameItem(id, id, "$id.iso", 1, 0, "cover_$id")
}

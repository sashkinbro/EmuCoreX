package com.sbro.emucorex.ui.home

import com.sbro.emucorex.data.GameItem

internal data class HomeCoverPrefetchRow(val key: String, val coverPaths: List<String?>)

/** A small image-only lookahead; it never composes or focuses offscreen cards. */
internal class HomeCoverPrefetchWindow(private val rows: List<HomeCoverPrefetchRow>) {
    private val indicesByKey = rows.mapIndexed { index, row -> row.key to index }.toMap()

    fun paths(visibleKeys: List<String>, forward: Boolean): List<String> {
        if (rows.isEmpty()) return emptyList()
        val visible = visibleKeys.mapNotNull(indicesByKey::get)
        val first = visible.minOrNull()
        val last = visible.maxOrNull()
        val candidates = if (first == null || last == null) {
            // Header/search/recent rows may fill the initial viewport.
            listOf(0, 1)
        } else if (forward) {
            listOf(last + 1, last + 2, first - 1)
        } else {
            listOf(first - 1, first - 2, last + 1)
        }
        val visiblePaths = visible.flatMap { rows[it].coverPaths }.toHashSet()
        return candidates.asSequence()
            .filter { it in rows.indices }
            .flatMap { rows[it].coverPaths.asSequence() }
            .filterNotNull()
            .filter { it.isNotBlank() && it !in visiblePaths }
            .distinct()
            .take(8)
            .toList()
    }
}

/** Section headers end a row even when that platform has fewer than [columns] games. */
internal fun buildHomeCoverPrefetchRows(
    sections: List<HomeLibrarySection>,
    columns: Int,
    listView: Boolean,
    coverPath: (GameItem) -> String? = { it.coverArtPath }
): List<HomeCoverPrefetchRow> {
    val rowSize = if (listView) 1 else columns.coerceAtLeast(1)
    return sections.flatMap { section ->
        section.games.chunked(rowSize).map { row ->
            HomeCoverPrefetchRow(homeLibraryRowKey(row.first()), row.map(coverPath))
        }
    }
}

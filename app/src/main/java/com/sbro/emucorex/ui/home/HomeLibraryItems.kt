package com.sbro.emucorex.ui.home

import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.runtime.Composable
import com.sbro.emucorex.data.GameItem

internal fun homeLibraryRowKey(game: GameItem): String = "row_${game.path}"

internal fun LazyGridScope.homeLibraryItems(
    sections: List<HomeLibrarySection>,
    columns: Int,
    listView: Boolean,
    header: @Composable (HomeLibrarySection) -> Unit,
    row: @Composable (List<GameItem>, Int) -> Unit
) {
    // Register rows, not a Column containing the entire catalog. Only visible rows
    // (plus the lazy layout's bounded prefetch) may compose cards and decode covers.
    sections.forEach { section ->
        if (section.platform != null) {
            item(key = "platform_${section.platform}", contentType = "platform",
                span = { GridItemSpan(maxLineSpan) }) { header(section) }
        }
        val count = if (listView) 1 else
            columns.coerceAtLeast(1)
        items(count = (section.games.size + count - 1) / count,
            key = { index -> homeLibraryRowKey(section.games[index * count]) },
            contentType = { if (listView) "list" else "covers_$count" },
            span = { GridItemSpan(maxLineSpan) }) { index ->
                val start = index * count
                row(section.games.subList(start, minOf(start + count, section.games.size)), count)
            }
    }
}

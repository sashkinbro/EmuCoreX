package com.sbro.emucorex.ui.home

import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.platform.LocalContext
import com.sbro.emucorex.ui.common.prefetchGameCoverArt
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map

@Composable
internal fun HomeCoverPrefetch(
    gridState: LazyGridState,
    window: HomeCoverPrefetchWindow,
    enabled: Boolean,
    // The same integration can be checked with a deterministic image loader.
    load: (suspend (String) -> Unit)? = null
) {
    val context = LocalContext.current.applicationContext
    LaunchedEffect(gridState, window, enabled, load) {
        if (!enabled) return@LaunchedEffect
        var previousIndex = gridState.firstVisibleItemIndex
        var previousOffset = gridState.firstVisibleItemScrollOffset
        var forward = true
        snapshotFlow {
            Triple(
                gridState.layoutInfo.visibleItemsInfo.map { it.key.toString() },
                gridState.firstVisibleItemIndex,
                gridState.firstVisibleItemScrollOffset
            )
        }.map { (keys, index, offset) ->
            if (index != previousIndex || offset != previousOffset) {
                forward = index > previousIndex || (index == previousIndex && offset > previousOffset)
                previousIndex = index
                previousOffset = offset
            }
            window.paths(keys, forward)
        }.distinctUntilChanged().collectLatest { paths ->
            // Sequential speculation leaves the other loader permits for visible cards.
            // A direction/filter change cancels this queue instead of enqueuing a library.
            for (path in paths) {
                currentCoroutineContext().ensureActive()
                if (load != null) load(path) else prefetchGameCoverArt(context, path)
            }
        }
    }
}

package com.sbro.emucorex.ui.home

import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.focus.FocusRequester
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first

/** Tracks only composed library rows; headers and prefetched rows cannot steal entry focus. */
internal class HomeLibraryFocusRegistry {
    private val requesters = mutableStateMapOf<Any, FocusRequester>()

    fun register(key: Any, requester: FocusRequester) { requesters[key] = requester }
    fun unregister(key: Any, requester: FocusRequester) {
        if (requesters[key] === requester) requesters.remove(key)
    }
    fun visibleRequester(visibleKeys: List<Any>): FocusRequester? =
        visibleKeys.firstNotNullOfOrNull { requesters[it] }
}

@Composable
internal fun HomeLibraryFocusRegistry.rememberRowRequester(key: Any): FocusRequester {
    val requester = remember(this, key) { FocusRequester() }
    DisposableEffect(this, key, requester) {
        register(key, requester)
        onDispose { unregister(key, requester) }
    }
    return requester
}

@Composable
internal fun HomeLibraryFocusOnEntryAndResume(
    registry: HomeLibraryFocusRegistry,
    gridState: LazyGridState,
    enabled: Boolean,
    entryKey: Any
) {
    val lifecycleOwner = LocalLifecycleOwner.current
    fun visibleRequester() = registry.visibleRequester(
        gridState.layoutInfo.visibleItemsInfo.sortedBy { it.offset.y }.map { it.key }
    )
    LaunchedEffect(registry, gridState, enabled, entryKey) {
        if (enabled) snapshotFlow { visibleRequester() }.filterNotNull().first().requestFocus()
    }
    DisposableEffect(lifecycleOwner, registry, gridState, enabled) {
        val observer = LifecycleEventObserver { _, event ->
            if (enabled && event == Lifecycle.Event.ON_RESUME) visibleRequester()?.requestFocus()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }
}

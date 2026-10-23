package com.sbro.emucorex.ui.home

import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertIsFocused
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class HomeLibraryFocusInstrumentedTest {
    @get:Rule val compose = createComposeRule()

    @Test fun recentFocusRemainsAttachedAfterFirstHorizontalCardIsDisposed() {
        val registry = HomeLibraryFocusRegistry()
        var clearFocus: () -> Unit = {}
        var resumeFocus: () -> Unit = {}
        compose.setContent {
            val gridState = rememberLazyGridState()
            val focusManager = LocalFocusManager.current
            clearFocus = { focusManager.clearFocus(force = true) }
            HomeLibraryFocusOnEntryAndResume(registry, gridState, true, Unit)
            resumeFocus = {
                registry.visibleRequester(gridState.layoutInfo.visibleItemsInfo.map { it.key })?.requestFocus()
            }
            LazyVerticalGrid(GridCells.Fixed(1), Modifier.testTag("library"), state = gridState) {
                item(key = "recent") {
                    val requester = registry.rememberRowRequester("recent")
                    val recentRowState = rememberLazyListState()
                    val recentFocusIndex by remember(recentRowState) {
                        derivedStateOf { recentRowState.firstVisibleItemIndex }
                    }
                    LazyRow(Modifier.testTag("recent"), state = recentRowState) {
                        items(1000) { index ->
                            val focusModifier = if (index == recentFocusIndex) Modifier.focusRequester(requester) else Modifier
                            Box(Modifier.width(120.dp).height(120.dp).testTag("recent_$index")
                                .then(focusModifier).focusable())
                        }
                    }
                }
            }
        }
        compose.onNodeWithTag("recent_0").assertIsFocused()
        // Leaving for a game clears focus; otherwise Compose pins the focused lazy item.
        compose.runOnIdle { clearFocus() }
        compose.onNodeWithTag("recent").performScrollToIndex(600)
        compose.onNodeWithTag("recent_0").assertDoesNotExist()
        compose.runOnIdle { resumeFocus() }
        compose.onNodeWithTag("recent_600").assertIsFocused()
    }

    @Test fun entryAndResumeFocusVisibleGamesAfterOriginalCardIsDisposed() {
        val registry = HomeLibraryFocusRegistry()
        var clearFocus: () -> Unit = {}
        var resumeFocus: () -> Unit = {}
        var targetKey = ""
        compose.setContent {
            val gridState = rememberLazyGridState()
            val focusManager = LocalFocusManager.current
            clearFocus = { focusManager.clearFocus(force = true) }
            HomeLibraryFocusOnEntryAndResume(registry, gridState, true, Unit)
            resumeFocus = {
                val keys = gridState.layoutInfo.visibleItemsInfo.sortedBy { it.offset.y }.map { it.key }
                targetKey = keys.first { it.toString().startsWith("row_") }.toString()
                registry.visibleRequester(keys)?.requestFocus()
            }
            LazyVerticalGrid(GridCells.Fixed(1), Modifier.testTag("library"), state = gridState) {
                item(key = "header") {
                    Box(Modifier.fillMaxWidth().height(32.dp).testTag("header").focusable())
                }
                items(1000, key = { "row_$it" }) { index ->
                    val key = "row_$index"
                    val requester = registry.rememberRowRequester(key)
                    Box(Modifier.fillMaxWidth().height(120.dp).testTag(key).focusRequester(requester).focusable())
                }
            }
        }
        compose.onNodeWithTag("row_0").assertIsFocused()
        compose.runOnIdle { clearFocus() }
        compose.onNodeWithTag("library").performScrollToIndex(600)
        compose.runOnIdle {
            assertNull("Original row should be disposed", registry.visibleRequester(listOf("row_0")))
            resumeFocus()
            assertEquals("row_599", targetKey)
        }
        compose.onNodeWithTag("row_599").assertIsFocused()
    }
}

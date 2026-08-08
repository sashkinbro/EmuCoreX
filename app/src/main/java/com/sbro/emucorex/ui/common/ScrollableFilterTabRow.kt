package com.sbro.emucorex.ui.common

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.gestures.animateScrollBy
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlin.math.abs

/** Shared top-level tab presentation used by settings and feature hubs. */
@Composable
fun <T> ScrollableFilterTabRow(
    tabs: List<T>,
    selectedTab: T,
    onSelected: (T) -> Unit,
    key: (T) -> Any,
    label: @Composable (T) -> String,
    icon: (T) -> ImageVector,
    selectedTabFocusRequester: FocusRequester? = null,
    horizontalContentPadding: Dp = ScreenHorizontalPadding,
    compact: Boolean = false,
    modifier: Modifier = Modifier
) {
    val listState = rememberLazyListState()
    LaunchedEffect(selectedTab, tabs) {
        val selectedIndex = tabs.indexOf(selectedTab).coerceAtLeast(0)
        var selectedItem = listState.layoutInfo.visibleItemsInfo.firstOrNull { it.index == selectedIndex }
        if (selectedItem == null) {
            listState.scrollToItem(selectedIndex)
            withFrameNanos { }
            selectedItem = listState.layoutInfo.visibleItemsInfo.firstOrNull { it.index == selectedIndex }
        }
        selectedItem?.let { item ->
            val layoutInfo = listState.layoutInfo
            val viewportCenter = (layoutInfo.viewportStartOffset + layoutInfo.viewportEndOffset) / 2f
            val itemCenter = item.offset + item.size / 2f
            val delta = itemCenter - viewportCenter
            if (abs(delta) > 1f) listState.animateScrollBy(delta)
        }
    }

    LazyRow(
        state = listState,
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp)
            .tvFocusGroup(),
        contentPadding = PaddingValues(horizontal = horizontalContentPadding),
        horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)
    ) {
        items(items = tabs, key = key) { tab ->
            val interactionSource = remember { MutableInteractionSource() }
            val focusModifier = if (tab == selectedTab && selectedTabFocusRequester != null) {
                Modifier.focusRequester(selectedTabFocusRequester)
            } else {
                Modifier
            }
            FilterChip(
                modifier = focusModifier
                    .heightIn(min = if (compact) 34.dp else 38.dp)
                    .tvGamepadFocusableCard(
                        shape = RoundedCornerShape(16.dp),
                        interactionSource = interactionSource,
                        addFocusTarget = false
                    ),
                selected = selectedTab == tab,
                onClick = { onSelected(tab) },
                interactionSource = interactionSource,
                colors = FilterChipDefaults.filterChipColors(
                    containerColor = Color.Transparent,
                    labelColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    iconColor = MaterialTheme.colorScheme.primary,
                    selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
                    selectedLabelColor = MaterialTheme.colorScheme.onPrimaryContainer,
                    selectedLeadingIconColor = MaterialTheme.colorScheme.onPrimaryContainer,
                    selectedTrailingIconColor = MaterialTheme.colorScheme.onPrimaryContainer
                ),
                label = {
                    Text(
                        label(tab),
                        style = if (compact) MaterialTheme.typography.labelMedium else MaterialTheme.typography.labelLarge
                    )
                },
                leadingIcon = {
                    Icon(
                        imageVector = icon(tab),
                        contentDescription = null,
                        modifier = Modifier.size(if (compact) 16.dp else 17.dp)
                    )
                }
            )
        }
    }
}

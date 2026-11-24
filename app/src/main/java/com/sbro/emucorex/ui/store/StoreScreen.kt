package com.sbro.emucorex.ui.store

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Sort
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudOff
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SearchOff
import androidx.compose.material.icons.rounded.Storefront
import androidx.compose.material.icons.rounded.Wallpaper
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.data.store.InstalledStoreItem
import com.sbro.emucorex.data.store.StoreCategory
import com.sbro.emucorex.data.store.StoreItem
import com.sbro.emucorex.data.store.StoreSnapshot
import com.sbro.emucorex.ui.common.ProvideGamepadShoulderActions
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.tvFocusGroup
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonChipShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import com.sbro.emucorex.ui.theme.style.DropdownMenu
import com.sbro.emucorex.ui.theme.style.FilledTonalButton
import com.sbro.emucorex.ui.theme.style.FilterChip
import com.sbro.emucorex.ui.theme.style.Surface
import com.sbro.emucorex.ui.theme.style.TextButton
import kotlinx.coroutines.delay

private val StoreTab.icon: ImageVector
    get() = when (this) {
        StoreTab.THEMES -> Icons.Rounded.Palette
        StoreTab.WALLPAPERS -> Icons.Rounded.Wallpaper
        StoreTab.SOUNDS -> Icons.Rounded.GraphicEq
        StoreTab.CONTROLS -> Icons.Rounded.Gamepad
    }

@Composable
private fun StoreTab.label(): String = stringResource(
    when (this) {
        StoreTab.THEMES -> R.string.store_tab_themes
        StoreTab.WALLPAPERS -> R.string.store_tab_wallpapers
        StoreTab.SOUNDS -> R.string.store_tab_sounds
        StoreTab.CONTROLS -> R.string.store_tab_controls
    }
)

@Composable
private fun StoreSort.label(): String = stringResource(
    when (this) {
        StoreSort.FEATURED -> R.string.store_sort_featured
        StoreSort.NEWEST -> R.string.store_sort_newest
        StoreSort.NAME -> R.string.store_sort_name
    }
)

@Composable
private fun StoreAccessFilter.label(): String = stringResource(
    when (this) {
        StoreAccessFilter.ALL -> R.string.hub_theme_filter_all
        StoreAccessFilter.FREE -> R.string.hub_theme_filter_free
        StoreAccessFilter.PRO -> R.string.hub_theme_filter_pro
        StoreAccessFilter.INSTALLED -> R.string.hub_theme_installed
    }
)

@Stable
class StoreFilters(initialTab: StoreTab) {
    var tab by mutableStateOf(initialTab)
        private set
    var access by mutableStateOf(StoreAccessFilter.ALL)
    var sort by mutableStateOf(StoreSort.FEATURED)
    var categoryId by mutableStateOf<String?>(null)

    fun selectTab(value: StoreTab) {
        if (value != tab) {
            tab = value
            categoryId = null
        }
    }

    internal fun restore(access: StoreAccessFilter, sort: StoreSort, categoryId: String?) {
        this.access = access
        this.sort = sort
        this.categoryId = categoryId
    }

    val filtersActive: Boolean get() = access != StoreAccessFilter.ALL || categoryId != null

    fun clear() {
        access = StoreAccessFilter.ALL
        categoryId = null
    }

    val modeKey: String get() = "${tab.name}:${access.name}:${sort.name}:$categoryId"
}

@Composable
fun rememberStoreFilters(initialTab: String?): StoreFilters = rememberSaveable(
    initialTab,
    saver = listSaver(
        save = { listOf(it.tab.name, it.access.name, it.sort.name, it.categoryId.orEmpty()) },
        restore = {
            StoreFilters(StoreTab.valueOf(it[0])).apply {
                restore(
                    StoreAccessFilter.valueOf(it[1]),
                    StoreSort.valueOf(it[2]),
                    it[3].ifEmpty { null }
                )
            }
        }
    )
) { StoreFilters(StoreTab.fromKey(initialTab)) }

@Stable
class StoreContentModel internal constructor(
    val viewModel: StoreViewModel,
    val filters: StoreFilters,
    val query: String,
    val snapshot: StoreSnapshot?,
    val installed: Map<String, InstalledStoreItem>,
    val applied: Set<String>,
    val refreshing: Boolean,
    val offline: Boolean,
    val tabItems: List<StoreItem>,
    val visible: List<StoreItem>,
    val categories: List<StoreCategory>,
    val featured: List<StoreItem>
)

@Composable
fun rememberStoreContentModel(
    viewModel: StoreViewModel,
    filters: StoreFilters,
    query: String
): StoreContentModel {
    val snapshot by viewModel.catalog.collectAsState()
    val installed by viewModel.installed.collectAsState()
    val applied by viewModel.appliedIds.collectAsState()
    val refreshing by viewModel.refreshing.collectAsState()
    val offline by viewModel.offline.collectAsState()
    val locale = viewModel.locale
    val tab = filters.tab
    val access = filters.access
    val sort = filters.sort
    val categoryId = filters.categoryId

    val items = snapshot?.catalog?.items.orEmpty()
    val tabItems = remember(items, tab) { items.filter { it.kind == tab.kind } }
    val visible = remember(items, tab, access, categoryId, query, sort, installed, locale) {
        filterStoreItems(items, tab.kind, access, categoryId, query, sort, installed.keys, locale)
    }
    val categories = remember(snapshot, tab) {
        val used = tabItems.flatMapTo(mutableSetOf()) { it.categories }
        snapshot?.catalog?.categories.orEmpty().filter { it.kind == tab.kind && it.id in used }
    }
    val featured = remember(tabItems) { tabItems.filter { it.featured }.take(6) }
    return StoreContentModel(
        viewModel, filters, query, snapshot, installed, applied, refreshing, offline,
        tabItems, visible, categories, featured
    )
}

/** Store body (sub-tabs, offline banner, carousel, filters, grid) emitted into a host grid; no header of its own. */
fun LazyGridScope.storeContent(model: StoreContentModel, onOpenItem: (String) -> Unit) {
    val filters = model.filters
    val tab = filters.tab
    val snapshot = model.snapshot
    val locale = model.viewModel.locale
    item(key = "store-tabs", span = { GridItemSpan(maxLineSpan) }) {
        StoreTabRow(selected = tab, onSelected = filters::selectTab)
    }
    if (model.offline && snapshot != null) {
        item(key = "store-offline", span = { GridItemSpan(maxLineSpan) }) {
            Surface(
                shape = neonShape(12.dp),
                color = MaterialTheme.colorScheme.tertiaryContainer,
                contentColor = MaterialTheme.colorScheme.onTertiaryContainer
            ) {
                Text(
                    stringResource(R.string.hub_offline_cached),
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                    style = MaterialTheme.typography.labelMedium
                )
            }
        }
    }
    when {
        snapshot == null && model.refreshing -> {
            items(6, key = { "store-skeleton-$it" }) { StoreSkeletonCard() }
        }
        snapshot == null -> {
            item(key = "store-unavailable", span = { GridItemSpan(maxLineSpan) }) {
                StoreMessageState(
                    icon = Icons.Rounded.CloudOff,
                    title = stringResource(R.string.store_unavailable_title),
                    body = stringResource(R.string.store_unavailable_body)
                ) {
                    FilledTonalButton(onClick = { model.viewModel.refresh(force = true) }, shape = neonButtonShape()) {
                        Text(stringResource(R.string.store_retry))
                    }
                }
            }
        }
        model.tabItems.isEmpty() -> {
            item(key = "store-coming-soon", span = { GridItemSpan(maxLineSpan) }) {
                StoreMessageState(
                    icon = tab.icon,
                    title = stringResource(R.string.store_coming_soon_title),
                    body = stringResource(R.string.store_coming_soon_body)
                )
            }
        }
        else -> {
            if (model.featured.isNotEmpty() && model.query.isBlank() && !filters.filtersActive) {
                item(key = "store-featured-${tab.key}", span = { GridItemSpan(maxLineSpan) }) {
                    StoreFeaturedCarousel(
                        items = model.featured,
                        snapshot = snapshot,
                        locale = locale,
                        onOpen = { onOpenItem(it.id) }
                    )
                }
            }
            item(key = "store-filters", span = { GridItemSpan(maxLineSpan) }) {
                StoreFilterBar(
                    access = filters.access,
                    onAccess = { filters.access = it },
                    sort = filters.sort,
                    onSort = { filters.sort = it },
                    categories = model.categories,
                    categoryId = filters.categoryId,
                    onCategory = { filters.categoryId = it },
                    locale = locale
                )
            }
            if (model.visible.isEmpty()) {
                item(key = "store-no-results", span = { GridItemSpan(maxLineSpan) }) {
                    StoreMessageState(
                        icon = Icons.Rounded.SearchOff,
                        title = stringResource(R.string.store_no_results),
                        body = ""
                    ) {
                        TextButton(onClick = { filters.clear() }) { Text(stringResource(R.string.store_clear_filters)) }
                    }
                }
            } else {
                items(model.visible, key = { "store-${it.id}" }) { item ->
                    StoreItemCard(
                        item = item,
                        snapshot = snapshot,
                        status = model.viewModel.status(item, model.installed, model.applied),
                        locale = locale,
                        onClick = { onOpenItem(item.id) }
                    )
                }
            }
        }
    }
}

@Composable
private fun StoreTabRow(selected: StoreTab, onSelected: (StoreTab) -> Unit) {
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(StoreTab.entries, key = { it.name }) { tab ->
            FilterChip(
                selected = tab == selected,
                onClick = { onSelected(tab) },
                label = { Text(tab.label(), maxLines = 1) },
                leadingIcon = { Icon(tab.icon, contentDescription = null, modifier = Modifier.size(18.dp)) },
                shape = neonChipShape()
            )
        }
    }
}

@Composable
private fun StoreFilterBar(
    access: StoreAccessFilter,
    onAccess: (StoreAccessFilter) -> Unit,
    sort: StoreSort,
    onSort: (StoreSort) -> Unit,
    categories: List<StoreCategory>,
    categoryId: String?,
    onCategory: (String?) -> Unit,
    locale: String
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            LazyRow(
                modifier = Modifier.weight(1f),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(StoreAccessFilter.entries, key = { it.name }) { filter ->
                    FilterChip(
                        selected = access == filter,
                        onClick = { onAccess(filter) },
                        label = { Text(filter.label(), maxLines = 1) },
                        shape = neonChipShape()
                    )
                }
            }
            var menuOpen by remember { mutableStateOf(false) }
            Box {
                IconButton(onClick = { menuOpen = true }) {
                    Icon(
                        Icons.AutoMirrored.Rounded.Sort,
                        contentDescription = stringResource(R.string.store_sort),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    StoreSort.entries.forEach { option ->
                        DropdownMenuItem(
                            text = {
                                Text(
                                    option.label(),
                                    fontWeight = if (option == sort) FontWeight.Bold else FontWeight.Normal
                                )
                            },
                            onClick = {
                                menuOpen = false
                                onSort(option)
                            }
                        )
                    }
                }
            }
        }
        if (categories.isNotEmpty()) {
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(categories, key = { it.id }) { category ->
                    val selected = categoryId == category.id
                    FilterChip(
                        selected = selected,
                        onClick = { onCategory(if (selected) null else category.id) },
                        label = { Text(category.label(locale), maxLines = 1) },
                        shape = neonChipShape()
                    )
                }
            }
        }
    }
}

@Composable
private fun StoreFeaturedCarousel(
    items: List<StoreItem>,
    snapshot: StoreSnapshot?,
    locale: String,
    onOpen: (StoreItem) -> Unit
) {
    val pagerState: PagerState = rememberPagerState(pageCount = { items.size })
    LaunchedEffect(items.size) {
        if (items.size > 1) {
            while (true) {
                delay(5000)
                if (!pagerState.isScrollInProgress) {
                    pagerState.animateScrollToPage((pagerState.currentPage + 1) % items.size)
                }
            }
        }
    }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            stringResource(R.string.store_featured),
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onSurface
        )
        Box {
            HorizontalPager(
                state = pagerState,
                pageSpacing = 10.dp,
                modifier = Modifier.fillMaxWidth().widthIn(max = 1000.dp)
            ) { page ->
                val item = items[page]
                val interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                val shape = neonShape(20.dp)
                val open = rememberDebouncedClick(onClick = { onOpen(item) })
                Surface(
                    onClick = open,
                    interactionSource = interactionSource,
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(16f / 8f)
                        .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
                    shape = shape,
                    color = MaterialTheme.colorScheme.surfaceContainerLow
                ) {
                    Box(Modifier.fillMaxSize()) {
                        StoreImage(
                            file = item.cover,
                            snapshot = snapshot,
                            swatch = item.swatch,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop,
                            alignment = Alignment.TopCenter
                        )
                        Box(
                            Modifier
                                .fillMaxSize()
                                .background(
                                    Brush.verticalGradient(
                                        0.4f to Color.Transparent,
                                        1f to Color.Black.copy(alpha = 0.78f)
                                    )
                                )
                        )
                        Row(
                            modifier = Modifier
                                .align(Alignment.BottomStart)
                                .padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                item.displayName(locale),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = Color.White,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f, fill = false)
                            )
                            if (item.requiresPro) ProBadge()
                        }
                    }
                }
            }
            if (items.size > 1) {
                Row(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(14.dp),
                    horizontalArrangement = Arrangement.spacedBy(5.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    repeat(items.size) { index ->
                        val current = pagerState.currentPage == index
                        Box(
                            Modifier
                                .size(if (current) 8.dp else 6.dp)
                                .clip(CircleShape)
                                .background(Color.White.copy(alpha = if (current) 0.95f else 0.5f))
                        )
                    }
                }
            }
        }
    }
}

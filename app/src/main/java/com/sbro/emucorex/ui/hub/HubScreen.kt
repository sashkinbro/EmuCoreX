package com.sbro.emucorex.ui.hub

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.MenuBook
import androidx.compose.material.icons.rounded.Article
import androidx.compose.material.icons.rounded.Bookmark
import androidx.compose.material.icons.rounded.BookmarkBorder
import androidx.compose.material.icons.rounded.CalendarMonth
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.HistoryEdu
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Newspaper
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.VideoLibrary
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.LocalTvUiEnvironment
import com.sbro.emucorex.data.hub.HubItem
import com.sbro.emucorex.data.hub.HubKind
import com.sbro.emucorex.ui.common.ProvideGamepadShoulderActions
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.common.tvFocusGroup
import com.sbro.emucorex.ui.common.tvGamepadFocusableCard
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.delay
import java.time.Instant
import java.time.LocalDate
import java.time.YearMonth
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@Composable
fun HubScreen(
    onBackClick: () -> Unit,
    onOpenArticle: (String) -> Unit,
    viewModel: HubViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    var searchVisible by rememberSaveable { mutableStateOf(false) }
    var playingVideo by remember { mutableStateOf<HubItem?>(null) }
    var minimumSkeletonVisible by remember(viewModel) { mutableStateOf(!viewModel.hasShownInitialSkeleton) }
    val selectedTabFocusRequester = remember { FocusRequester() }
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled
    val shouldRequestGamepadFocus = tvUiEnabled || remember { GamepadManager.isGamepadConnected() }
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val compact = LocalConfiguration.current.screenWidthDp < 600
    val horizontalPadding = if (compact) 12.dp else ScreenHorizontalPadding

    LaunchedEffect(viewModel, minimumSkeletonVisible) {
        if (minimumSkeletonVisible) {
            delay(MINIMUM_SKELETON_DURATION_MS)
            viewModel.markInitialSkeletonShown()
            minimumSkeletonVisible = false
        }
    }

    ProvideGamepadShoulderActions(
        enabled = !searchVisible,
        onPrevious = { viewModel.selectRelativeTab(-1) },
        onNext = { viewModel.selectRelativeTab(1) }
    )
    LaunchedEffect(uiState.selectedTab, shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus) {
            withFrameNanos { }
            runCatching { selectedTabFocusRequester.requestFocus() }
        }
    }
    RequestFocusOnResume(
        focusRequester = selectedTabFocusRequester,
        enabled = shouldRequestGamepadFocus
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(navigationBarsHorizontalPaddingValues())
    ) {
        val gridState = rememberSaveable(saver = LazyGridState.Saver) { LazyGridState() }
        var lastTabName by rememberSaveable { mutableStateOf(uiState.selectedTab.name) }
        LaunchedEffect(uiState.selectedTab) {
            if (lastTabName != uiState.selectedTab.name) {
                gridState.scrollToItem(0)
                lastTabName = uiState.selectedTab.name
            }
        }
        PullToRefreshBox(
            isRefreshing = uiState.isRefreshing,
            onRefresh = { viewModel.refresh(force = true) },
            modifier = Modifier.fillMaxSize()
        ) {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(300.dp),
                state = gridState,
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(
                    start = horizontalPadding,
                    end = horizontalPadding,
                    bottom = bottomInset + 18.dp
                ),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                item(key = "hub-header", span = { GridItemSpan(maxLineSpan) }) {
                    HubTopBar(
                        topInset = topInset,
                        onBackClick = onBackClick,
                        onSearchClick = {
                            searchVisible = !searchVisible
                            if (!searchVisible) viewModel.clearSearch()
                        },
                        onRefresh = { viewModel.refresh(force = true) },
                        refreshing = uiState.isRefreshing
                    )
                }
                item(key = "hub-navigation", span = { GridItemSpan(maxLineSpan) }) {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        AnimatedVisibility(
                            visible = searchVisible,
                            enter = expandVertically(
                                animationSpec = tween(durationMillis = 220),
                                expandFrom = Alignment.Top
                            ) + fadeIn(animationSpec = tween(durationMillis = 180)),
                            exit = shrinkVertically(
                                animationSpec = tween(durationMillis = 190),
                                shrinkTowards = Alignment.Top
                            ) + fadeOut(animationSpec = tween(durationMillis = 140))
                        ) {
                            OutlinedTextField(
                                value = uiState.searchQuery,
                                onValueChange = viewModel::setSearchQuery,
                                modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp),
                                singleLine = true,
                                shape = RoundedCornerShape(18.dp),
                                label = { Text(stringResource(R.string.hub_search)) },
                                placeholder = { Text(stringResource(R.string.hub_search_hint)) },
                                leadingIcon = {
                                    Icon(
                                        Icons.Rounded.Search,
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                },
                                trailingIcon = {
                                    if (uiState.searchQuery.isNotEmpty()) {
                                        IconButton(onClick = viewModel::clearSearch) {
                                            Icon(
                                                Icons.Rounded.Close,
                                                contentDescription = stringResource(R.string.hub_close),
                                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }
                                }
                            )
                        }
                        HubTabRow(
                            selected = uiState.selectedTab,
                            onSelected = viewModel::selectTab,
                            selectedFocusRequester = selectedTabFocusRequester,
                            compact = compact,
                            horizontalPadding = horizontalPadding
                        )
                        if (uiState.isOffline) {
                            HubStatusBanner(
                                text = stringResource(R.string.hub_offline_cached),
                                containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                                contentColor = MaterialTheme.colorScheme.onTertiaryContainer
                            )
                        } else if (uiState.fallbackLocale != null) {
                            HubStatusBanner(
                                text = stringResource(R.string.hub_language_fallback),
                                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                            )
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.38f))
                    }
                }

                when {
                    minimumSkeletonVisible -> {
                        items(6, key = { "hub-minimum-skeleton-$it" }) {
                            HubSkeletonCard(compact = compact)
                        }
                    }
                    uiState.isInitialLoading && uiState.items.isEmpty() -> {
                        items(6, key = { "hub-skeleton-$it" }) {
                            HubSkeletonCard(compact = compact)
                        }
                    }
                    uiState.error != null && uiState.items.isEmpty() -> {
                        item(key = "hub-error", span = { GridItemSpan(maxLineSpan) }) {
                            HubErrorState(onRetry = { viewModel.refresh(true) })
                        }
                    }
                    uiState.visibleItems.isEmpty() -> {
                        item(key = "hub-empty", span = { GridItemSpan(maxLineSpan) }) {
                            HubEmptyState(uiState.selectedTab, uiState.searchQuery.isNotBlank())
                        }
                    }
                    else -> {
                        items(items = uiState.visibleItems, key = HubItem::id) { item ->
                            HubCard(
                                item = item,
                                compact = compact,
                                onOpen = {
                                    if (item.kind == HubKind.VIDEOS && item.providerId != null) playingVideo = item
                                    else onOpenArticle(item.id)
                                },
                                onToggleFavorite = { viewModel.toggleFavorite(item) }
                            )
                        }
                    }
                }
            }
        }
    }

    playingVideo?.let { video ->
        video.providerId?.let { id ->
            HubVideoPlayer(youtubeId = id, title = video.title, onDismiss = { playingVideo = null })
        }
    }
}

private const val MINIMUM_SKELETON_DURATION_MS = 500L

@Composable
private fun HubTopBar(
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    onSearchClick: () -> Unit,
    onRefresh: () -> Unit,
    refreshing: Boolean
) {
    com.sbro.emucorex.ui.common.ScreenTopBar(
        title = stringResource(R.string.hub_title),
        onBackClick = onBackClick,
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                top = topInset,
                bottom = 2.dp
            ),
        actions = {
            IconButton(onClick = onSearchClick) {
                Icon(
                    Icons.Rounded.Search,
                    contentDescription = stringResource(R.string.hub_search),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            IconButton(onClick = onRefresh, enabled = !refreshing) {
                Icon(
                    Icons.Rounded.Refresh,
                    contentDescription = stringResource(R.string.hub_refresh),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    )
}

@Composable
private fun HubTabRow(
    selected: HubTab,
    onSelected: (HubTab) -> Unit,
    selectedFocusRequester: FocusRequester,
    compact: Boolean,
    horizontalPadding: androidx.compose.ui.unit.Dp
) {
    com.sbro.emucorex.ui.common.ScrollableFilterTabRow(
        tabs = HubTab.entries,
        selectedTab = selected,
        onSelected = onSelected,
        key = HubTab::name,
        label = HubTab::label,
        icon = HubTab::icon,
        selectedTabFocusRequester = selectedFocusRequester,
        compact = compact,
        horizontalContentPadding = horizontalPadding,
        modifier = Modifier.expandIntoHorizontalPadding(horizontalPadding)
    )
}

private fun Modifier.expandIntoHorizontalPadding(horizontalPadding: androidx.compose.ui.unit.Dp): Modifier =
    layout { measurable, constraints ->
        if (!constraints.hasBoundedWidth) {
            val placeable = measurable.measure(constraints)
            layout(placeable.width, placeable.height) { placeable.placeRelative(0, 0) }
        } else {
            val inset = horizontalPadding.roundToPx()
            val expandedWidth = constraints.maxWidth + inset * 2
            val placeable = measurable.measure(
                constraints.copy(minWidth = expandedWidth, maxWidth = expandedWidth)
            )
            layout(constraints.maxWidth, placeable.height) {
                placeable.placeRelative(-inset, 0)
            }
        }
    }

@Composable
private fun HubCard(item: HubItem, compact: Boolean, onOpen: () -> Unit, onToggleFavorite: () -> Unit) {
    val interactionSource = remember { MutableInteractionSource() }
    val cardRadius = if (compact) 18.dp else 22.dp
    val shape = RoundedCornerShape(cardRadius)
    val open = rememberDebouncedClick(onClick = onOpen)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .widthIn(max = 560.dp)
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        interactionSource = interactionSource,
        onClick = open
    ) {
        Column {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(if (compact) 2f else 16f / 9f)
                    .clip(RoundedCornerShape(topStart = cardRadius, topEnd = cardRadius))
            ) {
                HubImage(
                    spec = item.heroThumbnailUrl?.let {
                        HubImageSpec(
                            url = it,
                            sha256 = item.heroThumbnailSha256,
                            bytes = item.heroThumbnailBytes,
                            contentDescription = item.title
                        )
                    },
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Crop
                )
                Box(
                    modifier = Modifier.matchParentSize().background(
                        Brush.verticalGradient(
                            colorStops = arrayOf(
                                0f to Color.Black.copy(alpha = 0.06f),
                                0.52f to Color.Transparent,
                                1f to Color.Black.copy(alpha = 0.68f)
                            )
                        )
                    )
                )
                if (item.kind == HubKind.VIDEOS) {
                    Surface(
                        modifier = Modifier.align(Alignment.Center),
                        shape = CircleShape,
                        color = Color.Black.copy(alpha = 0.58f)
                    ) {
                        Icon(
                            Icons.Rounded.PlayArrow,
                            contentDescription = stringResource(R.string.hub_watch_video),
                            tint = Color.White,
                            modifier = Modifier.padding(if (compact) 10.dp else 14.dp).size(if (compact) 24.dp else 28.dp)
                        )
                    }
                }
                Surface(
                    modifier = Modifier.align(Alignment.BottomStart).padding(if (compact) 8.dp else 10.dp),
                    shape = RoundedCornerShape(999.dp),
                    color = Color.Black.copy(alpha = 0.55f)
                ) {
                    Text(
                        text = item.kind.label(),
                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                        color = Color.White,
                        style = if (compact) MaterialTheme.typography.labelSmall else MaterialTheme.typography.labelMedium
                    )
                }
            }
            Column(
                modifier = Modifier.padding(if (compact) 11.dp else 14.dp),
                verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)
            ) {
                Row(verticalAlignment = Alignment.Top) {
                    Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                        Text(
                            text = item.title,
                            style = if (compact) MaterialTheme.typography.titleSmall else MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = item.displayDate(),
                            style = if (compact) MaterialTheme.typography.labelSmall else MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                    IconButton(onClick = onToggleFavorite, modifier = Modifier.size(if (compact) 40.dp else 48.dp)) {
                        Icon(
                            imageVector = if (item.isFavorite) Icons.Rounded.Bookmark else Icons.Rounded.BookmarkBorder,
                            contentDescription = stringResource(if (item.isFavorite) R.string.hub_remove_saved else R.string.hub_save),
                            tint = if (item.isFavorite) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Text(
                    text = item.summary,
                    style = if (compact) MaterialTheme.typography.bodySmall else MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.78f),
                    maxLines = if (compact) 2 else 3,
                    overflow = TextOverflow.Ellipsis
                )
                if (item.channelTitle != null) {
                    Text(
                        text = item.channelTitle,
                        style = if (compact) MaterialTheme.typography.labelMedium else MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
        }
    }
}

@Composable
private fun HubSkeletonCard(compact: Boolean) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(if (compact) 18.dp else 22.dp))
            .background(MaterialTheme.colorScheme.surfaceContainerLow)
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(if (compact) 2f else 16f / 9f).shimmer())
        Column(Modifier.padding(if (compact) 11.dp else 14.dp), verticalArrangement = Arrangement.spacedBy(if (compact) 7.dp else 10.dp)) {
            Box(Modifier.fillMaxWidth(0.82f).height(20.dp).clip(RoundedCornerShape(8.dp)).shimmer())
            Box(Modifier.fillMaxWidth(0.42f).height(14.dp).clip(RoundedCornerShape(8.dp)).shimmer())
            Box(Modifier.fillMaxWidth().height(52.dp).clip(RoundedCornerShape(10.dp)).shimmer())
        }
    }
}

@Composable
private fun HubErrorState(onRetry: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxWidth().height(360.dp).padding(28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(Icons.Rounded.ErrorOutline, contentDescription = null, modifier = Modifier.size(52.dp), tint = MaterialTheme.colorScheme.error)
        Text(
            stringResource(R.string.hub_load_failed),
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.padding(top = 14.dp, bottom = 12.dp)
        )
        Button(onClick = onRetry) { Text(stringResource(R.string.hub_retry)) }
    }
}

@Composable
private fun HubEmptyState(tab: HubTab, searchActive: Boolean) {
    val text = if (searchActive) stringResource(R.string.hub_no_search_results) else when (tab) {
        HubTab.NEWS -> stringResource(R.string.hub_empty_news)
        HubTab.VIDEOS -> stringResource(R.string.hub_empty_videos)
        HubTab.HISTORY -> stringResource(R.string.hub_empty_history)
        HubTab.MANUALS -> stringResource(R.string.hub_empty_manuals)
        HubTab.FAVORITES -> stringResource(R.string.hub_empty_favorites)
    }
    Column(
        modifier = Modifier.fillMaxWidth().height(360.dp).padding(28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(
            imageVector = if (tab == HubTab.FAVORITES) Icons.Rounded.Favorite else Icons.Rounded.Newspaper,
            contentDescription = null,
            modifier = Modifier.size(54.dp),
            tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.7f)
        )
        Text(text, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 14.dp))
    }
}

@Composable
private fun HubStatusBanner(text: String, containerColor: Color, contentColor: Color) {
    Surface(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        shape = RoundedCornerShape(14.dp),
        color = containerColor
    ) {
        Text(text, modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp), color = contentColor, style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun HubTab.label(): String = when (this) {
    HubTab.NEWS -> stringResource(R.string.hub_tab_news)
    HubTab.VIDEOS -> stringResource(R.string.hub_tab_videos)
    HubTab.HISTORY -> stringResource(R.string.hub_tab_history)
    HubTab.MANUALS -> stringResource(R.string.hub_tab_manuals)
    HubTab.FAVORITES -> stringResource(R.string.hub_tab_favorites)
}

private fun HubTab.icon(): ImageVector = when (this) {
    HubTab.NEWS -> Icons.Rounded.Newspaper
    HubTab.VIDEOS -> Icons.Rounded.VideoLibrary
    HubTab.HISTORY -> Icons.Rounded.HistoryEdu
    HubTab.MANUALS -> Icons.AutoMirrored.Rounded.MenuBook
    HubTab.FAVORITES -> Icons.Rounded.Bookmark
}

@Composable
private fun HubKind.label(): String = when (this) {
    HubKind.NEWS -> stringResource(R.string.hub_tab_news)
    HubKind.VIDEOS -> stringResource(R.string.hub_tab_videos)
    HubKind.HISTORY -> stringResource(R.string.hub_tab_history)
    HubKind.MANUALS -> stringResource(R.string.hub_tab_manuals)
}

private fun HubItem.displayDate(): String = runCatching {
    when {
        kind == HubKind.HISTORY && datePrecision == "year" -> eventDate.orEmpty()
        kind == HubKind.HISTORY && datePrecision == "month" -> YearMonth.parse(eventDate).format(DateTimeFormatter.ofPattern("LLLL yyyy"))
        kind == HubKind.HISTORY -> LocalDate.parse(eventDate).format(DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM))
        else -> Instant.parse(publishedAt).atZone(java.time.ZoneId.systemDefault()).toLocalDate()
            .format(DateTimeFormatter.ofLocalizedDate(FormatStyle.MEDIUM).withLocale(Locale.getDefault()))
    }
}.getOrDefault(eventDate ?: publishedAt.substringBefore('T'))

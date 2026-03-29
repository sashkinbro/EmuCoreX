package com.sbro.emucorex.ui.catalog

import android.annotation.SuppressLint
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityStatus
import com.sbro.emucorex.data.ps2.Ps2CatalogSummary
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.PremiumLoadingAnimation
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.launch
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight", "FrequentlyChangingValue")
@Composable
fun CatalogSearchScreen(
    onGameClick: (Long) -> Unit,
    onBackClick: () -> Unit,
    viewModel: CatalogSearchViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val gridState = rememberLazyGridState()
    val scope = rememberCoroutineScope()
    val showScrollToTop = gridState.firstVisibleItemIndex > 2 || gridState.firstVisibleItemScrollOffset > 900
    var showGenreMenu by remember { mutableStateOf(false) }
    var showYearMenu by remember { mutableStateOf(false) }
    var showRatingMenu by remember { mutableStateOf(false) }
    val guardedBackClick = rememberDebouncedClick(onClick = onBackClick)
    val hasActiveFilters = uiState.selectedGenre != null || uiState.selectedYear != null || uiState.minRating != null
    val backFocusRequester = remember { FocusRequester() }
    val firstResultFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }

    LaunchedEffect(uiState.isLoading, uiState.results.size, shouldRequestGamepadFocus) {
        if (!uiState.isLoading && shouldRequestGamepadFocus) {
            if (uiState.results.isNotEmpty()) {
                firstResultFocusRequester.requestFocus()
            } else {
                backFocusRequester.requestFocus()
            }
        }
    }
    RequestFocusOnResume(
        focusRequester = if (uiState.results.isNotEmpty()) firstResultFocusRequester else backFocusRequester,
        enabled = shouldRequestGamepadFocus && !uiState.isLoading
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
    ) {
        when {
            uiState.isLoading -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    PremiumLoadingAnimation(size = 56.dp)
                }
            }

            !uiState.hasCatalog -> {
                CatalogMessageCard(
                    title = R.string.catalog_missing_title,
                    body = R.string.catalog_missing_body,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp)
                        .align(Alignment.TopCenter)
                )
            }

            else -> {
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(minSize = if (isLandscape) 128.dp else 160.dp),
                    state = gridState,
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding,
                        top = topInset,
                        bottom = 110.dp
                    ),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            NavigationBackButton(
                                onClick = guardedBackClick,
                                modifier = Modifier.focusRequester(backFocusRequester)
                            )
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = stringResource(R.string.catalog_search_title),
                                    style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                                    color = MaterialTheme.colorScheme.onBackground
                                )
                            }
                        }
                    }

                    item(span = { GridItemSpan(maxLineSpan) }) {
                        OutlinedTextField(
                            value = uiState.query,
                            onValueChange = viewModel::updateQuery,
                            modifier = Modifier
                                .fillMaxWidth(),
                            placeholder = { Text(text = stringResource(R.string.catalog_search_hint)) },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Search,
                                    contentDescription = null
                                )
                            },
                            trailingIcon = {
                                if (uiState.query.isNotBlank()) {
                                    Surface(
                                        modifier = Modifier.gamepadFocusableCard(shape = RoundedCornerShape(999.dp)),
                                        shape = RoundedCornerShape(999.dp),
                                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                                        onClick = { viewModel.updateQuery("") }
                                    ) {
                                        Box(
                                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                                            contentAlignment = Alignment.Center
                                        ) {
                                            Icon(
                                                imageVector = Icons.Rounded.Close,
                                                contentDescription = stringResource(R.string.home_search_clear),
                                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                                modifier = Modifier.size(16.dp)
                                            )
                                        }
                                    }
                                }
                            },
                            singleLine = true,
                            shape = RoundedCornerShape(20.dp),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f),
                                unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f),
                                focusedContainerColor = MaterialTheme.colorScheme.surface,
                                unfocusedContainerColor = MaterialTheme.colorScheme.surface
                            )
                        )
                    }

                    item(span = { GridItemSpan(maxLineSpan) }) {
                        Column(
                            modifier = Modifier.fillMaxWidth(),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Text(
                                text = stringResource(R.string.catalog_filters_title),
                                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            FlowRow(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                FilterMenuChip(
                                    label = stringResource(R.string.catalog_filter_genre),
                                    value = uiState.selectedGenre ?: stringResource(R.string.catalog_filter_all_genres),
                                    expanded = showGenreMenu,
                                    onExpandedChange = { showGenreMenu = it }
                                ) {
                                    DropdownMenuItem(
                                        text = { Text(stringResource(R.string.catalog_filter_all_genres)) },
                                        onClick = {
                                            showGenreMenu = false
                                            viewModel.updateGenre(null)
                                        }
                                    )
                                    uiState.availableGenres.forEach { genre ->
                                        DropdownMenuItem(
                                            text = { Text(genre) },
                                            onClick = {
                                                showGenreMenu = false
                                                viewModel.updateGenre(genre)
                                            }
                                        )
                                    }
                                }

                                FilterMenuChip(
                                    label = stringResource(R.string.catalog_filter_year),
                                    value = uiState.selectedYear?.toString()
                                        ?: stringResource(R.string.catalog_filter_all_years),
                                    expanded = showYearMenu,
                                    onExpandedChange = { showYearMenu = it }
                                ) {
                                    DropdownMenuItem(
                                        text = { Text(stringResource(R.string.catalog_filter_all_years)) },
                                        onClick = {
                                            showYearMenu = false
                                            viewModel.updateYear(null)
                                        }
                                    )
                                    uiState.availableYears.forEach { year ->
                                        DropdownMenuItem(
                                            text = { Text(year.toString()) },
                                            onClick = {
                                                showYearMenu = false
                                                viewModel.updateYear(year)
                                            }
                                        )
                                    }
                                }

                                FilterMenuChip(
                                    label = stringResource(R.string.catalog_filter_rating),
                                    value = uiState.minRating?.let(::formatRatingFilter)
                                        ?: stringResource(R.string.catalog_filter_any_rating),
                                    expanded = showRatingMenu,
                                    onExpandedChange = { showRatingMenu = it }
                                ) {
                                    DropdownMenuItem(
                                        text = { Text(stringResource(R.string.catalog_filter_any_rating)) },
                                        onClick = {
                                            showRatingMenu = false
                                            viewModel.updateMinRating(null)
                                        }
                                    )
                                    listOf(70.0, 80.0, 90.0).forEach { rating ->
                                        DropdownMenuItem(
                                            text = { Text(formatRatingFilter(rating)) },
                                            onClick = {
                                                showRatingMenu = false
                                                viewModel.updateMinRating(rating)
                                            }
                                        )
                                    }
                                }

                                if (hasActiveFilters) {
                                    TextButton(
                                        modifier = Modifier.focusable(),
                                        onClick = { viewModel.clearFilters() }
                                    ) {
                                        Text(text = stringResource(R.string.catalog_filters_clear))
                                    }
                                }
                            }
                        }
                    }

                    if (uiState.results.isEmpty()) {
                        item(span = { GridItemSpan(maxLineSpan) }) {
                            CatalogMessageCard(
                                title = R.string.catalog_empty_title,
                                body = R.string.catalog_empty_body,
                                modifier = Modifier.fillMaxWidth()
                            )
                        }
                    } else {
                        items(uiState.results.size, key = { index -> uiState.results[index].igdbId }) { index ->
                            val game = uiState.results[index]
                            LaunchedEffect(index, uiState.results.size, uiState.hasMore, uiState.isLoadingMore) {
                                viewModel.loadMoreIfNeeded(index)
                            }
                            CatalogGameCard(
                                modifier = if (index == 0) Modifier.focusRequester(firstResultFocusRequester) else Modifier,
                                game = game,
                                onClick = { onGameClick(game.igdbId) },
                                compact = isLandscape
                            )
                        }
                        if (uiState.isLoadingMore) {
                            item(span = { GridItemSpan(maxLineSpan) }) {
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 16.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    PremiumLoadingAnimation(size = 32.dp)
                                }
                            }
                        }
                    }
                }

                ScrollToTopButton(
                    visible = showScrollToTop,
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(end = 16.dp, bottom = 24.dp),
                    onClick = {
                        scope.launch {
                            gridState.animateScrollToItem(0)
                        }
                    }
                )
            }
        }
    }
}

@Composable
private fun ScrollToTopButton(
    visible: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(tween(180)) + scaleIn(tween(180)),
        exit = fadeOut(tween(140)) + scaleOut(tween(140)),
        modifier = modifier
    ) {
        Surface(
            modifier = Modifier.gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.78f),
            tonalElevation = 2.dp,
            shadowElevation = 4.dp,
            onClick = onClick
        ) {
            Box(
                modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.KeyboardArrowUp,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.size(22.dp)
                )
            }
        }
    }
}

@Composable
private fun FilterMenuChip(
    modifier: Modifier = Modifier,
    label: String,
    value: String,
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    content: @Composable () -> Unit
) {
    Box {
        Surface(
            modifier = modifier.focusable(),
            shape = RoundedCornerShape(999.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 0.dp,
            shadowElevation = 0.dp,
            onClick = { onExpandedChange(true) }
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    text = "$label: $value",
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
            }
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { onExpandedChange(false) }
        ) {
            content()
        }
    }
}

private fun formatRatingFilter(value: Double): String {
    return String.format(Locale.US, "%.1f+", value / 10.0)
}

@Composable
private fun CatalogGameCard(
    modifier: Modifier = Modifier,
    game: Ps2CatalogSummary,
    onClick: () -> Unit,
    compact: Boolean
) {
    Surface(
        modifier = modifier.gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        onClick = onClick
    ) {
        Column {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(2f / 3f)
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            ) {
                GameCoverArt(
                    coverPath = game.coverUrl,
                    fallbackTitle = game.name,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.FillHeight
                )
                game.pcsx2Compatibility?.let { compatibility ->
                    Surface(
                        modifier = Modifier
                            .padding(8.dp)
                            .align(Alignment.TopStart),
                        shape = RoundedCornerShape(8.dp),
                        color = catalogCompatibilityColor(compatibility.status),
                        shadowElevation = 0.dp,
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)) {
                            Text(
                                text = stringResource(catalogCompatibilityStatusRes(compatibility.status)),
                                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                                color = androidx.compose.ui.graphics.Color.White
                            )
                        }
                    }
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = if (compact) 76.dp else 84.dp)
                    .padding(
                        horizontal = if (compact) 10.dp else 12.dp,
                        vertical = if (compact) 8.dp else 10.dp
                    ),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = game.name,
                    style = if (compact) {
                        MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium)
                    } else {
                        MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Medium)
                    },
                    minLines = 2,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    color = MaterialTheme.colorScheme.onSurface
                )
                val meta = buildString {
                    game.year?.let { append(it) }
                    game.rating?.let {
                        if (isNotBlank()) append(" • ")
                        append(String.format(Locale.US, "%.1f", it / 10.0))
                    }
                }
                Text(
                    text = meta.ifBlank { " " },
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 1,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

private fun catalogCompatibilityStatusRes(status: Pcsx2CompatibilityStatus): Int {
    return when (status) {
        Pcsx2CompatibilityStatus.UNKNOWN -> R.string.pcsx2_compatibility_unknown
        Pcsx2CompatibilityStatus.NOTHING -> R.string.pcsx2_compatibility_nothing
        Pcsx2CompatibilityStatus.INTRO -> R.string.pcsx2_compatibility_intro
        Pcsx2CompatibilityStatus.IN_GAME -> R.string.pcsx2_compatibility_in_game
        Pcsx2CompatibilityStatus.PLAYABLE -> R.string.pcsx2_compatibility_playable
        Pcsx2CompatibilityStatus.PERFECT -> R.string.pcsx2_compatibility_perfect
    }
}

private fun catalogCompatibilityColor(status: Pcsx2CompatibilityStatus): androidx.compose.ui.graphics.Color {
    return when (status) {
        Pcsx2CompatibilityStatus.PERFECT -> androidx.compose.ui.graphics.Color(0xFF2E7D32)
        Pcsx2CompatibilityStatus.PLAYABLE -> androidx.compose.ui.graphics.Color(0xFF1B5E20)
        Pcsx2CompatibilityStatus.IN_GAME -> androidx.compose.ui.graphics.Color(0xFFEF6C00)
        Pcsx2CompatibilityStatus.INTRO -> androidx.compose.ui.graphics.Color(0xFF8E24AA)
        Pcsx2CompatibilityStatus.NOTHING -> androidx.compose.ui.graphics.Color(0xFFC62828)
        Pcsx2CompatibilityStatus.UNKNOWN -> androidx.compose.ui.graphics.Color(0xFF546E7A)
    }
}

@Composable
private fun CatalogMessageCard(
    title: Int,
    body: Int,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = stringResource(body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

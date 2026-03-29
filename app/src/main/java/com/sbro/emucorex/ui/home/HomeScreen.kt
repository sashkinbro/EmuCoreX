package com.sbro.emucorex.ui.home

import android.annotation.SuppressLint
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Sort
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ViewAgenda
import androidx.compose.material.icons.rounded.ViewModule
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityStatus
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.PremiumLoadingAnimation
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.theme.GradientEnd
import com.sbro.emucorex.ui.theme.GradientStart
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.launch
import java.util.Locale
import androidx.compose.foundation.lazy.itemsIndexed as rowItemsIndexed

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight", "FrequentlyChangingValue")
@Composable
fun HomeScreen(
    onGameClick: (GameItem) -> Unit,
    onMenuClick: (() -> Unit)? = null,
    viewModel: HomeViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val isWide = configuration.screenWidthDp >= 900
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val horizontalInset = ScreenHorizontalPadding
    val sectionTopSpacing = 2.dp
    val sectionInnerSpacing = 4.dp
    val minCellSize = if (isLandscape) 128 else 140
    val columnsCount = maxOf(1, (configuration.screenWidthDp + 14) / (minCellSize + 14))
    val isListView = uiState.libraryViewMode == HomeLibraryViewMode.LIST
    val gridState = rememberLazyGridState()
    val scope = rememberCoroutineScope()
    val showScrollToTop = gridState.firstVisibleItemIndex > 2 || gridState.firstVisibleItemScrollOffset > 900
    val initialGamepadFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }

    val folderPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let { viewModel.onFolderSelected(it) }
    }
    val biosPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let { viewModel.onBiosFolderSelected(it) }
    }

    var showSortMenu by remember { mutableStateOf(false) }
    LaunchedEffect(uiState.isLoading, uiState.games.size, uiState.recentGames.size, shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus && !uiState.isLoading && uiState.games.isNotEmpty()) {
            initialGamepadFocusRequester.requestFocus()
        }
    }
    RequestFocusOnResume(
        focusRequester = initialGamepadFocusRequester,
        enabled = shouldRequestGamepadFocus && !uiState.isLoading && uiState.games.isNotEmpty()
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
    ) {
        if (uiState.isBootstrapping || uiState.isLoading) {
            LoadingState()
        } else if (!uiState.gameFolderSet || !uiState.biosValid) {
            EmptyState(
                biosReady = uiState.biosValid,
                gamesReady = uiState.gameFolderSet,
                onBiosClick = { biosPicker.launch(null) },
                onFolderClick = { folderPicker.launch(null) },
                topInset = topInset
            )
        } else {
            val columns = GridCells.Fixed(if (isListView) 1 else columnsCount)

            LazyVerticalGrid(
                columns = columns,
                state = gridState,
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(
                    top = topInset + 4.dp,
                    bottom = 100.dp
                ),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Header item
                item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(
                                start = horizontalInset,
                                end = horizontalInset,
                                top = if (isLandscape) sectionTopSpacing else 2.dp,
                                bottom = sectionTopSpacing
                            ),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(
                            modifier = Modifier
                                .weight(1f)
                                .padding(end = 8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            if (!isWide && onMenuClick != null) {
                                Surface(
                                    modifier = Modifier
                                        .size(44.dp),
                                    shape = RoundedCornerShape(14.dp),
                                    color = MaterialTheme.colorScheme.surface,
                                    tonalElevation = 3.dp,
                                    shadowElevation = 5.dp,
                                    onClick = rememberDebouncedClick(onClick = onMenuClick)
                                ) {
                                    Box(
                                        modifier = Modifier.fillMaxSize(),
                                        contentAlignment = Alignment.Center
                                    ) {
                                        Icon(
                                            imageVector = Icons.Rounded.Menu,
                                            contentDescription = null,
                                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                            modifier = Modifier.size(18.dp)
                                        )
                                    }
                                }
                                Spacer(modifier = Modifier.width(14.dp))
                            }
                            Column(
                                modifier = Modifier.weight(1f)
                            ) {
                                Text(
                                    text = stringResource(R.string.app_name),
                                    style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                                    color = MaterialTheme.colorScheme.onBackground,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = stringResource(R.string.home_game_count, uiState.games.size),
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                        Row(
                            modifier = Modifier.widthIn(min = 88.dp),
                            horizontalArrangement = Arrangement.spacedBy(2.dp)
                        ) {
                            IconButton(onClick = { viewModel.refreshGames() }) {
                                if (uiState.isRefreshing) {
                                    CircularProgressIndicator(
                                        modifier = Modifier.size(20.dp),
                                        strokeWidth = 2.dp,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                } else {
                                    Icon(
                                        imageVector = Icons.Rounded.Refresh,
                                        contentDescription = stringResource(R.string.home_refresh),
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                            IconButton(onClick = viewModel::toggleLibraryViewMode) {
                                Icon(
                                    imageVector = if (isListView) Icons.Rounded.ViewModule else Icons.Rounded.ViewAgenda,
                                    contentDescription = if (isListView) {
                                        stringResource(R.string.home_view_grid)
                                    } else {
                                        stringResource(R.string.home_view_list)
                                    },
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }

                // Search Bar item
                item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                    OutlinedTextField(
                        value = uiState.searchQuery,
                        onValueChange = viewModel::updateSearchQuery,
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = horizontalInset)
                            .padding(top = 0.dp, bottom = sectionTopSpacing),
                        placeholder = {
                            Text(
                                stringResource(R.string.home_search),
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
                            )
                        },
                        leadingIcon = {
                            Icon(
                                imageVector = Icons.Rounded.Search,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
                            )
                        },
                        trailingIcon = {
                            Row {
                                if (uiState.searchQuery.isNotBlank()) {
                                    IconButton(onClick = { viewModel.updateSearchQuery("") }) {
                                        Icon(
                                            imageVector = Icons.Rounded.Close,
                                            contentDescription = stringResource(R.string.home_search_clear),
                                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                }
                                Box {
                                    IconButton(onClick = { showSortMenu = true }) {
                                        Icon(
                                            imageVector = Icons.AutoMirrored.Rounded.Sort,
                                            contentDescription = null,
                                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                    DropdownMenu(
                                        expanded = showSortMenu,
                                        onDismissRequest = { showSortMenu = false }
                                    ) {
                                        SortMenuItem(
                                            label = stringResource(R.string.home_sort_title),
                                            isSelected = uiState.sortOption == HomeSortOption.TITLE,
                                            onClick = {
                                                viewModel.updateSortOption(HomeSortOption.TITLE)
                                                showSortMenu = false
                                            }
                                        )
                                        SortMenuItem(
                                            label = stringResource(R.string.home_sort_size_desc),
                                            isSelected = uiState.sortOption == HomeSortOption.SIZE_DESC,
                                            onClick = {
                                                viewModel.updateSortOption(HomeSortOption.SIZE_DESC)
                                                showSortMenu = false
                                            }
                                        )
                                        SortMenuItem(
                                            label = stringResource(R.string.home_sort_size_asc),
                                            isSelected = uiState.sortOption == HomeSortOption.SIZE_ASC,
                                            onClick = {
                                                viewModel.updateSortOption(HomeSortOption.SIZE_ASC)
                                                showSortMenu = false
                                            }
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

                // Empty / Games logic
                if (uiState.games.isEmpty()) {
                    item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                        NoGamesState()
                    }
                } else {
                    if (uiState.recentGames.isNotEmpty()) {
                        item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(top = sectionTopSpacing)
                            ) {
                                Text(
                                    text = stringResource(R.string.home_recent_title),
                                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                                    color = MaterialTheme.colorScheme.onSurface,
                                    modifier = Modifier
                                        .padding(horizontal = horizontalInset)
                                        .padding(vertical = 4.dp)
                                )
                                Spacer(modifier = Modifier.height(sectionInnerSpacing))
                                LazyRow(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 4.dp),
                                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                                ) {
                                    rowItemsIndexed(
                                        items = uiState.recentGames,
                                        key = { _, game -> "recent_${game.path}" }
                                    ) { index, game ->
                                        RecentGameCard(
                                            modifier = Modifier
                                                .padding(
                                                    start = if (index == 0) horizontalInset else 0.dp,
                                                    end = if (index == uiState.recentGames.lastIndex) horizontalInset else 0.dp
                                                )
                                                .then(
                                                    if (index == 0) Modifier.focusRequester(initialGamepadFocusRequester)
                                                    else Modifier
                                                ),
                                            game = game,
                                            onClick = { onGameClick(game) },
                                            compact = isLandscape
                                        )
                                    }
                                }
                            }
                        }
                    }

                    item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = horizontalInset),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            uiState.games.chunked(if (isListView) 1 else columnsCount).forEach { rowGames ->
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    rowGames.forEach { game ->
                                        Box(modifier = Modifier.weight(1f)) {
                                            val itemModifier = if (uiState.recentGames.isEmpty() && game == uiState.games.first()) {
                                                Modifier.focusRequester(initialGamepadFocusRequester)
                                            } else {
                                                Modifier
                                            }
                                            if (isListView) {
                                                GameListCard(
                                                    modifier = itemModifier,
                                                    game = game,
                                                    onClick = { onGameClick(game) }
                                                )
                                            } else {
                                                GameCard(
                                                    modifier = itemModifier,
                                                    game = game,
                                                    onClick = { onGameClick(game) },
                                                    compact = isLandscape
                                                )
                                            }
                                        }
                                    }
                                    repeat((if (isListView) 1 else columnsCount) - rowGames.size) {
                                        Spacer(modifier = Modifier.weight(1f))
                                    }
                                }
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
                    contentDescription = stringResource(R.string.back),
                    tint = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.size(22.dp)
                )
            }
        }
    }
}

@Composable
private fun SortMenuItem(
    label: String,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    DropdownMenuItem(
        text = {
            Text(
                text = label,
                color = if (isSelected) MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.onSurface
            )
        },
        onClick = onClick,
        trailingIcon = {
            if (isSelected) {
                Icon(
                    imageVector = Icons.Rounded.CheckCircle,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(18.dp)
                )
            }
        }
    )
}

@Composable
private fun LoadingState() {
    Box(
        modifier = Modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(24.dp)
        ) {
            PremiumLoadingAnimation(size = 80.dp)

        }
    }
}

@Composable
private fun EmptyState(
    biosReady: Boolean,
    gamesReady: Boolean,
    onBiosClick: () -> Unit,
    onFolderClick: () -> Unit,
    topInset: androidx.compose.ui.unit.Dp
) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .padding(
                top = topInset,
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                bottom = 24.dp
            ),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(20.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(100.dp)
                    .clip(RoundedCornerShape(28.dp))
                    .background(
                        Brush.linearGradient(
                            colors = listOf(
                                GradientStart.copy(alpha = 0.15f),
                                GradientEnd.copy(alpha = 0.15f)
                            )
                        )
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.SportsEsports,
                    contentDescription = null,
                    modifier = Modifier.size(48.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }

            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = stringResource(R.string.home_empty_title),
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = stringResource(R.string.home_empty_subtitle),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                FilledTonalButton(
                    onClick = onBiosClick,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Memory,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(stringResource(R.string.home_choose_bios))
                }
                FilledTonalButton(
                    onClick = onFolderClick,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.FolderOpen,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(stringResource(R.string.settings_game_path))
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            StatusCard(
                icon = Icons.Rounded.CheckCircle,
                title = stringResource(R.string.settings_bios_path),
                isReady = biosReady,
                modifier = Modifier.fillMaxWidth()
            )
            StatusCard(
                icon = Icons.Rounded.FolderOpen,
                title = stringResource(R.string.settings_game_path),
                isReady = gamesReady,
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}

@Composable
private fun StatusCard(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    isReady: Boolean,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(18.dp),
        color = if (isReady) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
        } else {
            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.25f)
        },
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(
                        if (isReady) MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                        else MaterialTheme.colorScheme.error.copy(alpha = 0.15f)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = if (isReady) MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(20.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = if (isReady) stringResource(R.string.home_status_ready)
                    else stringResource(R.string.home_status_missing),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (isReady) MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.error
                )
            }
        }
    }
}

@Composable
private fun NoGamesState() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(400.dp)
            .padding(ScreenHorizontalPadding),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.Search,
                contentDescription = null,
                modifier = Modifier.size(56.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.25f)
            )
            Text(
                text = stringResource(R.string.home_empty_search_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = stringResource(R.string.home_empty_search_subtitle),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Composable
private fun RecentGameCard(
    modifier: Modifier = Modifier,
    game: GameItem,
    onClick: () -> Unit,
    compact: Boolean
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.95f else 1f, tween(100))

    Surface(
        modifier = modifier
            .width(if (compact) 112.dp else 128.dp)
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        onClick = debouncedClick
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(2f / 3f)
                    .clip(RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            ) {
                GameCoverArt(
                    coverPath = game.coverArtPath,
                    fallbackTitle = game.title,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.FillHeight
                )
                game.pcsx2Compatibility?.let { compatibility ->
                    CompatibilityBadge(
                        status = compatibility.status,
                        modifier = Modifier
                            .padding(8.dp)
                            .align(Alignment.TopStart)
                    )
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = if (compact) 72.dp else 78.dp)
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                    minLines = 2,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    color = MaterialTheme.colorScheme.onSurface
                )
                if (game.catalogYear != null || game.catalogRating != null) {
                    CardMetaText(
                        year = game.catalogYear,
                        rating = game.catalogRating
                    )
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.PlayArrow,
                        contentDescription = null,
                        modifier = Modifier.size(12.dp),
                        tint = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = stringResource(R.string.home_recent_label),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
    }
}

@Composable
private fun GameCard(
    modifier: Modifier = Modifier,
    game: GameItem,
    onClick: () -> Unit,
    compact: Boolean
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.96f else 1f, tween(100))

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .animateContentSize()
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        onClick = debouncedClick
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(2f / 3f)
                    .clip(RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            ) {
                GameCoverArt(
                    coverPath = game.coverArtPath,
                    fallbackTitle = game.title,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.FillHeight
                )
                game.pcsx2Compatibility?.let { compatibility ->
                    CompatibilityBadge(
                        status = compatibility.status,
                        modifier = Modifier
                            .padding(8.dp)
                            .align(Alignment.TopStart)
                    )
                }
                // Format badge
                Box(
                    modifier = Modifier
                        .padding(8.dp)
                        .align(Alignment.TopEnd)
                        .clip(RoundedCornerShape(8.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.85f))
                        .padding(horizontal = 8.dp, vertical = 3.dp)
                ) {
                    Text(
                        text = game.fileName.substringAfterLast('.').uppercase(),
                        style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                        color = Color.White
                    )
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = if (compact) 60.dp else 68.dp)
                    .padding(
                        horizontal = if (compact) 10.dp else 12.dp,
                        vertical = if (compact) 8.dp else 10.dp
                    ),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = game.title,
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
                if (game.catalogYear != null || game.catalogRating != null) {
                    CardMetaText(
                        year = game.catalogYear,
                        rating = game.catalogRating
                    )
                }
            }
        }
    }
}

@Composable
private fun GameListCard(
    modifier: Modifier = Modifier,
    game: GameItem,
    onClick: () -> Unit
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.985f else 1f, tween(100))

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .animateContentSize()
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        onClick = debouncedClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .width(92.dp)
                    .aspectRatio(2f / 3f)
                    .clip(RoundedCornerShape(14.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            ) {
                GameCoverArt(
                    coverPath = game.coverArtPath,
                    fallbackTitle = game.title,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.FillHeight
                )
                game.pcsx2Compatibility?.let { compatibility ->
                    CompatibilityBadge(
                        status = compatibility.status,
                        modifier = Modifier
                            .padding(8.dp)
                            .align(Alignment.TopStart)
                    )
                }
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.Top
                ) {
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Spacer(modifier = Modifier.width(10.dp))
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f))
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = game.fileName.substringAfterLast('.').uppercase(),
                            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
                if (game.catalogYear != null || game.catalogRating != null) {
                    CardMetaText(
                        year = game.catalogYear,
                        rating = game.catalogRating
                    )
                }
                Text(
                    text = game.fileName,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = game.serial ?: "",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

@Composable
private fun CardMetaText(
    year: Int?,
    rating: Double?
) {
    val meta = buildString {
        year?.let { append(it) }
        rating?.let {
            if (isNotBlank()) append(" • ")
            append(String.format(Locale.US, "%.1f", it / 10.0))
        }
    }
    if (meta.isBlank()) return
    Text(
        text = meta,
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 1
    )
}

@Composable
private fun CompatibilityBadge(
    status: Pcsx2CompatibilityStatus,
    modifier: Modifier = Modifier
) {
    val background = when (status) {
        Pcsx2CompatibilityStatus.PERFECT -> Color(0xFF2E7D32)
        Pcsx2CompatibilityStatus.PLAYABLE -> Color(0xFF1B5E20)
        Pcsx2CompatibilityStatus.IN_GAME -> Color(0xFFEF6C00)
        Pcsx2CompatibilityStatus.INTRO -> Color(0xFF8E24AA)
        Pcsx2CompatibilityStatus.NOTHING -> Color(0xFFC62828)
        Pcsx2CompatibilityStatus.UNKNOWN -> MaterialTheme.colorScheme.surface.copy(alpha = 0.92f)
    }
    val contentColor = when (status) {
        Pcsx2CompatibilityStatus.UNKNOWN -> MaterialTheme.colorScheme.onSurface
        else -> Color.White
    }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(background.copy(alpha = if (status == Pcsx2CompatibilityStatus.UNKNOWN) 1f else 0.92f))
            .padding(horizontal = 8.dp, vertical = 3.dp)
    ) {
        Text(
            text = stringResource(homeCompatibilityStatusRes(status)),
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
            color = contentColor
        )
    }
}

private fun homeCompatibilityStatusRes(status: Pcsx2CompatibilityStatus): Int {
    return when (status) {
        Pcsx2CompatibilityStatus.UNKNOWN -> R.string.pcsx2_compatibility_unknown
        Pcsx2CompatibilityStatus.NOTHING -> R.string.pcsx2_compatibility_nothing
        Pcsx2CompatibilityStatus.INTRO -> R.string.pcsx2_compatibility_intro
        Pcsx2CompatibilityStatus.IN_GAME -> R.string.pcsx2_compatibility_in_game
        Pcsx2CompatibilityStatus.PLAYABLE -> R.string.pcsx2_compatibility_playable
        Pcsx2CompatibilityStatus.PERFECT -> R.string.pcsx2_compatibility_perfect
    }
}

package com.sbro.emucorex.ui.home

import android.annotation.SuppressLint
import android.net.Uri
import android.widget.Toast
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
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Sort
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SportsEsports
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
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.data.CustomGameCoverRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.PremiumLoadingAnimation
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
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
    onContinueGame: (GameItem) -> Unit,
    onLoadSaveClick: (GameItem) -> Unit,
    onManageGameClick: (GameItem) -> Unit,
    onCreateShortcutClick: (GameItem) -> Unit,
    onMenuClick: (() -> Unit)? = null,
    viewModel: HomeViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val configuration = LocalConfiguration.current
    val context = LocalContext.current
    val customCoverRepository = remember(context) { CustomGameCoverRepository(context) }
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val isTabletClass = configuration.smallestScreenWidthDp >= 600
    val isWide = isTabletClass && configuration.screenWidthDp >= 900
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val horizontalInset = ScreenHorizontalPadding
    val sectionTopSpacing = 2.dp
    val sectionInnerSpacing = 4.dp
    val minCellSize = if (isLandscape) 94.dp else 102.dp
    val contentWidthDp = if (isWide) (configuration.screenWidthDp - 332).coerceAtLeast(320) else configuration.screenWidthDp
    val columnsCount = maxOf(1, (contentWidthDp + 12) / (minCellSize.value.toInt() + 12))
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
    var pendingCustomCoverGame by remember { mutableStateOf<GameItem?>(null) }
    var gameAwaitingPickerLaunch by remember { mutableStateOf<GameItem?>(null) }
    val customCoverAppliedMessage = stringResource(R.string.home_game_menu_custom_cover_applied)
    val customCoverFailedMessage = stringResource(R.string.home_game_menu_custom_cover_failed)
    val customCoverPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        val targetGame = pendingCustomCoverGame
        pendingCustomCoverGame = null
        if (uri != null && targetGame != null) {
            scope.launch {
                val success = viewModel.setCustomCover(targetGame, uri)
                Toast.makeText(
                    context,
                    if (success) customCoverAppliedMessage else customCoverFailedMessage,
                    Toast.LENGTH_SHORT
                ).show()
            }
        }
    }
    LaunchedEffect(gameAwaitingPickerLaunch) {
        val game = gameAwaitingPickerLaunch ?: return@LaunchedEffect
        pendingCustomCoverGame = game
        gameAwaitingPickerLaunch = null
        customCoverPicker.launch("image/*")
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
            .padding(horizontalSystemBarPadding)
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
            val columns = if (isListView) GridCells.Fixed(1) else GridCells.Adaptive(minSize = minCellSize)

            LazyVerticalGrid(
                columns = columns,
                state = gridState,
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(
                    top = topInset + 4.dp,
                    bottom = 76.dp + bottomInset
                ),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
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
                            IconButton(
                                onClick = { viewModel.refreshGames() },
                                enabled = !uiState.isLoading && !uiState.isRefreshing
                            ) {
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

                if (uiState.showHomeSearch) {
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
                                                label = stringResource(R.string.home_sort_title_asc),
                                                isSelected = uiState.sortOption == HomeSortOption.TITLE_ASC,
                                                onClick = {
                                                    viewModel.updateSortOption(HomeSortOption.TITLE_ASC)
                                                    showSortMenu = false
                                                }
                                            )
                                            SortMenuItem(
                                                label = stringResource(R.string.home_sort_title_desc),
                                                isSelected = uiState.sortOption == HomeSortOption.TITLE_DESC,
                                                onClick = {
                                                    viewModel.updateSortOption(HomeSortOption.TITLE_DESC)
                                                    showSortMenu = false
                                                }
                                            )
                                            SortMenuItem(
                                                label = stringResource(R.string.home_sort_recent_desc),
                                                isSelected = uiState.sortOption == HomeSortOption.RECENT_DESC,
                                                onClick = {
                                                    viewModel.updateSortOption(HomeSortOption.RECENT_DESC)
                                                    showSortMenu = false
                                                }
                                            )
                                            SortMenuItem(
                                                label = stringResource(R.string.home_sort_recent_asc),
                                                isSelected = uiState.sortOption == HomeSortOption.RECENT_ASC,
                                                onClick = {
                                                    viewModel.updateSortOption(HomeSortOption.RECENT_ASC)
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
                                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    rowItemsIndexed(
                                        items = uiState.recentGames,
                                        key = { _, game -> "recent_${game.path}" }
                                    ) { index, game ->
                                        val showCoverPlaceholder = uiState.isCoverArtDisabled &&
                                            !customCoverRepository.isCustomCoverPath(game.coverArtPath)
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
                                            showCenteredTitlePlaceholder = showCoverPlaceholder,
                                            onClick = { onGameClick(game) },
                                            onLongClickStart = { onGameClick(game) },
                                            onLongClickContinue = { onContinueGame(game) },
                                            onLongClickLoadSave = { onLoadSaveClick(game) },
                                            onLongClickManage = { onManageGameClick(game) },
                                            onLongClickCreateShortcut = { onCreateShortcutClick(game) },
                                            onLongClickCustomCover = {
                                                gameAwaitingPickerLaunch = game
                                            },
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
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            uiState.games.chunked(if (isListView) 1 else columnsCount).forEach { rowGames ->
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                                ) {
                                    rowGames.forEach { game ->
                                        val showCoverPlaceholder = uiState.isCoverArtDisabled &&
                                            !customCoverRepository.isCustomCoverPath(game.coverArtPath)
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
                                                    isCoverArtDisabled = showCoverPlaceholder,
                                                    onClick = { onGameClick(game) },
                                                    onLongClickStart = { onGameClick(game) },
                                                    onLongClickContinue = { onContinueGame(game) },
                                                    onLongClickLoadSave = { onLoadSaveClick(game) },
                                                    onLongClickManage = { onManageGameClick(game) },
                                                    onLongClickCreateShortcut = { onCreateShortcutClick(game) },
                                                    onLongClickCustomCover = {
                                                        gameAwaitingPickerLaunch = game
                                                    }
                                                )
                                            } else {
                                                GameCard(
                                                    modifier = itemModifier,
                                                    game = game,
                                                    showCenteredTitlePlaceholder = showCoverPlaceholder,
                                                    onClick = { onGameClick(game) },
                                                    onLongClickStart = { onGameClick(game) },
                                                    onLongClickContinue = { onContinueGame(game) },
                                                    onLongClickLoadSave = { onLoadSaveClick(game) },
                                                    onLongClickManage = { onManageGameClick(game) },
                                                    onLongClickCreateShortcut = { onCreateShortcutClick(game) },
                                                    onLongClickCustomCover = {
                                                        gameAwaitingPickerLaunch = game
                                                    }
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
                        .padding(end = 16.dp, bottom = 16.dp + bottomInset),
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
        val shape = RoundedCornerShape(18.dp)
        val interactionSource = remember { MutableInteractionSource() }

        Box(
            modifier = Modifier
                .clip(shape)
                .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.78f))
                .clickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = onClick
                )
                .padding(horizontal = 14.dp, vertical = 14.dp),
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
    val scrollState = rememberScrollState()
    Box(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(
                top = topInset,
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                bottom = 24.dp
            ),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier.fillMaxWidth(),
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

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
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
    showCenteredTitlePlaceholder: Boolean,
    onClick: () -> Unit,
    onLongClickStart: () -> Unit,
    onLongClickContinue: () -> Unit,
    onLongClickLoadSave: () -> Unit,
    onLongClickManage: () -> Unit,
    onLongClickCreateShortcut: () -> Unit,
    onLongClickCustomCover: () -> Unit,
    compact: Boolean
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.95f else 1f, tween(100))
    var showMenu by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .width(if (compact) 98.dp else 108.dp)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .graphicsLayer(scaleX = scale, scaleY = scale)
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = debouncedClick,
                    onLongClick = { showMenu = true }
                )
                .gamepadFocusableCard(shape = RoundedCornerShape(16.dp)),
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 0.dp,
            shadowElevation = 0.dp
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(2f / 3f)
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            ) {
                if (showCenteredTitlePlaceholder) {
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.78f),
                        textAlign = TextAlign.Center,
                        maxLines = if (compact) 3 else 4,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(horizontal = 10.dp, vertical = 12.dp)
                            .wrapContentSize(Alignment.Center)
                    )
                } else {
                    GameCoverArt(
                        coverPath = game.coverArtPath,
                        fallbackTitle = game.title,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Crop
                    )
                }
            }
        }
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 8.dp, bottom = 8.dp)
        ) {
            GameCardContextMenu(
                expanded = showMenu,
                offset = DpOffset(x = 0.dp, y = 10.dp),
                onDismiss = { showMenu = false },
                onStart = {
                    showMenu = false
                    onLongClickStart()
                },
                onContinue = {
                    showMenu = false
                    onLongClickContinue()
                },
                onLoadSave = {
                    showMenu = false
                    onLongClickLoadSave()
                },
                onManage = {
                    showMenu = false
                    onLongClickManage()
                },
                onCreateShortcut = {
                    showMenu = false
                    onLongClickCreateShortcut()
                },
                onCustomCover = {
                    showMenu = false
                    onLongClickCustomCover()
                }
            )
        }
    }
}

@Composable
private fun GameCard(
    modifier: Modifier = Modifier,
    game: GameItem,
    showCenteredTitlePlaceholder: Boolean,
    onClick: () -> Unit,
    onLongClickStart: () -> Unit,
    onLongClickContinue: () -> Unit,
    onLongClickLoadSave: () -> Unit,
    onLongClickManage: () -> Unit,
    onLongClickCreateShortcut: () -> Unit,
    onLongClickCustomCover: () -> Unit
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.96f else 1f, tween(100))
    var showMenu by remember { mutableStateOf(false) }

    Box(modifier = modifier.fillMaxWidth()) {
        Surface(
        modifier = modifier
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .animateContentSize()
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = debouncedClick,
                onLongClick = { showMenu = true }
            )
            .gamepadFocusableCard(shape = RoundedCornerShape(16.dp)),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(2f / 3f)
                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
        ) {
            if (showCenteredTitlePlaceholder) {
                GridCoverPlaceholder(
                    modifier = Modifier.fillMaxSize(),
                    title = game.title,
                    titleMaxLines = 4,
                    contentScale = ContentScale.Crop
                )
            } else {
                GameCoverArt(
                    coverPath = game.coverArtPath,
                    fallbackTitle = game.title,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Crop
                )
            }
        }
    }
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 8.dp, bottom = 8.dp)
        ) {
            GameCardContextMenu(
                expanded = showMenu,
                offset = DpOffset(x = 0.dp, y = 10.dp),
                onDismiss = { showMenu = false },
                onStart = {
                    showMenu = false
                    onLongClickStart()
                },
                onContinue = {
                    showMenu = false
                    onLongClickContinue()
                },
                onLoadSave = {
                    showMenu = false
                    onLongClickLoadSave()
                },
                onManage = {
                    showMenu = false
                    onLongClickManage()
                },
                onCreateShortcut = {
                    showMenu = false
                    onLongClickCreateShortcut()
                },
                onCustomCover = {
                    showMenu = false
                    onLongClickCustomCover()
                }
            )
        }
    }
}

@Composable
private fun GameListCard(
    modifier: Modifier = Modifier,
    game: GameItem,
    isCoverArtDisabled: Boolean,
    onClick: () -> Unit,
    onLongClickStart: () -> Unit,
    onLongClickContinue: () -> Unit,
    onLongClickLoadSave: () -> Unit,
    onLongClickManage: () -> Unit,
    onLongClickCreateShortcut: () -> Unit,
    onLongClickCustomCover: () -> Unit
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) 0.985f else 1f, tween(100))
    var showMenu by remember { mutableStateOf(false) }

    Box(modifier = modifier.fillMaxWidth()) {
        Surface(
        modifier = modifier
            .graphicsLayer(scaleX = scale, scaleY = scale)
            .animateContentSize()
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = debouncedClick,
                onLongClick = { showMenu = true }
            )
            .gamepadFocusableCard(shape = RoundedCornerShape(16.dp)),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 9.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.Top
        ) {
            Box(
                modifier = Modifier
                    .width(52.dp)
                    .aspectRatio(2f / 3f)
                    .clip(RoundedCornerShape(10.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            ) {
                if (isCoverArtDisabled) {
                    CoverPlaceholderArt(
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.FillHeight
                    )
                } else {
                    GameCoverArt(
                        coverPath = game.coverArtPath,
                        fallbackTitle = game.title,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.FillHeight
                    )
                }
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .heightIn(min = 78.dp),
                verticalArrangement = Arrangement.spacedBy(3.dp)
            ) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.fillMaxWidth(),
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 2
                )
                SerialLabel(game = game)
                Text(
                    text = game.fileName,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = formatCompactFileSize(game.fileSize),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.align(Alignment.End)
                )
            }
        }
    }
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 8.dp, bottom = 8.dp)
        ) {
            GameCardContextMenu(
                expanded = showMenu,
                offset = DpOffset.Zero,
                onDismiss = { showMenu = false },
                onStart = {
                    showMenu = false
                    onLongClickStart()
                },
                onContinue = {
                    showMenu = false
                    onLongClickContinue()
                },
                onLoadSave = {
                    showMenu = false
                    onLongClickLoadSave()
                },
                onManage = {
                    showMenu = false
                    onLongClickManage()
                },
                onCreateShortcut = {
                    showMenu = false
                    onLongClickCreateShortcut()
                },
                onCustomCover = {
                    showMenu = false
                    onLongClickCustomCover()
                }
            )
        }
    }
}

@Composable
private fun GridCoverPlaceholder(
    modifier: Modifier = Modifier,
    title: String? = null,
    titleMaxLines: Int = 3,
    contentScale: ContentScale = ContentScale.Crop
) {
    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        CoverPlaceholderArt(
            modifier = Modifier.fillMaxSize(),
            contentScale = contentScale
        )

        if (!title.isNullOrBlank()) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurface,
                textAlign = TextAlign.Center,
                maxLines = titleMaxLines,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 14.dp)
            )
        }
    }
}

@Composable
private fun CoverPlaceholderArt(
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop
) {
    Image(
        painter = painterResource(R.drawable.game_cover_placeholder_bg),
        contentDescription = null,
        modifier = modifier,
        contentScale = contentScale,
        alpha = 0.98f
    )
}

@Composable
private fun GameCardContextMenu(
    expanded: Boolean,
    offset: DpOffset,
    onDismiss: () -> Unit,
    onStart: () -> Unit,
    onContinue: () -> Unit,
    onLoadSave: () -> Unit,
    onManage: () -> Unit,
    onCreateShortcut: () -> Unit,
    onCustomCover: () -> Unit
) {
    DropdownMenu(
        expanded = expanded,
        offset = offset,
        onDismissRequest = onDismiss
    ) {
        DropdownMenuItem(
            text = { Text(stringResource(R.string.home_game_menu_start)) },
            onClick = onStart
        )
        DropdownMenuItem(
            text = { Text(stringResource(R.string.home_game_menu_continue)) },
            onClick = onContinue
        )
        DropdownMenuItem(
            text = { Text(stringResource(R.string.home_game_menu_load_save)) },
            onClick = onLoadSave
        )
        DropdownMenuItem(
            text = { Text(stringResource(R.string.home_game_menu_manager)) },
            onClick = onManage
        )
        DropdownMenuItem(
            text = { Text(stringResource(R.string.home_game_menu_shortcut)) },
            onClick = onCreateShortcut
        )
        DropdownMenuItem(
            text = { Text(stringResource(R.string.home_game_menu_custom_cover)) },
            onClick = onCustomCover
        )
    }
}

@Composable
private fun SerialLabel(
    game: GameItem,
    modifier: Modifier = Modifier
) {
    val serial = game.serial?.takeIf { it.isNotBlank() } ?: return
    Text(
        modifier = modifier,
        text = serial,
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.primary,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis
    )
}



private fun formatCompactFileSize(bytes: Long): String {
    return when {
        bytes >= 1_073_741_824L -> String.format(Locale.US, "%.2f GB", bytes / 1_073_741_824.0)
        bytes >= 1_048_576L -> String.format(Locale.US, "%.1f MB", bytes / 1_048_576.0)
        bytes >= 1024L -> String.format(Locale.US, "%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }
}

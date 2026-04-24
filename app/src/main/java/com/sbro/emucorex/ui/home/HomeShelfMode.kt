package com.sbro.emucorex.ui.home

import android.annotation.SuppressLint
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PageSize
import androidx.compose.foundation.pager.PagerDefaults
import androidx.compose.foundation.pager.PagerSnapDistance
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ViewCarousel
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.data.CustomGameCoverRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import kotlinx.coroutines.launch
import kotlin.math.absoluteValue

@Composable
internal fun HomeShelfMode(
    games: List<GameItem>,
    recentGames: List<GameItem>,
    isCoverArtDisabled: Boolean,
    topInset: Dp,
    bottomInset: Dp,
    horizontalInset: Dp,
    modifier: Modifier = Modifier,
    onExitShelfMode: () -> Unit,
    onEnable3dCoverArt: () -> Unit,
    onGameClick: (GameItem) -> Unit,
    onLongClickStart: (GameItem) -> Unit,
    onLongClickContinue: (GameItem) -> Unit,
    onLongClickLoadSave: (GameItem) -> Unit,
    onLongClickManage: (GameItem) -> Unit,
    onLongClickCreateShortcut: (GameItem) -> Unit,
    onLongClickCustomCover: (GameItem) -> Unit
) {
    if (games.isEmpty()) {
        NoGamesState()
        return
    }

    val startIndex = remember(games, recentGames) {
        recentGames.firstOrNull()
            ?.let { recent -> games.indexOfFirst { it.path == recent.path } }
            ?.takeIf { it >= 0 }
            ?: 0
    }
    val pagerState = rememberPagerState(
        initialPage = startIndex.coerceIn(0, games.lastIndex),
        pageCount = { games.size }
    )
    val scope = rememberCoroutineScope()
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }
    val cardFocusRequesters = remember(games) { List(games.size) { FocusRequester() } }
    val currentPage = pagerState.currentPage.coerceIn(0, games.lastIndex)
    val activeGame = games[currentPage]
    val pagerFlingBehavior = PagerDefaults.flingBehavior(
        state = pagerState,
        pagerSnapDistance = PagerSnapDistance.atMost(4)
    )

    LaunchedEffect(games.size) {
        val lastIndex = games.lastIndex
        if (pagerState.currentPage > lastIndex) {
            pagerState.scrollToPage(lastIndex)
        }
    }
    LaunchedEffect(pagerState.currentPage, shouldRequestGamepadFocus) {
        if (shouldRequestGamepadFocus) {
            cardFocusRequesters[pagerState.currentPage.coerceIn(0, cardFocusRequesters.lastIndex)].requestFocus()
        }
    }
    RequestFocusOnResume(
        focusRequester = cardFocusRequesters[pagerState.currentPage.coerceIn(0, cardFocusRequesters.lastIndex)],
        enabled = shouldRequestGamepadFocus
    )

    BoxWithConstraints(
        modifier = modifier.fillMaxSize()
    ) {
        val isLandscape = maxWidth > maxHeight
        val baseCardWidth = when {
            maxWidth >= 1100.dp -> 254.dp
            maxWidth >= 840.dp -> 228.dp
            maxWidth >= 620.dp -> 204.dp
            else -> 182.dp
        }
        val coverPromptHeightAllowance = if (isCoverArtDisabled) 58.dp else 0.dp
        val reservedHeight = (if (isLandscape) 124.dp else 164.dp) + coverPromptHeightAllowance
        val maxCardWidthFromHeight =
            ((maxHeight - topInset - bottomInset - reservedHeight).coerceAtLeast(210.dp)) * (2f / 3f)
        val cardWidth = (if (isLandscape) baseCardWidth * 0.84f else baseCardWidth)
            .coerceAtMost(maxCardWidthFromHeight)
        val horizontalPadding = ((maxWidth - cardWidth) / 2).coerceAtLeast(0.dp)

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        ) {
            val activeShelfCoverPath = rememberShelfCoverPath(activeGame, isCoverArtDisabled)
            ShelfBackdrop(
                coverPath = activeShelfCoverPath,
                modifier = Modifier.fillMaxSize()
            )

            Column(
                modifier = Modifier.fillMaxSize()
            ) {
                ShelfTopBar(
                    title = activeGame.title,
                    topInset = topInset,
                    horizontalInset = horizontalInset,
                    onExitShelfMode = onExitShelfMode
                )
                if (isCoverArtDisabled) {
                    ShelfCoverArtPrompt(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = horizontalInset)
                            .padding(top = 2.dp, bottom = 4.dp),
                        onEnable3dCoverArt = onEnable3dCoverArt
                    )
                }
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth()
                        .padding(top = 8.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(bottom = bottomInset + 46.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        HorizontalPager(
                            state = pagerState,
                            pageSize = PageSize.Fixed(cardWidth),
                            contentPadding = PaddingValues(horizontal = horizontalPadding),
                            pageSpacing = 8.dp,
                            beyondViewportPageCount = 3,
                            flingBehavior = pagerFlingBehavior,
                            modifier = Modifier.fillMaxSize()
                        ) { page ->
                            val pageOffset = pagerState.getOffsetDistanceInPages(page)
                            val distanceFromCenter = pageOffset.absoluteValue.coerceIn(0f, 1.25f)
                            val focusProgress = 1f - distanceFromCenter.coerceIn(0f, 1f)
                            val scale = if (isLandscape) {
                                0.82f + (0.22f * focusProgress)
                            } else {
                                0.88f + (0.16f * focusProgress)
                            }
                            val cardAlpha = 0.42f + (0.58f * focusProgress)
                            val cardShiftY = if (isLandscape) {
                                4.dp * distanceFromCenter.coerceIn(0f, 1f)
                            } else {
                                8.dp * distanceFromCenter.coerceIn(0f, 1f)
                            }
                            val tilt = -pageOffset.coerceIn(-1f, 1f) * 15f
                            val game = games[page]

                            ShelfCoverCard(
                                game = game,
                                isActive = page == pagerState.currentPage,
                                isCoverArtDisabled = isCoverArtDisabled,
                                focusRequester = cardFocusRequesters[page],
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .graphicsLayer {
                                        scaleX = scale
                                        scaleY = scale
                                        alpha = cardAlpha
                                        translationY = cardShiftY.value * density
                                        rotationY = tilt
                                        cameraDistance = 18f * density
                                    },
                                onClick = {
                                    if (page == pagerState.currentPage) {
                                        onGameClick(game)
                                    } else {
                                        scope.launch { pagerState.animateScrollToPage(page) }
                                    }
                                },
                                onLongClickStart = {
                                    if (page != pagerState.currentPage) {
                                        scope.launch { pagerState.animateScrollToPage(page) }
                                    }
                                    onLongClickStart(game)
                                },
                                onLongClickContinue = { onLongClickContinue(game) },
                                onLongClickLoadSave = { onLongClickLoadSave(game) },
                                onLongClickManage = { onLongClickManage(game) },
                                onLongClickCreateShortcut = { onLongClickCreateShortcut(game) },
                                onLongClickCustomCover = { onLongClickCustomCover(game) },
                                onNavigateLeft = {
                                    if (page > 0) {
                                        scope.launch { pagerState.animateScrollToPage(page - 1) }
                                    }
                                },
                                onNavigateRight = {
                                    if (page < games.lastIndex) {
                                        scope.launch { pagerState.animateScrollToPage(page + 1) }
                                    }
                                }
                            )
                        }
                    }

                    AnimatedContent(
                        targetState = activeGame.path,
                        transitionSpec = {
                            fadeIn(animationSpec = tween(220)) togetherWith
                                fadeOut(animationSpec = tween(140))
                        },
                        label = "shelf-details",
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .fillMaxWidth()
                            .padding(horizontal = horizontalInset)
                            .padding(bottom = bottomInset + 8.dp)
                    ) { activePath ->
                        val selectedGame = games.first { it.path == activePath }
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(
                                    Brush.verticalGradient(
                                        listOf(
                                            Color.Black.copy(alpha = 0f),
                                            Color.Black.copy(alpha = 0.12f),
                                            Color.Black.copy(alpha = 0.34f)
                                        )
                                    )
                                )
                                .padding(horizontal = 24.dp, vertical = 12.dp)
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.Bottom
                            ) {
                                Text(
                                    modifier = Modifier
                                        .weight(1f)
                                        .padding(end = 12.dp),
                                    text = selectedGame.fileName,
                                    style = MaterialTheme.typography.bodyMedium.copy(
                                        fontWeight = FontWeight.Medium,
                                        shadow = Shadow(
                                            color = Color.Black.copy(alpha = 0.72f),
                                            blurRadius = 8f
                                        )
                                    ),
                                    color = Color.White.copy(alpha = 0.94f),
                                    textAlign = TextAlign.Start,
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )

                                Text(
                                    text = androidx.compose.ui.res.stringResource(
                                        R.string.home_shelf_games_counter,
                                        currentPage + 1,
                                        games.size
                                    ),
                                    modifier = Modifier.padding(start = 12.dp),
                                    style = MaterialTheme.typography.labelMedium.copy(
                                        fontWeight = FontWeight.SemiBold,
                                        shadow = Shadow(
                                            color = Color.Black.copy(alpha = 0.72f),
                                            blurRadius = 8f
                                        )
                                    ),
                                    color = Color.White.copy(alpha = 0.88f),
                                    maxLines = 1
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ShelfTopBar(
    title: String,
    topInset: Dp,
    horizontalInset: Dp,
    onExitShelfMode: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = horizontalInset,
                end = horizontalInset,
                top = topInset + 30.dp,
                bottom = 6.dp
            ),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = title,
            modifier = Modifier
                .weight(1f)
                .padding(end = 12.dp),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = Color.White.copy(alpha = 0.94f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )

        IconButton(onClick = onExitShelfMode) {
            Icon(
                imageVector = Icons.Rounded.ViewCarousel,
                contentDescription = androidx.compose.ui.res.stringResource(R.string.home_exit_shelf),
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun ShelfBackdrop(
    coverPath: String?,
    modifier: Modifier = Modifier
) {
    val baseGradient = listOf(
        Color(0xFF11141A),
        Color(0xFF18202A),
        Color(0xFF101318)
    )
    val stageGlow = listOf(
        Color(0xFF344153).copy(alpha = 0.42f),
        Color(0xFF263240).copy(alpha = 0.22f),
        Color.Transparent
    )
    val coverAlpha = 0.16f
    val verticalVeil = listOf(
        Color(0xFF0F1217).copy(alpha = 0.08f),
        Color.Transparent,
        Color(0xFF0F1217).copy(alpha = 0.24f)
    )
    val sideVeil = listOf(
        Color.Black.copy(alpha = 0.18f),
        Color.Transparent,
        Color.Black.copy(alpha = 0.18f)
    )

    Box(
        modifier = modifier.background(Brush.verticalGradient(baseGradient))
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Brush.radialGradient(stageGlow))
        )

        if (!coverPath.isNullOrBlank()) {
            GameCoverArt(
                coverPath = coverPath,
                fallbackTitle = "",
                modifier = Modifier
                    .fillMaxSize()
                    .graphicsLayer { alpha = coverAlpha },
                contentScale = ContentScale.Crop
            )
        }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.verticalGradient(verticalVeil)
                )
        )
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.horizontalGradient(sideVeil)
                )
        )
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun ShelfCoverCard(
    game: GameItem,
    isActive: Boolean,
    isCoverArtDisabled: Boolean,
    focusRequester: FocusRequester,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
    onLongClickStart: () -> Unit,
    onLongClickContinue: () -> Unit,
    onLongClickLoadSave: () -> Unit,
    onLongClickManage: () -> Unit,
    onLongClickCreateShortcut: () -> Unit,
    onLongClickCustomCover: () -> Unit,
    onNavigateLeft: () -> Unit,
    onNavigateRight: () -> Unit
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    var showMenu by remember(game.path) { mutableStateOf(false) }
    val shape = RoundedCornerShape(24.dp)
    val coverAspectRatio = 2f / 3f
    val horizontalCoverPadding = if (isActive) 6.dp else 8.dp
    val verticalCoverPadding = if (isActive) 6.dp else 4.dp
    val shelfCoverPath = rememberShelfCoverPath(game, isCoverArtDisabled)
    fun dismissMenu(action: () -> Unit) {
        showMenu = false
        action()
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .aspectRatio(coverAspectRatio)
            .focusRequester(focusRequester)
            .focusable()
            .onPreviewKeyEvent { keyEvent ->
                if (keyEvent.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                when (keyEvent.key) {
                    Key.DirectionLeft -> {
                        onNavigateLeft()
                        true
                    }
                    Key.DirectionRight -> {
                        onNavigateRight()
                        true
                    }
                    Key.DirectionCenter,
                    Key.Enter,
                    Key.NumPadEnter -> {
                        debouncedClick()
                        true
                    }
                    else -> false
                }
            }
            .combinedClickable(
                onClick = debouncedClick,
                onLongClick = { showMenu = true }
            ),
        shape = shape,
        color = Color.Transparent,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        if (!shelfCoverPath.isNullOrBlank()) {
            GameCoverArt(
                coverPath = shelfCoverPath,
                fallbackTitle = game.title,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = horizontalCoverPadding, vertical = verticalCoverPadding),
                contentScale = ContentScale.Fit
            )
        } else {
            ShelfMissingCoverCard(
                title = game.title,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = horizontalCoverPadding, vertical = verticalCoverPadding)
            )
        }
    }

    GameCardContextMenu(
        expanded = showMenu,
        offset = DpOffset.Zero,
        onDismiss = { showMenu = false },
        onStart = { dismissMenu(onLongClickStart) },
        onContinue = { dismissMenu(onLongClickContinue) },
        onLoadSave = { dismissMenu(onLongClickLoadSave) },
        onManage = { dismissMenu(onLongClickManage) },
        onCreateShortcut = { dismissMenu(onLongClickCreateShortcut) },
        onCustomCover = { dismissMenu(onLongClickCustomCover) }
    )
}

@Composable
private fun rememberShelfCoverPath(game: GameItem, isCoverArtDisabled: Boolean): String? {
    val context = LocalContext.current
    val customCoverRepository = remember(context) { CustomGameCoverRepository(context) }
    val currentCoverPath = game.coverArtPath
    val isCustomCover = remember(currentCoverPath, customCoverRepository) {
        customCoverRepository.isCustomCoverPath(currentCoverPath)
    }
    return when {
        isCustomCover -> currentCoverPath
        isCoverArtDisabled -> null
        else -> currentCoverPath
    }
}

@Composable
private fun ShelfCoverArtPrompt(
    modifier: Modifier = Modifier,
    onEnable3dCoverArt: () -> Unit
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = Color(0xFF171B24).copy(alpha = 0.88f),
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = androidx.compose.ui.res.stringResource(R.string.home_shelf_enable_3d_title),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = Color.White.copy(alpha = 0.94f),
                maxLines = 2,
                overflow = TextOverflow.Clip
            )
            FilledTonalButton(
                onClick = onEnable3dCoverArt,
                colors = ButtonDefaults.filledTonalButtonColors(
                    containerColor = Color(0xFF252C38),
                    contentColor = Color.White.copy(alpha = 0.92f)
                )
            ) {
                Icon(
                    imageVector = Icons.Rounded.ViewCarousel,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = androidx.compose.ui.res.stringResource(R.string.home_shelf_enable_3d_action),
                    maxLines = 1,
                    overflow = TextOverflow.Clip
                )
            }
        }
    }
}

@Composable
private fun ShelfMissingCoverCard(
    title: String,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .background(
                Brush.verticalGradient(
                    listOf(
                        Color(0xFF2C333D).copy(alpha = 0.92f),
                        Color(0xFF1C222B).copy(alpha = 0.96f)
                    )
                )
            )
            .padding(18.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
            color = Color.White.copy(alpha = 0.86f),
            textAlign = TextAlign.Center,
            maxLines = 5,
            overflow = TextOverflow.Ellipsis
        )
    }
}

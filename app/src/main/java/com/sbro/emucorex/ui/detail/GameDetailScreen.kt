package com.sbro.emucorex.ui.detail

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.content.ContextWrapper
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.provider.MediaStore
import android.view.ViewGroup
import android.widget.Toast
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CalendarToday
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.VideoLibrary
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.net.toUri
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.YouTubePlayer
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.listeners.AbstractYouTubePlayerListener
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.options.IFramePlayerOptions
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.views.YouTubePlayerView
import com.sbro.emucorex.R
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityEntry
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityStatus
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.RequestFocusOnResume
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@SuppressLint("SetJavaScriptEnabled", "ConfigurationScreenWidthHeight")
@Composable
fun GameDetailScreen(
    catalogGameId: Long,
    onBackClick: () -> Unit,
    viewModel: GameDetailViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val debouncedBack = rememberDebouncedClick(onClick = onBackClick)
    var selectedScreenshotIndex by remember { mutableIntStateOf(-1) }
    var selectedVideoIndex by remember { mutableIntStateOf(-1) }
    val horizontalInset = ScreenHorizontalPadding
    val contentMaxWidth = if (isLandscape) 760.dp else Dp.Unspecified
    val heroMaxWidth = if (isLandscape) 240.dp else Dp.Unspecified
    val backFocusRequester = remember { FocusRequester() }
    val shouldRequestGamepadFocus = remember { GamepadManager.isGamepadConnected() }

    LaunchedEffect(catalogGameId) {
        viewModel.loadGame(catalogGameId)
    }

    val catalog = uiState.catalogDetails
    val heroImage = catalog?.coverUrl ?: catalog?.heroUrl

    LaunchedEffect(uiState.isLoading, shouldRequestGamepadFocus, catalog?.name) {
        if (!uiState.isLoading && shouldRequestGamepadFocus) {
            backFocusRequester.requestFocus()
        }
    }
    RequestFocusOnResume(
        focusRequester = backFocusRequester,
        enabled = shouldRequestGamepadFocus && !uiState.isLoading
    )

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
            .verticalScroll(rememberScrollState())
    ) {
        Spacer(modifier = Modifier.height(topInset))
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .widthIn(max = contentMaxWidth)
                .align(Alignment.CenterHorizontally)
                .padding(horizontal = horizontalInset, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            NavigationBackButton(
                onClick = debouncedBack,
                modifier = Modifier.focusRequester(backFocusRequester),
                contentColor = MaterialTheme.colorScheme.onSurface
            )
        }

        if (uiState.isLoading) {
            DetailSkeleton(
                isInstalledGame = false,
                horizontalInset = horizontalInset
            )
        } else if (catalog == null) {
            EmptyDetailState(
                isCatalogAvailable = uiState.isCatalogAvailable,
                modifier = Modifier.padding(20.dp)
            )
        } else {
            val title = catalog.name

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = horizontalInset),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth(if (isLandscape) 0.4f else 1f)
                        .widthIn(max = heroMaxWidth)
                        .aspectRatio(2f / 3f)
                        .clip(RoundedCornerShape(28.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
                ) {
                    GameCoverArt(
                        coverPath = heroImage,
                        fallbackTitle = title,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.FillHeight
                    )
                }
            }

            Column(
                modifier = Modifier.padding(vertical = 16.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onBackground,
                    modifier = Modifier
                        .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                        .widthIn(max = contentMaxWidth)
                        .padding(horizontal = horizontalInset)
                )

                Box(
                    modifier = Modifier
                        .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                        .widthIn(max = contentMaxWidth)
                        .padding(horizontal = horizontalInset)
                ) {
                    MetaRow(
                        year = catalog.year,
                        rating = catalog.rating,
                        serial = catalog.primarySerial
                    )
                }

                catalog.pcsx2Compatibility?.let { compatibility ->
                    Box(
                        modifier = Modifier
                            .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                            .widthIn(max = contentMaxWidth)
                            .padding(horizontal = horizontalInset)
                    ) {
                        CompatibilitySection(compatibility = compatibility)
                    }
                }

                if (catalog.genres.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                            .widthIn(max = contentMaxWidth)
                            .padding(horizontal = horizontalInset)
                    ) {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            catalog.genres.forEach { genre ->
                                GenreChip(text = genre)
                            }
                        }
                    }
                }

                val description = catalog.storyline?.takeIf { it.isNotBlank() }
                    ?: catalog.summary?.takeIf { it.isNotBlank() }
                if (!description.isNullOrBlank()) {
                    Box(
                        modifier = Modifier
                            .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                            .widthIn(max = contentMaxWidth)
                            .padding(horizontal = horizontalInset)
                    ) {
                        ExpandableInfoSection(
                            label = stringResource(R.string.detail_overview),
                            value = description
                        )
                    }
                }

                if (catalog.screenshots.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                            .widthIn(max = contentMaxWidth)
                            .padding(horizontal = horizontalInset)
                    ) {
                        MediaSectionTitle(
                            icon = Icons.Rounded.VideoLibrary,
                            title = stringResource(R.string.detail_screenshots)
                        )
                    }
                    LazyRow(
                        contentPadding = PaddingValues(horizontal = horizontalInset),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        itemsIndexed(catalog.screenshots, key = { index, item -> "$index-$item" }) { index, screenshot ->
                            ScreenshotCard(
                                imageUrl = screenshot,
                                title = title,
                                onClick = { selectedScreenshotIndex = index }
                            )
                        }
                    }
                }

                if (catalog.videos.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .then(if (isLandscape) Modifier.align(Alignment.CenterHorizontally) else Modifier)
                            .widthIn(max = contentMaxWidth)
                            .padding(horizontal = horizontalInset)
                    ) {
                        MediaSectionTitle(
                            icon = Icons.Rounded.SmartDisplay,
                            title = stringResource(R.string.detail_videos)
                        )
                    }
                    LazyRow(
                        contentPadding = PaddingValues(horizontal = horizontalInset),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        itemsIndexed(catalog.videos, key = { index, item -> "$index-$item" }) { index, video ->
                            VideoCard(
                                youtubeId = video,
                                title = title,
                                onClick = { selectedVideoIndex = index }
                            )
                        }
                    }
                }
            }
        }
    }

    if (catalog != null && selectedScreenshotIndex >= 0) {
        ScreenshotViewerOverlay(
            title = catalog.name,
            screenshots = catalog.screenshots,
            startIndex = selectedScreenshotIndex,
            onDismiss = { selectedScreenshotIndex = -1 }
        )
    }

    if (catalog != null && selectedVideoIndex >= 0) {
        VideoPlayerOverlay(
            title = catalog.name,
            videoIds = catalog.videos,
            startIndex = selectedVideoIndex,
            onDismiss = { selectedVideoIndex = -1 }
        )
    }
}

@Composable
private fun DetailSkeleton(
    isInstalledGame: Boolean,
    horizontalInset: Dp
) {
    Column(
        modifier = Modifier.padding(vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .aspectRatio(2f / 3f)
                .clip(RoundedCornerShape(28.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .padding(horizontal = horizontalInset)
                .fillMaxWidth(0.72f)
                .height(34.dp)
                .clip(RoundedCornerShape(14.dp))
        )
        Row(
            modifier = Modifier.padding(horizontal = horizontalInset),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            repeat(3) {
                SkeletonBlock(
                    modifier = Modifier
                        .height(36.dp)
                        .width(if (it == 2) 132.dp else 72.dp)
                        .clip(RoundedCornerShape(999.dp))
                )
            }
        }
        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .height(58.dp)
                .clip(RoundedCornerShape(18.dp))
        )

        if (isInstalledGame) {
            repeat(2) {
                SkeletonBlock(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = horizontalInset)
                        .height(74.dp)
                        .clip(RoundedCornerShape(16.dp))
                )
            }
            SkeletonBlock(
                modifier = Modifier
                    .padding(horizontal = horizontalInset)
                    .width(128.dp)
                    .height(24.dp)
                    .clip(RoundedCornerShape(10.dp))
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = horizontalInset),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                repeat(5) {
                    SkeletonBlock(
                        modifier = Modifier
                            .weight(1f)
                            .height(72.dp)
                            .clip(RoundedCornerShape(14.dp))
                    )
                }
            }
        }

        SkeletonBlock(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalInset)
                .height(144.dp)
                .clip(RoundedCornerShape(16.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .padding(horizontal = horizontalInset)
                .width(156.dp)
                .height(24.dp)
                .clip(RoundedCornerShape(10.dp))
        )
        LazyRow(
            contentPadding = PaddingValues(horizontal = horizontalInset),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(3) {
                SkeletonBlock(
                    modifier = Modifier
                        .width(220.dp)
                        .height(130.dp)
                        .clip(RoundedCornerShape(18.dp))
                )
            }
        }
    }
}

@Composable
private fun SkeletonBlock(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            .shimmer()
    )
}

@Composable
private fun EmptyDetailState(
    isCatalogAvailable: Boolean,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = stringResource(R.string.detail_no_data_title),
                style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.onSurface,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = stringResource(
                    if (isCatalogAvailable) R.string.detail_no_data_body
                    else R.string.detail_catalog_missing_body
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun MetaRow(
    year: Int?,
    rating: Double?,
    serial: String?
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        year?.let {
            MetaChip(
                icon = Icons.Rounded.CalendarToday,
                text = it.toString()
            )
        }
        rating?.let {
            MetaChip(
                icon = Icons.Rounded.Star,
                text = String.format(Locale.US, "%.1f", it / 10.0)
            )
        }
        serial?.takeIf { it.isNotBlank() }?.let {
            MetaChip(
                icon = Icons.Rounded.Save,
                text = it
            )
        }
    }
}

@Composable
private fun MetaChip(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    text: String
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(16.dp)
            )
            Text(
                text = text,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun GenreChip(text: String) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
private fun CompatibilitySection(
    compatibility: Pcsx2CompatibilityEntry
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = stringResource(R.string.pcsx2_compatibility_title),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
            CompatibilityInfoRow(
                label = stringResource(R.string.pcsx2_compatibility_status),
                value = stringResource(detailCompatibilityStatusRes(compatibility.status)),
                accent = compatibilityStatusColor(compatibility.status)
            )
            compatibility.region?.takeIf { it.isNotBlank() }?.let { region ->
                CompatibilityInfoRow(
                    label = stringResource(R.string.pcsx2_compatibility_region),
                    value = region
                )
            }
        }
    }
}

@Composable
private fun CompatibilityInfoRow(
    label: String,
    value: String,
    accent: Color = MaterialTheme.colorScheme.primary
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(10.dp)
                .clip(CircleShape)
                .background(accent)
        )
        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = value,
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun compatibilityStatusColor(status: Pcsx2CompatibilityStatus): Color {
    return when (status) {
        Pcsx2CompatibilityStatus.PERFECT -> Color(0xFF2E7D32)
        Pcsx2CompatibilityStatus.PLAYABLE -> Color(0xFF1B5E20)
        Pcsx2CompatibilityStatus.IN_GAME -> Color(0xFFEF6C00)
        Pcsx2CompatibilityStatus.INTRO -> Color(0xFF8E24AA)
        Pcsx2CompatibilityStatus.NOTHING -> Color(0xFFC62828)
        Pcsx2CompatibilityStatus.UNKNOWN -> MaterialTheme.colorScheme.primary
    }
}

private fun detailCompatibilityStatusRes(status: Pcsx2CompatibilityStatus): Int {
    return when (status) {
        Pcsx2CompatibilityStatus.UNKNOWN -> R.string.pcsx2_compatibility_unknown
        Pcsx2CompatibilityStatus.NOTHING -> R.string.pcsx2_compatibility_nothing
        Pcsx2CompatibilityStatus.INTRO -> R.string.pcsx2_compatibility_intro
        Pcsx2CompatibilityStatus.IN_GAME -> R.string.pcsx2_compatibility_in_game
        Pcsx2CompatibilityStatus.PLAYABLE -> R.string.pcsx2_compatibility_playable
        Pcsx2CompatibilityStatus.PERFECT -> R.string.pcsx2_compatibility_perfect
    }
}

@Composable
private fun ExpandableInfoSection(
    label: String,
    value: String,
    collapsedMaxChars: Int = 200
) {
    var expanded by remember(value) { mutableStateOf(false) }
    val trimmedValue = value.trim()
    val canExpand = trimmedValue.length > collapsedMaxChars
    val displayValue = when {
        expanded || !canExpand -> trimmedValue
        else -> trimmedValue.take(collapsedMaxChars).trimEnd() + "..."
    }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(
                animationSpec = spring(
                    dampingRatio = Spring.DampingRatioNoBouncy,
                    stiffness = Spring.StiffnessLow
                )
            ),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = displayValue,
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
            if (canExpand) {
                TextButton(
                    onClick = { expanded = !expanded },
                    contentPadding = PaddingValues(0.dp)
                ) {
                    Text(
                        text = stringResource(
                            if (expanded) R.string.detail_show_less
                            else R.string.detail_show_more
                        )
                    )
                }
            }
        }
    }
}

@Composable
private fun MediaSectionTitle(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(18.dp)
        )
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onSurface
        )
    }
}

@Composable
private fun ScreenshotCard(
    imageUrl: String,
    title: String,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .width(220.dp)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(130.dp)
                .clip(RoundedCornerShape(18.dp))
        ) {
            GameCoverArt(
                coverPath = imageUrl,
                fallbackTitle = title,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
        }
    }
}

@Composable
private fun VideoCard(
    youtubeId: String,
    title: String,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .width(232.dp)
            .gamepadFocusableCard(shape = RoundedCornerShape(18.dp)),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        onClick = onClick
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(16f / 9f)
                .clip(RoundedCornerShape(18.dp))
                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
        ) {
            GameCoverArt(
                coverPath = youtubeThumbnailUrl(youtubeId),
                fallbackTitle = title,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .background(
                        Brush.verticalGradient(
                            colors = listOf(
                                Color.Transparent,
                                Color.Transparent,
                                Color.Black.copy(alpha = 0.22f)
                            )
                        )
                    )
            )
            Box(
                modifier = Modifier
                    .align(Alignment.Center)
                    .clip(CircleShape)
                    .background(Color.Black.copy(alpha = 0.55f))
                    .padding(14.dp)
            ) {
                Icon(
                    imageVector = Icons.Rounded.PlayArrow,
                    contentDescription = null,
                    tint = Color.White,
                    modifier = Modifier.size(24.dp)
                )
            }
            Surface(
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(10.dp),
                shape = RoundedCornerShape(999.dp),
                color = Color.Black.copy(alpha = 0.48f)
            ) {
                Text(
                    text = stringResource(R.string.detail_videos),
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                    style = MaterialTheme.typography.labelMedium,
                    color = Color.White
                )
            }
        }
    }
}

@Composable
private fun ScreenshotViewerOverlay(
    title: String,
    screenshots: List<String>,
    startIndex: Int,
    onDismiss: () -> Unit
) {
    if (screenshots.isEmpty()) return
    val start = startIndex.coerceIn(0, screenshots.lastIndex)
    val pagerState = rememberPagerState(initialPage = start, pageCount = { screenshots.size })
    val context = LocalContext.current
    val activity = remember(context) { context.findActivity() }
    val coroutineScope = rememberCoroutineScope()
    val screenshotSavedMessage = stringResource(R.string.detail_screenshot_saved)
    val screenshotSaveFailedMessage = stringResource(R.string.detail_screenshot_save_failed)

    androidx.compose.runtime.DisposableEffect(activity) {
        val window = activity?.window
        val controller = window?.let { WindowInsetsControllerCompat(it, it.decorView) }
        controller?.hide(WindowInsetsCompat.Type.systemBars())
        controller?.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        onDispose {
            controller?.show(WindowInsetsCompat.Type.systemBars())
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        val dialogView = LocalView.current
        val dialogWindow = (dialogView.parent as? DialogWindowProvider)?.window

        androidx.compose.runtime.DisposableEffect(dialogWindow) {
            if (dialogWindow != null) {
                WindowCompat.setDecorFitsSystemWindows(dialogWindow, false)
                dialogWindow.setLayout(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
            }
            onDispose { }
        }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black)
                .pointerInput(Unit) {
                    detectTapGestures(onTap = { })
                }
        ) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(vertical = 64.dp),
                    contentAlignment = Alignment.Center
                ) {
                    GameCoverArt(
                        coverPath = screenshots[page],
                        fallbackTitle = title,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.FillWidth
                    )
                }
            }

            ViewerTopBar(
                title = title,
                counter = "${pagerState.currentPage + 1}/${screenshots.size}",
                onSave = {
                    coroutineScope.launch {
                        val saved = saveScreenshotToGallery(
                            context = context,
                            imageUrl = screenshots[pagerState.currentPage],
                            title = title
                        )
                        Toast.makeText(
                            context,
                            if (saved) screenshotSavedMessage else screenshotSaveFailedMessage,
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                },
                onDismiss = onDismiss,
                compact = true,
                respectStatusBar = false,
                fillWidthBackground = true,
                closeAtEnd = true,
                extraTopPadding = 24.dp
            )
        }
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun VideoPlayerOverlay(
    title: String,
    videoIds: List<String>,
    startIndex: Int,
    onDismiss: () -> Unit
) {
    if (videoIds.isEmpty()) return
    val normalizedIds = remember(videoIds) { videoIds.map(::normalizeYoutubeId) }
    val start = startIndex.coerceIn(0, normalizedIds.lastIndex)
    val pagerState = rememberPagerState(initialPage = start, pageCount = { normalizedIds.size })
    val lifecycleOwner = LocalLifecycleOwner.current
    val configuration = LocalConfiguration.current
    val context = LocalContext.current
    val activity = remember(context) { context.findActivity() }
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val layoutDirection = LocalLayoutDirection.current
    val statusInsets = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues()
    val landscapeStartInset = if (isLandscape) {
        statusInsets.calculateLeftPadding(layoutDirection) + 16.dp
    } else {
        0.dp
    }
    val landscapeEndInset = if (isLandscape) {
        statusInsets.calculateRightPadding(layoutDirection) + 16.dp
    } else {
        0.dp
    }

    androidx.compose.runtime.DisposableEffect(activity) {
        val window = activity?.window
        val controller = window?.let { WindowInsetsControllerCompat(it, it.decorView) }
        controller?.hide(WindowInsetsCompat.Type.systemBars())
        controller?.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        onDispose {
            controller?.show(WindowInsetsCompat.Type.systemBars())
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        val dialogView = LocalView.current
        val dialogWindow = (dialogView.parent as? DialogWindowProvider)?.window

        androidx.compose.runtime.DisposableEffect(dialogWindow) {
            if (dialogWindow != null) {
                WindowCompat.setDecorFitsSystemWindows(dialogWindow, false)
                dialogWindow.setLayout(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
            }
            onDispose { }
        }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black)
                .windowInsetsPadding(WindowInsets.displayCutout)
        ) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                Fit16x9(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(start = landscapeStartInset, end = landscapeEndInset)
                ) { width, height ->
                    Box(
                        modifier = Modifier
                            .size(width = width, height = height)
                            .background(Color.Black)
                    ) {
                        YouTubePlayerBlock(
                            youtubeId = normalizedIds[page],
                            lifecycleOwner = lifecycleOwner,
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
            }

            if (isLandscape) {
                LandscapeVideoTopBar(
                    counter = "${pagerState.currentPage + 1}/${normalizedIds.size}",
                    onDismiss = onDismiss
                )
            } else {
                ViewerTopBar(
                    title = title,
                    counter = "${pagerState.currentPage + 1}/${normalizedIds.size}",
                    onDismiss = onDismiss,
                    compact = false,
                    respectStatusBar = true,
                    fillWidthBackground = true
                )
            }
        }
    }
}

@Composable
private fun Fit16x9(
    modifier: Modifier = Modifier,
    content: @Composable (widthDp: Dp, heightDp: Dp) -> Unit
) {
    BoxWithConstraints(
        modifier = modifier,
        contentAlignment = Alignment.Center
    ) {
        val density = LocalDensity.current
        val maxWidthPx = with(density) { maxWidth.toPx() }
        val maxHeightPx = with(density) { maxHeight.toPx() }
        val videoRatio = 16f / 9f

        val heightFromWidth = maxWidthPx / videoRatio
        val widthFromHeight = maxHeightPx * videoRatio

        val (finalWidth, finalHeight) = if (heightFromWidth <= maxHeightPx) {
            maxWidthPx to heightFromWidth
        } else {
            widthFromHeight to maxHeightPx
        }

        content(
            with(density) { finalWidth.toDp() },
            with(density) { finalHeight.toDp() }
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LandscapeVideoTopBar(
    counter: String,
    onDismiss: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(WindowInsets.statusBarsIgnoringVisibility.asPaddingValues())
            .padding(horizontal = 16.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Surface(
            shape = RoundedCornerShape(999.dp),
            color = Color.Black.copy(alpha = 0.42f)
        ) {
            Text(
                text = counter,
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                color = Color.White.copy(alpha = 0.88f),
                style = MaterialTheme.typography.labelLarge
            )
        }

        Surface(
            shape = CircleShape,
            color = Color.Black.copy(alpha = 0.42f),
            onClick = onDismiss
        ) {
            Box(
                modifier = Modifier.size(42.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.Close,
                    contentDescription = stringResource(R.string.back),
                    tint = Color.White
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ViewerTopBar(
    title: String,
    counter: String,
    onSave: (() -> Unit)? = null,
    onDismiss: () -> Unit,
    compact: Boolean = false,
    respectStatusBar: Boolean = true,
    fillWidthBackground: Boolean = true,
    closeAtEnd: Boolean = false,
    extraTopPadding: Dp = 0.dp
) {
    val layoutDirection = LocalLayoutDirection.current
    val statusInsets = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues()
    val topSafePadding = if (respectStatusBar) {
        statusInsets.calculateTopPadding()
    } else {
        0.dp
    }
    val startSafePadding = statusInsets.calculateLeftPadding(layoutDirection)
    val endSafePadding = statusInsets.calculateRightPadding(layoutDirection)
    val horizontalPadding = if (compact) 12.dp else 8.dp
    val topPadding = topSafePadding + extraTopPadding + if (compact) 8.dp else 12.dp
    val bottomPadding = if (compact) 10.dp else 16.dp
    val rowModifier = if (fillWidthBackground) {
        Modifier
            .fillMaxWidth()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        Color.Black.copy(alpha = 0.48f),
                        Color.Black.copy(alpha = 0.18f),
                        Color.Transparent
                    )
                )
            )
    } else {
        Modifier
            .padding(top = 4.dp)
            .clip(RoundedCornerShape(22.dp))
            .background(Color.Black.copy(alpha = 0.44f))
    }

    Row(
        modifier = rowModifier
            .padding(
                start = startSafePadding + horizontalPadding,
                end = endSafePadding + horizontalPadding,
                top = topPadding,
                bottom = bottomPadding
            ),
        verticalAlignment = Alignment.CenterVertically
    ) {
        val closeButton: @Composable () -> Unit = {
            IconButton(onClick = onDismiss) {
                Icon(
                    imageVector = Icons.Rounded.Close,
                    contentDescription = stringResource(R.string.back),
                    tint = Color.White
                )
            }
        }
        val saveButton: @Composable (() -> Unit)? = onSave?.let { saveAction ->
            {
                IconButton(onClick = saveAction) {
                    Icon(
                        imageVector = Icons.Rounded.Save,
                        contentDescription = stringResource(R.string.detail_save_screenshot),
                        tint = Color.White
                    )
                }
            }
        }

        val textBlock: @Composable (Modifier) -> Unit = { modifier ->
            Column(modifier = modifier) {
                if (!compact || fillWidthBackground) {
                    Text(
                        text = title,
                        color = Color.White,
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 1
                    )
                }
                Text(
                    text = counter,
                    color = Color.White.copy(alpha = 0.74f),
                    style = if (compact) {
                        MaterialTheme.typography.labelMedium
                    } else {
                        MaterialTheme.typography.bodySmall
                    }
                )
            }
        }

        if (closeAtEnd) {
            textBlock(
                Modifier
                    .weight(1f)
                    .padding(start = 12.dp, end = 8.dp)
            )
            saveButton?.invoke()
            closeButton()
        } else {
            closeButton()
            saveButton?.invoke()
            textBlock(Modifier.padding(start = 4.dp, end = if (compact) 2.dp else 8.dp))
        }
    }
}

@SuppressLint("ObsoleteSdkInt")
private suspend fun saveScreenshotToGallery(
    context: Context,
    imageUrl: String,
    title: String
): Boolean = withContext(Dispatchers.IO) {
    val bitmap = loadBitmapForSaving(context, imageUrl) ?: return@withContext false
    val resolver = context.contentResolver
    val fileName = buildScreenshotFileName(title)
    val contentValues = ContentValues().apply {
        put(MediaStore.Images.Media.DISPLAY_NAME, fileName)
        put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/EmuCoreX")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
    }

    val imageUri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues)
        ?: return@withContext false

    val saved = runCatching {
        resolver.openOutputStream(imageUri, "w")?.use { output ->
            bitmap.compress(Bitmap.CompressFormat.JPEG, 95, output)
        } == true
    }.getOrElse { false }

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        val finalizedValues = ContentValues().apply {
            put(MediaStore.Images.Media.IS_PENDING, 0)
        }
        resolver.update(imageUri, finalizedValues, null, null)
    }

    if (!saved) {
        resolver.delete(imageUri, null, null)
    }

    saved
}

private fun buildScreenshotFileName(title: String): String {
    val safeTitle = title
        .lowercase(Locale.ROOT)
        .replace(Regex("[^a-z0-9]+"), "_")
        .trim('_')
        .ifBlank { "screenshot" }
    return "emucorex_${safeTitle}_${System.currentTimeMillis()}.jpg"
}

private fun loadBitmapForSaving(context: Context, imageUrl: String): Bitmap? = runCatching {
    fun openStream() = when {
        imageUrl.startsWith("content://") -> {
            context.contentResolver.openInputStream(imageUrl.toUri())
        }
        imageUrl.startsWith("http://") || imageUrl.startsWith("https://") -> {
            val connection = URL(imageUrl).openConnection() as HttpURLConnection
            connection.connectTimeout = 8_000
            connection.readTimeout = 12_000
            connection.instanceFollowRedirects = true
            connection.inputStream
        }
        else -> {
            val file = File(imageUrl)
            if (file.exists()) file.inputStream() else null
        }
    }

    openStream()?.use { stream ->
        BitmapFactory.decodeStream(stream)
    }
}.getOrNull()

@Composable
private fun YouTubePlayerBlock(
    youtubeId: String,
    lifecycleOwner: androidx.lifecycle.LifecycleOwner,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val options = remember {
        IFramePlayerOptions.Builder(context)
            .controls(1)
            .build()
    }
    val view = remember {
        YouTubePlayerView(context).apply {
            enableAutomaticInitialization = false
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            clipChildren = false
            clipToPadding = false
        }
    }
    val playerState = remember { mutableStateOf<YouTubePlayer?>(null) }
    var initialized by remember { mutableStateOf(false) }
    var loadedVideoId by remember { mutableStateOf<String?>(null) }

    androidx.compose.runtime.DisposableEffect(lifecycleOwner, view) {
        lifecycleOwner.lifecycle.addObserver(view)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(view)
            view.release()
        }
    }

    androidx.compose.runtime.DisposableEffect(view) {
        if (!initialized) {
            val listener = object : AbstractYouTubePlayerListener() {
                override fun onReady(youTubePlayer: YouTubePlayer) {
                    playerState.value = youTubePlayer
                }
            }
            view.initialize(listener, options)
            initialized = true
        }
        onDispose { }
    }

    LaunchedEffect(youtubeId, playerState.value) {
        val player = playerState.value ?: return@LaunchedEffect
        if (loadedVideoId != youtubeId) {
            player.loadVideo(youtubeId, 0f)
            loadedVideoId = youtubeId
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { view }
    )
}

private fun youtubeThumbnailUrl(rawId: String): String {
    return "https://img.youtube.com/vi/${normalizeYoutubeId(rawId)}/hqdefault.jpg"
}

private fun normalizeYoutubeId(raw: String): String {
    val value = raw.trim()
    return when {
        value.contains("v=") -> value.substringAfter("v=").substringBefore("&").substringBefore("?")
        value.contains("youtu.be/") -> value.substringAfter("youtu.be/").substringBefore("?").substringBefore("&")
        else -> value.substringBefore("?").substringBefore("&")
    }
}

private fun Context.findActivity(): Activity? {
    var current = this
    while (current is ContextWrapper) {
        if (current is Activity) return current
        current = current.baseContext
    }
    return null
}

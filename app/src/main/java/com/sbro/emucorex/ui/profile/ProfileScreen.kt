package com.sbro.emucorex.ui.profile

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.automirrored.rounded.Login
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AccountCircle
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Email
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.Leaderboard
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.WorkspacePremium
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.data.PlayerActivityDay
import com.sbro.emucorex.data.PlayerGamePlayStat
import com.sbro.emucorex.data.PlayerLeaderboardEntry
import com.sbro.emucorex.data.PlayerProfile
import com.sbro.emucorex.data.PlayerRankInsights
import com.sbro.emucorex.ui.common.BitmapPathImage
import com.sbro.emucorex.ui.common.GameCoverArt
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.common.tvGamepadFocusableCard
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

private enum class ProfileTab {
    Overview,
    Games,
    Leaderboard,
    Stats
}

private enum class StatsActivityState {
    Loading,
    Empty,
    Ready
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ProfileScreen(
    onBackClick: () -> Unit,
    onOpenGameDetails: (Long) -> Unit,
    viewModel: ProfileViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val selectedTab = rememberSaveable { mutableIntStateOf(0) }
    var showProCustomization by rememberSaveable { mutableStateOf(false) }
    val isViewingLeaderboardProfile = uiState.viewedProfile != null || uiState.isViewedProfileLoading

    if (showProCustomization && uiState.profile != null) {
        ProProfileCustomizationDialog(
            profile = uiState.profile!!,
            games = uiState.games,
            onDismiss = { showProCustomization = false },
            onSave = { accent, favoriteGameKeys ->
                showProCustomization = false
                viewModel.updateProProfile(accent, favoriteGameKeys)
            }
        )
    }

    BackHandler(enabled = isViewingLeaderboardProfile) {
        viewModel.closeViewedProfile()
    }

    LaunchedEffect(uiState.messageKey, uiState.errorMessage) {
        uiState.messageKey?.let { key ->
            Toast.makeText(context, profileMessageRes(key), Toast.LENGTH_SHORT).show()
            viewModel.clearTransientMessages()
        }
        uiState.errorMessage?.let { message ->
            Toast.makeText(context, message, Toast.LENGTH_LONG).show()
            viewModel.clearTransientMessages()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        if (uiState.account == null) {
            AuthContent(
                isLoading = uiState.isAuthLoading,
                topInset = topInset,
                onBackClick = onBackClick,
                onSignIn = viewModel::signIn,
                onCreateAccount = viewModel::createAccount,
                onResetPassword = viewModel::sendPasswordReset,
                onGoogleSignIn = {
                    context.findActivity()?.let(viewModel::signInWithGoogle)
                        ?: Toast.makeText(context, R.string.profile_google_failed, Toast.LENGTH_SHORT).show()
                },
                modifier = Modifier.weight(1f)
            )
        } else {
            val tabs = ProfileTab.entries
            LaunchedEffect(uiState.account?.uid, selectedTab.intValue) {
                viewModel.onProfileTabSelected(tabs[selectedTab.intValue].name)
            }
            if (isViewingLeaderboardProfile) {
                ViewedPlayerProfile(
                    profile = uiState.viewedProfile,
                    isLoading = uiState.isViewedProfileLoading,
                    isLoadingMoreGames = uiState.isViewedGamesLoadingMore,
                    hasMoreGames = uiState.hasMoreViewedGames,
                    topInset = topInset,
                    onBack = viewModel::closeViewedProfile,
                    onLoadMoreGames = viewModel::loadMoreViewedGames,
                    onGameClick = { game -> viewModel.openGameDetails(game, onOpenGameDetails) },
                    modifier = Modifier.weight(1f)
                )
            } else {
                Box(modifier = Modifier.weight(1f)) {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = PaddingValues(
                            start = ScreenHorizontalPadding,
                            end = ScreenHorizontalPadding,
                            top = topInset + 8.dp,
                            bottom = bottomInset + 110.dp
                        ),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        item {
                            ScreenTopBar(
                                title = stringResource(R.string.profile_title),
                                onBackClick = onBackClick
                            )
                        }
                        when (tabs[selectedTab.intValue]) {
                            ProfileTab.Overview -> {
                                item {
                                    RevealOnEnter(revealKey = "overview-${uiState.account?.uid}") {
                                        ProfileOverview(
                                            profile = uiState.profile,
                                            isLoading = uiState.isProfileLoading,
                                            photoURL = uiState.account?.photoURL,
                                            email = uiState.account?.email,
                                            isActionLoading = uiState.isAuthLoading,
                                            isProUnlocked = uiState.isProUnlocked,
                                            rankInsights = uiState.rankInsights,
                                            onCustomizePro = { showProCustomization = true },
                                            onShareCard = {
                                                uiState.profile?.let { profile ->
                                                    PlayerCardSharer.share(context, profile, uiState.rankInsights)
                                                }
                                            },
                                            onUpdateName = viewModel::updateDisplayName,
                                            onSignOut = {
                                                viewModel.signOut()
                                            }
                                        )
                                    }
                                }
                                if (uiState.isProfileLoading && uiState.profile == null) {
                                    item { RecentGamesSkeletonCard() }
                                } else {
                                    uiState.profile?.games
                                        .orEmpty()
                                        .filter { (it.lastPlayedAtMs ?: 0L) > 0L }
                                        .sortedByDescending { it.lastPlayedAtMs ?: 0L }
                                        .take(8)
                                        .takeIf { it.isNotEmpty() }
                                        ?.let { recentGames ->
                                            item {
                                                RevealOnEnter(revealKey = "recent-${recentGames.first().gameKey}") {
                                                    RecentGamesCard(
                                                        games = recentGames,
                                                        onGameClick = { game -> viewModel.openGameDetails(game, onOpenGameDetails) }
                                                    )
                                                }
                                            }
                                        }
                                }
                            }

                            ProfileTab.Games -> {
                                val showGamesSkeleton = uiState.isProfileLoading && uiState.games.isEmpty()
                                if (showGamesSkeleton) {
                                    items(4, key = { "game-skeleton-$it" }) { GamePlayStatSkeletonRow() }
                                } else if (uiState.games.isEmpty()) {
                                    item { EmptyProfileState(text = stringResource(R.string.profile_games_empty)) }
                                } else {
                                    items(uiState.games, key = { it.gameKey }) { game ->
                                        GamePlayStatRow(
                                            game = game,
                                            onClick = { viewModel.openGameDetails(game, onOpenGameDetails) }
                                        )
                                    }
                                }
                            }

                            ProfileTab.Leaderboard -> {
                                item {
                                    RevealOnEnter(revealKey = "leaderboard-search") {
                                        PlayerSearchField(
                                            query = uiState.leaderboardSearchQuery,
                                            isLoading = uiState.isPlayerSearchLoading,
                                            onQueryChange = viewModel::updatePlayerSearch,
                                            onRefresh = viewModel::refreshLeaderboard
                                        )
                                    }
                                }
                                val isSearching = uiState.leaderboardSearchQuery.trim().length >= 2
                                val visibleEntries = if (isSearching) uiState.searchResults else uiState.leaderboard
                                val showLeaderboardSkeleton = visibleEntries.isEmpty() && (
                                    if (isSearching) uiState.isPlayerSearchLoading
                                    else uiState.isLeaderboardLoading || !uiState.hasLoadedLeaderboard
                                )
                                if (showLeaderboardSkeleton) {
                                    items(if (isSearching) 3 else 6, key = { "leaderboard-skeleton-$it" }) {
                                        LeaderboardRowSkeleton()
                                    }
                                } else if (isSearching && !uiState.isPlayerSearchLoading && visibleEntries.isEmpty()) {
                                    item { EmptyProfileState(text = stringResource(R.string.profile_leaderboard_no_results)) }
                                } else if (!isSearching && visibleEntries.isEmpty()) {
                                    item { EmptyProfileState(text = stringResource(R.string.profile_leaderboard_empty)) }
                                } else {
                                    items(visibleEntries, key = { it.uid }) { entry ->
                                        LeaderboardRow(
                                            entry = entry,
                                            currentUid = uiState.account?.uid,
                                            onClick = {
                                                if (entry.uid == uiState.account?.uid) {
                                                    selectedTab.intValue = ProfileTab.Overview.ordinal
                                                    viewModel.onProfileTabSelected(ProfileTab.Overview.name)
                                                } else {
                                                    viewModel.viewLeaderboardProfile(entry)
                                                }
                                            }
                                        )
                                    }
                                }
                                if (!isSearching && uiState.hasMoreLeaderboard && uiState.leaderboard.isNotEmpty()) {
                                    item {
                                        LaunchedEffect(uiState.leaderboard.size) { viewModel.loadMoreLeaderboard() }
                                        LeaderboardRowSkeleton()
                                    }
                                }
                            }

                            ProfileTab.Stats -> {
                                item {
                                    RevealOnEnter(revealKey = "profile-stats-${uiState.account?.uid}") {
                                        AdvancedStatsContent(
                                            isProUnlocked = uiState.isProUnlocked,
                                            isLoading = uiState.isActivityLoading,
                                            hasAttemptedLoad = uiState.hasAttemptedActivityLoad,
                                            isProfileLoading = uiState.isProfileLoading,
                                            activity = uiState.activity,
                                            rankInsights = uiState.rankInsights,
                                            profile = uiState.profile,
                                            onRefresh = viewModel::loadActivity
                                        )
                                    }
                                }
                            }
                        }
                    }
                    ProfileBottomNav(
                        tabs = tabs,
                        selectedIndex = selectedTab.intValue,
                        onSelect = {
                            selectedTab.intValue = it
                            viewModel.onProfileTabSelected(tabs[it].name)
                        },
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .padding(horizontal = ScreenHorizontalPadding)
                            .padding(bottom = bottomInset + 12.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun ProfileBottomNav(
    tabs: List<ProfileTab>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            tabs.forEachIndexed { index, tab ->
                val selected = selectedIndex == index
                val interactionSource = remember(tab) { MutableInteractionSource() }
                Surface(
                    onClick = { onSelect(index) },
                    modifier = Modifier
                        .weight(1f)
                        .height(56.dp)
                        .tvGamepadFocusableCard(
                            shape = RoundedCornerShape(22.dp),
                            interactionSource = interactionSource,
                            addFocusTarget = false
                        ),
                    shape = RoundedCornerShape(22.dp),
                    interactionSource = interactionSource,
                    color = if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.16f) else Color.Transparent
                ) {
                    Column(
                        modifier = Modifier.fillMaxSize(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Icon(
                            imageVector = tab.icon(),
                            contentDescription = null,
                            tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(22.dp)
                        )
                        Spacer(Modifier.height(3.dp))
                        Text(
                            text = stringResource(tab.titleRes()),
                            style = MaterialTheme.typography.labelMedium.copy(fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium),
                            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun AuthContent(
    isLoading: Boolean,
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    onSignIn: (String, String) -> Unit,
    onCreateAccount: (String, String, String) -> Unit,
    onResetPassword: (String) -> Unit,
    onGoogleSignIn: () -> Unit,
    modifier: Modifier = Modifier
) {
    var email by rememberSaveable { mutableStateOf("") }
    var password by rememberSaveable { mutableStateOf("") }
    var displayName by rememberSaveable { mutableStateOf("") }
    var createMode by rememberSaveable { mutableStateOf(false) }

    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset + 8.dp,
            bottom = 28.dp
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.profile_title),
                onBackClick = onBackClick
            )
        }
        item {
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(22.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 2.dp,
                border = profileCardBorder()
            ) {
                Column(
                    modifier = Modifier
                        .padding(18.dp)
                        .animateContentSize(
                            animationSpec = tween(
                                durationMillis = 260,
                                easing = FastOutSlowInEasing
                            )
                        ),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Text(
                        text = stringResource(R.string.profile_auth_title),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    AnimatedVisibility(
                        visible = createMode,
                        enter = fadeIn(animationSpec = tween(160)) + expandVertically(
                            animationSpec = tween(260, easing = FastOutSlowInEasing)
                        ),
                        exit = fadeOut(animationSpec = tween(120)) + shrinkVertically(
                            animationSpec = tween(220, easing = FastOutSlowInEasing)
                        )
                    ) {
                        OutlinedTextField(
                            value = displayName,
                            onValueChange = { displayName = it },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            shape = RoundedCornerShape(20.dp),
                            label = { Text(stringResource(R.string.profile_display_name)) },
                            leadingIcon = { Icon(Icons.Rounded.Person, contentDescription = null) }
                        )
                    }
                    OutlinedTextField(
                        value = email,
                        onValueChange = { email = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(20.dp),
                        label = { Text(stringResource(R.string.profile_email)) },
                        leadingIcon = { Icon(Icons.Rounded.Email, contentDescription = null) }
                    )
                    OutlinedTextField(
                        value = password,
                        onValueChange = { password = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        shape = RoundedCornerShape(20.dp),
                        label = { Text(stringResource(R.string.profile_password)) },
                        visualTransformation = PasswordVisualTransformation(),
                        leadingIcon = { Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null) }
                    )
                    Button(
                        enabled = !isLoading,
                        onClick = {
                            if (createMode) {
                                onCreateAccount(email, password, displayName)
                            } else {
                                onSignIn(email, password)
                            }
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        if (isLoading) {
                            CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                            Spacer(Modifier.width(10.dp))
                        }
                        Text(stringResource(if (createMode) R.string.profile_create_account else R.string.profile_sign_in))
                    }
                    OutlinedButton(
                        enabled = !isLoading,
                        onClick = onGoogleSignIn,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.AccountCircle, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.profile_google_sign_in))
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        TextButton(onClick = { createMode = !createMode }) {
                            Text(stringResource(if (createMode) R.string.profile_have_account else R.string.profile_need_account))
                        }
                        TextButton(enabled = email.isNotBlank() && !isLoading, onClick = { onResetPassword(email) }) {
                            Text(stringResource(R.string.profile_reset_password))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ViewedPlayerProfile(
    profile: PlayerProfile?,
    isLoading: Boolean,
    isLoadingMoreGames: Boolean,
    hasMoreGames: Boolean,
    topInset: androidx.compose.ui.unit.Dp,
    onBack: () -> Unit,
    onLoadMoreGames: () -> Unit,
    onGameClick: (PlayerGamePlayStat) -> Unit,
    modifier: Modifier = Modifier
) {
    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset + 8.dp,
            bottom = 18.dp
        ),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.profile_title),
                onBackClick = onBack
            )
        }

        if (isLoading) {
            item { ReadOnlyProfileSkeletonCard() }
            items(3) { GamePlayStatSkeletonRow() }
        } else if (profile == null) {
            item { EmptyProfileState(text = stringResource(R.string.profile_player_not_found)) }
        } else {
            item {
                ReadOnlyProfileCard(profile = profile)
            }
            if (profile.games.isNotEmpty()) {
                item {
                    Text(
                        text = stringResource(R.string.profile_tab_games),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onBackground,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
                items(profile.games, key = { it.gameKey }) { game ->
                    GamePlayStatRow(game = game, onClick = { onGameClick(game) })
                }
                if (hasMoreGames) {
                    item {
                        LaunchedEffect(profile.games.size) { onLoadMoreGames() }
                        GamePlayStatSkeletonRow()
                    }
                } else if (isLoadingMoreGames) {
                    item { GamePlayStatSkeletonRow() }
                }
            }
        }
    }
}

@Composable
private fun ReadOnlyProfileCard(profile: PlayerProfile) {
    val accent = profileAccentColor(profile.profileAccent)
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = if (profile.isProMember) BorderStroke(1.dp, accent.copy(alpha = 0.72f)) else profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (profile.isProMember) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End
                    ) {
                        ProBadge(accent)
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                Box(
                    modifier = Modifier
                        .size(72.dp)
                        .clip(CircleShape)
                        .background(if (profile.isProMember) accent.copy(alpha = 0.22f) else MaterialTheme.colorScheme.primary.copy(alpha = 0.12f))
                        .padding(if (profile.isProMember) 3.dp else 0.dp)
                        .clip(CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    BitmapPathImage(
                        imagePath = profile.photoURL,
                        contentDescription = profile.displayName,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Crop,
                        fallback = {
                            Icon(
                                imageVector = Icons.Rounded.Person,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(38.dp)
                            )
                        }
                    )
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = profile.displayName,
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    profile.playerTag.takeIf { it.isNotBlank() }?.let { tag ->
                        Text(
                            text = tag,
                            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                            color = if (profile.isProMember) accent else MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatChip(
                    icon = Icons.Rounded.Schedule,
                    label = stringResource(R.string.profile_total_time),
                    value = formatDuration(profile.totalPlayTimeMs),
                    modifier = Modifier.weight(1f)
                )
                StatChip(
                    icon = Icons.Rounded.SportsEsports,
                    label = stringResource(R.string.profile_games_played),
                    value = profile.gamesPlayed.toString(),
                    modifier = Modifier.weight(1f)
                )
            }
            FavoriteGamesShowcase(profile)
        }
    }
}

@Composable
private fun ProfileOverview(
    profile: PlayerProfile?,
    isLoading: Boolean,
    photoURL: String?,
    email: String?,
    isActionLoading: Boolean,
    isProUnlocked: Boolean,
    rankInsights: PlayerRankInsights?,
    onCustomizePro: () -> Unit,
    onShareCard: () -> Unit,
    onUpdateName: (String) -> Unit,
    onSignOut: () -> Unit
) {
    var editingName by rememberSaveable(profile?.displayName) { mutableStateOf(profile?.displayName.orEmpty()) }
    var isEditingName by rememberSaveable { mutableStateOf(false) }
    val avatarUrl = photoURL ?: profile?.photoURL
    val accent = profileAccentColor(profile?.profileAccent.orEmpty())

    if (isEditingName) {
        EditProfileNameDialog(
            currentName = profile?.displayName.orEmpty(),
            initialName = editingName,
            isLoading = isActionLoading,
            onNameChange = { editingName = it.take(32) },
            onDismiss = {
                editingName = profile?.displayName.orEmpty()
                isEditingName = false
            },
            onSave = {
                val cleanName = editingName.trim()
                if (cleanName.isNotEmpty() && cleanName != profile?.displayName) {
                    onUpdateName(cleanName)
                    isEditingName = false
                }
            }
        )
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = profileCardBorder()
    ) {
        AnimatedContent(
            targetState = isLoading && profile == null,
            modifier = Modifier
                .padding(16.dp)
                .animateContentSize(tween(260, easing = FastOutSlowInEasing)),
            transitionSpec = {
                fadeIn(tween(220, easing = FastOutSlowInEasing)) togetherWith
                    fadeOut(tween(140))
            },
            label = "profile-overview-content"
        ) { showSkeleton ->
            if (showSkeleton) {
                Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    ProfileOverviewSkeletonContent(
                        showAccountActions = true,
                        showRank = true,
                        showProActions = isProUnlocked
                    )
                }
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (isProUnlocked) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            ProBadge(accent)
                        }
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                    Box(
                        modifier = Modifier
                            .size(72.dp)
                            .clip(CircleShape)
                            .background(if (isProUnlocked) accent.copy(alpha = 0.22f) else MaterialTheme.colorScheme.primary.copy(alpha = 0.12f))
                            .padding(if (isProUnlocked) 3.dp else 0.dp)
                            .clip(CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        BitmapPathImage(
                            imagePath = avatarUrl,
                            contentDescription = profile?.displayName,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop,
                            fallback = {
                                Icon(
                                    imageVector = Icons.Rounded.Person,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(38.dp)
                                )
                            }
                        )
                    }
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = profile?.displayName ?: stringResource(R.string.profile_player),
                            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        profile?.playerTag?.takeIf { it.isNotBlank() }?.let { tag ->
                            Text(
                                text = stringResource(R.string.profile_player_id_format, tag),
                                style = MaterialTheme.typography.labelMedium,
                                color = if (isProUnlocked) accent else MaterialTheme.colorScheme.primary
                            )
                        }
                        Text(
                            text = email.orEmpty(),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    OutlinedButton(
                        enabled = !isActionLoading,
                        onClick = {
                            editingName = profile?.displayName.orEmpty()
                            isEditingName = true
                        },
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Edit,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.profile_edit_name), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    OutlinedButton(
                        enabled = !isActionLoading,
                        onClick = onSignOut,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Rounded.Logout,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.profile_sign_out), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }

                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatChip(
                        icon = Icons.Rounded.Schedule,
                        label = stringResource(R.string.profile_total_time),
                        value = formatDuration(profile?.totalPlayTimeMs ?: 0L),
                        modifier = Modifier.weight(1f)
                    )
                    StatChip(
                        icon = Icons.Rounded.SportsEsports,
                        label = stringResource(R.string.profile_games_played),
                        value = (profile?.gamesPlayed ?: 0).toString(),
                        modifier = Modifier.weight(1f)
                    )
                }

                rankInsights?.let { insights ->
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        StatChip(
                            icon = Icons.Rounded.EmojiEvents,
                            label = stringResource(R.string.profile_rank),
                            value = "#${insights.rank}",
                            modifier = Modifier.weight(1f)
                        )
                        StatChip(
                            icon = Icons.Rounded.Leaderboard,
                            label = stringResource(R.string.profile_total_players),
                            value = insights.totalPlayers.toString(),
                            modifier = Modifier.weight(1f)
                        )
                    }
                }

                if (isProUnlocked) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        OutlinedButton(onClick = onCustomizePro, modifier = Modifier.weight(1f)) {
                            Icon(Icons.Rounded.Palette, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(Modifier.width(7.dp))
                            Text(stringResource(R.string.profile_customize_pro), maxLines = 1)
                        }
                        OutlinedButton(onClick = onShareCard, modifier = Modifier.weight(1f)) {
                            Icon(Icons.Rounded.Share, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(Modifier.width(7.dp))
                            Text(stringResource(R.string.profile_player_card_share), maxLines = 1)
                        }
                    }
                    profile?.let { FavoriteGamesShowcase(it) }
                }

                }
            }
        }
    }
}

@Composable
private fun EditProfileNameDialog(
    currentName: String,
    initialName: String,
    isLoading: Boolean,
    onNameChange: (String) -> Unit,
    onDismiss: () -> Unit,
    onSave: () -> Unit
) {
    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current
    val cleanName = initialName.trim()
    val canSave = !isLoading && cleanName.isNotEmpty() && cleanName != currentName

    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
        keyboardController?.show()
    }

    Dialog(
        onDismissRequest = { if (!isLoading) onDismiss() },
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            dismissOnBackPress = !isLoading,
            dismissOnClickOutside = !isLoading
        )
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 560.dp),
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 6.dp,
                border = profileCardBorder(alpha = 0.82f)
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(18.dp)
                ) {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(14.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(48.dp),
                            shape = RoundedCornerShape(16.dp),
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    imageVector = Icons.Rounded.Edit,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(24.dp)
                                )
                            }
                        }
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(2.dp)
                        ) {
                            Text(
                                text = stringResource(R.string.profile_edit_name),
                                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                text = stringResource(R.string.profile_edit_name_description),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }

                    OutlinedTextField(
                        value = initialName,
                        onValueChange = onNameChange,
                        modifier = Modifier
                            .fillMaxWidth()
                            .focusRequester(focusRequester),
                        enabled = !isLoading,
                        singleLine = true,
                        shape = RoundedCornerShape(18.dp),
                        label = { Text(stringResource(R.string.profile_display_name)) },
                        leadingIcon = { Icon(Icons.Rounded.Person, contentDescription = null) },
                        supportingText = {
                            Text(stringResource(R.string.profile_name_character_count, initialName.length))
                        },
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                        keyboardActions = KeyboardActions(
                            onDone = {
                                if (canSave) {
                                    keyboardController?.hide()
                                    onSave()
                                }
                            }
                        )
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        OutlinedButton(
                            onClick = onDismiss,
                            enabled = !isLoading,
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(stringResource(R.string.cancel))
                        }
                        Button(
                            onClick = {
                                keyboardController?.hide()
                                onSave()
                            },
                            enabled = canSave,
                            modifier = Modifier.weight(1f)
                        ) {
                            if (isLoading) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(18.dp),
                                    strokeWidth = 2.dp
                                )
                            } else {
                                Text(stringResource(R.string.save))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ProBadge(accent: Color, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier.widthIn(min = 58.dp),
        shape = RoundedCornerShape(10.dp),
        color = accent.copy(alpha = 0.2f),
        border = BorderStroke(1.dp, accent.copy(alpha = 0.72f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 7.dp, vertical = 3.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Rounded.WorkspacePremium,
                contentDescription = null,
                tint = accent,
                modifier = Modifier.size(14.dp)
            )
            Text(
                text = stringResource(R.string.profile_pro_badge),
                style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Black),
                color = accent,
                maxLines = 1,
                softWrap = false
            )
        }
    }
}

@Composable
private fun FavoriteGamesShowcase(profile: PlayerProfile) {
    val gamesByKey = remember(profile.games) { profile.games.associateBy { it.gameKey } }
    val favorites = remember(profile.favoriteGameKeys, profile.games) {
        profile.favoriteGameKeys.mapNotNull(gamesByKey::get).take(3)
    }
    if (favorites.isEmpty()) return

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = stringResource(R.string.profile_showcase_title),
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onSurface
        )
        LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            items(favorites, key = { it.gameKey }) { game ->
                Row(
                    modifier = Modifier.width(180.dp),
                    horizontalArrangement = Arrangement.spacedBy(9.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    GameCoverArt(
                        coverPath = game.coverArtPath,
                        fallbackTitle = game.title,
                        modifier = Modifier
                            .size(width = 48.dp, height = 68.dp)
                            .clip(RoundedCornerShape(9.dp)),
                        contentScale = ContentScale.Crop
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = game.title,
                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = formatDuration(game.totalPlayTimeMs),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun PlayerSearchField(
    query: String,
    isLoading: Boolean,
    onQueryChange: (String) -> Unit,
    onRefresh: () -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            OutlinedTextField(
                value = query,
                onValueChange = onQueryChange,
                modifier = Modifier.weight(1f),
                singleLine = true,
                shape = RoundedCornerShape(20.dp),
                placeholder = { Text(stringResource(R.string.profile_leaderboard_search_hint)) },
                leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                trailingIcon = if (isLoading) {
                    { CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp) }
                } else null
            )
            Surface(
                onClick = onRefresh,
                modifier = Modifier.size(56.dp),
                shape = CircleShape,
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.24f))
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.Refresh,
                        contentDescription = stringResource(R.string.profile_leaderboard_refresh),
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(24.dp)
                    )
                }
            }
        }
        Text(
            text = stringResource(R.string.profile_leaderboard_search_help),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProProfileCustomizationDialog(
    profile: PlayerProfile,
    games: List<PlayerGamePlayStat>,
    onDismiss: () -> Unit,
    onSave: (String, List<String>) -> Unit
) {
    var accent by rememberSaveable(profile.profileAccent) { mutableStateOf(profile.profileAccent) }
    var favoriteKeys by rememberSaveable(profile.favoriteGameKeys) {
        mutableStateOf(profile.favoriteGameKeys.take(3))
    }
    val accents = listOf("gold", "crimson", "blue", "violet", "emerald")
    val sortedGames = remember(games) { games.sortedByDescending { it.totalPlayTimeMs }.take(30) }
    val windowSize = LocalWindowInfo.current.containerSize
    val density = LocalDensity.current
    val windowWidth = with(density) { windowSize.width.toDp() }
    val windowHeight = with(density) { windowSize.height.toDp() }
    val isLandscape = windowWidth > windowHeight
    val maxDialogHeight = if (isLandscape) {
        (windowHeight - 40.dp).coerceAtLeast(300.dp)
    } else {
        (windowHeight - 64.dp).coerceAtLeast(520.dp)
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(
                    start = if (isLandscape) 10.dp else 14.dp,
                    top = if (isLandscape) 10.dp else 14.dp,
                    end = if (isLandscape) 10.dp else 14.dp,
                    bottom = if (isLandscape) 4.dp else 8.dp
                ),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth(if (isLandscape) 0.98f else 0.94f)
                    .widthIn(max = if (isLandscape) 1600.dp else 720.dp),
                shape = RoundedCornerShape(30.dp),
                color = MaterialTheme.colorScheme.surface,
                border = profileCardBorder(alpha = 0.6f)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = maxDialogHeight)
                        .verticalScroll(rememberScrollState())
                        .padding(horizontal = 22.dp, vertical = 22.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(14.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(58.dp),
                            shape = RoundedCornerShape(18.dp),
                            color = profileAccentColor(accent).copy(alpha = 0.18f),
                            border = BorderStroke(1.dp, profileAccentColor(accent).copy(alpha = 0.34f))
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    imageVector = Icons.Rounded.WorkspacePremium,
                                    contentDescription = null,
                                    tint = profileAccentColor(accent),
                                    modifier = Modifier.size(30.dp)
                                )
                            }
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.profile_pro_customization_eyebrow),
                                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                                color = profileAccentColor(accent)
                            )
                            Text(
                                text = stringResource(R.string.profile_customize_pro_title),
                                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))

                    ProCustomizationSection(
                        title = stringResource(R.string.profile_customize_pro_accent),
                        description = stringResource(R.string.profile_customize_pro_accent_help)
                    ) {
                        FlowRow(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                            maxItemsInEachRow = 2
                        ) {
                            accents.forEach { option ->
                                ProfileAccentChoice(
                                    accent = option,
                                    selected = accent == option,
                                    onClick = { accent = option }
                                )
                            }
                        }
                    }

                    ProCustomizationSection(
                        title = stringResource(R.string.profile_customize_pro_games),
                        description = stringResource(R.string.profile_customize_pro_games_help),
                        trailing = stringResource(R.string.profile_showcase_count_format, favoriteKeys.size)
                    ) {
                        if (sortedGames.isEmpty()) {
                            Text(
                                text = stringResource(R.string.profile_games_empty),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        } else {
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                sortedGames.forEach { game ->
                                    val selected = game.gameKey in favoriteKeys
                                    ProShowcaseGameChoice(
                                        game = game,
                                        selected = selected,
                                        enabled = selected || favoriteKeys.size < 3,
                                        onClick = {
                                            favoriteKeys = if (selected) {
                                                favoriteKeys - game.gameKey
                                            } else {
                                                favoriteKeys + game.gameKey
                                            }
                                        }
                                    )
                                }
                            }
                        }
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        OutlinedButton(onClick = onDismiss, modifier = Modifier.weight(1f)) {
                            Text(stringResource(R.string.cancel))
                        }
                        Button(
                            onClick = { onSave(accent, favoriteKeys) },
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(stringResource(R.string.save))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ProCustomizationSection(
    title: String,
    description: String,
    trailing: String? = null,
    content: @Composable () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.28f),
        border = profileCardBorder(alpha = 0.38f)
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = title,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                trailing?.let {
                    Text(
                        text = it,
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
            Text(
                text = description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            content()
        }
    }
}

@Composable
private fun ProfileAccentChoice(
    accent: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    val color = profileAccentColor(accent)
    Surface(
        onClick = onClick,
        modifier = Modifier
            .width(132.dp)
            .height(52.dp),
        shape = RoundedCornerShape(16.dp),
        color = if (selected) color.copy(alpha = 0.16f) else MaterialTheme.colorScheme.surface.copy(alpha = 0.72f),
        border = BorderStroke(1.dp, if (selected) color.copy(alpha = 0.72f) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.48f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(9.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(Modifier.size(16.dp).clip(CircleShape).background(color))
            Text(
                text = stringResource(accentNameRes(accent)),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                maxLines = 1
            )
            if (selected) {
                Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = color, modifier = Modifier.size(18.dp))
            }
        }
    }
}

@Composable
private fun ProShowcaseGameChoice(
    game: PlayerGamePlayStat,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(17.dp),
        color = if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.13f) else MaterialTheme.colorScheme.surface.copy(alpha = if (enabled) 0.72f else 0.32f),
        border = BorderStroke(
            1.dp,
            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.42f) else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.38f)
        )
    ) {
        Row(
            modifier = Modifier.padding(9.dp),
            horizontalArrangement = Arrangement.spacedBy(11.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            GameCoverArt(
                coverPath = game.coverArtPath,
                fallbackTitle = game.title,
                modifier = Modifier
                    .size(width = 42.dp, height = 58.dp)
                    .clip(RoundedCornerShape(10.dp)),
                contentScale = ContentScale.Crop
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = formatDuration(game.totalPlayTimeMs),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Icon(
                imageVector = Icons.Rounded.CheckCircle,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant,
                modifier = Modifier.size(24.dp)
            )
        }
    }
}

@Composable
private fun AdvancedStatsContent(
    isProUnlocked: Boolean,
    isLoading: Boolean,
    hasAttemptedLoad: Boolean,
    isProfileLoading: Boolean,
    activity: List<PlayerActivityDay>,
    rankInsights: PlayerRankInsights?,
    profile: PlayerProfile?,
    onRefresh: () -> Unit
) {
    if (!isProUnlocked) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(28.dp),
            color = MaterialTheme.colorScheme.surface,
            border = profileCardBorder()
        ) {
            Column(
                modifier = Modifier.padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Icon(
                    Icons.Rounded.WorkspacePremium,
                    contentDescription = null,
                    tint = Color(0xFFFFC857),
                    modifier = Modifier.size(42.dp)
                )
                Text(
                    stringResource(R.string.profile_stats_locked_title),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
                )
                Text(
                    stringResource(R.string.profile_stats_locked_body),
                    modifier = Modifier.fillMaxWidth(),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }
        }
        return
    }
    if (isProfileLoading && profile == null) {
        AdvancedStatsSkeleton()
        return
    }
    val sorted = remember(activity) { activity.sortedBy { it.day } }
    val last7 = remember(sorted) { playTimeWithinDays(sorted, 7) }
    val last30 = remember(sorted) { playTimeWithinDays(sorted, 30) }
    val streaks = remember(sorted) { calculateStreaks(sorted) }
    val totalSessions = remember(profile?.games) { profile?.games.orEmpty().sumOf { it.sessions } }
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(26.dp),
            color = MaterialTheme.colorScheme.surface,
            border = profileCardBorder(alpha = 0.72f)
        ) {
            Row(
                modifier = Modifier.padding(18.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Surface(
                    modifier = Modifier.size(52.dp),
                    shape = RoundedCornerShape(17.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            imageVector = Icons.Rounded.EmojiEvents,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(28.dp)
                        )
                    }
                }
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Text(
                        text = stringResource(R.string.profile_stats_lifetime_title),
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(R.string.profile_stats_lifetime_body),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Surface(
                    onClick = onRefresh,
                    enabled = !isLoading,
                    modifier = Modifier.size(44.dp),
                    shape = CircleShape,
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
                    border = profileCardBorder(alpha = 0.52f)
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = stringResource(R.string.profile_stats_refresh),
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(21.dp)
                        )
                    }
                }
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChip(
                Icons.Rounded.Schedule,
                stringResource(R.string.profile_total_time),
                formatDuration(profile?.totalPlayTimeMs ?: 0L),
                Modifier.weight(1f)
            )
            StatChip(
                Icons.Rounded.SportsEsports,
                stringResource(R.string.profile_games_played),
                (profile?.gamesPlayed ?: 0).toString(),
                Modifier.weight(1f)
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChip(
                Icons.Rounded.Leaderboard,
                stringResource(R.string.profile_rank),
                rankInsights?.let { "#${it.rank}" } ?: "—",
                Modifier.weight(1f)
            )
            StatChip(
                Icons.Rounded.AccountCircle,
                stringResource(R.string.profile_stats_total_sessions),
                totalSessions.toString(),
                Modifier.weight(1f)
            )
        }

        Text(
            text = stringResource(R.string.profile_stats_activity),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.padding(start = 4.dp, top = 4.dp)
        )

        val activityState = when {
            isLoading || !hasAttemptedLoad -> StatsActivityState.Loading
            sorted.isEmpty() -> StatsActivityState.Empty
            else -> StatsActivityState.Ready
        }
        AnimatedContent(
            targetState = activityState,
            modifier = Modifier.fillMaxWidth(),
            transitionSpec = {
                fadeIn(tween(220, easing = FastOutSlowInEasing)) togetherWith
                    fadeOut(tween(140))
            },
            label = "profile-stats-activity"
        ) { state ->
            when (state) {
                StatsActivityState.Loading -> StatsActivitySkeleton()
                StatsActivityState.Empty -> EmptyActivityCard()
                StatsActivityState.Ready -> Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        StatChip(Icons.Rounded.Schedule, stringResource(R.string.profile_stats_this_week), formatDuration(last7), Modifier.weight(1f))
                        StatChip(Icons.Rounded.Schedule, stringResource(R.string.profile_stats_this_month), formatDuration(last30), Modifier.weight(1f))
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        StatChip(Icons.Rounded.SportsEsports, stringResource(R.string.profile_stats_current_streak), stringResource(R.string.profile_stats_days_format, streaks.first), Modifier.weight(1f))
                        StatChip(Icons.Rounded.EmojiEvents, stringResource(R.string.profile_stats_best_streak), stringResource(R.string.profile_stats_days_format, streaks.second), Modifier.weight(1f))
                    }
                    ActivityChart(sorted.takeLast(14))
                }
            }
        }
    }
}

@Composable
private fun AdvancedStatsSkeleton() {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(26.dp),
            color = MaterialTheme.colorScheme.surface,
            border = profileCardBorder(alpha = 0.72f)
        ) {
            Row(
                modifier = Modifier.padding(18.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                SkeletonBlock(Modifier.size(52.dp).clip(RoundedCornerShape(17.dp)))
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(7.dp)
                ) {
                    SkeletonBlock(
                        Modifier
                            .fillMaxWidth(0.58f)
                            .height(20.dp)
                            .clip(RoundedCornerShape(9.dp))
                    )
                    SkeletonBlock(
                        Modifier
                            .fillMaxWidth(0.92f)
                            .height(14.dp)
                            .clip(RoundedCornerShape(7.dp))
                    )
                    SkeletonBlock(
                        Modifier
                            .fillMaxWidth(0.72f)
                            .height(14.dp)
                            .clip(RoundedCornerShape(7.dp))
                    )
                }
                SkeletonBlock(Modifier.size(44.dp).clip(CircleShape))
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChipSkeleton(Modifier.weight(1f))
            StatChipSkeleton(Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChipSkeleton(Modifier.weight(1f))
            StatChipSkeleton(Modifier.weight(1f))
        }
        SkeletonBlock(
            Modifier
                .padding(start = 4.dp, top = 4.dp)
                .width(90.dp)
                .height(20.dp)
                .clip(RoundedCornerShape(9.dp))
        )
        StatsActivitySkeleton()
    }
}

@Composable
private fun EmptyActivityCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Row(
            modifier = Modifier.padding(18.dp),
            horizontalArrangement = Arrangement.spacedBy(13.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(46.dp),
                shape = CircleShape,
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.Schedule,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = stringResource(R.string.profile_stats_history_empty_title),
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = stringResource(R.string.profile_stats_empty),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun ActivityChart(activity: List<PlayerActivityDay>) {
    val maxMs = activity.maxOfOrNull { it.playTimeMs }?.coerceAtLeast(1L) ?: 1L
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        border = profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                stringResource(R.string.profile_stats_weekly_chart),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold)
            )
            LazyRow(
                modifier = Modifier.height(150.dp),
                horizontalArrangement = Arrangement.spacedBy(9.dp),
                verticalAlignment = Alignment.Bottom
            ) {
                items(activity, key = { it.day }) { day ->
                    val barHeight = (92f * day.playTimeMs / maxMs).coerceAtLeast(4f).dp
                    Column(
                        modifier = Modifier.width(34.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Bottom
                    ) {
                        Text(
                            formatDuration(day.playTimeMs),
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 1
                        )
                        Spacer(Modifier.height(4.dp))
                        Box(
                            Modifier
                                .width(24.dp)
                                .height(barHeight)
                                .clip(RoundedCornerShape(topStart = 7.dp, topEnd = 7.dp))
                                .background(MaterialTheme.colorScheme.primary)
                        )
                        Spacer(Modifier.height(5.dp))
                        Text(day.day.takeLast(5), style = MaterialTheme.typography.labelSmall)
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileOverviewSkeletonContent(
    showAccountActions: Boolean,
    showRank: Boolean,
    showProActions: Boolean
) {
    if (showProActions) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End
        ) {
            SkeletonBlock(
                modifier = Modifier
                    .width(68.dp)
                    .height(24.dp)
                    .clip(RoundedCornerShape(10.dp))
            )
        }
    }
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        SkeletonBlock(
            modifier = Modifier
                .size(72.dp)
                .clip(CircleShape)
        )
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            SkeletonBlock(
                modifier = Modifier
                    .fillMaxWidth(0.72f)
                    .height(24.dp)
                    .clip(RoundedCornerShape(10.dp))
            )
            SkeletonBlock(
                modifier = Modifier
                    .fillMaxWidth(0.9f)
                    .height(16.dp)
                    .clip(RoundedCornerShape(8.dp))
            )
        }
    }

    if (showAccountActions) {
        SkeletonButtonRow()
    }

    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        StatChipSkeleton(Modifier.weight(1f))
        StatChipSkeleton(Modifier.weight(1f))
    }
    if (showRank) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChipSkeleton(Modifier.weight(1f))
            StatChipSkeleton(Modifier.weight(1f))
        }
    }
    if (showProActions) {
        SkeletonButtonRow()
    }
}

@Composable
private fun SkeletonButtonRow() {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        SkeletonBlock(
            modifier = Modifier
                .weight(1f)
                .height(44.dp)
                .clip(RoundedCornerShape(18.dp))
        )
        SkeletonBlock(
            modifier = Modifier
                .weight(1f)
                .height(44.dp)
                .clip(RoundedCornerShape(18.dp))
        )
    }
}

@Composable
private fun ReadOnlyProfileSkeletonCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            ProfileOverviewSkeletonContent(
                showAccountActions = false,
                showRank = false,
                showProActions = false
            )
        }
    }
}

@Composable
private fun GamePlayStatRow(
    game: PlayerGamePlayStat,
    onClick: (() -> Unit)? = null
) {
    val content: @Composable () -> Unit = {
        Row(
            modifier = Modifier.padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            GameCoverArt(
                coverPath = game.coverArtPath,
                fallbackTitle = game.title,
                modifier = Modifier
                    .size(width = 54.dp, height = 78.dp)
                    .clip(RoundedCornerShape(10.dp)),
                contentScale = ContentScale.Crop
            )
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = stringResource(R.string.profile_game_sessions_format, game.sessions),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                game.lastPlayedAtMs?.let { lastPlayed ->
                    Text(
                        text = stringResource(R.string.profile_game_last_played_format, formatDate(lastPlayed)),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Text(
                text = formatDuration(game.totalPlayTimeMs),
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
    if (onClick != null) {
        Surface(
            onClick = onClick,
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 1.dp,
            border = profileCardBorder(alpha = 0.48f),
            content = content
        )
    } else {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 1.dp,
            border = profileCardBorder(alpha = 0.48f),
            content = content
        )
    }
}

@Composable
private fun GamePlayStatSkeletonRow() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = profileCardBorder(alpha = 0.48f)
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            SkeletonBlock(
                modifier = Modifier
                    .size(width = 54.dp, height = 78.dp)
                    .clip(RoundedCornerShape(10.dp))
            )
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                SkeletonBlock(
                    modifier = Modifier
                        .fillMaxWidth(0.82f)
                        .height(18.dp)
                        .clip(RoundedCornerShape(8.dp))
                )
                SkeletonBlock(
                    modifier = Modifier
                        .fillMaxWidth(0.46f)
                        .height(14.dp)
                        .clip(RoundedCornerShape(7.dp))
                )
                SkeletonBlock(
                    modifier = Modifier
                        .fillMaxWidth(0.58f)
                        .height(12.dp)
                        .clip(RoundedCornerShape(6.dp))
                )
            }
            SkeletonBlock(
                modifier = Modifier
                    .width(44.dp)
                    .height(18.dp)
                    .clip(RoundedCornerShape(8.dp))
            )
        }
    }
}

@Composable
private fun LeaderboardRowSkeleton() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = profileCardBorder(alpha = 0.5f)
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            SkeletonBlock(
                Modifier
                    .size(width = 62.dp, height = 38.dp)
                    .clip(RoundedCornerShape(18.dp))
            )
            SkeletonBlock(Modifier.size(52.dp).clip(CircleShape))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                SkeletonBlock(
                    Modifier
                        .fillMaxWidth(0.78f)
                        .height(20.dp)
                        .clip(RoundedCornerShape(9.dp))
                )
                SkeletonBlock(
                    Modifier
                        .fillMaxWidth(0.58f)
                        .height(14.dp)
                        .clip(RoundedCornerShape(7.dp))
                )
                SkeletonBlock(
                    Modifier
                        .fillMaxWidth(0.4f)
                        .height(14.dp)
                        .clip(RoundedCornerShape(7.dp))
                )
            }
            SkeletonBlock(
                Modifier
                    .width(66.dp)
                    .height(21.dp)
                    .clip(RoundedCornerShape(9.dp))
            )
        }
    }
}

@Composable
private fun StatsActivitySkeleton() {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChipSkeleton(Modifier.weight(1f))
            StatChipSkeleton(Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatChipSkeleton(Modifier.weight(1f))
            StatChipSkeleton(Modifier.weight(1f))
        }
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surface,
            border = profileCardBorder()
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                SkeletonBlock(
                    Modifier
                        .width(138.dp)
                        .height(18.dp)
                        .clip(RoundedCornerShape(8.dp))
                )
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(150.dp),
                    horizontalArrangement = Arrangement.SpaceEvenly,
                    verticalAlignment = Alignment.Bottom
                ) {
                    listOf(48, 82, 60, 108, 72, 126, 94).forEach { height ->
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            SkeletonBlock(
                                Modifier
                                    .width(24.dp)
                                    .height(height.dp)
                                    .clip(RoundedCornerShape(topStart = 7.dp, topEnd = 7.dp))
                            )
                            SkeletonBlock(
                                Modifier
                                    .width(28.dp)
                                    .height(10.dp)
                                    .clip(RoundedCornerShape(5.dp))
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun StatChipSkeleton(modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.36f),
        border = profileCardBorder(alpha = 0.34f)
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            SkeletonBlock(Modifier.size(24.dp).clip(CircleShape))
            SkeletonBlock(
                Modifier
                    .fillMaxWidth(0.54f)
                    .height(22.dp)
                    .clip(RoundedCornerShape(9.dp))
            )
            SkeletonBlock(
                Modifier
                    .fillMaxWidth(0.76f)
                    .height(15.dp)
                    .clip(RoundedCornerShape(7.dp))
            )
        }
    }
}

@Composable
private fun RecentGamesCard(
    games: List<PlayerGamePlayStat>,
    onGameClick: (PlayerGamePlayStat) -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = stringResource(R.string.profile_recently_played),
                modifier = Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            LazyRow(
                contentPadding = PaddingValues(horizontal = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                items(games, key = { it.gameKey }) { game ->
                    Column(
                        modifier = Modifier.width(96.dp),
                        verticalArrangement = Arrangement.spacedBy(7.dp)
                    ) {
                        Surface(
                            onClick = { onGameClick(game) },
                            shape = RoundedCornerShape(16.dp),
                            color = Color.Transparent
                        ) {
                            GameCoverArt(
                                coverPath = game.coverArtPath,
                                fallbackTitle = game.title,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(136.dp)
                                    .clip(RoundedCornerShape(16.dp)),
                                contentScale = ContentScale.Crop
                            )
                        }
                        Text(
                            text = game.title,
                            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun RecentGamesSkeletonCard() {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = profileCardBorder()
    ) {
        Column(
            modifier = Modifier.padding(vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            SkeletonBlock(
                modifier = Modifier
                    .padding(horizontal = 16.dp)
                    .width(148.dp)
                    .height(22.dp)
                    .clip(RoundedCornerShape(10.dp))
            )
            LazyRow(
                contentPadding = PaddingValues(horizontal = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                items(4) {
                    Column(
                        modifier = Modifier.width(96.dp),
                        verticalArrangement = Arrangement.spacedBy(7.dp)
                    ) {
                        SkeletonBlock(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(136.dp)
                                .clip(RoundedCornerShape(16.dp))
                        )
                        SkeletonBlock(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(14.dp)
                                .clip(RoundedCornerShape(7.dp))
                        )
                        SkeletonBlock(
                            modifier = Modifier
                                .fillMaxWidth(0.68f)
                                .height(12.dp)
                                .clip(RoundedCornerShape(6.dp))
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun LeaderboardRow(
    entry: PlayerLeaderboardEntry,
    currentUid: String?,
    onClick: () -> Unit
) {
    val isCurrentUser = entry.uid == currentUid
    val proAccent = profileAccentColor(entry.profileAccent)
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = if (isCurrentUser) MaterialTheme.colorScheme.primary.copy(alpha = 0.12f) else MaterialTheme.colorScheme.surface,
        tonalElevation = 2.dp,
        border = if (isCurrentUser) {
            BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.28f))
        } else {
            profileCardBorder(alpha = 0.5f)
        }
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            if (entry.isProMember) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End
                ) {
                    ProBadge(proAccent)
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                RankBadge(rank = entry.rank)
                Box(
                    modifier = Modifier
                        .size(52.dp)
                        .clip(CircleShape)
                        .background(
                            if (entry.isProMember) proAccent.copy(alpha = 0.72f)
                            else MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                        )
                        .padding(if (entry.isProMember) 3.dp else 0.dp)
                        .clip(CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    BitmapPathImage(
                        imagePath = entry.photoURL,
                        contentDescription = entry.displayName,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Crop,
                        fallback = {
                            Icon(
                                imageVector = Icons.Rounded.Person,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(25.dp)
                            )
                        }
                    )
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = entry.displayName,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    entry.playerTag.takeIf { it.isNotBlank() }?.let { tag ->
                        Text(
                            text = tag,
                            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.SemiBold),
                            color = if (entry.isProMember) proAccent else MaterialTheme.colorScheme.primary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Text(
                        text = stringResource(R.string.profile_leaderboard_games_format, entry.gamesPlayed),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Text(
                    text = formatDuration(entry.totalPlayTimeMs),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
private fun RankBadge(rank: Int?) {
    val topRank = rank != null && rank <= 3
    Surface(
        shape = RoundedCornerShape(18.dp),
        color = if (topRank) colorForRank(rank).copy(alpha = 0.18f) else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(5.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (topRank) {
                Icon(
                    imageVector = Icons.Rounded.EmojiEvents,
                    contentDescription = null,
                    tint = colorForRank(rank),
                    modifier = Modifier.size(18.dp)
                )
            }
            Text(
                text = rank?.let { "#$it" } ?: "—",
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                color = if (topRank) colorForRank(rank) else MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}


@Composable
private fun SkeletonBlock(modifier: Modifier = Modifier) {
    Box(modifier = modifier.shimmer())
}

@Composable
private fun RevealOnEnter(
    revealKey: Any,
    content: @Composable () -> Unit
) {
    var visible by remember(revealKey) { mutableStateOf(false) }
    LaunchedEffect(revealKey) { visible = true }
    AnimatedVisibility(
        visible = visible,
        enter = fadeIn(tween(160, easing = FastOutSlowInEasing)),
        exit = fadeOut(tween(100)),
        content = { content() }
    )
}

@Composable
private fun EmptyProfileState(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        border = profileCardBorder(alpha = 0.48f)
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(18.dp)
        )
    }
}

@Composable
private fun StatChip(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    value: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.36f),
        border = profileCardBorder(alpha = 0.34f)
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            Text(
                text = value,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(text = label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

private fun ProfileTab.titleRes(): Int = when (this) {
    ProfileTab.Overview -> R.string.profile_tab_overview
    ProfileTab.Games -> R.string.profile_tab_games
    ProfileTab.Leaderboard -> R.string.profile_tab_leaderboard
    ProfileTab.Stats -> R.string.profile_tab_stats
}

private fun ProfileTab.icon() = when (this) {
    ProfileTab.Overview -> Icons.Rounded.Person
    ProfileTab.Games -> Icons.Rounded.SportsEsports
    ProfileTab.Leaderboard -> Icons.Rounded.Leaderboard
    ProfileTab.Stats -> Icons.Rounded.EmojiEvents
}

@Composable
private fun profileCardBorder(alpha: Float = 0.58f): BorderStroke {
    return BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = alpha))
}

private fun profileMessageRes(key: String): Int = when (key) {
    "profile_signed_in" -> R.string.profile_signed_in
    "profile_account_created" -> R.string.profile_account_created
    "profile_password_reset_sent" -> R.string.profile_password_reset_sent
    "profile_name_updated" -> R.string.profile_name_updated
    "profile_signed_out" -> R.string.profile_signed_out
    "profile_pro_updated" -> R.string.profile_pro_updated
    else -> R.string.profile_done
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

private fun colorForRank(rank: Int) = when (rank) {
    1 -> Color(0xFFFFC857)
    2 -> Color(0xFFB6C2D9)
    else -> Color(0xFFD89C64)
}

private fun profileAccentColor(accent: String): Color = when (accent) {
    "crimson" -> Color(0xFFE84A5F)
    "blue" -> Color(0xFF4D9EFF)
    "violet" -> Color(0xFFA979FF)
    "emerald" -> Color(0xFF36C98F)
    else -> Color(0xFFFFC857)
}

private fun accentNameRes(accent: String): Int = when (accent) {
    "crimson" -> R.string.profile_accent_crimson
    "blue" -> R.string.profile_accent_blue
    "violet" -> R.string.profile_accent_violet
    "emerald" -> R.string.profile_accent_emerald
    else -> R.string.profile_accent_gold
}

private fun calculateStreaks(activity: List<PlayerActivityDay>): Pair<Int, Int> {
    val parser = SimpleDateFormat("yyyy-MM-dd", Locale.US).apply { isLenient = false }
    val activeDays = activity
        .filter { it.playTimeMs > 0L }
        .mapNotNull { runCatching { parser.parse(it.day)?.time }.getOrNull() }
        .distinct()
        .sorted()
    if (activeDays.isEmpty()) return 0 to 0

    var best = 1
    var run = 1
    for (index in 1 until activeDays.size) {
        val gapDays = (activeDays[index] - activeDays[index - 1]) / 86_400_000L
        run = if (gapDays == 1L) run + 1 else 1
        best = maxOf(best, run)
    }

    val today = Calendar.getInstance().apply {
        set(Calendar.HOUR_OF_DAY, 0)
        set(Calendar.MINUTE, 0)
        set(Calendar.SECOND, 0)
        set(Calendar.MILLISECOND, 0)
    }.timeInMillis
    val lastGap = (today - activeDays.last()) / 86_400_000L
    if (lastGap > 1L) return 0 to best

    var current = 1
    for (index in activeDays.lastIndex downTo 1) {
        if ((activeDays[index] - activeDays[index - 1]) / 86_400_000L == 1L) current++ else break
    }
    return current to best
}

private fun playTimeWithinDays(activity: List<PlayerActivityDay>, days: Int): Long {
    val parser = SimpleDateFormat("yyyy-MM-dd", Locale.US).apply { isLenient = false }
    val today = Calendar.getInstance().apply {
        set(Calendar.HOUR_OF_DAY, 0)
        set(Calendar.MINUTE, 0)
        set(Calendar.SECOND, 0)
        set(Calendar.MILLISECOND, 0)
    }.timeInMillis
    val earliest = today - (days.coerceAtLeast(1) - 1L) * 86_400_000L
    return activity.sumOf { day ->
        val timestamp = runCatching { parser.parse(day.day)?.time }.getOrNull()
        if (timestamp != null && timestamp in earliest..(today + 86_399_999L)) day.playTimeMs else 0L
    }
}

private fun formatDuration(durationMs: Long): String {
    val totalMinutes = (durationMs / 60_000L).coerceAtLeast(0L)
    val hours = totalMinutes / 60L
    val minutes = totalMinutes % 60L
    return when {
        hours > 0L -> String.format(Locale.US, "%dh %02dm", hours, minutes)
        else -> String.format(Locale.US, "%dm", minutes)
    }
}

private fun formatDate(timestampMs: Long): String {
    return DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(timestampMs))
}

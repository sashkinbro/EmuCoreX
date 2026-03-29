package com.sbro.emucorex.ui.achievements

import android.annotation.SuppressLint
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.LockOpen
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.utils.RetroAchievementsLoginRequestReason
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.core.utils.RetroAchievementsUiState
import com.sbro.emucorex.data.LibraryUnlockedAchievement
import com.sbro.emucorex.data.RetroAchievementEntry
import com.sbro.emucorex.data.RetroAchievementGameData
import com.sbro.emucorex.data.RetroAchievementsRepository
import com.sbro.emucorex.ui.common.BitmapPathImage
import com.sbro.emucorex.ui.common.NavigationBackButton
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

private data class HubContentState(
    val isLoading: Boolean = true,
    val unlocked: List<LibraryUnlockedAchievement> = emptyList()
)

private data class GameContentState(
    val isLoading: Boolean = true,
    val gameData: RetroAchievementGameData? = null
)

private data class UnlockedAchievementGameGroup(
    val gameTitle: String,
    val gamePath: String,
    val achievements: List<LibraryUnlockedAchievement>
)

private data class AccountUnlockedContentState(
    val isLoading: Boolean = true,
    val groups: List<UnlockedAchievementGameGroup> = emptyList()
)

@Composable
fun AchievementsHubScreen(
    onOpenGameAchievements: (String, String?) -> Unit,
    onOpenUnlockedAchievements: () -> Unit,
    onBackClick: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val retroState by RetroAchievementsStateManager.state.collectAsState()
    var username by rememberSaveable(retroState.storedUsername) { mutableStateOf(retroState.storedUsername.orEmpty()) }
    var password by rememberSaveable { mutableStateOf("") }
    val hubState by produceState(
        initialValue = HubContentState(),
        key1 = retroState.enabled,
        key2 = retroState.user?.username
    ) {
        value = HubContentState(isLoading = true)
        value = withContext(Dispatchers.IO) {
            if (!retroState.enabled || retroState.user == null) {
                HubContentState(isLoading = false)
            } else {
                HubContentState(
                    isLoading = false,
                    unlocked = runCatching { repository.loadUnlockedAchievementsFromLibrary() }.getOrDefault(emptyList())
                )
            }
        }
    }

    LaunchedEffect(Unit) {
        RetroAchievementsStateManager.initialize()
    }

    if (hubState.isLoading && retroState.user != null) {
        AchievementsHubSkeleton(onBackClick = onBackClick)
        return
    }

    val earnedGames = hubState.unlocked.map { it.gamePath }.distinct().size
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .verticalScroll(rememberScrollState())
            .padding(bottom = 28.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_title),
            subtitle = androidx.compose.ui.res.stringResource(R.string.achievements_subtitle),
            onBackClick = onBackClick
        )
        AchievementToggleCard(retroState = retroState)
        AchievementAccountCard(
            retroState = retroState,
            username = username,
            password = password,
            onUsernameChange = {
                username = it
                if (retroState.errorMessage != null || retroState.loginRequestReason != null) {
                    RetroAchievementsStateManager.clearTransientState()
                }
            },
            onPasswordChange = {
                password = it
                if (retroState.errorMessage != null || retroState.loginRequestReason != null) {
                    RetroAchievementsStateManager.clearTransientState()
                }
            },
            onLogin = {
                RetroAchievementsStateManager.login(username, password)
                password = ""
            },
            onOpenUnlockedAchievements = if (retroState.user != null) onOpenUnlockedAchievements else null
        )
        SummaryRow(
            firstLabel = androidx.compose.ui.res.stringResource(R.string.achievements_earned_total),
            firstValue = hubState.unlocked.size.toString(),
            secondLabel = androidx.compose.ui.res.stringResource(R.string.achievements_games_with_unlocks),
            secondValue = earnedGames.toString()
        )
        SectionTitle(
            text = androidx.compose.ui.res.stringResource(R.string.achievements_unlocked_section),
            topPadding = 2.dp,
            bottomPadding = 0.dp
        )
        when {
            !retroState.enabled -> {
                NoticeCard(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_empty_disabled), isError = false)
            }
            retroState.user == null -> {
                NoticeCard(text = androidx.compose.ui.res.stringResource(R.string.achievements_login_to_sync), isError = false)
            }
            hubState.unlocked.isEmpty() -> {
                CompactHintRow(text = androidx.compose.ui.res.stringResource(R.string.achievements_unlocked_empty))
            }
            else -> {
                Column(
                    modifier = Modifier.padding(horizontal = ScreenHorizontalPadding),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    hubState.unlocked.forEach { unlocked ->
                        LibraryUnlockedCard(
                            item = unlocked,
                            onClick = { onOpenGameAchievements(unlocked.gamePath, unlocked.gameTitle) }
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun AccountUnlockedAchievementsScreen(
    onOpenGameAchievements: (String, String?) -> Unit,
    onBackClick: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val retroState by RetroAchievementsStateManager.state.collectAsState()
    val contentState by produceState(
        initialValue = AccountUnlockedContentState(),
        key1 = retroState.enabled,
        key2 = retroState.user?.username
    ) {
        value = AccountUnlockedContentState(isLoading = true)
        value = withContext(Dispatchers.IO) {
            if (!retroState.enabled || retroState.user == null) {
                AccountUnlockedContentState(isLoading = false)
            } else {
                val unlocked = runCatching { repository.loadUnlockedAchievementsFromLibrary() }.getOrDefault(emptyList())
                AccountUnlockedContentState(
                    isLoading = false,
                    groups = unlocked
                        .groupBy { it.gamePath }
                        .values
                        .map { group ->
                            UnlockedAchievementGameGroup(
                                gameTitle = group.first().gameTitle,
                                gamePath = group.first().gamePath,
                                achievements = group.sortedBy { it.achievement.title.lowercase() }
                            )
                        }
                        .sortedBy { it.gameTitle.lowercase() }
                )
            }
        }
    }

    if (contentState.isLoading && retroState.user != null) {
        AchievementsHubSkeleton(onBackClick = onBackClick)
        return
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_account_unlocked_title),
            subtitle = retroState.user?.displayName
                ?: androidx.compose.ui.res.stringResource(R.string.achievements_account_unlocked_subtitle),
            onBackClick = onBackClick
        )
        when {
            !retroState.enabled -> {
                NoticeCard(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_empty_disabled), isError = false)
            }
            retroState.user == null -> {
                NoticeCard(text = androidx.compose.ui.res.stringResource(R.string.achievements_login_to_sync), isError = false)
            }
            contentState.groups.isEmpty() -> {
                CompactHintRow(text = androidx.compose.ui.res.stringResource(R.string.achievements_unlocked_empty))
            }
            else -> {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(
                        start = ScreenHorizontalPadding,
                        end = ScreenHorizontalPadding,
                        top = 0.dp,
                        bottom = 24.dp
                    ),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    items(contentState.groups, key = { it.gamePath }) { group ->
                        UnlockedGameGroupCard(
                            group = group,
                            onOpenGameAchievements = { onOpenGameAchievements(group.gamePath, group.gameTitle) }
                        )
                    }
                }
            }
        }
    }
}

@SuppressLint("FrequentlyChangingValue")
@Composable
fun GameAchievementsScreen(
    gamePath: String,
    gameTitle: String?,
    onBackClick: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val retroState by RetroAchievementsStateManager.state.collectAsState()
    val listState = rememberLazyListState()
    val scope = androidx.compose.runtime.rememberCoroutineScope()
    val showScrollToTop = listState.firstVisibleItemIndex > 2 || listState.firstVisibleItemScrollOffset > 900
    val contentState by produceState(
        initialValue = GameContentState(),
        key1 = gamePath,
        key2 = retroState.user?.username,
        key3 = retroState.enabled
    ) {
        value = GameContentState(isLoading = true)
        value = withContext(Dispatchers.IO) {
            GameContentState(
                isLoading = false,
                gameData = runCatching { repository.loadGameData(gamePath) }.getOrNull()
            )
        }
    }

    LaunchedEffect(Unit) {
        RetroAchievementsStateManager.initialize()
    }

    if (contentState.isLoading) {
        AchievementsGameSkeleton(onBackClick = onBackClick)
        return
    }

    val gameData = contentState.gameData
    val resolvedSubtitle = remember(context, gamePath, gameTitle, gameData?.title) {
        gameData?.title
            ?: gameTitle.takeIf { it.isUsableAchievementTitle() }
            ?: runCatching { DocumentPathResolver.getDisplayName(context, gamePath) }.getOrNull()
                ?.substringBeforeLast('.')
                ?.takeIf { it.isUsableAchievementTitle() }
            ?: File(gamePath).nameWithoutExtension.takeIf { it.isUsableAchievementTitle() }
            ?: ""
    }
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(bottom = 100.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                AchievementsTopBar(
                    title = androidx.compose.ui.res.stringResource(R.string.achievements_game_title),
                    subtitle = resolvedSubtitle,
                    onBackClick = onBackClick
                )
            }

            if (gameData == null) {
                item {
                    NoticeCard(
                        text = androidx.compose.ui.res.stringResource(R.string.achievements_game_unavailable),
                        isError = true
                    )
                }
            } else {
                item {
                    SummaryRow(
                        firstLabel = androidx.compose.ui.res.stringResource(R.string.achievements_earned_progress),
                        firstValue = "${gameData.earnedCount}/${gameData.totalCount}",
                        secondLabel = androidx.compose.ui.res.stringResource(R.string.achievements_points_progress),
                        secondValue = "${gameData.earnedPoints}/${gameData.totalPoints}"
                    )
                }

                if (retroState.user == null) {
                    item {
                        NoticeCard(
                            text = androidx.compose.ui.res.stringResource(R.string.achievements_game_logged_out_hint),
                            isError = false
                        )
                    }
                }

                if (gameData.achievements.isEmpty()) {
                    item {
                        NoticeCard(
                            text = androidx.compose.ui.res.stringResource(
                                if (gameData.resolvedOnly) {
                                    R.string.achievements_game_resolved_only
                                } else {
                                    R.string.achievements_game_empty
                                }
                            ),
                            isError = false
                        )
                    }
                } else {
                    items(gameData.achievements, key = { it.id }) { achievement ->
                        AchievementCard(
                            achievement = achievement,
                            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
                        )
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
                    listState.animateScrollToItem(0)
                }
            }
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AchievementsTopBar(title: String, subtitle: String, onBackClick: () -> Unit) {
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(start = ScreenHorizontalPadding, end = ScreenHorizontalPadding, top = topInset + 8.dp, bottom = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        NavigationBackButton(
            onClick = onBackClick,
            contentColor = MaterialTheme.colorScheme.onSurface
        )
        Column(modifier = Modifier.padding(start = 6.dp)) {
            Text(text = title, style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold), color = MaterialTheme.colorScheme.onBackground)
            Text(text = subtitle, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
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
                    contentDescription = androidx.compose.ui.res.stringResource(R.string.back),
                    tint = MaterialTheme.colorScheme.onSurface
                )
            }
        }
    }
}

@Composable
private fun AchievementToggleCard(retroState: RetroAchievementsUiState) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(modifier = Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_overview), style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold), color = MaterialTheme.colorScheme.onSurface)
            ToggleRow(
                icon = Icons.Rounded.Star,
                title = androidx.compose.ui.res.stringResource(R.string.settings_ra_enabled),
                subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_enabled_desc),
                checked = retroState.enabled,
                onCheckedChange = RetroAchievementsStateManager::setEnabled
            )
            ToggleRow(
                icon = Icons.Rounded.Speed,
                title = androidx.compose.ui.res.stringResource(R.string.settings_ra_hardcore),
                subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_hardcore_desc),
                checked = retroState.hardcorePreference,
                onCheckedChange = RetroAchievementsStateManager::setHardcore
            )
        }
    }
}

@Composable
private fun AchievementAccountCard(
    retroState: RetroAchievementsUiState,
    username: String,
    password: String,
    onUsernameChange: (String) -> Unit,
    onPasswordChange: (String) -> Unit,
    onLogin: () -> Unit,
    onOpenUnlockedAchievements: (() -> Unit)? = null
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(modifier = Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_profile), style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold), color = MaterialTheme.colorScheme.onSurface)
            retroState.loginRequestReason?.let { NoticeCard(text = androidx.compose.ui.res.stringResource(loginReasonString(it)), isError = it == RetroAchievementsLoginRequestReason.TOKEN_INVALID) }
            retroState.errorMessage?.let { NoticeCard(text = it, isError = true) }
            retroState.user?.let { user ->
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(60.dp)
                            .clip(RoundedCornerShape(18.dp))
                            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)),
                        contentAlignment = Alignment.Center
                    ) {
                        BitmapPathImage(
                            imagePath = user.avatarPath,
                            contentDescription = user.displayName,
                            modifier = Modifier
                                .size(60.dp)
                                .clip(RoundedCornerShape(18.dp)),
                            fallback = {
                                Icon(Icons.Rounded.Star, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                            }
                        )
                    }
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(2.dp)
                    ) {
                        Text(
                            text = user.displayName,
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = "@${user.username}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    if (retroState.isLoading) {
                        CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                    }
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 22.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    AccountStatCard(
                        value = user.points.toString(),
                        label = androidx.compose.ui.res.stringResource(R.string.settings_ra_points_label),
                        modifier = Modifier.weight(1f),
                    )
                    AccountStatCard(
                        value = user.softcorePoints.toString(),
                        label = androidx.compose.ui.res.stringResource(R.string.settings_ra_softcore_points_label),
                        modifier = Modifier.weight(1f),
                    )
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    ActionBadgeButton(
                        modifier = Modifier.weight(1f),
                        text = androidx.compose.ui.res.stringResource(R.string.settings_ra_refresh),
                        onClick = { RetroAchievementsStateManager.refreshState() }
                    )
                    ActionBadgeButton(
                        modifier = Modifier.weight(1f),
                        text = androidx.compose.ui.res.stringResource(R.string.settings_ra_logout),
                        onClick = { RetroAchievementsStateManager.logout() }
                    )
                    if (onOpenUnlockedAchievements != null) {
                        ActionBadgeButton(
                            modifier = Modifier.weight(1f),
                            text = androidx.compose.ui.res.stringResource(R.string.achievements_open_all_unlocked),
                            onClick = onOpenUnlockedAchievements
                        )
                    }
                }
            } ?: run {
                Text(text = androidx.compose.ui.res.stringResource(R.string.achievements_sign_in_hint), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                OutlinedTextField(
                    value = username,
                    onValueChange = onUsernameChange,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(18.dp),
                    singleLine = true,
                    label = { Text(androidx.compose.ui.res.stringResource(R.string.settings_ra_username)) }
                )
                OutlinedTextField(
                    value = password,
                    onValueChange = onPasswordChange,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(18.dp),
                    singleLine = true,
                    label = { Text(androidx.compose.ui.res.stringResource(R.string.settings_ra_password)) },
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password)
                )
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterHorizontally),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    ActionBadgeButton(
                        text = androidx.compose.ui.res.stringResource(R.string.settings_ra_refresh),
                        onClick = { RetroAchievementsStateManager.refreshState() }
                    )
                    ActionBadgeButton(
                        text = androidx.compose.ui.res.stringResource(R.string.settings_ra_login),
                        onClick = onLogin,
                        enabled = username.isNotBlank() && password.isNotBlank() && !retroState.isAuthenticating,
                        isLoading = retroState.isAuthenticating
                    )
                }
            }
        }
    }
}

@Composable
private fun ActionBadgeButton(
    modifier: Modifier = Modifier,
    text: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
    isLoading: Boolean = false
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(999.dp),
        color = if (enabled) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
        },
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        onClick = onClick,
        enabled = enabled && !isLoading
    ) {
        Box(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
            contentAlignment = Alignment.Center
        ) {
            if (isLoading) {
                CircularProgressIndicator(
                    modifier = Modifier.size(16.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.primary
                )
            } else {
                Text(
                    text = text,
                    style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun AccountStatCard(
    value: String,
    label: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                text = value,
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun UnlockedGameGroupCard(
    group: UnlockedAchievementGameGroup,
    onOpenGameAchievements: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Text(
                        text = group.gameTitle,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = androidx.compose.ui.res.stringResource(
                            R.string.achievements_account_group_summary,
                            group.achievements.size
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                TextButton(onClick = onOpenGameAchievements) {
                    Text(text = androidx.compose.ui.res.stringResource(R.string.achievements_open_game))
                }
            }
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                group.achievements.forEach { unlocked ->
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        AchievementBadge(
                            imagePath = unlocked.achievement.badgeUrl ?: unlocked.achievement.badgeLockedUrl,
                            earned = true
                        )
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = unlocked.achievement.title,
                                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium),
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                text = unlocked.achievement.description,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        MiniBadge(text = "${unlocked.achievement.points} ${androidx.compose.ui.res.stringResource(R.string.settings_ra_points_label)}")
                    }
                }
            }
        }
    }
}

@Composable
private fun ToggleRow(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(48.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
        }
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(start = 12.dp, end = 10.dp)
        ) {
            Text(text = title, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
            Text(text = subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun SummaryRow(
    firstLabel: String,
    firstValue: String,
    secondLabel: String,
    secondValue: String,
    compact: Boolean = false
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        SummaryCard(label = firstLabel, value = firstValue, modifier = Modifier.weight(1f), compact = compact)
        SummaryCard(label = secondLabel, value = secondValue, modifier = Modifier.weight(1f), compact = compact)
    }
}

@Composable
private fun SummaryCard(label: String, value: String, modifier: Modifier = Modifier, compact: Boolean = false) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = if (compact) 14.dp else 16.dp)) {
            Text(text = value, style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold), color = MaterialTheme.colorScheme.onSurface)
            Spacer(modifier = Modifier.height(4.dp))
            Text(text = label, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun SectionTitle(text: String, topPadding: androidx.compose.ui.unit.Dp = 12.dp, bottomPadding: androidx.compose.ui.unit.Dp = 12.dp) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
        color = MaterialTheme.colorScheme.onBackground,
        modifier = Modifier.padding(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topPadding,
            bottom = bottomPadding
        )
    )
}

@Composable
private fun NoticeCard(text: String, isError: Boolean) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = RoundedCornerShape(22.dp),
        color = if (isError) MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.4f)
        else MaterialTheme.colorScheme.surface
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = if (isError) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun CompactHintRow(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
    )
}

@Composable
private fun LibraryUnlockedCard(item: LibraryUnlockedAchievement, onClick: () -> Unit) {
    val cleanAchievementTitle = item.achievement.title.takeUnless { it.isTechnicalAchievementMessage() }
    val cleanAchievementDescription = item.achievement.description.takeUnless { it.isTechnicalAchievementMessage() }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        onClick = onClick
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(14.dp), verticalAlignment = Alignment.Top) {
                AchievementBadge(
                    imagePath = item.achievement.badgeUrl ?: item.achievement.badgeLockedUrl,
                    earned = true
                )
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                    Text(
                        text = item.gameTitle,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    cleanAchievementTitle?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.primary,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    cleanAchievementDescription?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 3,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                MiniBadge(text = "${item.achievement.points} ${androidx.compose.ui.res.stringResource(R.string.settings_ra_points_label)}")
                MiniBadge(text = androidx.compose.ui.res.stringResource(R.string.achievements_open_game))
            }
        }
    }
}

private fun String.isTechnicalAchievementMessage(): Boolean {
    val normalized = trim().lowercase()
    if (normalized.isBlank()) return true
    return normalized.startsWith("warning:") ||
        normalized.contains("outdated emulator") ||
        normalized.contains("hardcore unlocks cannot be earned")
}

@Composable
private fun AchievementCard(
    achievement: RetroAchievementEntry,
    modifier: Modifier = Modifier
) {
    val imagePath = if (achievement.isEarned) achievement.badgeUrl ?: achievement.badgeLockedUrl else achievement.badgeLockedUrl ?: achievement.badgeUrl
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(14.dp), verticalAlignment = Alignment.Top) {
                AchievementBadge(imagePath = imagePath, earned = achievement.isEarned)
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = achievement.title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = achievement.description,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                MiniBadge(text = "${achievement.points} ${androidx.compose.ui.res.stringResource(R.string.settings_ra_points_label)}")
                MiniBadge(text = if (achievement.isEarned) androidx.compose.ui.res.stringResource(R.string.achievements_status_earned) else androidx.compose.ui.res.stringResource(R.string.achievements_status_locked))
                if (achievement.earnedHardcore) {
                    MiniBadge(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_hardcore_badge))
                }
            }
        }
    }
}

@Composable
private fun AchievementBadge(imagePath: String?, earned: Boolean) {
    Box(
        modifier = Modifier
            .size(74.dp)
            .clip(RoundedCornerShape(20.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)),
        contentAlignment = Alignment.Center
    ) {
        BitmapPathImage(
            imagePath = imagePath,
            contentDescription = null,
            modifier = Modifier
                .size(74.dp)
                .clip(RoundedCornerShape(20.dp)),
            fallback = {
                Icon(
                    imageVector = if (earned) Icons.Rounded.Star else Icons.Rounded.LockOpen,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        )
    }
}

@Composable
private fun MiniBadge(text: String) {
    Surface(shape = RoundedCornerShape(999.dp), color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
private fun AchievementsHubSkeleton(onBackClick: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_title),
            subtitle = androidx.compose.ui.res.stringResource(R.string.achievements_subtitle),
            onBackClick = onBackClick
        )
        repeat(4) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding, vertical = 6.dp),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surface
            ) {
                Box(modifier = Modifier.height(if (it == 0) 132.dp else 104.dp).fillMaxWidth().shimmer())
            }
        }
    }
}

@Composable
private fun AchievementsGameSkeleton(onBackClick: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_game_title),
            subtitle = " ",
            onBackClick = onBackClick
        )
        repeat(5) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding, vertical = 6.dp),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surface
            ) {
                Box(modifier = Modifier.height(if (it == 0) 96.dp else 144.dp).fillMaxWidth().shimmer())
            }
        }
    }
}

private fun loginReasonString(reason: RetroAchievementsLoginRequestReason): Int {
    return when (reason) {
        RetroAchievementsLoginRequestReason.USER_INITIATED -> R.string.settings_ra_login_reason_user
        RetroAchievementsLoginRequestReason.TOKEN_INVALID -> R.string.settings_ra_login_reason_token
        RetroAchievementsLoginRequestReason.UNKNOWN -> R.string.settings_ra_login_reason_user
    }
}

private fun String?.isUsableAchievementTitle(): Boolean {
    if (this.isNullOrBlank()) return false
    val value = this.trim()
    return !value.startsWith("content://") &&
        !value.startsWith("primary%3A", ignoreCase = true) &&
        !value.contains("%2F", ignoreCase = true) &&
        !value.contains("%3A", ignoreCase = true)
}

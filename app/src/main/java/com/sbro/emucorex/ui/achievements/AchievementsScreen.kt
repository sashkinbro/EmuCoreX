package com.sbro.emucorex.ui.achievements

import android.annotation.SuppressLint
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.LockOpen
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.Notifications
import androidx.compose.material.icons.rounded.Timer
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.ui.graphics.Color
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.utils.RetroAchievementsBridge
import com.sbro.emucorex.core.utils.RetroAchievementsLoginRequestReason
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.core.utils.RetroAchievementsUiState
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.LibraryAchievementGame
import com.sbro.emucorex.data.LibraryUnlockedAchievement
import com.sbro.emucorex.data.RetroAchievementEntry
import com.sbro.emucorex.data.RetroAchievementGameData
import com.sbro.emucorex.data.RetroAchievementsRepository
import com.sbro.emucorex.ui.common.BitmapPathImage
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.gamepadFocusableCard
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.shimmer
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import kotlin.time.Duration.Companion.milliseconds
import com.sbro.emucorex.ui.theme.neon.neonShape

private data class HubContentState(
    val isLoading: Boolean = true,
    val unlocked: List<LibraryUnlockedAchievement> = emptyList(),
    val games: List<LibraryAchievementGame> = emptyList()
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
    val context = LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val preferences = remember(context) { AppPreferences(context) }
    val uiScope = rememberCoroutineScope()
    val retroState by RetroAchievementsStateManager.state.collectAsState()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    var username by rememberSaveable(retroState.storedUsername) { mutableStateOf(retroState.storedUsername.orEmpty()) }
    val initialRememberPassword = remember(preferences) { preferences.getAchievementsRememberPasswordSync() }
    var rememberPassword by rememberSaveable { mutableStateOf(initialRememberPassword) }
    var password by rememberSaveable {
        mutableStateOf(
            if (initialRememberPassword) {
                preferences.getAchievementsPasswordSync().orEmpty()
            } else {
                ""
            }
        )
    }
    val hubState by produceState(
        HubContentState(),
        retroState.enabled,
        retroState.user?.username,
        retroState.game?.gameId,
        retroState.game?.totalAchievements,
        retroState.sessionRevision
    ) {
        if (!retroState.enabled || retroState.user == null) {
            value = HubContentState(isLoading = false)
        } else {
            val cachedState = withContext(Dispatchers.IO) {
                HubContentState(
                    isLoading = false,
                    unlocked = runCatching { repository.peekCachedUnlockedAchievementsFromLibrary() }.getOrNull().orEmpty(),
                    games = runCatching {
                        repository.peekCachedAchievementGamesFromLibrary(
                            activeGameTitle = retroState.game?.title,
                            activeGameId = retroState.game?.gameId
                        )
                    }.getOrNull().orEmpty()
                )
            }
            value = if (cachedState.games.isNotEmpty() || cachedState.unlocked.isNotEmpty()) {
                cachedState
            } else {
                HubContentState(isLoading = true)
            }
            value = withContext(Dispatchers.IO) {
                HubContentState(
                    isLoading = false,
                    unlocked = runCatching { repository.peekCachedUnlockedAchievementsFromLibrary() }.getOrNull().orEmpty(),
                    games = runCatching {
                        repository.loadAchievementGamesFromLibrary(
                            activeGameTitle = retroState.game?.title,
                            activeGameId = retroState.game?.gameId
                        )
                    }.getOrNull().orEmpty()
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

    val earnedGames = hubState.games.size
    val totalGameAchievements = hubState.games.sumOf { it.gameData.totalCount }
    val earnedGameAchievements = hubState.games.sumOf { it.gameData.earnedCount }
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
            .verticalScroll(rememberScrollState())
            .padding(bottom = 28.dp + bottomInset),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_title),
            subtitle = "",
            onBackClick = onBackClick,
            actions = {
                if (retroState.user != null) {
                    IconButton(onClick = { RetroAchievementsStateManager.refreshState() }) {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = androidx.compose.ui.res.stringResource(R.string.settings_ra_refresh),
                            tint = MaterialTheme.colorScheme.primary
                        )
                    }
                    IconButton(onClick = { RetroAchievementsStateManager.logout() }) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Rounded.Logout,
                            contentDescription = androidx.compose.ui.res.stringResource(R.string.settings_ra_logout),
                            tint = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }
        )
        AchievementToggleCard(retroState = retroState)
        AchievementAccountCard(
            retroState = retroState,
            username = username,
            password = password,
            rememberPassword = rememberPassword,
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
            onRememberPasswordChange = { remember ->
                rememberPassword = remember
                uiScope.launch {
                    preferences.setAchievementsRememberPassword(remember)
                }
            },
            onLogin = {
                RetroAchievementsStateManager.login(username, password, rememberPassword)
                if (!rememberPassword) {
                    password = ""
                }
            },
            onOpenUnlockedAchievements = if (retroState.user != null) onOpenUnlockedAchievements else null
        )
        SummaryRow(
            firstLabel = androidx.compose.ui.res.stringResource(R.string.achievements_earned_total),
            firstValue = if (totalGameAchievements > 0) "$earnedGameAchievements/$totalGameAchievements" else hubState.unlocked.size.toString(),
            secondLabel = androidx.compose.ui.res.stringResource(R.string.achievements_games_with_unlocks),
            secondValue = earnedGames.toString()
        )
        SectionTitle(
            text = androidx.compose.ui.res.stringResource(R.string.achievements_library_games_section),
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
            hubState.games.isEmpty() -> {
                CompactHintRow(text = androidx.compose.ui.res.stringResource(R.string.achievements_library_games_empty))
            }
            else -> {
                Column(
                    modifier = Modifier.padding(horizontal = ScreenHorizontalPadding),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    hubState.games.forEach { game ->
                        LibraryAchievementGameCard(
                            item = game,
                            onClick = {
                                if (game.gamePath.isNotBlank()) {
                                    onOpenGameAchievements(game.gamePath, game.gameTitle)
                                }
                            }
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
    val context = LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val retroState by RetroAchievementsStateManager.state.collectAsState()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val contentState by produceState(
        initialValue = AccountUnlockedContentState(),
        key1 = retroState.enabled,
        key2 = "${retroState.user?.username.orEmpty()}|${retroState.sessionRevision}"
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
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding),
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
                        bottom = 24.dp + bottomInset
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
    val context = LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val retroState by RetroAchievementsStateManager.state.collectAsState()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val showScrollToTop = listState.firstVisibleItemIndex > 2 || listState.firstVisibleItemScrollOffset > 900
    val activeRetroGame = retroState.game
    val contentState by produceState(
        GameContentState(),
        gamePath,
        retroState.user?.username,
        retroState.enabled,
        activeRetroGame?.gameId,
        activeRetroGame?.title,
        activeRetroGame?.totalAchievements
    ) {
        value = GameContentState(isLoading = true)
        value = withContext(Dispatchers.IO) {
            val activeData = if (
                retroState.enabled &&
                isRetroAchievementsActiveGameRequest(
                    context = context,
                    gamePath = gamePath,
                    gameTitle = gameTitle,
                    activeTitle = activeRetroGame?.title
                )
            ) {
                loadActiveRetroAchievementsGameDataWithRetry(repository)
            } else {
                null
            }
            GameContentState(
                isLoading = false,
                gameData = activeData
                    ?: runCatching { repository.loadGameData(gamePath) }.getOrNull()
                    ?: gameTitle?.let { title ->
                        runCatching { repository.loadGameData(title) }.getOrNull()
                    }
            )
        }
    }

    LaunchedEffect(gamePath, retroState.enabled) {
        RetroAchievementsStateManager.initialize()
        if (retroState.enabled) {
            RetroAchievementsStateManager.refreshState(invalidateCaches = false)
        }
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
            .padding(horizontalSystemBarPadding)
    ) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(bottom = 100.dp + bottomInset),
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
                .padding(end = 16.dp, bottom = 16.dp + bottomInset),
            onClick = {
                scope.launch {
                    listState.animateScrollToItem(0)
                }
            }
        )
    }
}

private suspend fun loadActiveRetroAchievementsGameDataWithRetry(
    repository: RetroAchievementsRepository
): RetroAchievementGameData? {
    repeat(8) { attempt ->
        val data = runCatching { repository.loadActiveGameData() }.getOrNull()
        if (data != null && (data.achievements.isNotEmpty() || data.totalCount > 0)) {
            return data
        }
        if (attempt < 7) {
            delay(500.milliseconds)
            RetroAchievementsStateManager.refreshState(invalidateCaches = false)
        }
    }
    return null
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AchievementsTopBar(
    title: String,
    subtitle: String,
    onBackClick: () -> Unit,
    actions: @Composable RowScope.() -> Unit = {}
) {
    val topInset = appScreenTopPadding()
    ScreenTopBar(
        title = title,
        subtitle = subtitle.takeIf { it.isNotEmpty() },
        onBackClick = onBackClick,
        backContentColor = MaterialTheme.colorScheme.onSurface,
        modifier = Modifier.padding(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = 12.dp
        ),
        actions = actions
    )
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
            shape = neonShape(20.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f),
            tonalElevation = 6.dp,
            shadowElevation = 6.dp,
            onClick = onClick
        ) {
            Box(
                modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.KeyboardArrowUp,
                    contentDescription = androidx.compose.ui.res.stringResource(R.string.back),
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}

@Composable
private fun AchievementToggleCard(retroState: RetroAchievementsUiState) {
    var showDisplaySettings by rememberSaveable { mutableStateOf(false) }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
        )
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_overview),
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Surface(
                    onClick = { showDisplaySettings = true },
                    modifier = Modifier.size(48.dp),
                    shape = neonShape(14.dp),
                    color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.42f)
                ) {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.MoreVert,
                            contentDescription = androidx.compose.ui.res.stringResource(R.string.settings_more_options),
                            tint = MaterialTheme.colorScheme.primary
                        )
                    }
                }
            }
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
            if (retroState.hardcorePreference && !retroState.hardcoreActive) {
                NoticeCard(
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_hardcore_pending_desc),
                    isError = false
                )
            }
        }
    }

    if (showDisplaySettings) {
        AchievementDisplaySettingsDialog(
            retroState = retroState,
            onDismiss = { showDisplaySettings = false }
        )
    }
}

@Composable
private fun AchievementDisplaySettingsDialog(
    retroState: RetroAchievementsUiState,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var soundImportError by remember { mutableStateOf<AchievementSoundImportError?>(null) }
    val soundPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        soundImportError = null
        scope.launch {
            when (val result = withContext(Dispatchers.IO) { importAchievementUnlockSound(context, uri) }) {
                is AchievementSoundImportResult.Success -> {
                    RetroAchievementsStateManager.setUnlockSound(result.path, result.displayName)
                    runCatching { RetroAchievementsBridge.nativePreviewSound(result.path) }
                }
                is AchievementSoundImportResult.Error -> soundImportError = result.reason
            }
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(14.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth(0.94f)
                    .widthIn(max = 580.dp)
                    .heightIn(max = maxHeight),
                shape = neonShape(28.dp),
                color = MaterialTheme.colorScheme.surface,
                border = androidx.compose.foundation.BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
                ),
                tonalElevation = 0.dp,
                shadowElevation = 14.dp
            ) {
                Column(
                    modifier = Modifier
                        .verticalScroll(rememberScrollState())
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Surface(
                            modifier = Modifier.size(52.dp),
                            shape = neonShape(17.dp),
                            color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    imageVector = Icons.Rounded.Visibility,
                                    contentDescription = null,
                                    modifier = Modifier.size(26.dp),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                        }
                        Spacer(modifier = Modifier.width(14.dp))
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(2.dp)
                        ) {
                            Text(
                                text = androidx.compose.ui.res.stringResource(R.string.settings_ra_overview),
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Text(
                                text = androidx.compose.ui.res.stringResource(R.string.settings_title),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))

                    ToggleRow(
                        icon = Icons.Rounded.Notifications,
                        title = androidx.compose.ui.res.stringResource(R.string.settings_ra_achievement_notifications),
                        subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_achievement_notifications_desc),
                        checked = retroState.notifications,
                        onCheckedChange = RetroAchievementsStateManager::setNotifications
                    )
                    ToggleRow(
                        icon = Icons.Rounded.EmojiEvents,
                        title = androidx.compose.ui.res.stringResource(R.string.settings_ra_leaderboard_notifications),
                        subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_leaderboard_notifications_desc),
                        checked = retroState.leaderboardNotifications,
                        onCheckedChange = RetroAchievementsStateManager::setLeaderboardNotifications
                    )
                    ToggleRow(
                        icon = Icons.Rounded.Visibility,
                        title = androidx.compose.ui.res.stringResource(R.string.settings_ra_achievement_indicators),
                        subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_achievement_indicators_desc),
                        checked = retroState.achievementIndicators,
                        onCheckedChange = RetroAchievementsStateManager::setAchievementIndicators
                    )
                    ToggleRow(
                        icon = Icons.Rounded.Timer,
                        title = androidx.compose.ui.res.stringResource(R.string.settings_ra_leaderboard_trackers),
                        subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_leaderboard_trackers_desc),
                        checked = retroState.leaderboardTrackers,
                        onCheckedChange = RetroAchievementsStateManager::setLeaderboardTrackers
                    )
                    ToggleRow(
                        icon = Icons.AutoMirrored.Rounded.VolumeUp,
                        title = androidx.compose.ui.res.stringResource(R.string.settings_ra_sound_effects),
                        subtitle = androidx.compose.ui.res.stringResource(R.string.settings_ra_sound_effects_desc),
                        checked = retroState.soundEffects,
                        onCheckedChange = RetroAchievementsStateManager::setSoundEffects
                    )

                    AchievementUnlockSoundRow(
                        currentName = retroState.unlockSoundName,
                        onSelect = {
                            soundImportError = null
                            soundPicker.launch(arrayOf("audio/wav", "audio/x-wav", "audio/wave"))
                        },
                        onReset = {
                            soundImportError = null
                            runCatching { achievementUnlockSoundFile(context).delete() }
                            RetroAchievementsStateManager.setUnlockSound(null, null)
                        }
                    )

                    soundImportError?.let { error ->
                        NoticeCard(
                            text = androidx.compose.ui.res.stringResource(
                                when (error) {
                                    AchievementSoundImportError.INVALID_WAV -> R.string.settings_ra_unlock_sound_invalid
                                    AchievementSoundImportError.TOO_LARGE -> R.string.settings_ra_unlock_sound_too_large
                                    AchievementSoundImportError.IMPORT_FAILED -> R.string.settings_ra_unlock_sound_failed
                                }
                            ),
                            isError = true
                        )
                    }

                    Button(
                        onClick = onDismiss,
                        modifier = Modifier.fillMaxWidth(),
                        shape = neonShape(18.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.primary,
                            contentColor = MaterialTheme.colorScheme.onPrimary
                        )
                    ) {
                        Text(
                            text = androidx.compose.ui.res.stringResource(R.string.close),
                            modifier = Modifier.padding(vertical = 5.dp),
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun AchievementUnlockSoundRow(
    currentName: String?,
    onSelect: () -> Unit,
    onReset: () -> Unit
) {
    Surface(
        onClick = onSelect,
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
        )
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(48.dp),
                shape = neonShape(15.dp),
                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.72f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.MusicNote,
                        contentDescription = null,
                        modifier = Modifier.size(24.dp),
                        tint = MaterialTheme.colorScheme.primary
                    )
                }
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(3.dp)
            ) {
                Text(
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_unlock_sound),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_unlock_sound_desc),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = currentName
                        ?: androidx.compose.ui.res.stringResource(R.string.settings_ra_unlock_sound_default),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            if (currentName != null) {
                TextButton(onClick = onReset) {
                    Text(androidx.compose.ui.res.stringResource(R.string.controls_editor_reset))
                }
            }
        }
    }
}

private const val MAX_ACHIEVEMENT_SOUND_BYTES = 16L * 1024L * 1024L

private enum class AchievementSoundImportError {
    INVALID_WAV,
    TOO_LARGE,
    IMPORT_FAILED
}

private class AchievementSoundTooLargeException : Exception()

private sealed interface AchievementSoundImportResult {
    data class Success(val path: String, val displayName: String) : AchievementSoundImportResult
    data class Error(val reason: AchievementSoundImportError) : AchievementSoundImportResult
}

private fun achievementUnlockSoundFile(context: Context): File =
    File(File(context.filesDir, "achievements"), "unlock_sound.wav")

private fun importAchievementUnlockSound(context: Context, uri: Uri): AchievementSoundImportResult {
    val resolver = context.contentResolver
    val header = ByteArray(12)
    val headerRead = runCatching {
        resolver.openInputStream(uri)?.use { input ->
            var offset = 0
            while (offset < header.size) {
                val count = input.read(header, offset, header.size - offset)
                if (count <= 0) break
                offset += count
            }
            offset
        } ?: 0
    }.getOrDefault(0)
    if (headerRead != header.size || !isValidAchievementWavHeader(header)) {
        return AchievementSoundImportResult.Error(AchievementSoundImportError.INVALID_WAV)
    }

    val target = achievementUnlockSoundFile(context)
    val temp = File(target.parentFile, "${target.name}.tmp")
    val backup = File(target.parentFile, "${target.name}.bak")
    var backupCreated = false
    return runCatching {
        target.parentFile?.mkdirs()
        check(!temp.exists() || temp.delete())
        check(!backup.exists() || backup.delete())
        resolver.openInputStream(uri)?.use { input ->
            FileOutputStream(temp).use { output ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                var total = 0L
                while (true) {
                    val count = input.read(buffer)
                    if (count <= 0) break
                    total += count
                    if (!isAchievementSoundSizeAllowed(total)) throw AchievementSoundTooLargeException()
                    output.write(buffer, 0, count)
                }
                output.fd.sync()
            }
        } ?: error("Unable to open selected sound")

        check(temp.length() >= header.size)
        if (target.exists()) {
            check(target.renameTo(backup))
            backupCreated = true
        }
        check(temp.renameTo(target))
        if (backupCreated) {
            backup.delete()
            backupCreated = false
        }

        val displayName = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            }
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
            ?.take(80)
            ?: target.name
        AchievementSoundImportResult.Success(target.absolutePath, displayName)
    }.getOrElse { error ->
        temp.delete()
        if (backupCreated && backup.exists()) {
            target.delete()
            backup.renameTo(target)
        }
        AchievementSoundImportResult.Error(
            if (error is AchievementSoundTooLargeException) {
                AchievementSoundImportError.TOO_LARGE
            } else {
                AchievementSoundImportError.IMPORT_FAILED
            }
        )
    }
}

internal fun isValidAchievementWavHeader(header: ByteArray): Boolean {
    if (header.size < 12) return false
    return header[0] == 'R'.code.toByte() &&
        header[1] == 'I'.code.toByte() &&
        header[2] == 'F'.code.toByte() &&
        header[3] == 'F'.code.toByte() &&
        header[8] == 'W'.code.toByte() &&
        header[9] == 'A'.code.toByte() &&
        header[10] == 'V'.code.toByte() &&
        header[11] == 'E'.code.toByte()
}

internal fun isAchievementSoundSizeAllowed(byteCount: Long): Boolean =
    byteCount in 0..MAX_ACHIEVEMENT_SOUND_BYTES

@Composable
private fun AchievementAccountCard(
    retroState: RetroAchievementsUiState,
    username: String,
    password: String,
    rememberPassword: Boolean,
    onUsernameChange: (String) -> Unit,
    onPasswordChange: (String) -> Unit,
    onRememberPasswordChange: (Boolean) -> Unit,
    onLogin: () -> Unit,
    onOpenUnlockedAchievements: (() -> Unit)? = null
) {
    val hardcorePending = retroState.hardcorePreference && !retroState.hardcoreActive
    val hardcoreStatusColor = when {
        retroState.hardcoreActive -> Color(0xFFF27121)
        hardcorePending -> Color(0xFFFFB020)
        else -> MaterialTheme.colorScheme.primary
    }
    val hardcoreStatusText = androidx.compose.ui.res.stringResource(
        when {
            retroState.hardcoreActive -> R.string.settings_ra_hardcore_active
            hardcorePending -> R.string.settings_ra_hardcore_pending
            else -> R.string.settings_ra_softcore_active
        }
    )
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
        )
    ) {
        Column(
            modifier = Modifier
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_profile),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                if (retroState.user != null) {
                    Surface(
                        shape = RoundedCornerShape(99.dp),
                        color = hardcoreStatusColor.copy(alpha = 0.2f)
                    ) {
                        Text(
                            text = hardcoreStatusText,
                            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                            color = hardcoreStatusColor,
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp)
                        )
                    }
                }
            }

            retroState.loginRequestReason?.let {
                NoticeCard(
                    text = androidx.compose.ui.res.stringResource(loginReasonString(it)),
                    isError = it == RetroAchievementsLoginRequestReason.TOKEN_INVALID
                )
            }
            retroState.errorMessage?.let { NoticeCard(text = it, isError = true) }
            if (retroState.user != null && hardcorePending) {
                NoticeCard(
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_hardcore_pending_desc),
                    isError = false
                )
            }

            retroState.user?.let { user ->
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(76.dp)
                            .clip(neonShape(22.dp))
                            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f))
                            .border(
                                width = 2.dp,
                                brush = androidx.compose.ui.graphics.Brush.linearGradient(
                                    colors = if (retroState.hardcorePreference) {
                                        listOf(hardcoreStatusColor, Color(0xFFF27121))
                                    } else {
                                        listOf(MaterialTheme.colorScheme.primary, MaterialTheme.colorScheme.tertiary)
                                    }
                                ),
                                shape = neonShape(22.dp)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        BitmapPathImage(
                            imagePath = user.avatarPath,
                            contentDescription = user.displayName,
                            modifier = Modifier
                                .size(72.dp)
                                .clip(neonShape(20.dp)),
                            fallback = {
                                Icon(
                                    imageVector = Icons.Rounded.Person,
                                    contentDescription = null,
                                    modifier = Modifier.size(36.dp),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                        )
                    }
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(2.dp)
                    ) {
                        Text(
                            text = user.displayName,
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = "@${user.username}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    AccountStatCard(
                        value = user.points.toString(),
                        label = androidx.compose.ui.res.stringResource(R.string.settings_ra_points_label),
                        icon = Icons.Rounded.Star,
                        modifier = Modifier.weight(1f),
                    )
                    AccountStatCard(
                        value = user.softcorePoints.toString(),
                        label = androidx.compose.ui.res.stringResource(R.string.settings_ra_softcore_points_label),
                        icon = Icons.Rounded.EmojiEvents,
                        modifier = Modifier.weight(1f),
                    )
                }
                if (onOpenUnlockedAchievements != null) {
                    ActionBadgeButton(
                        modifier = Modifier.fillMaxWidth(),
                        text = androidx.compose.ui.res.stringResource(R.string.achievements_open_all_unlocked),
                        icon = Icons.Rounded.EmojiEvents,
                        onClick = onOpenUnlockedAchievements
                    )
                }
            } ?: run {
                OutlinedTextField(
                    value = username,
                    onValueChange = onUsernameChange,
                    modifier = Modifier.fillMaxWidth(),
                    shape = neonShape(16.dp),
                    singleLine = true,
                    leadingIcon = {
                        Icon(
                            imageVector = Icons.Rounded.Person,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                    },
                    label = { Text(androidx.compose.ui.res.stringResource(R.string.settings_ra_username)) },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.primary,
                        unfocusedBorderColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
                    )
                )
                OutlinedTextField(
                    value = password,
                    onValueChange = onPasswordChange,
                    modifier = Modifier.fillMaxWidth(),
                    shape = neonShape(16.dp),
                    singleLine = true,
                    leadingIcon = {
                        Icon(
                            imageVector = Icons.Rounded.Lock,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                    },
                    label = { Text(androidx.compose.ui.res.stringResource(R.string.settings_ra_password)) },
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.primary,
                        unfocusedBorderColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
                    )
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = androidx.compose.ui.res.stringResource(R.string.settings_ra_remember_password),
                        style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Switch(
                        checked = rememberPassword,
                        onCheckedChange = onRememberPasswordChange
                    )
                }
                ActionBadgeButton(
                    modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
                    text = androidx.compose.ui.res.stringResource(R.string.settings_ra_login),
                    onClick = onLogin,
                    enabled = username.isNotBlank() && password.isNotBlank() && !retroState.isAuthenticating,
                    isLoading = retroState.isAuthenticating,
                    isPrimary = true
                )
            }
        }
    }
}

@Composable
private fun ActionBadgeButton(
    modifier: Modifier = Modifier,
    text: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector? = null,
    onClick: () -> Unit,
    enabled: Boolean = true,
    isLoading: Boolean = false,
    isPrimary: Boolean = false
) {
    Surface(
        modifier = modifier.height(54.dp),
        shape = neonShape(16.dp),
        color = if (isPrimary && enabled) {
            MaterialTheme.colorScheme.primary
        } else if (isPrimary) {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.72f)
        } else if (enabled) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
        },
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
        onClick = onClick,
        enabled = enabled && !isLoading
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            if (isLoading) {
                CircularProgressIndicator(
                    modifier = Modifier.size(16.dp),
                    strokeWidth = 2.dp,
                    color = if (isPrimary && enabled) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.primary
                )
            } else {
                val contentColor = when {
                    isPrimary && enabled -> MaterialTheme.colorScheme.onPrimary
                    enabled -> MaterialTheme.colorScheme.primary
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                }
                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (icon != null) {
                        Icon(
                            imageVector = icon,
                            contentDescription = null,
                            modifier = Modifier.size(16.dp),
                            tint = contentColor
                        )
                    }
                    Text(
                        text = text,
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                        color = contentColor
                    )
                }
            }
        }
    }
}

@Composable
private fun AccountStatCard(
    value: String,
    label: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f)
        )
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(16.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Medium),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Text(
                text = value,
                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Black),
                color = MaterialTheme.colorScheme.onSurface
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
        shape = neonShape(28.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
        )
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
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
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
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
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                group.achievements.forEach { unlocked ->
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(14.dp),
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
                                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                text = unlocked.achievement.description,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        MiniBadge(text = "${unlocked.achievement.points} pts")
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
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(16.dp)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = { onCheckedChange(!checked) }
            )
            .gamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            )
            .padding(horizontal = 10.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(48.dp)
                .clip(neonShape(16.dp))
                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
        }
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(start = 14.dp, end = 10.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Switch(checked = checked, onCheckedChange = null)
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
        shape = neonShape(26.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
        )
    ) {
        Column(modifier = Modifier.padding(horizontal = 20.dp, vertical = if (compact) 16.dp else 20.dp)) {
            Text(
                text = value,
                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Black),
                color = MaterialTheme.colorScheme.onSurface
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun SectionTitle(text: String, topPadding: androidx.compose.ui.unit.Dp = 12.dp, bottomPadding: androidx.compose.ui.unit.Dp = 12.dp) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
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
        shape = neonShape(20.dp),
        color = if (isError) {
            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.25f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
        },
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            if (isError) MaterialTheme.colorScheme.error.copy(alpha = 0.2f) else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
        )
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 18.dp, vertical = 14.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = if (isError) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun CompactHintRow(text: String) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding, vertical = 22.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center
        )
    }
}

@Composable
private fun LibraryAchievementGameCard(item: LibraryAchievementGame, onClick: () -> Unit) {
    val imagePath = item.coverArtPath ?: item.gameData.gameImageUrl
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(72.dp)
                    .clip(neonShape(18.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)),
                contentAlignment = Alignment.Center
            ) {
                BitmapPathImage(
                    imagePath = imagePath,
                    contentDescription = item.gameTitle,
                    modifier = Modifier
                        .size(72.dp)
                        .clip(neonShape(18.dp)),
                    fallback = {
                        Icon(
                            imageVector = Icons.Rounded.EmojiEvents,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(30.dp)
                        )
                    }
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    text = item.gameData.title.ifBlank { item.gameTitle },
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                item.serial?.takeIf { it.isNotBlank() }?.let { serial ->
                    Text(
                        text = serial,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    MiniBadge(text = "${item.gameData.earnedCount}/${item.gameData.totalCount}")
                    MiniBadge(text = "${item.gameData.earnedPoints}/${item.gameData.totalPoints} pts")
                }
            }
            Icon(
                imageVector = Icons.Rounded.EmojiEvents,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(22.dp)
            )
        }
    }
}

@Composable
private fun AchievementCard(
    achievement: RetroAchievementEntry,
    modifier: Modifier = Modifier
) {
    val imagePath = if (achievement.isEarned) achievement.badgeUrl ?: achievement.badgeLockedUrl else achievement.badgeLockedUrl ?: achievement.badgeUrl
    val cardBorderColor = if (achievement.isEarned) {
        if (achievement.earnedHardcore) Color(0xFFF27121).copy(alpha = 0.35f) else MaterialTheme.colorScheme.primary.copy(alpha = 0.25f)
    } else {
        MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
    }

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(26.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(1.dp, cardBorderColor)
    ) {
        Row(
            modifier = Modifier.padding(18.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            AchievementBadge(imagePath = imagePath, earned = achievement.isEarned)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = achievement.title,
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontWeight = FontWeight.Bold,
                        color = if (achievement.isEarned) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
                    ),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = achievement.description,
                    style = MaterialTheme.typography.bodyMedium.copy(
                        color = if (achievement.isEarned) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                    ),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
                if (achievement.hasMeasuredProgress) {
                    Text(
                        text = androidx.compose.ui.res.stringResource(
                            R.string.achievements_measured_progress,
                            achievement.measuredProgress.orEmpty()
                        ),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
                Row(
                    modifier = Modifier.padding(top = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    MiniBadge(text = "${achievement.points} pts")
                    if (achievement.isPrimed) {
                        MiniBadge(
                            text = androidx.compose.ui.res.stringResource(R.string.achievements_status_primed),
                            isHardcore = true
                        )
                    } else if (achievement.isEarned) {
                        MiniBadge(
                            text = if (achievement.earnedHardcore) "HARDCORE" else "EARNED",
                            isHardcore = achievement.earnedHardcore
                        )
                    } else {
                        MiniBadge(text = "LOCKED", isLocked = true)
                    }
                }
            }
        }
    }
}

@Composable
private fun AchievementBadge(imagePath: String?, earned: Boolean) {
    Box(
        modifier = Modifier
            .size(76.dp)
            .clip(neonShape(20.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f))
            .border(
                width = if (earned) 1.dp else 0.dp,
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f),
                shape = neonShape(20.dp)
            ),
        contentAlignment = Alignment.Center
    ) {
        BitmapPathImage(
            imagePath = imagePath,
            contentDescription = null,
            modifier = Modifier
                .size(76.dp)
                .clip(neonShape(20.dp)),
            fallback = {
                Icon(
                    imageVector = if (earned) Icons.Rounded.Star else Icons.Rounded.LockOpen,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary.copy(alpha = if (earned) 1f else 0.4f)
                )
            }
        )
    }
}

@Composable
private fun MiniBadge(
    text: String,
    isHardcore: Boolean = false,
    isLocked: Boolean = false
) {
    val bgColor = when {
        isHardcore -> Color(0xFFF27121).copy(alpha = 0.12f)
        isLocked -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f)
        else -> MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    }

    val textColor = when {
        isHardcore -> Color(0xFFF27121)
        isLocked -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
        else -> MaterialTheme.colorScheme.primary
    }

    Surface(
        shape = neonShape(8.dp),
        color = bgColor
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
            color = textColor
        )
    }
}

@Composable
private fun AchievementsHubSkeleton(onBackClick: () -> Unit) {
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_title),
            subtitle = "",
            onBackClick = onBackClick
        )
        repeat(4) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding, vertical = 6.dp),
                shape = neonShape(24.dp),
                color = MaterialTheme.colorScheme.surface
            ) {
                Box(modifier = Modifier.height(if (it == 0) 132.dp else 104.dp).fillMaxWidth().shimmer())
            }
        }
    }
}

@Composable
private fun AchievementsGameSkeleton(onBackClick: () -> Unit) {
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
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
                shape = neonShape(24.dp),
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

private fun isRetroAchievementsActiveGameRequest(
    context: android.content.Context,
    gamePath: String,
    gameTitle: String?,
    activeTitle: String?
): Boolean {
    val activeKey = activeTitle.toAchievementScreenTitleKey()
    if (activeKey.isBlank()) return false

    val requestedKeys = buildSet {
        add(gameTitle.toAchievementScreenTitleKey())
        add(gamePath.toAchievementScreenTitleKey())
        if (gamePath.startsWith("content://")) {
            runCatching { DocumentPathResolver.getDisplayName(context, gamePath) }.getOrNull()
                ?.substringBeforeLast('.')
                ?.let { add(it.toAchievementScreenTitleKey()) }
        } else {
            File(gamePath).nameWithoutExtension
                .takeIf { it.isNotBlank() }
                ?.let { add(it.toAchievementScreenTitleKey()) }
        }
    }.filter { it.isNotBlank() }

    return requestedKeys.any { key ->
        key == activeKey ||
            (key.length >= 6 && activeKey.length >= 6 && (key.contains(activeKey) || activeKey.contains(key)))
    }
}

private fun String?.toAchievementScreenTitleKey(): String {
    if (this.isNullOrBlank()) return ""
    return lowercase()
        .replace(Regex("\\([^)]*\\)"), "")
        .substringBeforeLast('.')
        .replace(Regex("[^a-z0-9]+"), "")
}

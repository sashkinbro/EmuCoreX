package com.sbro.emucorex.ui.achievements

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
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

@Composable
fun AchievementsHubScreen(
    onOpenGameAchievements: (String, String?) -> Unit,
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
                    unlocked = repository.loadUnlockedAchievementsFromLibrary()
                )
            }
        }
    }

    LaunchedEffect(Unit) {
        RetroAchievementsStateManager.initialize()
        RetroAchievementsStateManager.refreshState()
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
            }
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
fun GameAchievementsScreen(
    gamePath: String,
    gameTitle: String?,
    onBackClick: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember(context) { RetroAchievementsRepository(context) }
    val retroState by RetroAchievementsStateManager.state.collectAsState()
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
                gameData = repository.loadGameData(gamePath)
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
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        AchievementsTopBar(
            title = androidx.compose.ui.res.stringResource(R.string.achievements_game_title),
            subtitle = resolvedSubtitle,
            onBackClick = onBackClick
        )
        if (gameData == null) {
            NoticeCard(
                text = androidx.compose.ui.res.stringResource(R.string.achievements_game_unavailable),
                isError = true
            )
            return@Column
        }
        SummaryRow(
            firstLabel = androidx.compose.ui.res.stringResource(R.string.achievements_earned_progress),
            firstValue = "${gameData.earnedCount}/${gameData.totalCount}",
            secondLabel = androidx.compose.ui.res.stringResource(R.string.achievements_points_progress),
            secondValue = "${gameData.earnedPoints}/${gameData.totalPoints}"
        )
        if (retroState.user == null) {
            NoticeCard(
                text = androidx.compose.ui.res.stringResource(R.string.achievements_game_logged_out_hint),
                isError = false
            )
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = 0.dp,
                bottom = 24.dp
            ),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            items(gameData.achievements, key = { it.id }) { achievement ->
                AchievementCard(achievement = achievement)
            }
        }
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
    onLogin: () -> Unit
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
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
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
                    Column(modifier = Modifier.weight(1f)) {
                        Text(text = user.displayName, style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold), color = MaterialTheme.colorScheme.onSurface)
                        Text(text = "@${user.username}", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    if (retroState.isLoading) {
                        CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                    }
                }
                SummaryRow(
                    firstLabel = androidx.compose.ui.res.stringResource(R.string.settings_ra_points_label),
                    firstValue = user.points.toString(),
                    secondLabel = androidx.compose.ui.res.stringResource(R.string.settings_ra_softcore_points_label),
                    secondValue = user.softcorePoints.toString(),
                    compact = true
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { RetroAchievementsStateManager.refreshState() }) {
                        Text(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_refresh))
                    }
                    TextButton(onClick = { RetroAchievementsStateManager.logout() }) {
                        Text(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_logout))
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
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { RetroAchievementsStateManager.refreshState() }) {
                        Text(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_refresh))
                    }
                    TextButton(
                        onClick = onLogin,
                        enabled = username.isNotBlank() && password.isNotBlank() && !retroState.isAuthenticating
                    ) {
                        if (retroState.isAuthenticating) {
                            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        } else {
                            Text(text = androidx.compose.ui.res.stringResource(R.string.settings_ra_login))
                        }
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
                        text = item.achievement.title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = item.gameTitle,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = item.achievement.description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis
                    )
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

@Composable
private fun AchievementCard(achievement: RetroAchievementEntry) {
    val imagePath = if (achievement.isEarned) achievement.badgeUrl ?: achievement.badgeLockedUrl else achievement.badgeLockedUrl ?: achievement.badgeUrl
    Surface(
        modifier = Modifier.fillMaxWidth(),
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

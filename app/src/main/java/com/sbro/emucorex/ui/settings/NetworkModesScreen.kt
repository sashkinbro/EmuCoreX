package com.sbro.emucorex.ui.settings

import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.provider.Settings as AndroidSettings
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.MenuBook
import androidx.compose.material.icons.rounded.CastConnected
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.Public
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.utils.NetworkAdapterCollector
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.network.InternetLinkError
import com.sbro.emucorex.network.InternetLinkSession
import com.sbro.emucorex.network.InternetLinkStatus
import com.sbro.emucorex.network.NetPlaySession
import com.sbro.emucorex.network.NetPlayStatus
import com.sbro.emucorex.network.RemotePlayCaptureService
import com.sbro.emucorex.network.RemotePlaySession
import com.sbro.emucorex.network.RemotePlayStatus
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.ScrollableFilterTabRow
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.skipGamepadTextFieldFocus
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import org.webrtc.SurfaceViewRenderer
private enum class NetworkHubTab {
    Overview,
    Online,
    LocalLink,
    InternetLink,
    NetPlay,
    RemotePlay,
    Guides,
    Advanced
}

@Composable
fun NetworkModesScreen(
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val defaults = remember { SettingsSnapshot() }
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    var selectedTab by rememberSaveable { mutableStateOf(NetworkHubTab.Overview) }
    var selectedGuide by rememberSaveable { mutableStateOf<MultiplayerGuideId?>(null) }
    val tabs = remember { NetworkHubTab.entries }

    BackHandler(enabled = selectedGuide != null) {
        selectedGuide = null
    }

    selectedGuide?.let { guide ->
        MultiplayerGuideDetailScreen(
            guide = guide,
            onBackClick = { selectedGuide = null }
        )
        return
    }

    if (!uiState.isLoaded) {
        Box(
            modifier = Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background),
            contentAlignment = Alignment.Center
        ) {
            CircularProgressIndicator()
        }
        return
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontalSystemBarPadding)
            .verticalScroll(rememberScrollState())
    ) {
        ScreenTopBar(
            title = stringResource(R.string.network_hub_title),
            onBackClick = onBackClick,
            modifier = Modifier
                .padding(horizontal = ScreenHorizontalPadding)
                .padding(top = topInset, bottom = 4.dp)
        )
        ScrollableFilterTabRow(
            tabs = tabs,
            selectedTab = selectedTab,
            onSelected = { selectedTab = it },
            key = { it.name },
            label = { tab -> networkHubTabLabel(tab) },
            icon = { tab -> networkHubTabIcon(tab) },
            compact = true
        )
        Column(
            modifier = Modifier.padding(top = 8.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            when (selectedTab) {
                NetworkHubTab.Overview -> NetworkOverview(
                    uiState = uiState,
                    onOpen = { selectedTab = it }
                )
                NetworkHubTab.Online -> NetworkSettingsTab(
                    uiState = uiState,
                    context = context,
                    defaults = defaults,
                    viewModel = viewModel,
                    panel = NetworkHubTab.Online
                )
                NetworkHubTab.LocalLink -> NetworkSettingsTab(
                    uiState = uiState,
                    context = context,
                    defaults = defaults,
                    viewModel = viewModel,
                    panel = NetworkHubTab.LocalLink
                )
                NetworkHubTab.InternetLink -> NetworkInternetLinkPanel(
                    viewModel = viewModel
                )
                NetworkHubTab.NetPlay -> NetworkNetPlayPanel()
                NetworkHubTab.RemotePlay -> NetworkRemotePlayPanel()
                NetworkHubTab.Guides -> MultiplayerGuidesPanel(
                    onOpenGuide = { selectedGuide = it }
                )
                NetworkHubTab.Advanced -> NetworkSettingsTab(
                    uiState = uiState,
                    context = context,
                    defaults = defaults,
                    viewModel = viewModel,
                    panel = NetworkHubTab.Advanced
                )
            }
        }
        Spacer(modifier = Modifier.height(bottomInset))
    }
}

@Composable
private fun NetworkOverview(
    uiState: SettingsUiState,
    onOpen: (NetworkHubTab) -> Unit
) {
    SettingsSection(title = stringResource(R.string.network_overview_choose_mode)) {
        SettingsInlineNote(stringResource(R.string.network_overview_desc))
        SettingsItem(
            icon = Icons.Rounded.Public,
            label = stringResource(R.string.network_online_title),
            value = stringResource(R.string.network_online_desc),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
            onClick = { onOpen(NetworkHubTab.Online) }
        )
        SettingsItem(
            icon = Icons.Rounded.Link,
            label = stringResource(R.string.network_local_link_title),
            value = if (uiState.dev9LocalLinkMode == AppPreferences.DEV9_LOCAL_LINK_HOST) {
                stringResource(R.string.settings_network_mode_local_host)
            } else if (uiState.dev9LocalLinkMode == AppPreferences.DEV9_LOCAL_LINK_JOIN) {
                stringResource(R.string.settings_network_mode_local_join)
            } else {
                stringResource(R.string.network_local_link_desc)
            },
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
            onClick = { onOpen(NetworkHubTab.LocalLink) }
        )
        SettingsItem(
            icon = Icons.Rounded.Public,
            label = stringResource(R.string.network_internet_link_title),
            value = stringResource(R.string.network_internet_link_desc),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
            onClick = { onOpen(NetworkHubTab.InternetLink) }
        )
        SettingsItem(
            icon = Icons.Rounded.Gamepad,
            label = stringResource(R.string.network_netplay_title),
            value = stringResource(R.string.network_netplay_desc),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
            onClick = { onOpen(NetworkHubTab.NetPlay) }
        )
        SettingsItem(
            icon = Icons.Rounded.CastConnected,
            label = stringResource(R.string.network_remote_play_title),
            value = stringResource(R.string.network_remote_play_desc),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
            onClick = { onOpen(NetworkHubTab.RemotePlay) }
        )
    }
}

@Composable
private fun NetworkNetPlayPanel() {
    val context = LocalContext.current
    val state by NetPlaySession.state.collectAsState()
    var roomDraft by rememberSaveable { mutableStateOf("") }
    val busy = state.status in setOf(
        NetPlayStatus.Creating,
        NetPlayStatus.WaitingForPeer,
        NetPlayStatus.Joining,
        NetPlayStatus.Connecting
    )
    val active = busy || state.status == NetPlayStatus.Connected
    val statusText = when (state.status) {
        NetPlayStatus.Idle -> stringResource(R.string.network_session_idle)
        NetPlayStatus.Creating -> stringResource(R.string.network_session_creating)
        NetPlayStatus.WaitingForPeer -> stringResource(R.string.network_session_waiting)
        NetPlayStatus.Joining -> stringResource(R.string.network_session_joining)
        NetPlayStatus.Connecting -> stringResource(R.string.network_session_connecting)
        NetPlayStatus.Connected -> stringResource(R.string.network_netplay_connected)
        NetPlayStatus.Error -> internetLinkErrorLabel(state.error)
    }

    SettingsSection(title = stringResource(R.string.network_netplay_title)) {
        SettingsInlineNote(stringResource(R.string.network_netplay_beginner_hint))
        SettingsItem(
            icon = if (state.status == NetPlayStatus.Connected) Icons.Rounded.CheckCircle else Icons.Rounded.Gamepad,
            label = stringResource(R.string.network_session_status),
            value = statusText,
            border = BorderStroke(
                1.dp,
                if (state.status == NetPlayStatus.Connected) MaterialTheme.colorScheme.primary.copy(alpha = 0.62f)
                else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)
            ),
            progressVisible = busy,
            onClick = {}
        )
        if (state.roomCode.isNotBlank()) {
            SettingsItem(
                icon = Icons.Rounded.Gamepad,
                label = stringResource(R.string.network_room_code),
                value = state.roomCode,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
                onClick = {}
            )
        }
        if (!active) {
            Button(
                onClick = { NetPlaySession.createRoom(context) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Icon(Icons.Rounded.Gamepad, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.network_create_room))
            }
            HorizontalDivider(
                modifier = Modifier.padding(horizontal = 16.dp),
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            )
            OutlinedTextField(
                value = roomDraft,
                onValueChange = { roomDraft = NetPlaySession.sanitizeRoomCode(it) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).skipGamepadTextFieldFocus(),
                label = { Text(stringResource(R.string.network_room_code)) },
                supportingText = { Text(stringResource(R.string.network_room_code_hint)) },
                shape = RoundedCornerShape(18.dp),
                singleLine = true
            )
            OutlinedButton(
                onClick = { NetPlaySession.joinRoom(context, roomDraft) },
                enabled = roomDraft.length == 8,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(stringResource(R.string.network_join_room))
            }
        } else {
            SettingsInlineNote(stringResource(R.string.network_netplay_launch_note))
            OutlinedButton(
                onClick = NetPlaySession::disconnect,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(stringResource(R.string.network_end_session))
            }
        }
    }
}

@Composable
private fun NetworkRemotePlayPanel() {
    val context = LocalContext.current
    val state by RemotePlaySession.state.collectAsState()
    val remoteTrack by RemotePlaySession.remoteVideoTrack.collectAsState()
    var roomDraft by rememberSaveable { mutableStateOf("") }
    val projectionManager = remember {
        context.getSystemService(MediaProjectionManager::class.java)
    }
    val captureLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val permission = result.data
        if (result.resultCode == Activity.RESULT_OK && permission != null) {
            RemotePlayCaptureService.start(context)
            RemotePlaySession.host(context, permission)
        }
    }
    val busy = state.status in setOf(
        RemotePlayStatus.Creating,
        RemotePlayStatus.WaitingForPeer,
        RemotePlayStatus.Joining,
        RemotePlayStatus.Connecting
    )
    val active = busy || state.status == RemotePlayStatus.Connected
    val statusText = when (state.status) {
        RemotePlayStatus.Idle -> stringResource(R.string.network_session_idle)
        RemotePlayStatus.Creating -> stringResource(R.string.network_session_creating)
        RemotePlayStatus.WaitingForPeer -> stringResource(R.string.network_session_waiting)
        RemotePlayStatus.Joining -> stringResource(R.string.network_session_joining)
        RemotePlayStatus.Connecting -> stringResource(R.string.network_session_connecting)
        RemotePlayStatus.Connected -> stringResource(R.string.network_session_connected)
        RemotePlayStatus.Error -> internetLinkErrorLabel(state.error)
    }

    SettingsSection(title = stringResource(R.string.network_remote_play_title)) {
        SettingsInlineNote(stringResource(R.string.network_remote_play_beginner_hint))
        SettingsItem(
            icon = if (state.status == RemotePlayStatus.Connected) Icons.Rounded.CheckCircle else Icons.Rounded.CastConnected,
            label = stringResource(R.string.network_session_status),
            value = statusText,
            border = BorderStroke(
                1.dp,
                if (state.status == RemotePlayStatus.Connected) {
                    MaterialTheme.colorScheme.primary.copy(alpha = 0.62f)
                } else {
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)
                }
            ),
            progressVisible = busy,
            onClick = {}
        )
        if (state.roomCode.isNotBlank()) {
            SettingsItem(
                icon = Icons.Rounded.Gamepad,
                label = stringResource(R.string.network_room_code),
                value = state.roomCode,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
                onClick = {}
            )
        }
        if (!active) {
            Button(
                onClick = { captureLauncher.launch(projectionManager.createScreenCaptureIntent()) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Icon(Icons.Rounded.CastConnected, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.network_remote_play_host))
            }
            HorizontalDivider(
                modifier = Modifier.padding(horizontal = 16.dp),
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            )
            OutlinedTextField(
                value = roomDraft,
                onValueChange = { roomDraft = RemotePlaySession.sanitizeCode(it) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).skipGamepadTextFieldFocus(),
                label = { Text(stringResource(R.string.network_room_code)) },
                supportingText = { Text(stringResource(R.string.network_room_code_hint)) },
                shape = RoundedCornerShape(18.dp),
                singleLine = true
            )
            OutlinedButton(
                onClick = { RemotePlaySession.join(context, roomDraft) },
                enabled = roomDraft.length == 8,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(stringResource(R.string.network_remote_play_join))
            }
        } else {
            if (!state.isHost && remoteTrack != null) {
                RemotePlayVideo(track = remoteTrack!!)
                Text(
                    text = stringResource(R.string.network_remote_play_controls),
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
                )
                RemotePlayController()
            }
            OutlinedButton(
                onClick = RemotePlaySession::disconnect,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(stringResource(R.string.network_end_session))
            }
        }
    }
}

@Composable
private fun RemotePlayVideo(track: org.webrtc.VideoTrack) {
    var renderer by remember { mutableStateOf<SurfaceViewRenderer?>(null) }
    AndroidView(
        factory = { viewContext ->
            SurfaceViewRenderer(viewContext).apply {
                init(RemotePlaySession.eglContext, null)
                setEnableHardwareScaler(true)
                setMirror(false)
                renderer = this
            }
        },
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .aspectRatio(16f / 9f)
    )
    DisposableEffect(track, renderer) {
        val currentRenderer = renderer
        if (currentRenderer != null) track.addSink(currentRenderer)
        onDispose {
            if (currentRenderer != null) {
                track.removeSink(currentRenderer)
                currentRenderer.release()
                if (renderer === currentRenderer) renderer = null
            }
        }
    }
}

@Composable
private fun RemotePlayController() {
    val rows = listOf(
        listOf("↑" to 19, "△" to 100),
        listOf("←" to 21, "↓" to 20, "→" to 22),
        listOf("□" to 99, "×" to 96, "○" to 97),
        listOf("L1" to 102, "SELECT" to 109, "START" to 108, "R1" to 103)
    )
    Column(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        rows.forEach { buttons ->
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)
            ) {
                buttons.forEach { (label, index) ->
                    RemotePlayHoldButton(label = label, index = index, modifier = Modifier.weight(1f))
                }
            }
        }
    }
}

@Composable
private fun RemotePlayHoldButton(label: String, index: Int, modifier: Modifier = Modifier) {
    OutlinedButton(
        onClick = {},
        modifier = modifier.pointerInput(index) {
            detectTapGestures(
                onPress = {
                    RemotePlaySession.sendButton(index, 255, true)
                    tryAwaitRelease()
                    RemotePlaySession.sendButton(index, 0, false)
                }
            )
        },
        shape = RoundedCornerShape(16.dp)
    ) {
        Text(label, maxLines = 1)
    }
}

@Composable
private fun NetworkInternetLinkPanel(viewModel: SettingsViewModel) {
    val context = LocalContext.current
    val state by InternetLinkSession.state.collectAsState()
    var roomDraft by rememberSaveable { mutableStateOf("") }
    val busy = state.status in setOf(
        InternetLinkStatus.Creating,
        InternetLinkStatus.WaitingForPeer,
        InternetLinkStatus.Joining,
        InternetLinkStatus.Connecting
    )
    val active = busy || state.status == InternetLinkStatus.Connected
    val statusText = when (state.status) {
        InternetLinkStatus.Idle -> stringResource(R.string.network_session_idle)
        InternetLinkStatus.Creating -> stringResource(R.string.network_session_creating)
        InternetLinkStatus.WaitingForPeer -> stringResource(R.string.network_session_waiting)
        InternetLinkStatus.Joining -> stringResource(R.string.network_session_joining)
        InternetLinkStatus.Connecting -> stringResource(R.string.network_session_connecting)
        InternetLinkStatus.Connected -> stringResource(R.string.network_session_connected)
        InternetLinkStatus.Error -> internetLinkErrorLabel(state.error)
    }

    LaunchedEffect(state.status, state.isHost) {
        when (state.status) {
            InternetLinkStatus.Creating,
            InternetLinkStatus.WaitingForPeer,
            InternetLinkStatus.Connecting,
            InternetLinkStatus.Connected -> viewModel.setDev9LocalLinkMode(
                if (state.isHost) AppPreferences.DEV9_INTERNET_LINK_HOST
                else AppPreferences.DEV9_INTERNET_LINK_JOIN
            )
            else -> Unit
        }
    }

    SettingsSection(title = stringResource(R.string.network_internet_link_title)) {
        SettingsInlineNote(stringResource(R.string.network_internet_link_beginner_hint))
        SettingsItem(
            icon = if (state.status == InternetLinkStatus.Connected) Icons.Rounded.CheckCircle else Icons.Rounded.Public,
            label = stringResource(R.string.network_session_status),
            value = statusText,
            border = BorderStroke(
                1.dp,
                if (state.status == InternetLinkStatus.Connected) {
                    MaterialTheme.colorScheme.primary.copy(alpha = 0.62f)
                } else {
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)
                }
            ),
            progressVisible = busy,
            onClick = {}
        )
        if (state.roomCode.isNotBlank()) {
            SettingsItem(
                icon = Icons.Rounded.Gamepad,
                label = stringResource(R.string.network_room_code),
                value = state.roomCode,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
                onClick = {}
            )
        }
        if (!active) {
            Button(
                onClick = { InternetLinkSession.createRoom(context) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Icon(Icons.Rounded.Public, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.network_create_room))
            }
            HorizontalDivider(
                modifier = Modifier.padding(horizontal = 16.dp),
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            )
            OutlinedTextField(
                value = roomDraft,
                onValueChange = { roomDraft = InternetLinkSession.sanitizeRoomCode(it) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).skipGamepadTextFieldFocus(),
                label = { Text(stringResource(R.string.network_room_code)) },
                supportingText = { Text(stringResource(R.string.network_room_code_hint)) },
                shape = RoundedCornerShape(18.dp),
                singleLine = true
            )
            OutlinedButton(
                onClick = { InternetLinkSession.joinRoom(context, roomDraft) },
                enabled = roomDraft.length == 8,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(stringResource(R.string.network_join_room))
            }
        } else {
            OutlinedButton(
                onClick = {
                    InternetLinkSession.disconnect()
                    viewModel.setDev9LocalLinkMode(AppPreferences.DEV9_LOCAL_LINK_OFF)
                },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(18.dp)
            ) {
                Text(stringResource(R.string.network_end_session))
            }
        }
    }
}

@Composable
private fun internetLinkErrorLabel(error: InternetLinkError?): String = stringResource(
    when (error) {
        InternetLinkError.InvalidRoomCode -> R.string.network_error_invalid_code
        InternetLinkError.RoomNotFound -> R.string.network_error_room_not_found
        InternetLinkError.RoomFull -> R.string.network_error_room_full
        InternetLinkError.ConnectionFailed, null -> R.string.network_error_connection
    }
)

@Composable
private fun networkHubTabLabel(tab: NetworkHubTab): String = stringResource(
    when (tab) {
        NetworkHubTab.Overview -> R.string.network_tab_overview
        NetworkHubTab.Online -> R.string.network_tab_online
        NetworkHubTab.LocalLink -> R.string.network_tab_local
        NetworkHubTab.InternetLink -> R.string.network_tab_internet
        NetworkHubTab.NetPlay -> R.string.network_tab_netplay
        NetworkHubTab.RemotePlay -> R.string.network_tab_remote_play
        NetworkHubTab.Guides -> R.string.network_tab_guides
        NetworkHubTab.Advanced -> R.string.network_tab_advanced
    }
)

private fun networkHubTabIcon(tab: NetworkHubTab): ImageVector = when (tab) {
    NetworkHubTab.Overview -> Icons.Rounded.Gamepad
    NetworkHubTab.Online -> Icons.Rounded.Public
    NetworkHubTab.LocalLink -> Icons.Rounded.Link
    NetworkHubTab.InternetLink -> Icons.Rounded.Public
    NetworkHubTab.NetPlay -> Icons.Rounded.Gamepad
    NetworkHubTab.RemotePlay -> Icons.Rounded.CastConnected
    NetworkHubTab.Guides -> Icons.AutoMirrored.Rounded.MenuBook
    NetworkHubTab.Advanced -> Icons.Rounded.Tune
}

@Composable
private fun NetworkSettingsTab(
    uiState: SettingsUiState,
    context: android.content.Context,
    defaults: SettingsSnapshot,
    viewModel: SettingsViewModel,
    panel: NetworkHubTab
) {
    val lifecycleOwner = LocalLifecycleOwner.current
    var adapterRefreshKey by remember { mutableIntStateOf(0) }
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) adapterRefreshKey++
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }
    val adapters = remember(context, adapterRefreshKey) {
        NetworkAdapterCollector.collectAdapters(context)
            .filter { adapter ->
                adapter.isUp && !adapter.isLoopback && adapter.ipAddresses.any { !it.contains(':') }
            }
    }
    val devices = remember(adapters, uiState.dev9EthernetDevice) {
        buildList {
            add("Auto" to "Auto")
            adapters.forEach { adapter ->
                add(adapter.name to "${adapter.displayName} (${adapter.name})")
            }
            if (uiState.dev9EthernetDevice != "Auto" && none { it.first == uiState.dev9EthernetDevice }) {
                add(uiState.dev9EthernetDevice to uiState.dev9EthernetDevice)
            }
        }.distinctBy { it.first }
    }
    val dnsModes = listOf(
        AppPreferences.DEV9_DNS_MODE_AUTO to stringResource(R.string.settings_network_dns_mode_auto),
        AppPreferences.DEV9_DNS_MODE_MANUAL to stringResource(R.string.settings_network_dns_mode_manual),
        AppPreferences.DEV9_DNS_MODE_INTERNAL to stringResource(R.string.settings_network_dns_mode_internal)
    )
    val localAddresses: List<String> = remember(adapters) {
        adapters.asSequence()
            .filterNot { it.displayName == "VPN" || it.displayName == "Mobile data" }
            .flatMap { it.ipAddresses.asSequence() }
            .filter(::isPrivateIpv4)
            .distinct()
            .toList()
    }

    SettingsSection(title = networkHubTabLabel(panel)) {
        val localLinkActive = uiState.dev9LocalLinkMode != AppPreferences.DEV9_LOCAL_LINK_OFF
        if (panel == NetworkHubTab.Online) {
            SettingsInlineNote(stringResource(R.string.network_online_beginner_hint))
            ToggleItem(
                icon = Icons.Rounded.Public,
                title = stringResource(R.string.settings_network_enable),
                subtitle = stringResource(R.string.settings_network_enable_desc),
                checked = uiState.dev9EthernetEnabled && !localLinkActive,
                onCheckedChange = { enabled ->
                    viewModel.setDev9LocalLinkMode(AppPreferences.DEV9_LOCAL_LINK_OFF)
                    viewModel.setDev9EthernetEnabled(enabled)
                },
                helpText = stringResource(R.string.settings_network_enable_help),
                onResetToDefault = {
                    viewModel.setDev9LocalLinkMode(AppPreferences.DEV9_LOCAL_LINK_OFF)
                    viewModel.setDev9EthernetEnabled(defaults.dev9EthernetEnabled)
                }
            )
            SettingsItem(
                icon = Icons.Rounded.Link,
                label = stringResource(R.string.settings_network_api),
                value = stringResource(R.string.settings_network_api_sockets),
                onClick = {}
            )
            ChoiceSection(
                title = stringResource(R.string.settings_network_adapter),
                options = devices.mapIndexed { index, (_, label) -> index to label },
                selectedValue = devices.indexOfFirst { it.first == uiState.dev9EthernetDevice }.coerceAtLeast(0),
                onSelect = { index -> devices.getOrNull(index)?.first?.let(viewModel::setDev9EthernetDevice) },
                helpText = stringResource(R.string.settings_network_adapter_help),
                onResetToDefault = { viewModel.setDev9EthernetDevice(defaults.dev9EthernetDevice) }
            )
            ChoiceSection(
                title = stringResource(R.string.settings_network_dns_preset),
                options = listOf(
                    0 to stringResource(R.string.settings_network_dns_preset_system),
                    1 to stringResource(R.string.settings_network_dns_preset_ps2online),
                    2 to stringResource(R.string.settings_network_dns_preset_psrewired)
                ),
                selectedValue = when (uiState.dev9Dns1Mode) {
                    AppPreferences.DEV9_DNS_MODE_MANUAL -> when (uiState.dev9Dns1) {
                        "45.7.228.197" -> 1
                        "67.222.156.250" -> 2
                        else -> 0
                    }
                    else -> 0
                },
                onSelect = { preset ->
                    when (preset) {
                        1 -> {
                            viewModel.setDev9Dns1Mode(AppPreferences.DEV9_DNS_MODE_MANUAL)
                            viewModel.setDev9Dns1("45.7.228.197")
                        }
                        2 -> {
                            viewModel.setDev9Dns1Mode(AppPreferences.DEV9_DNS_MODE_MANUAL)
                            viewModel.setDev9Dns1("67.222.156.250")
                        }
                        else -> {
                            viewModel.setDev9Dns1Mode(AppPreferences.DEV9_DNS_MODE_AUTO)
                            viewModel.setDev9Dns1("0.0.0.0")
                        }
                    }
                },
                helpText = stringResource(R.string.settings_network_dns_preset_help)
            )
        }
        if (panel == NetworkHubTab.LocalLink) {
            SettingsInlineNote(stringResource(R.string.settings_network_local_summary))
            ChoiceSection(
                title = stringResource(R.string.settings_network_mode),
                options = listOf(
                    AppPreferences.DEV9_LOCAL_LINK_OFF to stringResource(R.string.emulation_local_multiplayer_off),
                    AppPreferences.DEV9_LOCAL_LINK_HOST to stringResource(R.string.settings_network_mode_local_host),
                    AppPreferences.DEV9_LOCAL_LINK_JOIN to stringResource(R.string.settings_network_mode_local_join)
                ),
                selectedValue = uiState.dev9LocalLinkMode,
                onSelect = viewModel::setDev9LocalLinkMode,
                helpText = stringResource(R.string.settings_network_mode_help),
                onResetToDefault = { viewModel.setDev9LocalLinkMode(defaults.dev9LocalLinkMode) }
            )
            SettingsItem(
                icon = Icons.Rounded.Link,
                label = stringResource(R.string.settings_network_open_wifi_settings),
                value = stringResource(R.string.settings_network_open_wifi_settings_desc),
                onClick = {
                    runCatching { context.startActivity(Intent(AndroidSettings.ACTION_WIRELESS_SETTINGS)) }
                }
            )
            if (uiState.dev9LocalLinkMode == AppPreferences.DEV9_LOCAL_LINK_HOST) {
                SettingsInlineNote(
                    stringResource(
                        R.string.settings_network_local_host_addresses,
                        localAddresses.joinToString().ifBlank { stringResource(R.string.settings_network_local_no_address) }
                    )
                )
            } else if (uiState.dev9LocalLinkMode == AppPreferences.DEV9_LOCAL_LINK_JOIN) {
                var hostDraft by remember(uiState.dev9LocalLinkAddress) { mutableStateOf(uiState.dev9LocalLinkAddress) }
                val hostValid = remember(hostDraft) { isValidIpv4(hostDraft) }
                OutlinedTextField(
                    value = hostDraft,
                    onValueChange = { value ->
                        hostDraft = value.filter { it.isDigit() || it == '.' }.take(15)
                        if (isValidIpv4(hostDraft)) viewModel.setDev9LocalLinkAddress(hostDraft)
                    },
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).skipGamepadTextFieldFocus(),
                    label = { Text(stringResource(R.string.settings_network_local_host_address)) },
                    supportingText = { Text(stringResource(R.string.settings_network_local_host_address_desc)) },
                    isError = !hostValid,
                    shape = RoundedCornerShape(18.dp),
                    singleLine = true
                )
            }
            if (uiState.dev9LocalLinkMode != AppPreferences.DEV9_LOCAL_LINK_OFF) {
            var portDraft by remember(uiState.dev9LocalLinkPort) { mutableStateOf(uiState.dev9LocalLinkPort.toString()) }
            val portValid = portDraft.toIntOrNull() in 1024..65535
            OutlinedTextField(
                value = portDraft,
                onValueChange = { value ->
                    portDraft = value.filter(Char::isDigit).take(5)
                    portDraft.toIntOrNull()?.takeIf { it in 1024..65535 }?.let(viewModel::setDev9LocalLinkPort)
                },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).skipGamepadTextFieldFocus(),
                label = { Text(stringResource(R.string.settings_network_local_port)) },
                supportingText = { Text(stringResource(R.string.settings_network_local_port_desc)) },
                isError = !portValid,
                shape = RoundedCornerShape(18.dp),
                singleLine = true
            )
            var roomDraft by remember(uiState.dev9LocalLinkRoomCode) { mutableStateOf(uiState.dev9LocalLinkRoomCode) }
            val roomValid = roomDraft.length in 4..12
            OutlinedTextField(
                value = roomDraft,
                onValueChange = { value ->
                    roomDraft = value.filter(Char::isLetterOrDigit).take(12).uppercase()
                    viewModel.setDev9LocalLinkRoomCode(roomDraft)
                },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).skipGamepadTextFieldFocus(),
                label = { Text(stringResource(R.string.settings_network_local_room_code)) },
                supportingText = { Text(stringResource(R.string.settings_network_local_room_code_desc)) },
                isError = !roomValid,
                shape = RoundedCornerShape(18.dp),
                singleLine = true
            )
            SettingsInlineNote(stringResource(R.string.settings_network_local_peer_id, uiState.dev9LocalLinkPeerId))
            SettingsInlineNote(stringResource(R.string.settings_network_local_compatibility_note))
            }
        }
        if (panel == NetworkHubTab.Advanced) {
        SettingsInlineNote(stringResource(R.string.network_advanced_desc))
        DnsModeSetting(
            title = stringResource(R.string.settings_network_dns1_mode),
            addressTitle = stringResource(R.string.settings_network_dns1),
            mode = uiState.dev9Dns1Mode,
            address = uiState.dev9Dns1,
            modes = dnsModes,
            onModeChange = viewModel::setDev9Dns1Mode,
            onAddressChange = viewModel::setDev9Dns1
        )
        DnsModeSetting(
            title = stringResource(R.string.settings_network_dns2_mode),
            addressTitle = stringResource(R.string.settings_network_dns2),
            mode = uiState.dev9Dns2Mode,
            address = uiState.dev9Dns2,
            modes = dnsModes,
            onModeChange = viewModel::setDev9Dns2Mode,
            onAddressChange = viewModel::setDev9Dns2
        )
        ToggleItem(
            icon = Icons.Rounded.Link,
            title = stringResource(R.string.settings_network_intercept_dhcp),
            subtitle = stringResource(R.string.settings_network_intercept_dhcp_desc),
            checked = uiState.dev9InterceptDhcp,
            onCheckedChange = viewModel::setDev9InterceptDhcp,
            onResetToDefault = { viewModel.setDev9InterceptDhcp(defaults.dev9InterceptDhcp) }
        )
        ToggleItem(
            icon = Icons.Rounded.Info,
            title = stringResource(R.string.settings_network_log_dhcp),
            subtitle = stringResource(R.string.settings_network_log_dhcp_desc),
            checked = uiState.dev9LogDhcp,
            onCheckedChange = viewModel::setDev9LogDhcp,
            onResetToDefault = { viewModel.setDev9LogDhcp(defaults.dev9LogDhcp) }
        )
        ToggleItem(
            icon = Icons.Rounded.Info,
            title = stringResource(R.string.settings_network_log_dns),
            subtitle = stringResource(R.string.settings_network_log_dns_desc),
            checked = uiState.dev9LogDns,
            onCheckedChange = viewModel::setDev9LogDns,
            onResetToDefault = { viewModel.setDev9LogDns(defaults.dev9LogDns) }
        )
        }
    }
}

@Composable
private fun DnsModeSetting(
    title: String,
    addressTitle: String,
    mode: String,
    address: String,
    modes: List<Pair<String, String>>,
    onModeChange: (String) -> Unit,
    onAddressChange: (String) -> Unit
) {
    ChoiceSection(
        title = title,
        options = modes.mapIndexed { index, (_, label) -> index to label },
        selectedValue = modes.indexOfFirst { it.first == mode }.coerceAtLeast(0),
        onSelect = { index -> modes.getOrNull(index)?.first?.let(onModeChange) },
        helpText = stringResource(R.string.settings_network_dns_mode_help)
    )
    if (mode == AppPreferences.DEV9_DNS_MODE_MANUAL) {
        var draft by remember(address) { mutableStateOf(address) }
        val valid = remember(draft) { isValidIpv4(draft) }
        OutlinedTextField(
            value = draft,
            onValueChange = { value ->
                draft = value.filter { it.isDigit() || it == '.' }.take(15)
                if (isValidIpv4(draft)) onAddressChange(draft)
            },
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .skipGamepadTextFieldFocus(),
            label = { Text(addressTitle) },
            supportingText = {
                Text(stringResource(if (valid) R.string.settings_network_dns_address_desc else R.string.settings_network_dns_invalid))
            },
            isError = !valid,
            shape = RoundedCornerShape(18.dp),
            singleLine = true
        )
    }
}

private fun isValidIpv4(value: String): Boolean {
    val parts = value.split('.')
    return parts.size == 4 && parts.all { part ->
        part.isNotEmpty() && part.length <= 3 && part.toIntOrNull() in 0..255
    }
}

private fun isPrivateIpv4(value: String): Boolean {
    val parts = value.split('.').mapNotNull(String::toIntOrNull)
    if (parts.size != 4 || parts.any { it !in 0..255 }) return false
    return parts[0] == 10 ||
        (parts[0] == 172 && parts[1] in 16..31) ||
        (parts[0] == 192 && parts[1] == 168)
}

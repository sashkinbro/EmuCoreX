package com.sbro.emucorex.ui.ps2shell

import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ActivityInfo
import android.graphics.Typeface
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.opengl.GLSurfaceView
import android.os.BatteryManager
import android.os.Build
import android.os.Environment
import android.os.StatFs
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.ViewGroup
import android.view.WindowManager
import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.res.ResourcesCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorex.BuildConfig
import com.sbro.emucorex.MainActivity
import com.sbro.emucorex.R
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.UiSoundManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CoverArtRepository
import com.sbro.emucorex.data.CustomGameCoverRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.MemoryCardInfo
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.data.SaveStateEntryInfo
import com.sbro.emucorex.data.SaveStateRepository
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityStatus
import com.sbro.emucorex.ps2ui.Ps2McContents
import com.sbro.emucorex.ps2ui.Ps2MemoryCardParser
import com.sbro.emucorex.ps2ui.Ps2ShellAudio
import com.sbro.emucorex.ps2ui.Ps2ShellHost
import com.sbro.emucorex.ps2ui.Ps2ShellNative
import com.sbro.emucorex.ps2ui.Ps2ShellSurfaceRenderer
import com.sbro.emucorex.ps2ui.Ps2TextRasterizer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs

private data class Ps2ResolvedGame(
    val title: String,
    val path: String,
    val serial: String,
    val coverPath: String,
    val sizeStr: String,
    val dateStr: String,
    val fileSizeBytes: Long,
    val saveCount: Int,
    val latestSaveSlot: Int,
    val compatStatus: String,
)

private data class Ps2MemoryCardBundle(
    val slot1Name: String = "",
    val slot1Meta: String = "",
    val slot1Status: String = "",
    val slot2Name: String = "",
    val slot2Meta: String = "",
    val slot2Status: String = "",
    val cards: List<MemoryCardInfo> = emptyList(),
)

private data class Ps2SystemSnapshot(
    val batteryLevel: Int = 100,
    val batteryCharging: Boolean = false,
    val wifiConnected: Boolean = false,
    val bluetoothConnected: Boolean = false,
    val audioMuted: Boolean = false,
    val freeStorage: String = "",
    val deviceName: String = "",
    val appVersion: String = BuildConfig.VERSION_NAME,
    val biosStatus: String = "",
    val biosValid: Boolean = false,
)

private object Ps2ShellSessionCache {
    @Volatile
    var biosAssetsLoaded: Boolean = false

    @Volatile
    var lastResolvedGames: List<Ps2ResolvedGame> = emptyList()

    @Volatile
    var lastSelectedGamePath: String = ""

    @Volatile
    var lastSaves: List<SaveStateEntryInfo> = emptyList()

    @Volatile
    var hasLoadedSaves: Boolean = false

    @Volatile
    var returnedFromGame: Boolean = false

    @Volatile
    var lastBiosValid: Boolean = false

    @Volatile
    var lastSystemSnapshot: Ps2SystemSnapshot = Ps2SystemSnapshot()

    @Volatile
    var slot1Contents: Ps2McContents? = null

    @Volatile
    var slot2Contents: Ps2McContents? = null
}

private val PS2_STRING_BINDINGS: List<Pair<String, Int>> = listOf(
    "ps2ui_main_menu" to R.string.ps2ui_main_menu,
    "ps2ui_menu_games" to R.string.ps2ui_menu_games,
    "ps2ui_menu_browser" to R.string.ps2ui_menu_browser,
    "ps2ui_menu_settings" to R.string.ps2ui_menu_settings,
    "ps2ui_menu_settings_sub" to R.string.ps2ui_menu_settings_sub,
    "ps2ui_boot_subtitle" to R.string.ps2ui_boot_subtitle,
    "ps2ui_hint_select" to R.string.ps2ui_hint_select,
    "ps2ui_hint_version" to R.string.ps2ui_hint_version,
    "ps2ui_hint_exit" to R.string.ps2ui_hint_exit,
    "ps2ui_hint_back" to R.string.ps2ui_hint_back,
    "ps2ui_hint_tab" to R.string.ps2ui_hint_tab,
    "ps2ui_hint_change" to R.string.ps2ui_hint_change,
    "ps2ui_btn_start" to R.string.ps2ui_btn_start,
    "ps2ui_btn_continue" to R.string.ps2ui_btn_continue,
    "ps2ui_btn_options" to R.string.ps2ui_btn_options,
    "ps2ui_loading_games" to R.string.ps2ui_loading_games,
    "ps2ui_no_games" to R.string.ps2ui_no_games,
    "ps2ui_discs" to R.string.ps2ui_discs,
    "ps2ui_saves_word" to R.string.ps2ui_saves_word,
    "ps2ui_cards_word" to R.string.ps2ui_cards_word,
    "ps2ui_mc_tab_all" to R.string.ps2ui_mc_tab_all,
    "ps2ui_mc_tab_game" to R.string.ps2ui_mc_tab_game,
    "ps2ui_mc_tab_cards" to R.string.ps2ui_mc_tab_cards,
    "ps2ui_mc_slot1" to R.string.ps2ui_mc_slot1,
    "ps2ui_mc_slot2" to R.string.ps2ui_mc_slot2,
    "ps2ui_mc_empty_slot" to R.string.ps2ui_mc_empty_slot,
    "ps2ui_mc_tap_cycle" to R.string.ps2ui_mc_tap_cycle,
    "ps2ui_mc_cycle" to R.string.ps2ui_mc_cycle,
    "ps2ui_mc_swap" to R.string.ps2ui_mc_swap,
    "ps2ui_mc_eject" to R.string.ps2ui_mc_eject,
    "ps2ui_mc_create_short" to R.string.ps2ui_mc_create_short,
    "ps2ui_mc_create_title" to R.string.ps2ui_mc_create_title,
    "ps2ui_mc_size_8" to R.string.ps2ui_mc_size_8,
    "ps2ui_mc_size_16" to R.string.ps2ui_mc_size_16,
    "ps2ui_mc_size_32" to R.string.ps2ui_mc_size_32,
    "ps2ui_mc_size_64" to R.string.ps2ui_mc_size_64,
    "ps2ui_mc_type_folder" to R.string.ps2ui_mc_type_folder,
    "ps2ui_mc_no_cards" to R.string.ps2ui_mc_no_cards,
    "ps2ui_mc_actions" to R.string.ps2ui_mc_actions,
    "ps2ui_mc_mount_1" to R.string.ps2ui_mc_mount_1,
    "ps2ui_mc_mount_2" to R.string.ps2ui_mc_mount_2,
    "ps2ui_mc_duplicate" to R.string.ps2ui_mc_duplicate,
    "ps2ui_loading_saves" to R.string.ps2ui_loading_saves,
    "ps2ui_mc_no_saves" to R.string.ps2ui_mc_no_saves,
    "ps2ui_mc_empty_block" to R.string.ps2ui_mc_empty_block,
    "ps2ui_mc_load" to R.string.ps2ui_mc_load,
    "ps2ui_mc_delete" to R.string.ps2ui_mc_delete,
    "ps2ui_mc_exit" to R.string.ps2ui_mc_exit,
    "ps2ui_theme_ocean" to R.string.ps2ui_theme_ocean,
    "ps2ui_theme_obsidian" to R.string.ps2ui_theme_obsidian,
    "ps2ui_theme_emerald" to R.string.ps2ui_theme_emerald,
    "ps2ui_theme_crimson" to R.string.ps2ui_theme_crimson,
    "ps2ui_theme_amethyst" to R.string.ps2ui_theme_amethyst,
    "ps2ui_theme_gold" to R.string.ps2ui_theme_gold,
    "ps2ui_aspect_auto" to R.string.ps2ui_aspect_auto,
    "ps2ui_aspect_16_9" to R.string.ps2ui_aspect_16_9,
    "ps2ui_aspect_4_3" to R.string.ps2ui_aspect_4_3,
    "ps2ui_orbs_normal" to R.string.ps2ui_orbs_normal,
    "ps2ui_orbs_calm" to R.string.ps2ui_orbs_calm,
    "ps2ui_orbs_static" to R.string.ps2ui_orbs_static,
    "ps2ui_towers_dynamic" to R.string.ps2ui_towers_dynamic,
    "ps2ui_towers_subtle" to R.string.ps2ui_towers_subtle,
    "ps2ui_scan_subtle" to R.string.ps2ui_scan_subtle,
    "ps2ui_scan_classic" to R.string.ps2ui_scan_classic,
    "ps2ui_boot_full" to R.string.ps2ui_boot_full,
    "ps2ui_boot_fast" to R.string.ps2ui_boot_fast,
    "ps2ui_on" to R.string.ps2ui_on,
    "ps2ui_off" to R.string.ps2ui_off,
    "ps2ui_btn_cross" to R.string.ps2ui_btn_cross,
    "ps2ui_btn_circle" to R.string.ps2ui_btn_circle,
    "ps2ui_time_24h" to R.string.ps2ui_time_24h,
    "ps2ui_time_12h" to R.string.ps2ui_time_12h,
    "ps2ui_view" to R.string.ps2ui_view,
    "ps2ui_reset" to R.string.ps2ui_reset,
    "ps2ui_set_theme" to R.string.ps2ui_set_theme,
    "ps2ui_set_aspect" to R.string.ps2ui_set_aspect,
    "ps2ui_set_orbs" to R.string.ps2ui_set_orbs,
    "ps2ui_set_towers" to R.string.ps2ui_set_towers,
    "ps2ui_set_scanlines" to R.string.ps2ui_set_scanlines,
    "ps2ui_set_boot_anim" to R.string.ps2ui_set_boot_anim,
    "ps2ui_set_key_tone" to R.string.ps2ui_set_key_tone,
    "ps2ui_set_ambient" to R.string.ps2ui_set_ambient,
    "ps2ui_set_confirm_btn" to R.string.ps2ui_set_confirm_btn,
    "ps2ui_set_touch" to R.string.ps2ui_set_touch,
    "ps2ui_set_touch_guide" to R.string.ps2ui_set_touch_guide,
    "ps2ui_set_time_fmt" to R.string.ps2ui_set_time_fmt,
    "ps2ui_set_date_fmt" to R.string.ps2ui_set_date_fmt,
    "ps2ui_set_keep_screen" to R.string.ps2ui_set_keep_screen,
    "ps2ui_set_sysinfo" to R.string.ps2ui_set_sysinfo,
    "ps2ui_set_restore" to R.string.ps2ui_set_restore,
    "ps2ui_launching_save" to R.string.ps2ui_launching_save,
    "ps2ui_launching_disc" to R.string.ps2ui_launching_disc,
    "ps2ui_exit_title" to R.string.ps2ui_exit_title,
    "ps2ui_exit_msg" to R.string.ps2ui_exit_msg,
    "ps2ui_del_title" to R.string.ps2ui_del_title,
    "ps2ui_del_msg" to R.string.ps2ui_del_msg,
    "ps2ui_del_card_title" to R.string.ps2ui_del_card_title,
    "ps2ui_del_card_msg" to R.string.ps2ui_del_card_msg,
    "ps2ui_restore_title" to R.string.ps2ui_restore_title,
    "ps2ui_restore_msg" to R.string.ps2ui_restore_msg,
    "ps2ui_yes" to R.string.ps2ui_yes,
    "ps2ui_no" to R.string.ps2ui_no,
    "ps2ui_opt_start" to R.string.ps2ui_opt_start,
    "ps2ui_opt_continue" to R.string.ps2ui_opt_continue,
    "ps2ui_opt_saves" to R.string.ps2ui_opt_saves,
    "ps2ui_opt_info" to R.string.ps2ui_opt_info,
    "ps2ui_opt_settings" to R.string.ps2ui_opt_settings,
    "ps2ui_info_title" to R.string.ps2ui_info_title,
    "ps2ui_info_serial" to R.string.ps2ui_info_serial,
    "ps2ui_info_media" to R.string.ps2ui_info_media,
    "ps2ui_info_size" to R.string.ps2ui_info_size,
    "ps2ui_close" to R.string.ps2ui_close,
    "ps2ui_sys_app" to R.string.ps2ui_sys_app,
    "ps2ui_sys_device" to R.string.ps2ui_sys_device,
    "ps2ui_sys_bios" to R.string.ps2ui_sys_bios,
    "ps2ui_sys_storage" to R.string.ps2ui_sys_storage,
    "ps2ui_touch_guide_title" to R.string.ps2ui_touch_guide_title,
    "ps2ui_touch_tip_1" to R.string.ps2ui_touch_tip_1,
    "ps2ui_touch_tip_2" to R.string.ps2ui_touch_tip_2,
    "ps2ui_touch_tip_3" to R.string.ps2ui_touch_tip_3,
    "ps2ui_touch_tip_4" to R.string.ps2ui_touch_tip_4,
    "ps2ui_got_it" to R.string.ps2ui_got_it,
    "ps2ui_mc_open_card" to R.string.ps2ui_mc_open_card,
    "ps2ui_mc_no_data" to R.string.ps2ui_mc_no_data,
    "ps2ui_games_tab_ps2" to R.string.home_platform_ps2,
    "ps2ui_games_tab_arcade" to R.string.home_platform_arcade,
)

private fun normalizeSerialKey(raw: String?): String? {
    if (raw.isNullOrBlank()) return null
    val upper = raw.trim().uppercase(Locale.ROOT)
    val dashPat = Regex("([A-Z]{4})[^A-Z0-9]*(\\d{3})[^A-Z0-9]*(\\d{2})")
    dashPat.find(upper)?.let { return "${it.groupValues[1]}-${it.groupValues[2]}${it.groupValues[3]}" }
    val compactPat = Regex("([A-Z]{4})[^A-Z0-9]*(\\d{5})")
    compactPat.find(upper)?.let { return "${it.groupValues[1]}-${it.groupValues[2]}" }
    return upper.replace(Regex("[^A-Z0-9]"), "").takeIf { it.isNotBlank() }
}

private fun formatFileSize(sizeBytes: Long): String = when {
    sizeBytes <= 0L -> ""
    sizeBytes < 1024L * 1024L -> "${((sizeBytes + 1023L) / 1024L).coerceAtLeast(1L)} KB"
    sizeBytes < 1024L * 1024L * 1024L -> String.format(Locale.US, "%.1f MB", sizeBytes / (1024.0 * 1024.0))
    else -> String.format(Locale.US, "%.2f GB", sizeBytes / (1024.0 * 1024.0 * 1024.0))
}

private fun formatTimestamp(ms: Long): String = if (ms > 0L) {
    SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.getDefault()).format(Date(ms))
} else {
    ""
}

private fun formatCompatLabel(status: Pcsx2CompatibilityStatus?): String = when (status) {
    Pcsx2CompatibilityStatus.PERFECT -> "Perfect"
    Pcsx2CompatibilityStatus.PLAYABLE -> "Playable"
    Pcsx2CompatibilityStatus.IN_GAME -> "In-Game"
    Pcsx2CompatibilityStatus.INTRO -> "Intro"
    Pcsx2CompatibilityStatus.NOTHING -> "Menu"
    else -> ""
}

private fun formatCardMeta(context: Context, card: MemoryCardInfo?): String {
    if (card == null) return ""
    return if (card.isFolder) {
        context.getString(R.string.ps2ui_card_folder_badge)
    } else {
        formatFileSize(card.sizeBytes).ifBlank { "8.0 MB" }
    }
}

private fun hasPhysicalGamepadConnected(): Boolean = runCatching {
    InputDevice.getDeviceIds().any { id ->
        val dev = InputDevice.getDevice(id) ?: return@any false
        if (dev.isVirtual) return@any false
        val sources = dev.sources
        val isGamepad = (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        isGamepad && dev.hasKeys(
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_DPAD_UP,
        ).any { it }
    }
}.getOrDefault(false)

private fun readSystemSnapshot(context: Context, biosValid: Boolean): Ps2SystemSnapshot {
    val batteryIntent = runCatching {
        context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
    }.getOrNull()
    val lvl = batteryIntent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
    val scale = batteryIntent?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
    val status = batteryIntent?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: -1
    val charging = status == BatteryManager.BATTERY_STATUS_CHARGING ||
        status == BatteryManager.BATTERY_STATUS_FULL
    val pct = if (scale > 0 && lvl >= 0) (lvl * 100) / scale else 100

    val wifiConnected = runCatching {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
        val net = cm?.activeNetwork
        val caps = if (net != null) cm.getNetworkCapabilities(net) else null
        caps?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) == true
    }.getOrDefault(false)

    val btEnabled = runCatching {
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter?.isEnabled == true
    }.getOrDefault(false)
    val btAudio = runCatching {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        am?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)?.any { dev ->
            dev.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                dev.type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                dev.type == AudioDeviceInfo.TYPE_BLE_HEADSET ||
                dev.type == AudioDeviceInfo.TYPE_BLE_SPEAKER
        } == true
    }.getOrDefault(false)
    val muted = runCatching {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        (am?.getStreamVolume(AudioManager.STREAM_MUSIC) ?: 1) <= 0
    }.getOrDefault(false)

    val freeText = runCatching {
        val stat = StatFs(Environment.getDataDirectory().path)
        val avail = stat.availableBytes
        if (avail >= 1024L * 1024L * 1024L) {
            String.format(Locale.US, "%.1f GB", avail / (1024.0 * 1024.0 * 1024.0))
        } else {
            "${(avail / (1024L * 1024L)).coerceAtLeast(0L)} MB"
        }
    }.getOrDefault("")

    return Ps2SystemSnapshot(
        batteryLevel = pct,
        batteryCharging = charging,
        wifiConnected = wifiConnected,
        bluetoothConnected = btEnabled && btAudio,
        audioMuted = muted,
        freeStorage = freeText,
        deviceName = Build.MODEL.orEmpty().trim().ifBlank { "Android" },
        appVersion = BuildConfig.VERSION_NAME,
        biosStatus = context.getString(if (biosValid) R.string.ps2ui_bios_ready else R.string.ps2ui_bios_missing),
        biosValid = biosValid,
    )
}

private fun pushStringsToNative(context: Context) {
    val keys = Array(PS2_STRING_BINDINGS.size) { i -> PS2_STRING_BINDINGS[i].first }
    val values = Array(PS2_STRING_BINDINGS.size) { i ->
        runCatching { context.getString(PS2_STRING_BINDINGS[i].second) }.getOrDefault("")
    }
    Ps2ShellNative.nativeSetStrings(keys, values)
}

private fun pushGamesToNative(games: List<Ps2ResolvedGame>) {
    Ps2ShellNative.nativeSetGames(
        titles = games.map { it.title }.toTypedArray(),
        paths = games.map { it.path }.toTypedArray(),
        serials = games.map { it.serial }.toTypedArray(),
        covers = games.map { it.coverPath }.toTypedArray(),
        sizes = games.map { it.sizeStr }.toTypedArray(),
        dates = games.map { it.dateStr }.toTypedArray(),
        fileSizesBytes = games.map { it.fileSizeBytes }.toLongArray(),
        saveCounts = games.map { it.saveCount }.toIntArray(),
        latestSaveSlots = games.map { it.latestSaveSlot }.toIntArray(),
        compatStatuses = games.map { it.compatStatus }.toTypedArray(),
    )
}

private fun pushSavesToNative(
    context: Context,
    saves: List<SaveStateEntryInfo>,
) {
    val autoLabel = context.getString(R.string.ps2ui_slot_auto)
    val titles = Array(saves.size) { i -> saves[i].gameTitle.ifBlank { saves[i].serial } }
    val slots = Array(saves.size) { i ->
        val s = saves[i]
        if (s.isAutoSave) autoLabel else context.getString(R.string.ps2ui_slot_fmt, s.slot)
    }
    val slotNums = IntArray(saves.size) { i -> saves[i].slot }
    val dates = Array(saves.size) { i -> formatTimestamp(saves[i].lastModified) }
    val sizes = Array(saves.size) { i -> formatFileSize(saves[i].sizeBytes) }
    val serials = Array(saves.size) { i -> saves[i].serial }
    val previews = Array(saves.size) { i -> saves[i].coverArtPath.orEmpty() }
    val savePaths = Array(saves.size) { i -> saves[i].absolutePath }
    val gamePaths = Array(saves.size) { i -> saves[i].gamePath.orEmpty() }
    Ps2ShellNative.nativeSetSaves(titles, slots, slotNums, dates, sizes, serials, previews, savePaths, gamePaths)
}

private fun pushSystemSnapshotToNative(snap: Ps2SystemSnapshot) {
    Ps2ShellNative.nativeSetSystemStatus(
        batteryLevel = snap.batteryLevel,
        batteryCharging = snap.batteryCharging,
        wifiConnected = snap.wifiConnected,
        bluetoothConnected = snap.bluetoothConnected,
        audioMuted = snap.audioMuted,
        freeStorage = snap.freeStorage,
        deviceName = snap.deviceName,
        appVersion = snap.appVersion,
        biosStatus = snap.biosStatus,
        biosValid = snap.biosValid,
    )
}

private fun pushMemoryCardsToNative(context: Context, bundle: Ps2MemoryCardBundle) {
    val cardDateFmt = SimpleDateFormat("dd/MM/yyyy", Locale.getDefault())
    Ps2ShellNative.nativeSetMemoryCards(
        slot1Name = bundle.slot1Name,
        slot1Meta = bundle.slot1Meta,
        slot1Status = bundle.slot1Status,
        slot2Name = bundle.slot2Name,
        slot2Meta = bundle.slot2Meta,
        slot2Status = bundle.slot2Status,
        cardNames = bundle.cards.map { it.name }.toTypedArray(),
        cardSizes = bundle.cards.map { formatCardMeta(context, it) }.toTypedArray(),
        cardDates = bundle.cards.map {
            if (it.modifiedTime > 0L) cardDateFmt.format(Date(it.modifiedTime)) else ""
        }.toTypedArray(),
        cardIsFolder = bundle.cards.map { it.isFolder }.toBooleanArray(),
        cardIsDefault = bundle.cards.map { it.isDefaultCard }.toBooleanArray(),
    )
}

private fun pushMemoryCardContentsToNative(slot: Int, contents: Ps2McContents) {
    Ps2ShellNative.nativeSetMemoryCardContents(
        slot = slot,
        cardName = contents.cardName,
        freeKb = contents.freeKb,
        totalKb = contents.totalKb,
        dirNames = Array(contents.saves.size) { i -> contents.saves[i].dirName },
        titles = Array(contents.saves.size) { i -> contents.saves[i].title },
        sizesKb = IntArray(contents.saves.size) { i -> contents.saves[i].sizeKb },
        dates = Array(contents.saves.size) { i -> contents.saves[i].dateStr },
        colorsRgb = IntArray(contents.saves.size) { i -> contents.saves[i].colorRgb },
        iconMeshes = Array(contents.saves.size) { i -> contents.saves[i].iconVerts },
    )
}

/**
 * Full-screen native GLES 3.0 PlayStation 2 BIOS shell replacing EmuCoreX's 3D carousel shelf.
 */
@SuppressLint("ClickableViewAccessibility")
@Composable
fun Ps2ShellScreen(
    games: List<GameItem>,
    isLoadingGames: Boolean,
    isCoverArtDisabled: Boolean,
    onGamePathSelected: (String) -> Unit,
    onSaveSelected: (gamePath: String, slot: Int) -> Unit,
    onManageGame: (GameItem) -> Unit,
    onExitShell: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val coroutineScope = rememberCoroutineScope()
    val localeTag = context.resources.configuration.locales.toLanguageTags()

    val currentGames = rememberUpdatedState(games)
    val currentLoadingGames = rememberUpdatedState(isLoadingGames)
    val currentGameLaunch = rememberUpdatedState(onGamePathSelected)
    val currentSaveLaunch = rememberUpdatedState(onSaveSelected)
    val currentManageGame = rememberUpdatedState(onManageGame)
    val currentExit = rememberUpdatedState(onExitShell)

    val appPreferences = remember(context) { AppPreferences(context) }
    val customCoverRepo = remember(context) { CustomGameCoverRepository(context) }
    val coverArtRepo = remember(context) { CoverArtRepository(context) }
    val saveRepo = remember(context) { SaveStateRepository(context) }
    val cardRepo = remember(context) { MemoryCardRepository(context, appPreferences) }

    val shellPrefs = remember(context) {
        context.getSharedPreferences("ps2_shell_state", Context.MODE_PRIVATE)
    }

    var savesRevision by remember { mutableIntStateOf(0) }
    val saveStates by produceState<List<SaveStateEntryInfo>>(
        initialValue = Ps2ShellSessionCache.lastSaves,
        savesRevision,
        games,
        context
    ) {
        val enriched = withContext(Dispatchers.IO) {
            val raw = runCatching { saveRepo.listEntries() }.getOrDefault(emptyList())
            val firstPass = runCatching { saveRepo.enrichGlobalEntries(raw) }.getOrDefault(raw)
            val bySerial = games.mapNotNull { g ->
                normalizeSerialKey(g.serial)?.let { it to g }
            }.toMap()
            firstPass.map { entry ->
                val match = if (entry.gamePath.isNullOrBlank()) {
                    normalizeSerialKey(entry.serial)?.let { bySerial[it] }
                } else {
                    null
                }
                val base = if (match != null) {
                    entry.copy(
                        gamePath = match.path,
                        gameTitle = match.title,
                        coverArtPath = match.coverArtPath ?: entry.coverArtPath
                    )
                } else {
                    entry
                }
                val repoPreview = runCatching { saveRepo.getPreviewImagePath(base) }.getOrNull()
                val resolvedPreview = when {
                    !repoPreview.isNullOrBlank() && File(repoPreview).isFile -> repoPreview
                    !base.coverArtPath.isNullOrBlank() && File(base.coverArtPath).isFile -> base.coverArtPath
                    else -> ""
                }
                base.copy(coverArtPath = resolvedPreview)
            }
        }
        Ps2ShellSessionCache.lastSaves = enriched
        Ps2ShellSessionCache.hasLoadedSaves = true
        value = enriched
    }
    val currentSaves = rememberUpdatedState(saveStates)

    val resolvedGames by produceState<List<Ps2ResolvedGame>>(
        initialValue = Ps2ShellSessionCache.lastResolvedGames,
        games,
        saveStates,
        isCoverArtDisabled,
        context
    ) {
        val resolved = withContext(Dispatchers.IO) {
            val savesByGamePath = saveStates.groupBy { it.gamePath.orEmpty() }
            val savesBySerial = saveStates.mapNotNull { sv ->
                normalizeSerialKey(sv.serial)?.let { it to sv }
            }.groupBy({ it.first }, { it.second })

            games.map { game ->
                val customCover = customCoverRepo.findCustomCoverPath(game.path)
                val cachedCover = if (!isCoverArtDisabled) {
                    game.coverArtPath?.takeIf { File(it).isFile }
                        ?: coverArtRepo.findCachedCoverPath(game.serial)
                } else {
                    null
                }
                val resolvedCover = customCover ?: cachedCover.orEmpty()

                val matchingSaves = buildList {
                    addAll(savesByGamePath[game.path].orEmpty())
                    normalizeSerialKey(game.serial)?.let { ser ->
                        addAll(savesBySerial[ser].orEmpty())
                    }
                }.distinctBy { it.absolutePath }
                val latestSlot = matchingSaves.maxByOrNull { it.lastModified }?.slot ?: -1

                Ps2ResolvedGame(
                    title = game.title,
                    path = game.path,
                    serial = game.serial.orEmpty(),
                    coverPath = resolvedCover,
                    sizeStr = formatFileSize(game.fileSize),
                    dateStr = formatTimestamp(game.lastModified),
                    fileSizeBytes = game.fileSize,
                    saveCount = matchingSaves.size,
                    latestSaveSlot = latestSlot,
                    compatStatus = formatCompatLabel(game.pcsx2Compatibility?.status),
                )
            }
        }
        Ps2ShellSessionCache.lastResolvedGames = resolved
        value = resolved
    }
    val currentResolvedGames = rememberUpdatedState(resolvedGames)

    val memoryCardBundle by produceState(initialValue = Ps2MemoryCardBundle(), savesRevision, context) {
        value = withContext(Dispatchers.IO) {
            val assignments = runCatching { cardRepo.ensureDefaultCardsAssigned() }.getOrNull()
            val cards = runCatching { cardRepo.listCards() }.getOrDefault(emptyList())
            val readyStr = context.getString(R.string.ps2ui_card_formatted)
            val folderStr = context.getString(R.string.ps2ui_card_folder_badge)
            val s1 = assignments?.slot1?.let { name -> cards.find { it.name.equals(name, ignoreCase = true) } }
            val s2 = assignments?.slot2?.let { name -> cards.find { it.name.equals(name, ignoreCase = true) } }
            Ps2MemoryCardBundle(
                slot1Name = assignments?.slot1.orEmpty(),
                slot1Meta = formatCardMeta(context, s1),
                slot1Status = when {
                    s1?.isFolder == true -> folderStr
                    s1?.formatted == true -> readyStr
                    assignments?.slot1 != null -> readyStr
                    else -> ""
                },
                slot2Name = assignments?.slot2.orEmpty(),
                slot2Meta = formatCardMeta(context, s2),
                slot2Status = when {
                    s2?.isFolder == true -> folderStr
                    s2?.formatted == true -> readyStr
                    assignments?.slot2 != null -> readyStr
                    else -> ""
                },
                cards = cards,
            )
        }
    }
    val currentCardBundle = rememberUpdatedState(memoryCardBundle)

    val biosValidState by produceState(initialValue = Ps2ShellSessionCache.lastBiosValid, context) {
        value = withContext(Dispatchers.IO) {
            val path = appPreferences.biosPath.first()
            BiosValidator.hasUsableBiosFiles(context, path).also {
                Ps2ShellSessionCache.lastBiosValid = it
            }
        }
    }
    val currentBiosValid = rememberUpdatedState(biosValidState)

    // Boot-intro textures come from the user's own BIOS file (read as a plain file, no emulator
    // involved). Without a BIOS the shell keeps its procedural intro.
    LaunchedEffect(Unit) {
        if (Ps2ShellSessionCache.biosAssetsLoaded) return@LaunchedEffect
        withContext(Dispatchers.IO) {
            runCatching {
                val path = appPreferences.biosPath.first()
                val selection = DocumentPathResolver.prepareBiosSelection(context, path) ?: return@runCatching
                val name = selection.fileName ?: return@runCatching
                val rom = File(selection.directoryPath, name)
                if (!rom.isFile || rom.length() < 4L * 1024 * 1024 || rom.length() > 8L * 1024 * 1024) {
                    return@runCatching
                }
                if (Ps2ShellNative.nativeLoadBiosAssets(rom.readBytes())) {
                    Ps2ShellSessionCache.biosAssetsLoaded = true
                }
            }
        }
    }

    fun applyHostSettings(
        keyTone: Boolean,
        ambientSound: Boolean,
        swapConfirm: Boolean,
        keepScreenOn: Boolean,
    ) {
        Ps2ShellAudio.keyToneEnabled = keyTone
        Ps2ShellAudio.ambientEnabled = ambientSound
        Ps2ShellHost.swapConfirmButton = swapConfirm
        val window = (context as? Activity)?.window
        if (keepScreenOn) {
            window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    fun pushSettingsToNativeOnGlThread() {
        val theme = shellPrefs.getInt("theme", 0)
        val aspectMode = shellPrefs.getInt("aspect_mode", 0)
        val orbsMode = shellPrefs.getInt("orbs_mode", 0)
        val towersMode = shellPrefs.getInt("towers_mode", 0)
        val scanlinesMode = shellPrefs.getInt("scanlines_mode", 0)
        val bootAnimMode = shellPrefs.getInt("boot_anim_mode", 0)
        val keyTone = shellPrefs.getBoolean("key_tone", true)
        val ambientSound = shellPrefs.getBoolean("ambient_sound", true)
        val swapConfirm = shellPrefs.getBoolean("swap_confirm", false)
        val touchControls = shellPrefs.getBoolean("touch_controls", true)
        val time24h = shellPrefs.getBoolean("time_24h", true)
        val dateFormat = shellPrefs.getInt("date_format", 0)
        val keepScreenOn = shellPrefs.getBoolean("keep_screen_on", true)
        Ps2ShellNative.nativeSetSettings(
            theme = theme,
            aspectMode = aspectMode,
            orbsMode = orbsMode,
            towersMode = towersMode,
            scanlinesMode = scanlinesMode,
            bootAnimMode = bootAnimMode,
            keyTone = keyTone,
            ambientSound = ambientSound,
            swapConfirm = swapConfirm,
            touchControls = touchControls,
            time24h = time24h,
            dateFormat = dateFormat,
            keepScreenOn = keepScreenOn,
        )
    }

    fun pushSystemStatusOnGlThread() {
        pushSystemSnapshotToNative(Ps2ShellSessionCache.lastSystemSnapshot)
    }

    fun refreshSystemStatusAsync(targetSurface: GLSurfaceView) {
        coroutineScope.launch(Dispatchers.IO) {
            val snap = readSystemSnapshot(context, currentBiosValid.value)
            Ps2ShellSessionCache.lastSystemSnapshot = snap
            targetSurface.queueEvent { pushSystemSnapshotToNative(snap) }
        }
    }

    fun syncTouchModeOnGlThread() {
        val touchEnabled = shellPrefs.getBoolean("touch_controls", true)
        val touchActive = touchEnabled && !hasPhysicalGamepadConnected()
        val tutorialShown = shellPrefs.getBoolean("touch_tutorial_shown", false)
        val showGuide = touchActive && !tutorialShown
        if (showGuide) {
            shellPrefs.edit().putBoolean("touch_tutorial_shown", true).apply()
        }
        Ps2ShellNative.nativeSetTouchMode(touchActive, showGuide)
    }

    fun saveShellNavigationState() {
        val state = Ps2ShellNative.nativeGetState()
        if (state.size >= 4) {
            shellPrefs.edit()
                .putInt("screen", state[0])
                .putInt("main_index", state[1])
                .putInt("game_index", state[2])
                .putInt("save_index", state[3])
                .apply()
        }
    }

    val mainActivity = context as? MainActivity
    val orientationToken = remember { "ps2shell_${System.identityHashCode(Any())}" }

    remember(context) {
        runCatching {
            // Rubik: soft rounded sans, the closest bundled match to the PS2 OSD font (Cyrillic-safe).
            val base = ResourcesCompat.getFont(context, R.font.rubik_variable)
            if (base != null) {
                val light = Typeface.create(base, 400, false)
                val regular = Typeface.create(base, 400, false)
                val bold = Typeface.create(base, 600, false)
                Ps2TextRasterizer.setCustomTypefaces(light, regular, bold)
            }
        }
        true
    }

    val surface = remember(context) {
        var downX = 0f
        var downY = 0f
        var multiTouchHandled = false
        var longPressTriggered = false
        var longPressRunnable: Runnable? = null

        GLSurfaceView(context).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            setEGLContextClientVersion(3)
            preserveEGLContextOnPause = true
            setRenderer(
                Ps2ShellSurfaceRenderer(context) {
                    pushStringsToNative(context)
                    pushSettingsToNativeOnGlThread()
                    pushSystemStatusOnGlThread()
                    syncTouchModeOnGlThread()

                    val gamesSnap = currentResolvedGames.value
                    val savesSnap = currentSaves.value
                    Ps2ShellNative.nativeSetGamesLoading(currentLoadingGames.value && gamesSnap.isEmpty())
                    Ps2ShellNative.nativeSetSavesLoading(!Ps2ShellSessionCache.hasLoadedSaves && savesSnap.isEmpty())
                    pushGamesToNative(gamesSnap)
                    pushSavesToNative(context, savesSnap)
                    pushMemoryCardsToNative(context, currentCardBundle.value)
                    Ps2ShellSessionCache.slot1Contents?.let { pushMemoryCardContentsToNative(1, it) }
                    Ps2ShellSessionCache.slot2Contents?.let { pushMemoryCardContentsToNative(2, it) }

                    Ps2ShellNative.nativeSetState(
                        shellPrefs.getInt("screen", 0),
                        shellPrefs.getInt("main_index", 0),
                        shellPrefs.getInt("game_index", 0),
                        shellPrefs.getInt("save_index", 0),
                    )
                    val restorePath = Ps2ShellSessionCache.lastSelectedGamePath
                        .ifBlank { shellPrefs.getString("last_game_path", "").orEmpty() }
                    if (restorePath.isNotBlank()) {
                        Ps2ShellNative.nativeSelectGameByPath(restorePath)
                    }
                    if (Ps2ShellSessionCache.returnedFromGame) {
                        Ps2ShellSessionCache.returnedFromGame = false
                        Ps2ShellNative.nativeTriggerTransition(1)
                    } else {
                        Ps2ShellNative.nativeTriggerTransition(0)
                    }
                }
            )
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
            // The shell's animations are tuned for 60 fps; on 90/120/144 Hz panels ask the
            // compositor for 60 so the GPU isn't drawing 2x the frames for no visible gain.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        runCatching {
                            holder.surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
                        }
                    }
                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit
                    override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
                })
            }
            isFocusable = true
            isFocusableInTouchMode = true
            defaultFocusHighlightEnabled = false
            requestFocus()

            setOnKeyListener { _, _, event -> Ps2ShellHost.handleKeyEvent(event) }

            setOnTouchListener { v, event ->
                val touchEnabled = shellPrefs.getBoolean("touch_controls", true)
                if (!touchEnabled && event.pointerCount < 2) {
                    return@setOnTouchListener true
                }
                val w = v.width.coerceAtLeast(1).toFloat()
                val h = v.height.coerceAtLeast(1).toFloat()
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        downX = event.x
                        downY = event.y
                        multiTouchHandled = false
                        longPressTriggered = false
                        val nx = event.x / w
                        val ny = event.y / h
                        longPressRunnable?.let(v::removeCallbacks)
                        val lp = Runnable {
                            if (!multiTouchHandled) {
                                longPressTriggered = true
                                queueEvent { Ps2ShellNative.nativeTouchLongPress(nx, ny) }
                            }
                        }
                        longPressRunnable = lp
                        v.postDelayed(lp, 440L)
                        queueEvent { Ps2ShellNative.nativeTouchDown(nx, ny) }
                        true
                    }
                    MotionEvent.ACTION_POINTER_DOWN -> {
                        if (event.pointerCount >= 2 && !multiTouchHandled) {
                            longPressRunnable?.let(v::removeCallbacks)
                            multiTouchHandled = true
                            queueEvent { Ps2ShellNative.nativeTouchTwoFingerBack() }
                        }
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        if (abs(event.x - downX) > 28f || abs(event.y - downY) > 28f) {
                            longPressRunnable?.let(v::removeCallbacks)
                        }
                        if (!multiTouchHandled && !longPressTriggered) {
                            val nx = event.x / w
                            val ny = event.y / h
                            queueEvent { Ps2ShellNative.nativeTouchMove(nx, ny) }
                        }
                        true
                    }
                    MotionEvent.ACTION_UP -> {
                        longPressRunnable?.let(v::removeCallbacks)
                        if (!multiTouchHandled && !longPressTriggered) {
                            val nx = event.x / w
                            val ny = event.y / h
                            queueEvent { Ps2ShellNative.nativeTouchUp(nx, ny) }
                        } else {
                            queueEvent { Ps2ShellNative.nativeTouchCancel() }
                        }
                        true
                    }
                    MotionEvent.ACTION_CANCEL -> {
                        longPressRunnable?.let(v::removeCallbacks)
                        queueEvent { Ps2ShellNative.nativeTouchCancel() }
                        true
                    }
                    else -> true
                }
            }
        }
    }

    BackHandler {
        surface.queueEvent {
            Ps2ShellNative.nativeKey(Ps2ShellHost.KEY_BACK)
        }
    }

    DisposableEffect(mainActivity, orientationToken) {
        mainActivity?.setOrientationOverride(
            orientationToken,
            ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        )
        onDispose {
            mainActivity?.clearOrientationOverrideDeferred(orientationToken)
        }
    }

    val statusReceiver = remember {
        object : BroadcastReceiver() {
            override fun onReceive(c: Context?, intent: Intent?) {
                refreshSystemStatusAsync(surface)
            }
        }
    }

    DisposableEffect(surface, lifecycleOwner) {
        UiSoundManager.setNativeShellActive(true)
        Ps2ShellAudio.resumeAudio()

        applyHostSettings(
            keyTone = shellPrefs.getBoolean("key_tone", true),
            ambientSound = shellPrefs.getBoolean("ambient_sound", true),
            swapConfirm = shellPrefs.getBoolean("swap_confirm", false),
            keepScreenOn = shellPrefs.getBoolean("keep_screen_on", true),
        )

        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_BATTERY_CHANGED)
            addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            addAction(BluetoothAdapter.ACTION_CONNECTION_STATE_CHANGED)
            @Suppress("DEPRECATION")
            addAction(ConnectivityManager.CONNECTIVITY_ACTION)
            addAction("android.media.VOLUME_CHANGED_ACTION")
        }
        runCatching { context.registerReceiver(statusReceiver, filter) }

        Ps2ShellHost.setGameLaunchListener { gamePath ->
            if (gamePath.isNotBlank()) {
                Ps2ShellSessionCache.lastSelectedGamePath = gamePath
                shellPrefs.edit().putString("last_game_path", gamePath).apply()
            }
            saveShellNavigationState()
            Ps2ShellSessionCache.returnedFromGame = true
            Ps2ShellAudio.pauseAudio()
            currentGameLaunch.value(gamePath)
        }

        Ps2ShellHost.setSaveLaunchListener { gamePath, slot ->
            if (gamePath.isNotBlank()) {
                Ps2ShellSessionCache.lastSelectedGamePath = gamePath
                shellPrefs.edit().putString("last_game_path", gamePath).apply()
            }
            saveShellNavigationState()
            Ps2ShellSessionCache.returnedFromGame = true
            Ps2ShellAudio.pauseAudio()
            currentSaveLaunch.value(gamePath, slot)
        }

        Ps2ShellHost.setDeleteSaveListener { index ->
            val entry = currentSaves.value.getOrNull(index)
            if (entry != null) {
                coroutineScope.launch(Dispatchers.IO) {
                    saveRepo.deleteEntry(entry)
                    Ps2ShellSessionCache.lastSaves = Ps2ShellSessionCache.lastSaves.filterNot {
                        it.absolutePath == entry.absolutePath
                    }
                    withContext(Dispatchers.Main) {
                        savesRevision++
                    }
                }
            }
        }

        Ps2ShellHost.setMemoryCardActionListener { action, slot, cardIndex, sizeMb ->
            coroutineScope.launch(Dispatchers.IO) {
                val cards = runCatching { cardRepo.listCards() }.getOrDefault(emptyList())
                val cur = runCatching { cardRepo.currentAssignments() }.getOrNull()
                when (action) {
                    Ps2ShellHost.ACTION_CYCLE_SLOT -> {
                        if (cards.isNotEmpty()) {
                            val curName = if (slot == 1) cur?.slot1 else cur?.slot2
                            val curIdx = cards.indexOfFirst { it.name.equals(curName, ignoreCase = true) }
                            val nextCard = cards[(curIdx + 1) % cards.size]
                            cardRepo.assignCardToSlot(slot, nextCard.name)
                        }
                    }
                    Ps2ShellHost.ACTION_SWAP_SLOTS -> {
                        if (cur != null) {
                            cardRepo.assignSlots(cur.slot2, cur.slot1)
                        }
                    }
                    Ps2ShellHost.ACTION_EJECT_SLOT -> {
                        cardRepo.assignCardToSlot(slot, null)
                    }
                    Ps2ShellHost.ACTION_CREATE_PS2_CARD -> {
                        val before = cards.map { it.name }.toSet()
                        val created = cardRepo.createPs2Card("MemoryCard", sizeMb)
                        if (created) {
                            val after = runCatching { cardRepo.listCards() }.getOrDefault(emptyList())
                            val createdCard = after.firstOrNull { it.name !in before }
                                ?: after.maxByOrNull { it.modifiedTime }
                            if (createdCard != null && slot in 1..2) {
                                cardRepo.assignCardToSlot(slot, createdCard.name)
                            }
                        }
                    }
                    Ps2ShellHost.ACTION_CREATE_FOLDER_CARD -> {
                        val before = cards.map { it.name }.toSet()
                        val created = cardRepo.createFolderCard("FolderCard")
                        if (created) {
                            val after = runCatching { cardRepo.listCards() }.getOrDefault(emptyList())
                            val createdCard = after.firstOrNull { it.name !in before }
                                ?: after.maxByOrNull { it.modifiedTime }
                            if (createdCard != null && slot in 1..2) {
                                cardRepo.assignCardToSlot(slot, createdCard.name)
                            }
                        }
                    }
                    Ps2ShellHost.ACTION_MOUNT_CARD -> {
                        val target = cards.getOrNull(cardIndex)
                        if (target != null && slot in 1..2) {
                            cardRepo.assignCardToSlot(slot, target.name)
                        }
                    }
                    Ps2ShellHost.ACTION_DUPLICATE_CARD -> {
                        val target = cards.getOrNull(cardIndex)
                        if (target != null) {
                            cardRepo.duplicateCard(target, target.name.removeSuffix(".ps2") + " Copy")
                        }
                    }
                    Ps2ShellHost.ACTION_DELETE_CARD -> {
                        val target = cards.getOrNull(cardIndex)
                        if (target != null && !target.isDefaultCard) {
                            cardRepo.deleteCard(target)
                        }
                    }
                }
                withContext(Dispatchers.Main) {
                    savesRevision++
                }
            }
        }

        Ps2ShellHost.setOpenGameSettingsListener { index ->
            val game = currentGames.value.getOrNull(index)
            if (game != null) {
                if (game.path.isNotBlank()) {
                    Ps2ShellSessionCache.lastSelectedGamePath = game.path
                    shellPrefs.edit().putString("last_game_path", game.path).apply()
                }
                saveShellNavigationState()
                Ps2ShellSessionCache.returnedFromGame = true
                Ps2ShellAudio.pauseAudio()
                currentManageGame.value(game)
            }
        }

        Ps2ShellHost.setSettingsListener { arr ->
            if (arr.size >= 13) {
                val keyTone = arr[6] != 0
                val ambientSound = arr[7] != 0
                val swapConfirm = arr[8] != 0
                val touchControls = arr[9] != 0
                val time24h = arr[10] != 0
                val keepScreenOn = arr[12] != 0
                shellPrefs.edit()
                    .putInt("theme", arr[0])
                    .putInt("aspect_mode", arr[1])
                    .putInt("orbs_mode", arr[2])
                    .putInt("towers_mode", arr[3])
                    .putInt("scanlines_mode", arr[4])
                    .putInt("boot_anim_mode", arr[5])
                    .putBoolean("key_tone", keyTone)
                    .putBoolean("ambient_sound", ambientSound)
                    .putBoolean("swap_confirm", swapConfirm)
                    .putBoolean("touch_controls", touchControls)
                    .putBoolean("time_24h", time24h)
                    .putInt("date_format", arr[11])
                    .putBoolean("keep_screen_on", keepScreenOn)
                    .apply()
                applyHostSettings(keyTone, ambientSound, swapConfirm, keepScreenOn)
                surface.queueEvent { syncTouchModeOnGlThread() }
            }
        }

        Ps2ShellHost.setNavigationListener { state ->
            if (state.size >= 4) {
                shellPrefs.edit()
                    .putInt("screen", state[0])
                    .putInt("main_index", state[1])
                    .putInt("game_index", state[2])
                    .putInt("save_index", state[3])
                    .apply()
            }
        }

        Ps2ShellHost.setExitListener {
            saveShellNavigationState()
            Ps2ShellAudio.pauseAudio()
            UiSoundManager.setNativeShellActive(false)
            mainActivity?.clearOrientationOverrideDeferred(orientationToken)
            currentExit.value()
        }

        Ps2ShellHost.setKeyListener { code ->
            surface.queueEvent {
                Ps2ShellNative.nativeKey(code)
            }
        }

        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> {
                    mainActivity?.setOrientationOverride(
                        orientationToken,
                        ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                    )
                    surface.onResume()
                    surface.requestFocus()
                    Ps2ShellAudio.resumeAudio()
                    savesRevision++
                    refreshSystemStatusAsync(surface)
                    surface.queueEvent {
                        syncTouchModeOnGlThread()
                    }
                }
                Lifecycle.Event.ON_PAUSE -> {
                    saveShellNavigationState()
                    Ps2ShellAudio.pauseAudio()
                    surface.onPause()
                }
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)

        onDispose {
            saveShellNavigationState()
            Ps2ShellAudio.pauseAudio()
            UiSoundManager.setNativeShellActive(false)
            (context as? Activity)?.window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            runCatching { context.unregisterReceiver(statusReceiver) }
            lifecycleOwner.lifecycle.removeObserver(observer)
            Ps2ShellHost.setGameLaunchListener(null)
            Ps2ShellHost.setSaveLaunchListener(null)
            Ps2ShellHost.setDeleteSaveListener(null)
            Ps2ShellHost.setMemoryCardActionListener(null)
            Ps2ShellHost.setOpenGameSettingsListener(null)
            Ps2ShellHost.setSettingsListener(null)
            Ps2ShellHost.setExitListener(null)
            Ps2ShellHost.setNavigationListener(null)
            Ps2ShellHost.setKeyListener(null)
        }
    }

    LaunchedEffect(localeTag) {
        val saves = currentSaves.value
        val cards = currentCardBundle.value
        refreshSystemStatusAsync(surface)
        surface.queueEvent {
            pushStringsToNative(context)
            pushSavesToNative(context, saves)
            pushMemoryCardsToNative(context, cards)
        }
    }

    LaunchedEffect(isLoadingGames, resolvedGames) {
        val snap = resolvedGames
        val loading = isLoadingGames && snap.isEmpty()
        surface.queueEvent {
            Ps2ShellNative.nativeSetGamesLoading(loading)
            pushGamesToNative(snap)
        }
    }

    LaunchedEffect(saveStates) {
        val saves = saveStates
        val loading = !Ps2ShellSessionCache.hasLoadedSaves && saves.isEmpty()
        surface.queueEvent {
            Ps2ShellNative.nativeSetSavesLoading(loading)
            pushSavesToNative(context, saves)
        }
    }

    LaunchedEffect(memoryCardBundle) {
        val bundle = memoryCardBundle
        surface.queueEvent {
            pushMemoryCardsToNative(context, bundle)
        }
        val (c1, c2) = withContext(Dispatchers.IO) {
            val s1Card = bundle.slot1Name.takeIf { it.isNotBlank() }?.let { n ->
                bundle.cards.find { it.name.equals(n, ignoreCase = true) }
            }
            val s2Card = bundle.slot2Name.takeIf { it.isNotBlank() }?.let { n ->
                bundle.cards.find { it.name.equals(n, ignoreCase = true) }
            }
            val res1 = if (s1Card != null) {
                Ps2MemoryCardParser.parse(s1Card.path, s1Card.name)
            } else {
                Ps2McContents(bundle.slot1Name, "", 0, 0, emptyList())
            }
            val res2 = if (s2Card != null) {
                Ps2MemoryCardParser.parse(s2Card.path, s2Card.name)
            } else {
                Ps2McContents(bundle.slot2Name, "", 0, 0, emptyList())
            }
            res1 to res2
        }
        Ps2ShellSessionCache.slot1Contents = c1
        Ps2ShellSessionCache.slot2Contents = c2
        surface.queueEvent {
            pushMemoryCardContentsToNative(1, c1)
            pushMemoryCardContentsToNative(2, c2)
        }
    }

    LaunchedEffect(biosValidState) {
        refreshSystemStatusAsync(surface)
    }

    AndroidView(
        factory = { surface },
        modifier = modifier,
    )
}

package com.sbro.emucorex.data

import android.content.Context
import android.provider.Settings
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.MutablePreferences
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.floatPreferencesKey
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import com.sbro.emucorex.core.AudioDefaults
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GpuHardwareProfiles
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.core.RendererDefaults
import com.sbro.emucorex.core.normalizeUpscale
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.first
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

data class RecentGameEntry(
    val path: String,
    val title: String,
    val lastPlayedAt: Long,
    val serial: String? = null
)

data class AchievementsProfileCache(
    val username: String,
    val displayName: String,
    val avatarPath: String?,
    val points: Int,
    val softcorePoints: Int,
    val unreadMessages: Int,
    val updatedAtMillis: Long
)

data class AchievementsAccountProgressCache(
    val username: String,
    val json: String,
    val updatedAtMillis: Long
)

data class SettingsSnapshot(
    val themeMode: ThemeMode = ThemeMode.SYSTEM,
    val appFontChoice: AppFontChoice = AppFontChoice.SYSTEM,
    val appFontScale: Float = AppPreferences.DEFAULT_APP_FONT_SCALE,
    val customFontName: String? = null,
    val customFontRevision: Int = 0,
    val homeGridScale: Float = AppPreferences.DEFAULT_HOME_GRID_SCALE,
    val homeBackgroundType: HomeBackgroundType = HomeBackgroundType.NONE,
    val homeBackgroundRevision: Int = 0,
    val homeBackgroundDim: Int = AppPreferences.DEFAULT_HOME_BACKGROUND_DIM,
    val touchControlVisualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    val touchControlPressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    val gameMenuLayoutStyle: GameMenuLayoutStyle = GameMenuLayoutStyle.SIDEBAR,
    val drawerVisualStyle: DrawerVisualStyle = DrawerVisualStyle.CLASSIC,
    val hiddenDrawerItems: Set<DrawerItemId> = emptySet(),
    val gameMenuTabOrder: List<GameMenuTabId> = DefaultGameMenuTabOrder,
    val hiddenGameMenuTabs: Set<GameMenuTabId> = emptySet(),
    val gameMenuSectionOrder: List<GameMenuSectionId> = DefaultGameMenuSectionOrder,
    val hiddenGameMenuSections: Set<GameMenuSectionId> = emptySet(),
    val proUnlocked: Boolean = false,
    val languageTag: String? = null,
    val performanceProfile: Int = PerformanceProfiles.SAFE,
    val gpuHardwareProfile: Int = GpuHardwareProfiles.ADRENO,
    val renderer: Int = RendererDefaults.defaultForHardware(),
    val upscaleMultiplier: Float = 1f,
    val aspectRatio: Int = 1,
    val audioVolume: Int = AudioDefaults.VOLUME_DEFAULT,
    val audioFastForwardVolume: Int = AudioDefaults.VOLUME_DEFAULT,
    val audioMuted: Boolean = false,
    val audioInterpolation: Int = AudioDefaults.INTERPOLATION_DEFAULT,
    val audioSyncMode: Int = AudioDefaults.SYNC_DEFAULT,
    val audioLightweightSpu2: Boolean = AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT,
    val audioBackend: Int = AudioDefaults.BACKEND_DEFAULT,
    val audioBufferMs: Int = AudioDefaults.BUFFER_MS_DEFAULT,
    val audioOutputLatencyMs: Int = AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT,
    val audioMinimalOutputLatency: Boolean = AudioDefaults.MINIMAL_OUTPUT_LATENCY_DEFAULT,
    val autoProgressiveScan: Boolean = false,
    val padVibration: Boolean = true,
    val padVibrationStrength: Int = AppPreferences.DEFAULT_PAD_VIBRATION_STRENGTH,
    val padVibrationFallback: Boolean = true,
    val showFps: Boolean = false,
    val fpsOverlayMode: Int = AppPreferences.FPS_OVERLAY_MODE_DETAILED,
    val fpsOverlayCorner: Int = AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT,
    val fpsOverlayScale: Int = AppPreferences.DEFAULT_FPS_OVERLAY_SCALE,
    val fpsOverlayMetrics: Int = PerformanceOverlayMetrics.DEFAULT,
    val confirmSaveLoadActions: Boolean = true,
    val backButtonExitsGame: Boolean = false,
    val compactControls: Boolean = true,
    val keepScreenOn: Boolean = true,
    val showRecentGames: Boolean = true,
    val showHomeSearch: Boolean = false,
    val showDebugOptions: Boolean = false,
    val preferEnglishGameTitles: Boolean = false,
    val biosPath: String? = null,
    val biosValid: Boolean = false,
    val gamePath: String? = null,
    val gamePaths: List<String> = emptyList(),
    val emulatorDataPath: String? = null,
    val coverDownloadBaseUrl: String? = null,
    val coverArtStyle: Int = AppPreferences.COVER_ART_STYLE_DEFAULT,
    val setupComplete: Boolean = false,
    val enableFastBoot: Boolean = true,
    val eeCycleRate: Int = PerformanceProfiles.safeConfig.eeCycleRate,
    val eeCycleSkip: Int = PerformanceProfiles.safeConfig.eeCycleSkip,
    val enableEeRecompiler: Boolean = true,
    val enableIopRecompiler: Boolean = true,
    val enableVu0Recompiler: Boolean = true,
    val enableVu1Recompiler: Boolean = true,
    val eeFpuRoundMode: Int = AppPreferences.DEFAULT_EE_FPU_ROUND_MODE,
    val vu0RoundMode: Int = AppPreferences.DEFAULT_VU_ROUND_MODE,
    val vu1RoundMode: Int = AppPreferences.DEFAULT_VU_ROUND_MODE,
    val eeFpuClampingMode: Int = AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE,
    val vu0ClampingMode: Int = AppPreferences.DEFAULT_VU0_CLAMPING_MODE,
    val vu1ClampingMode: Int = AppPreferences.DEFAULT_VU1_CLAMPING_MODE,
    val enableGameFixes: Boolean = true,
    val enableEeTimingHack: Boolean = false,
    val enableWaitLoopSpeedhack: Boolean = true,
    val enableIntcStatSpeedhack: Boolean = true,
    val enableVuFlagHack: Boolean = true,
    val enableInstantVu1: Boolean = true,
    val enableMtvu: Boolean = true,
    val enableThreadPinning: Boolean = AppPreferences.DEFAULT_THREAD_PINNING,
    val enableFastCdvd: Boolean = false,
    val enableCheats: Boolean = false,
    val hwDownloadMode: Int = PerformanceProfiles.safeConfig.hwDownloadMode,
    val frameSkip: Int = 0,
    val skipDuplicateFrames: Boolean = true,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val sgsrMode: Int = 0,
    val casMode: Int = 0,
    val casSharpness: Int = 50,
    val tvShader: Int = GsHackDefaults.TV_SHADER_DEFAULT,
    val shadeBoostEnabled: Boolean = false,
    val shadeBoostBrightness: Int = 50,
    val shadeBoostContrast: Int = 50,
    val shadeBoostSaturation: Int = 50,
    val shadeBoostGamma: Int = 50,
    val enableWidescreenPatches: Boolean = false,
    val enableNoInterlacingPatches: Boolean = false,
    val deinterlaceMode: Int = GsHackDefaults.DEINTERLACE_MODE_DEFAULT,
    val dithering: Int = GsHackDefaults.DITHERING_DEFAULT,
    val antiBlur: Boolean = GsHackDefaults.ANTI_BLUR_DEFAULT,
    val anisotropicFiltering: Int = 0,
    val enableHwMipmapping: Boolean = GsHackDefaults.HW_MIPMAPPING_DEFAULT,
    val cpuSpriteRenderSize: Int = GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT,
    val cpuSpriteRenderLevel: Int = GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT,
    val softwareClutRender: Int = GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT,
    val gpuTargetClutMode: Int = GsHackDefaults.GPU_TARGET_CLUT_DEFAULT,
    val skipDrawStart: Int = 0,
    val skipDrawEnd: Int = 0,
    val autoFlushHardware: Int = GsHackDefaults.AUTO_FLUSH_DEFAULT,
    val cpuFramebufferConversion: Boolean = false,
    val disableDepthConversion: Boolean = false,
    val disableSafeFeatures: Boolean = false,
    val disableRenderFixes: Boolean = false,
    val preloadFrameData: Boolean = false,
    val disablePartialInvalidation: Boolean = false,
    val textureInsideRt: Int = GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT,
    val readTargetsOnClose: Boolean = false,
    val estimateTextureRegion: Boolean = false,
    val gpuPaletteConversion: Boolean = false,
    val halfPixelOffset: Int = GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT,
    val nativeScaling: Int = GsHackDefaults.NATIVE_SCALING_DEFAULT,
    val roundSprite: Int = GsHackDefaults.ROUND_SPRITE_DEFAULT,
    val bilinearUpscale: Int = GsHackDefaults.BILINEAR_UPSCALE_DEFAULT,
    val textureOffsetX: Int = 0,
    val textureOffsetY: Int = 0,
    val alignSprite: Boolean = false,
    val mergeSprite: Boolean = false,
    val forceEvenSpritePosition: Boolean = false,
    val nativePaletteDraw: Boolean = false,
    val performancePreset: Int = PerformancePresets.CUSTOM,
    val overlayScale: Int = 100,
    val overlayOpacity: Int = AppPreferences.DEFAULT_OVERLAY_OPACITY,
    val overlayShow: Boolean = true,
    val racingMode: Boolean = false,
    val touchscreenRightStick: Boolean = AppPreferences.DEFAULT_TOUCHSCREEN_RIGHT_STICK,
    val touchscreenRightStickSensitivity: Int = AppPreferences.DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY,
    val touchHaptics: Boolean = false,
    val touchHapticsPreset: Int = AppPreferences.DEFAULT_TOUCH_HAPTICS_PRESET,
    val touchHapticsStrength: Int = AppPreferences.DEFAULT_TOUCH_HAPTICS_STRENGTH,
    val gyroMode: Int = AppPreferences.GYRO_MODE_OFF,
    val gyroSensitivity: Int = AppPreferences.DEFAULT_GYRO_SENSITIVITY,
    val gyroSmoothing: Int = AppPreferences.DEFAULT_GYRO_SMOOTHING,
    val gyroInvertX: Boolean = false,
    val gyroInvertY: Boolean = false,
    val leftStickSensitivity: Int = AppPreferences.DEFAULT_STICK_SENSITIVITY,
    val rightStickSensitivity: Int = AppPreferences.DEFAULT_STICK_SENSITIVITY,
    val invertLeftStick: Boolean = false,
    val invertRightStick: Boolean = false,
    val invertLeftStickHorizontal: Boolean = false,
    val invertRightStickHorizontal: Boolean = false,
    val enableAutoGamepad: Boolean = true,
    val hideOverlayOnGamepad: Boolean = true,
    val gamepadStickDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_DEADZONE,
    val gamepadLeftStickSensitivity: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY,
    val gamepadRightStickSensitivity: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY,
    val gamepadRightStickUpToR2: Boolean = false,
    val gamepadRightStickDownToL2: Boolean = false,
    val gamepadButtonHaptics: Boolean = false,
    val pressureModifierAmount: Int = AppPreferences.DEFAULT_PRESSURE_MODIFIER_AMOUNT,
    val gamepadBindings: Map<String, Int> = emptyMap(),
    val gamepadBindingsByPad: Map<Int, Map<String, Int>> = emptyMap(),
    val gpuDriverType: Int = 0,
    val mediatekAngleOpenGl: Boolean = false,
    val customDriverPath: String? = null,
    val dev9EthernetEnabled: Boolean = false,
    val dev9EthernetDevice: String = "Auto",
    val dev9InterceptDhcp: Boolean = false,
    val dev9Dns1Mode: String = AppPreferences.DEV9_DNS_MODE_AUTO,
    val dev9Dns1: String = "0.0.0.0",
    val dev9Dns2Mode: String = AppPreferences.DEV9_DNS_MODE_AUTO,
    val dev9Dns2: String = "0.0.0.0",
    val dev9LogDhcp: Boolean = false,
    val dev9LogDns: Boolean = false,
    val dev9LocalLinkMode: Int = AppPreferences.DEV9_LOCAL_LINK_OFF,
    val dev9LocalLinkAddress: String = "192.168.43.1",
    val dev9LocalLinkPort: Int = AppPreferences.DEFAULT_LOCAL_LINK_PORT,
    val dev9LocalLinkPeerId: Int = 2,
    val dev9LocalLinkRoomCode: String = "",
    val frameLimitEnabled: Boolean = true,
    val vSyncEnabled: Boolean = false,
    val fastForwardSpeed: Float = AppPreferences.DEFAULT_FAST_FORWARD_SPEED,
    val targetFps: Int = 0,
    val ntscFramerate: Float = AppPreferences.DEFAULT_NTSC_FRAMERATE,
    val palFramerate: Float = AppPreferences.DEFAULT_PAL_FRAMERATE,
    val achievementsEnabled: Boolean = false,
    val achievementsHardcore: Boolean = false,
    val achievementsNotifications: Boolean = true,
    val achievementsLeaderboardNotifications: Boolean = true,
    val achievementsIndicators: Boolean = true,
    val achievementsLeaderboardTrackers: Boolean = true,
    val achievementsSoundEffects: Boolean = true,
    val achievementsUnlockSoundPath: String? = null,
    val achievementsUnlockSoundName: String? = null,
    val achievementsUsername: String? = null,
    val achievementsToken: String? = null
)

data class OverlayLayoutSnapshot(
    val overlayScale: Int = 100,
    val overlayOpacity: Int = AppPreferences.DEFAULT_OVERLAY_OPACITY,
    val hideOverlayOnGamepad: Boolean = true,
    val dpadOffset: Pair<Float, Float> = AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y,
    val lstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y,
    val rstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y,
    val actionOffset: Pair<Float, Float> = AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y,
    val lbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y,
    val rbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y,
    val centerOffset: Pair<Float, Float> = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
    val stickScale: Int = 100,
    val leftStickSensitivity: Int = 100,
    val rightStickSensitivity: Int = 100,
    val invertLeftStick: Boolean = false,
    val invertRightStick: Boolean = false,
    val invertLeftStickHorizontal: Boolean = false,
    val invertRightStickHorizontal: Boolean = false,
    val stickSurfaceMode: Boolean = false,
    val controlLayouts: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts()
)

data class OverlayControlLayout(
    val offset: Pair<Float, Float> = 0f to 0f,
    val scale: Int = 100,
    val widthScale: Int = 100,
    val opacity: Int = 100,
    val visible: Boolean = true,
    val surfaceOnly: Boolean = false
)

private val LEGACY_CLAMPING_PREF_KEYS = listOf(
    booleanPreferencesKey("enable_ee_clamping"),
    booleanPreferencesKey("enable_vu0_clamping"),
    booleanPreferencesKey("enable_vu1_clamping")
)

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "settings")

class AppPreferences(private val context: Context) {

    private val localePrefs = context.getSharedPreferences("ui_locale", Context.MODE_PRIVATE)

    companion object {
        const val DEV9_DNS_MODE_MANUAL = "Manual"
        const val DEV9_DNS_MODE_AUTO = "Auto"
        const val DEV9_DNS_MODE_INTERNAL = "Internal"
        const val DEV9_LOCAL_LINK_OFF = 0
        const val DEV9_LOCAL_LINK_HOST = 1
        const val DEV9_LOCAL_LINK_JOIN = 2
        const val DEFAULT_LOCAL_LINK_PORT = 19072
        private const val CURRENT_OVERLAY_LAYOUT_VERSION = 16
        const val DEFAULT_NTSC_FRAMERATE = 59.94f
        const val DEFAULT_THREAD_PINNING = true
        const val DEFAULT_PAL_FRAMERATE = 50f
        const val DEFAULT_FAST_FORWARD_SPEED = 2.0f
        const val DEFAULT_APP_FONT_SCALE = 1.0f
        const val MIN_APP_FONT_SCALE = 0.75f
        const val MAX_APP_FONT_SCALE = 1.50f
        const val DEFAULT_HOME_GRID_SCALE = 1.0f
        const val MIN_HOME_GRID_SCALE = 0.60f
        const val MAX_HOME_GRID_SCALE = 1.60f
        const val DEFAULT_HOME_BACKGROUND_DIM = 48
        const val MIN_FAST_FORWARD_SPEED = 1.25f
        const val MAX_FAST_FORWARD_SPEED = 5.0f
        private const val LEGACY_DEFAULT_LSTICK_OFFSET_X = 18f
        private const val LEGACY_DEFAULT_LSTICK_OFFSET_Y = -214f
        private const val LEFT_SIDE_LAYOUT_SHIFT_X = -8f
        private const val PREVIOUS_DEFAULT_DPAD_OFFSET_X = 20f
        const val DEFAULT_DPAD_OFFSET_X = 0f
        const val DEFAULT_DPAD_OFFSET_Y = -141f
        private const val PREVIOUS_DEFAULT_LSTICK_OFFSET_X = 20f
        const val DEFAULT_LSTICK_OFFSET_X = 0f
        const val DEFAULT_LSTICK_OFFSET_Y = -141f
        const val DEFAULT_RSTICK_OFFSET_X = 0f
        const val DEFAULT_RSTICK_OFFSET_Y = 0f
        const val DEFAULT_ACTION_OFFSET_X = 40f
        const val DEFAULT_ACTION_OFFSET_Y = -176f
        const val DEFAULT_LBTN_OFFSET_X = 74f
        const val DEFAULT_LBTN_OFFSET_Y = 78f
        const val DEFAULT_RBTN_OFFSET_X = -74f
        const val DEFAULT_RBTN_OFFSET_Y = 78f
        private const val PREVIOUS_DEFAULT_CENTER_OFFSET_X = 32f
        private const val PREVIOUS_DEFAULT_CENTER_OFFSET_Y = 10f
        const val DEFAULT_CENTER_OFFSET_X = 0f
        const val DEFAULT_CENTER_OFFSET_Y = 10f
        const val DEFAULT_STICK_SENSITIVITY = 100
        const val OVERLAY_CONTROL_SCALE_MIN = 50
        const val OVERLAY_CONTROL_SCALE_MAX = 500
        const val OVERLAY_CONTROL_SCALE_DEFAULT = 100
        const val OVERLAY_CONTROL_OPACITY_MIN = 20
        const val OVERLAY_CONTROL_OPACITY_MAX = 100
        const val OVERLAY_CONTROL_OPACITY_DEFAULT = 100
        const val OVERLAY_OPACITY_MIN = 0
        const val OVERLAY_OPACITY_MAX = 100
        const val DEFAULT_OVERLAY_OPACITY = 80
        const val DEFAULT_TOUCHSCREEN_RIGHT_STICK = false
        const val TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN = 50
        const val TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX = 200
        const val DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY = 100
        const val DEFAULT_GAMEPAD_STICK_DEADZONE = 15
        const val DEFAULT_GAMEPAD_STICK_SENSITIVITY = 100
        const val DEFAULT_PRESSURE_MODIFIER_AMOUNT = 50
        const val DEFAULT_PAD_VIBRATION_STRENGTH = 100
        const val DEFAULT_TOUCH_HAPTICS_STRENGTH = 60
        const val TOUCH_HAPTICS_PRESET_SOFT = 0
        const val TOUCH_HAPTICS_PRESET_BALANCED = 1
        const val TOUCH_HAPTICS_PRESET_CRISP = 2
        const val TOUCH_HAPTICS_PRESET_STRONG = 3
        const val DEFAULT_TOUCH_HAPTICS_PRESET = TOUCH_HAPTICS_PRESET_BALANCED
        const val GYRO_MODE_OFF = 0
        const val GYRO_MODE_AIM = 1
        const val GYRO_MODE_STEERING = 2
        const val DEFAULT_GYRO_SENSITIVITY = 100
        const val DEFAULT_GYRO_SMOOTHING = 45
        const val COVER_ART_STYLE_DISABLED = -1
        const val COVER_ART_STYLE_DEFAULT = 0
        const val COVER_ART_STYLE_3D = 1
        const val FPS_OVERLAY_MODE_SIMPLE = 0
        const val FPS_OVERLAY_MODE_DETAILED = 1
        const val FPS_OVERLAY_CORNER_TOP_LEFT = 0
        const val FPS_OVERLAY_CORNER_TOP_RIGHT = 1
        const val FPS_OVERLAY_CORNER_BOTTOM_LEFT = 2
        const val FPS_OVERLAY_CORNER_BOTTOM_RIGHT = 3
        const val MIN_FPS_OVERLAY_SCALE = 75
        const val MAX_FPS_OVERLAY_SCALE = 200
        const val DEFAULT_FPS_OVERLAY_SCALE = 100
        const val FLOAT_ROUND_NEAREST = 0
        const val FLOAT_ROUND_NEGATIVE = 1
        const val FLOAT_ROUND_POSITIVE = 2
        const val FLOAT_ROUND_CHOP = 3
        const val CLAMPING_NONE = 0
        const val CLAMPING_NORMAL = 1
        const val CLAMPING_EXTRA = 2
        const val CLAMPING_FULL = 3
        const val DEFAULT_EE_FPU_ROUND_MODE = FLOAT_ROUND_CHOP
        const val DEFAULT_VU_ROUND_MODE = FLOAT_ROUND_CHOP
        const val DEFAULT_EE_FPU_CLAMPING_MODE = CLAMPING_NORMAL
        const val DEFAULT_VU0_CLAMPING_MODE = CLAMPING_NORMAL
        const val DEFAULT_VU1_CLAMPING_MODE = CLAMPING_NONE

        fun defaultOverlayControlLayouts(stickScale: Int = OVERLAY_CONTROL_SCALE_DEFAULT): Map<String, OverlayControlLayout> = mapOf(
            "l2" to OverlayControlLayout(),
            "l1" to OverlayControlLayout(),
            "r2" to OverlayControlLayout(),
            "r1" to OverlayControlLayout(),
            "dpad_up" to OverlayControlLayout(visible = false),
            "dpad_down" to OverlayControlLayout(visible = false),
            "dpad_left" to OverlayControlLayout(visible = false),
            "dpad_right" to OverlayControlLayout(visible = false),
            "dpad_cluster" to OverlayControlLayout(visible = false),
            "left_stick" to OverlayControlLayout(scale = stickScale, widthScale = 160, visible = true),
            "triangle" to OverlayControlLayout(),
            "cross" to OverlayControlLayout(),
            "square" to OverlayControlLayout(),
            "circle" to OverlayControlLayout(),
            "right_stick" to OverlayControlLayout(scale = stickScale, widthScale = 160, visible = false),
            "select" to OverlayControlLayout(scale = 80),
            "left_input_toggle" to OverlayControlLayout(scale = 80, visible = true),
            "pressure" to OverlayControlLayout(scale = 80, visible = false),
            "start" to OverlayControlLayout(scale = 80),
            "l3" to OverlayControlLayout(visible = false),
            "r3" to OverlayControlLayout(visible = false)
        )

        private val THEME_MODE = intPreferencesKey("theme_mode")
        private val APP_FONT_CHOICE = intPreferencesKey("app_font_choice")
        private val APP_FONT_SCALE = floatPreferencesKey("app_font_scale")
        private val CUSTOM_FONT_NAME = stringPreferencesKey("custom_font_name")
        private val CUSTOM_FONT_REVISION = intPreferencesKey("custom_font_revision")
        private val HOME_GRID_SCALE = floatPreferencesKey("home_grid_scale")
        private val HOME_BACKGROUND_TYPE = intPreferencesKey("home_background_type")
        private val HOME_BACKGROUND_REVISION = intPreferencesKey("home_background_revision")
        private val HOME_BACKGROUND_DIM = intPreferencesKey("home_background_dim")
        private val TOUCH_CONTROL_VISUAL_STYLE = intPreferencesKey("touch_control_visual_style")
        private val TOUCH_CONTROL_PRESS_EFFECT = intPreferencesKey("touch_control_press_effect")
        private val GAME_MENU_LAYOUT_STYLE = intPreferencesKey("game_menu_layout_style")
        private val DRAWER_VISUAL_STYLE = intPreferencesKey("drawer_visual_style")
        private val HIDDEN_DRAWER_ITEMS = stringPreferencesKey("hidden_drawer_items")
        private val GAME_MENU_TAB_ORDER = stringPreferencesKey("game_menu_tab_order")
        private val HIDDEN_GAME_MENU_TABS = stringPreferencesKey("hidden_game_menu_tabs")
        private val GAME_MENU_SECTION_ORDER = stringPreferencesKey("game_menu_section_order")
        private val HIDDEN_GAME_MENU_SECTIONS = stringPreferencesKey("hidden_game_menu_sections")
        private val PRO_UNLOCKED = booleanPreferencesKey("pro_unlocked")
        private val WELCOME_DIALOG_SHOWN = booleanPreferencesKey("welcome_dialog_shown")
        private val MEDIATEK_SETTINGS_NOTICE_SHOWN =
            booleanPreferencesKey("mediatek_settings_notice_shown")
        private val IN_APP_REVIEW_QUALIFYING_SESSION_COUNT =
            intPreferencesKey("in_app_review_qualifying_session_count")
        private val IN_APP_REVIEW_TOTAL_ACTIVE_PLAY_TIME_MS =
            longPreferencesKey("in_app_review_total_active_play_time_ms")
        private val IN_APP_REVIEW_LAST_ATTEMPT_AT_MS =
            longPreferencesKey("in_app_review_last_attempt_at_ms")
        private val IN_APP_REVIEW_REQUESTED = booleanPreferencesKey("in_app_review_requested")
        private val RENDERER = intPreferencesKey("renderer")
        private val UPSCALE = floatPreferencesKey("upscale_multiplier_v2")
        private val UPSCALE_LEGACY = intPreferencesKey("upscale_multiplier")
        private val BIOS_PATH = stringPreferencesKey("bios_path")
        private val GAME_PATH = stringPreferencesKey("game_path")
        private val GAME_PATHS = stringPreferencesKey("game_paths")
        private val EMULATOR_DATA_PATH = stringPreferencesKey("emulator_data_path")
        private val COVER_DOWNLOAD_BASE_URL = stringPreferencesKey("cover_download_base_url")
        private val COVER_ART_STYLE = intPreferencesKey("cover_art_style")
        private val ONBOARDING_COMPLETED = booleanPreferencesKey("onboarding_completed")
        private val PERFORMANCE_PROFILE = intPreferencesKey("performance_profile")
        private val GPU_HARDWARE_PROFILE = intPreferencesKey("gpu_hardware_profile")
        private val LANGUAGE_TAG = stringPreferencesKey("language_tag")
        private val ASPECT_RATIO = intPreferencesKey("aspect_ratio")
        private val AUDIO_VOLUME = intPreferencesKey("audio_volume")
        private val AUDIO_FAST_FORWARD_VOLUME = intPreferencesKey("audio_fast_forward_volume")
        private val AUDIO_MUTED = booleanPreferencesKey("audio_muted")
        private val AUDIO_INTERPOLATION = intPreferencesKey("audio_interpolation")
        private val AUDIO_SYNC_MODE = intPreferencesKey("audio_sync_mode")
        private val AUDIO_LIGHTWEIGHT_SPU2 = booleanPreferencesKey("audio_lightweight_spu2")
        private val AUDIO_BACKEND = intPreferencesKey("audio_backend")
        private val AUDIO_BUFFER_MS = intPreferencesKey("audio_buffer_ms")
        private val AUDIO_OUTPUT_LATENCY_MS = intPreferencesKey("audio_output_latency_ms")
        private val AUDIO_MINIMAL_OUTPUT_LATENCY = booleanPreferencesKey("audio_minimal_output_latency")
        private val AUTO_PROGRESSIVE_SCAN = booleanPreferencesKey("auto_progressive_scan")
        private val PAD_VIBRATION = booleanPreferencesKey("pad_vibration")
        private val PAD_VIBRATION_STRENGTH = intPreferencesKey("pad_vibration_strength")
        private val PAD_VIBRATION_FALLBACK = booleanPreferencesKey("pad_vibration_fallback")
        private val SHOW_FPS = booleanPreferencesKey("show_fps")
        private val FPS_OVERLAY_MODE = intPreferencesKey("fps_overlay_mode")
        private val FPS_OVERLAY_CORNER = intPreferencesKey("fps_overlay_corner")
        private val FPS_OVERLAY_SCALE = intPreferencesKey("fps_overlay_scale")
        private val FPS_OVERLAY_METRICS = intPreferencesKey("fps_overlay_metrics")
        private val CONFIRM_SAVE_LOAD_ACTIONS = booleanPreferencesKey("confirm_save_load_actions")
        private val BACK_BUTTON_EXITS_GAME = booleanPreferencesKey("back_button_exits_game")
        private val COMPACT_CONTROLS = booleanPreferencesKey("compact_controls")
        private val KEEP_SCREEN_ON = booleanPreferencesKey("keep_screen_on")
        private val SHOW_RECENT_GAMES = booleanPreferencesKey("show_recent_games")
        private val SHOW_HOME_SEARCH = booleanPreferencesKey("show_home_search")
        private val SHOW_DEBUG_OPTIONS = booleanPreferencesKey("show_debug_options")
        private val PREFER_ENGLISH_GAME_TITLES = booleanPreferencesKey("prefer_english_game_titles")
        private val RECENT_GAMES = stringPreferencesKey("recent_games")
        private val HOME_LIBRARY_VIEW_MODE = intPreferencesKey("home_library_view_mode")
        private const val MAX_RECENT_GAMES = 8
        // Overlay customization
        private val OVERLAY_SCALE = intPreferencesKey("overlay_scale")
        private val OVERLAY_OPACITY = intPreferencesKey("overlay_opacity")
        private val OVERLAY_SHOW = booleanPreferencesKey("overlay_show")
        private val RACING_MODE = booleanPreferencesKey("racing_mode")
        private val TOUCHSCREEN_RIGHT_STICK = booleanPreferencesKey("touchscreen_right_stick")
        private val TOUCHSCREEN_RIGHT_STICK_SENSITIVITY = intPreferencesKey("touchscreen_right_stick_sensitivity")
        // Extended emulator settings
        private val ENABLE_FAST_BOOT = booleanPreferencesKey("enable_fast_boot")
        private val EE_CYCLE_RATE = intPreferencesKey("ee_cycle_rate")
        private val EE_CYCLE_SKIP = intPreferencesKey("ee_cycle_skip")
        private val ENABLE_EE_RECOMPILER = booleanPreferencesKey("enable_ee_recompiler")
        private val ENABLE_IOP_RECOMPILER = booleanPreferencesKey("enable_iop_recompiler")
        private val ENABLE_VU0_RECOMPILER = booleanPreferencesKey("enable_vu0_recompiler")
        private val ENABLE_VU1_RECOMPILER = booleanPreferencesKey("enable_vu1_recompiler")
        private val EE_FPU_ROUND_MODE = intPreferencesKey("ee_fpu_round_mode")
        private val VU0_ROUND_MODE = intPreferencesKey("vu0_round_mode")
        private val VU1_ROUND_MODE = intPreferencesKey("vu1_round_mode")
        private val EE_FPU_CLAMPING_MODE = intPreferencesKey("ee_fpu_clamping_mode")
        private val VU0_CLAMPING_MODE = intPreferencesKey("vu0_clamping_mode")
        private val VU1_CLAMPING_MODE = intPreferencesKey("vu1_clamping_mode")
        private val ENABLE_GAME_FIXES = booleanPreferencesKey("enable_game_fixes")
        private val ENABLE_EE_TIMING_HACK = booleanPreferencesKey("enable_ee_timing_hack")
        private val ENABLE_WAIT_LOOP_SPEEDHACK = booleanPreferencesKey("enable_wait_loop_speedhack")
        private val ENABLE_INTC_STAT_SPEEDHACK = booleanPreferencesKey("enable_intc_stat_speedhack")
        private val ENABLE_VU_FLAG_HACK = booleanPreferencesKey("enable_vu_flag_hack")
        private val ENABLE_INSTANT_VU1 = booleanPreferencesKey("enable_instant_vu1")
        private val ENABLE_MTVU = booleanPreferencesKey("enable_mtvu")
        private val ENABLE_THREAD_PINNING = booleanPreferencesKey("enable_thread_pinning")
        private val ENABLE_FAST_CDVD = booleanPreferencesKey("enable_fast_cdvd")
        private val ENABLE_CHEATS = booleanPreferencesKey("enable_cheats")
        private val HW_DOWNLOAD_MODE = intPreferencesKey("hw_download_mode")
        private val FRAME_SKIP = intPreferencesKey("frame_skip")
        private val SKIP_DUPLICATE_FRAMES = booleanPreferencesKey("skip_duplicate_frames")
        private val TEXTURE_FILTERING = intPreferencesKey("texture_filtering")
        private val TRILINEAR_FILTERING = intPreferencesKey("trilinear_filtering")
        private val BLENDING_ACCURACY = intPreferencesKey("blending_accuracy")
        private val TEXTURE_PRELOADING = intPreferencesKey("texture_preloading")
        private val TEXTURE_REPLACEMENTS_ENABLED = booleanPreferencesKey("texture_replacements_enabled")
        private val TEXTURE_REPLACEMENTS_ASYNC = booleanPreferencesKey("texture_replacements_async")
        private val TEXTURE_REPLACEMENTS_PRECACHE = booleanPreferencesKey("texture_replacements_precache")
        private val TEXTURE_DUMPING_ENABLED = booleanPreferencesKey("texture_dumping_enabled")
        private val ENABLE_FXAA = booleanPreferencesKey("enable_fxaa")
        private val SGSR_MODE = intPreferencesKey("sgsr_mode")
        private val CAS_MODE = intPreferencesKey("cas_mode")
        private val CAS_SHARPNESS = intPreferencesKey("cas_sharpness")
        private val TV_SHADER = intPreferencesKey("tv_shader")
        private val SHADEBOOST_ENABLED = booleanPreferencesKey("shadeboost_enabled")
        private val SHADEBOOST_BRIGHTNESS = intPreferencesKey("shadeboost_brightness")
        private val SHADEBOOST_CONTRAST = intPreferencesKey("shadeboost_contrast")
        private val SHADEBOOST_SATURATION = intPreferencesKey("shadeboost_saturation")
        private val SHADEBOOST_GAMMA = intPreferencesKey("shadeboost_gamma")
        private val ENABLE_WIDESCREEN_PATCHES = booleanPreferencesKey("enable_widescreen_patches")
        private val ENABLE_NO_INTERLACING_PATCHES = booleanPreferencesKey("enable_no_interlacing_patches")
        private val DEINTERLACE_MODE = intPreferencesKey("deinterlace_mode")
        private val DITHERING = intPreferencesKey("dithering")
        private val ANTI_BLUR = booleanPreferencesKey("anti_blur")
        private val ANISOTROPIC_FILTERING = intPreferencesKey("anisotropic_filtering")
        private val ENABLE_HW_MIPMAPPING = booleanPreferencesKey("enable_hw_mipmapping")
        private val CPU_SPRITE_RENDER_SIZE = intPreferencesKey("cpu_sprite_render_size")
        private val CPU_SPRITE_RENDER_LEVEL = intPreferencesKey("cpu_sprite_render_level")
        private val SOFTWARE_CLUT_RENDER = intPreferencesKey("software_clut_render")
        private val GPU_TARGET_CLUT_MODE = intPreferencesKey("gpu_target_clut_mode")
        private val SKIP_DRAW_START = intPreferencesKey("skip_draw_start")
        private val SKIP_DRAW_END = intPreferencesKey("skip_draw_end")
        private val AUTO_FLUSH_HARDWARE = intPreferencesKey("auto_flush_hardware")
        private val CPU_FRAMEBUFFER_CONVERSION = booleanPreferencesKey("cpu_framebuffer_conversion")
        private val DISABLE_DEPTH_CONVERSION = booleanPreferencesKey("disable_depth_conversion")
        private val DISABLE_SAFE_FEATURES = booleanPreferencesKey("disable_safe_features")
        private val DISABLE_RENDER_FIXES = booleanPreferencesKey("disable_render_fixes")
        private val PRELOAD_FRAME_DATA = booleanPreferencesKey("preload_frame_data")
        private val DISABLE_PARTIAL_INVALIDATION = booleanPreferencesKey("disable_partial_invalidation")
        private val TEXTURE_INSIDE_RT = intPreferencesKey("texture_inside_rt")
        private val READ_TARGETS_ON_CLOSE = booleanPreferencesKey("read_targets_on_close")
        private val ESTIMATE_TEXTURE_REGION = booleanPreferencesKey("estimate_texture_region")
        private val GPU_PALETTE_CONVERSION = booleanPreferencesKey("gpu_palette_conversion")
        private val HALF_PIXEL_OFFSET = intPreferencesKey("half_pixel_offset")
        private val NATIVE_SCALING = intPreferencesKey("native_scaling")
        private val ROUND_SPRITE = intPreferencesKey("round_sprite")
        private val BILINEAR_UPSCALE = intPreferencesKey("bilinear_upscale")
        private val TEXTURE_OFFSET_X = intPreferencesKey("texture_offset_x")
        private val TEXTURE_OFFSET_Y = intPreferencesKey("texture_offset_y")
        private val ALIGN_SPRITE = booleanPreferencesKey("align_sprite")
        private val MERGE_SPRITE = booleanPreferencesKey("merge_sprite")
        private val FORCE_EVEN_SPRITE_POSITION = booleanPreferencesKey("force_even_sprite_position")
        private val NATIVE_PALETTE_DRAW = booleanPreferencesKey("native_palette_draw")
        private val PERFORMANCE_PRESET = intPreferencesKey("performance_preset")
        private val ENABLE_AUTO_GAMEPAD = booleanPreferencesKey("enable_auto_gamepad")
        private val HIDE_OVERLAY_ON_GAMEPAD = booleanPreferencesKey("hide_overlay_on_gamepad")
        private val TOUCH_HAPTICS = booleanPreferencesKey("touch_haptics")
        private val TOUCH_HAPTICS_PRESET = intPreferencesKey("touch_haptics_preset")
        private val TOUCH_HAPTICS_STRENGTH = intPreferencesKey("touch_haptics_strength")
        private val GYRO_MODE = intPreferencesKey("gyro_mode")
        private val GYRO_SENSITIVITY = intPreferencesKey("gyro_sensitivity")
        private val GYRO_SMOOTHING = intPreferencesKey("gyro_smoothing")
        private val GYRO_INVERT_X = booleanPreferencesKey("gyro_invert_x")
        private val GYRO_INVERT_Y = booleanPreferencesKey("gyro_invert_y")
        private val GAMEPAD_BUTTON_HAPTICS = booleanPreferencesKey("gamepad_button_haptics")
        private val PRESSURE_MODIFIER_AMOUNT = intPreferencesKey("pressure_modifier_amount")
        private val GAMEPAD_STICK_DEADZONE = intPreferencesKey("gamepad_stick_deadzone")
        private val GAMEPAD_LEFT_STICK_SENSITIVITY = intPreferencesKey("gamepad_left_stick_sensitivity")
        private val GAMEPAD_RIGHT_STICK_SENSITIVITY = intPreferencesKey("gamepad_right_stick_sensitivity")
        private val GAMEPAD_RIGHT_STICK_UP_TO_R2 = booleanPreferencesKey("gamepad_right_stick_up_to_r2")
        private val GAMEPAD_RIGHT_STICK_DOWN_TO_L2 = booleanPreferencesKey("gamepad_right_stick_down_to_l2")
        private val GAMEPAD_BINDINGS = stringPreferencesKey("gamepad_bindings")
        private val GPU_DRIVER_TYPE = intPreferencesKey("gpu_driver_type")
        private val MEDIATEK_ANGLE_OPENGL = booleanPreferencesKey("mediatek_angle_opengl")
        private val CUSTOM_DRIVER_PATH = stringPreferencesKey("custom_driver_path")
        private val DEV9_ETHERNET_ENABLED = booleanPreferencesKey("dev9_ethernet_enabled")
        private val DEV9_ETHERNET_DEVICE = stringPreferencesKey("dev9_ethernet_device")
        private val DEV9_INTERCEPT_DHCP = booleanPreferencesKey("dev9_intercept_dhcp")
        private val DEV9_DNS1_MODE = stringPreferencesKey("dev9_dns1_mode")
        private val DEV9_DNS1 = stringPreferencesKey("dev9_dns1")
        private val DEV9_DNS2_MODE = stringPreferencesKey("dev9_dns2_mode")
        private val DEV9_DNS2 = stringPreferencesKey("dev9_dns2")
        private val DEV9_LOG_DHCP = booleanPreferencesKey("dev9_log_dhcp")
        private val DEV9_LOG_DNS = booleanPreferencesKey("dev9_log_dns")
        private val DEV9_LOCAL_LINK_MODE = intPreferencesKey("dev9_local_link_mode")
        private val DEV9_LOCAL_LINK_ADDRESS = stringPreferencesKey("dev9_local_link_address")
        private val DEV9_LOCAL_LINK_PORT = intPreferencesKey("dev9_local_link_port")
        private val DEV9_LOCAL_LINK_PEER_ID = intPreferencesKey("dev9_local_link_peer_id")
        private val DEV9_LOCAL_LINK_ROOM_CODE = stringPreferencesKey("dev9_local_link_room_code")
        private val FRAME_LIMIT_ENABLED = booleanPreferencesKey("frame_limit_enabled")
        private val VSYNC_ENABLED = booleanPreferencesKey("vsync_enabled")
        private val FAST_FORWARD_SPEED = floatPreferencesKey("fast_forward_speed")
        private val TARGET_FPS = intPreferencesKey("target_fps")
        private val NTSC_FRAMERATE = floatPreferencesKey("ntsc_framerate")
        private val PAL_FRAMERATE = floatPreferencesKey("pal_framerate")
        private val AUTO_SAVE_ENABLED = booleanPreferencesKey("auto_save_enabled")
        private val AUTO_SAVE_INTERVAL_MINUTES = intPreferencesKey("auto_save_interval_minutes")
        private val MEMORY_CARD_SLOT1 = stringPreferencesKey("memory_card_slot_1")
        private val MEMORY_CARD_SLOT2 = stringPreferencesKey("memory_card_slot_2")

        // Control Layout Customization
        private val DPAD_OFFSET = stringPreferencesKey("dpad_offset")
        private val LSTICK_OFFSET = stringPreferencesKey("lstick_offset")
        private val RSTICK_OFFSET = stringPreferencesKey("rstick_offset")
        private val ACTION_OFFSET = stringPreferencesKey("action_offset")
        private val LBTN_OFFSET = stringPreferencesKey("lbtn_offset")
        private val RBTN_OFFSET = stringPreferencesKey("rbtn_offset")
        private val CENTER_OFFSET = stringPreferencesKey("center_offset")
        private val STICK_SCALE = intPreferencesKey("stick_scale")
        private val LEFT_STICK_SENSITIVITY = intPreferencesKey("left_stick_sensitivity")
        private val RIGHT_STICK_SENSITIVITY = intPreferencesKey("right_stick_sensitivity")
        private val INVERT_LEFT_STICK = booleanPreferencesKey("invert_left_stick")
        private val INVERT_RIGHT_STICK = booleanPreferencesKey("invert_right_stick")
        private val INVERT_LEFT_STICK_HORIZONTAL = booleanPreferencesKey("invert_left_stick_horizontal")
        private val INVERT_RIGHT_STICK_HORIZONTAL = booleanPreferencesKey("invert_right_stick_horizontal")
        private val STICK_SURFACE_MODE = booleanPreferencesKey("stick_surface_mode")
        private val CONTROL_LAYOUTS = stringPreferencesKey("control_layouts")
        private val OVERLAY_LAYOUT_VERSION = intPreferencesKey("overlay_layout_version")
        private val ACHIEVEMENTS_ENABLED = booleanPreferencesKey("achievements_enabled")
        private val ACHIEVEMENTS_HARDCORE = booleanPreferencesKey("achievements_hardcore")
        private val ACHIEVEMENTS_NOTIFICATIONS = booleanPreferencesKey("achievements_notifications")
        private val ACHIEVEMENTS_LEADERBOARD_NOTIFICATIONS = booleanPreferencesKey("achievements_leaderboard_notifications")
        private val ACHIEVEMENTS_INDICATORS = booleanPreferencesKey("achievements_indicators")
        private val ACHIEVEMENTS_LEADERBOARD_TRACKERS = booleanPreferencesKey("achievements_leaderboard_trackers")
        private val ACHIEVEMENTS_SOUND_EFFECTS = booleanPreferencesKey("achievements_sound_effects")
        private val ACHIEVEMENTS_UNLOCK_SOUND_PATH = stringPreferencesKey("achievements_unlock_sound_path")
        private val ACHIEVEMENTS_UNLOCK_SOUND_NAME = stringPreferencesKey("achievements_unlock_sound_name")
        private val ACHIEVEMENTS_USERNAME = stringPreferencesKey("achievements_username")
        private val ACHIEVEMENTS_TOKEN = stringPreferencesKey("achievements_token")
        private val ACHIEVEMENTS_LOGIN_TIMESTAMP = stringPreferencesKey("achievements_login_timestamp")
        private val ACHIEVEMENTS_AVATAR_PATH = stringPreferencesKey("achievements_avatar_path")
        private val ACHIEVEMENTS_PROFILE_USERNAME = stringPreferencesKey("achievements_profile_username")
        private val ACHIEVEMENTS_DISPLAY_NAME = stringPreferencesKey("achievements_display_name")
        private val ACHIEVEMENTS_POINTS = intPreferencesKey("achievements_points")
        private val ACHIEVEMENTS_SOFTCORE_POINTS = intPreferencesKey("achievements_softcore_points")
        private val ACHIEVEMENTS_UNREAD_MESSAGES = intPreferencesKey("achievements_unread_messages")
        private val ACHIEVEMENTS_PROFILE_UPDATED_AT = longPreferencesKey("achievements_profile_updated_at")
        private val ACHIEVEMENTS_REMEMBER_PASSWORD = booleanPreferencesKey("achievements_remember_password")
        private val ACHIEVEMENTS_PASSWORD = stringPreferencesKey("achievements_password")
        private val ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON = stringPreferencesKey("achievements_account_progress_json")
        private val ACHIEVEMENTS_ACCOUNT_PROGRESS_USERNAME = stringPreferencesKey("achievements_account_progress_username")
        private val ACHIEVEMENTS_ACCOUNT_PROGRESS_UPDATED_AT = longPreferencesKey("achievements_account_progress_updated_at")
    }
    private fun readThemeMode(prefs: Preferences): ThemeMode {
        return when (prefs[THEME_MODE]) {
            1 -> ThemeMode.LIGHT
            2 -> ThemeMode.DARK
            3 -> if (prefs[PRO_UNLOCKED] == true) ThemeMode.PRO else ThemeMode.SYSTEM
            else -> ThemeMode.SYSTEM
        }
    }

    // Theme
    val themeMode: Flow<ThemeMode> = context.dataStore.data.map { prefs -> readThemeMode(prefs) }

    val appFontChoice: Flow<AppFontChoice> = context.dataStore.data
        .map { prefs -> AppFontChoice.fromPreference(prefs[APP_FONT_CHOICE]) }
        .distinctUntilChanged()

    val appFontScale: Flow<Float> = context.dataStore.data
        .map { prefs -> (prefs[APP_FONT_SCALE] ?: DEFAULT_APP_FONT_SCALE).coerceIn(MIN_APP_FONT_SCALE, MAX_APP_FONT_SCALE) }
        .distinctUntilChanged()

    val customFontName: Flow<String?> = context.dataStore.data
        .map { prefs -> prefs[CUSTOM_FONT_NAME]?.takeIf(String::isNotBlank) }
        .distinctUntilChanged()

    val customFontRevision: Flow<Int> = context.dataStore.data
        .map { prefs -> (prefs[CUSTOM_FONT_REVISION] ?: 0).coerceAtLeast(0) }
        .distinctUntilChanged()

    val homeGridScale: Flow<Float> = context.dataStore.data
        .map { prefs -> (prefs[HOME_GRID_SCALE] ?: DEFAULT_HOME_GRID_SCALE).coerceIn(MIN_HOME_GRID_SCALE, MAX_HOME_GRID_SCALE) }
        .distinctUntilChanged()

    val homeBackgroundType: Flow<HomeBackgroundType> = context.dataStore.data
        .map { prefs -> HomeBackgroundType.fromPreference(prefs[HOME_BACKGROUND_TYPE]) }
        .distinctUntilChanged()

    val homeBackgroundRevision: Flow<Int> = context.dataStore.data
        .map { prefs -> (prefs[HOME_BACKGROUND_REVISION] ?: 0).coerceAtLeast(0) }
        .distinctUntilChanged()

    val homeBackgroundDim: Flow<Int> = context.dataStore.data
        .map { prefs -> (prefs[HOME_BACKGROUND_DIM] ?: DEFAULT_HOME_BACKGROUND_DIM).coerceIn(0, 85) }
        .distinctUntilChanged()

    val touchControlVisualStyle: Flow<TouchControlVisualStyle> = context.dataStore.data
        .map { prefs -> TouchControlVisualStyle.fromPreference(prefs[TOUCH_CONTROL_VISUAL_STYLE]) }
        .distinctUntilChanged()

    val touchControlPressEffect: Flow<TouchControlPressEffect> = context.dataStore.data
        .map { prefs -> TouchControlPressEffect.fromPreference(prefs[TOUCH_CONTROL_PRESS_EFFECT]) }
        .distinctUntilChanged()

    val gameMenuLayoutStyle: Flow<GameMenuLayoutStyle> = context.dataStore.data
        .map { prefs -> GameMenuLayoutStyle.fromPreference(prefs[GAME_MENU_LAYOUT_STYLE]) }
        .distinctUntilChanged()

    val drawerVisualStyle: Flow<DrawerVisualStyle> = context.dataStore.data
        .map { prefs -> DrawerVisualStyle.fromPreference(prefs[DRAWER_VISUAL_STYLE]) }
        .distinctUntilChanged()

    val hiddenDrawerItems: Flow<Set<DrawerItemId>> = context.dataStore.data
        .map { prefs -> sanitizeHiddenDrawerItems(prefs[HIDDEN_DRAWER_ITEMS]) }
        .distinctUntilChanged()

    val gameMenuTabOrder: Flow<List<GameMenuTabId>> = context.dataStore.data
        .map { prefs -> sanitizeGameMenuTabOrder(prefs[GAME_MENU_TAB_ORDER]) }
        .distinctUntilChanged()

    val hiddenGameMenuTabs: Flow<Set<GameMenuTabId>> = context.dataStore.data
        .map { prefs -> sanitizeHiddenGameMenuTabs(prefs[HIDDEN_GAME_MENU_TABS]) }
        .distinctUntilChanged()

    val gameMenuSectionOrder: Flow<List<GameMenuSectionId>> = context.dataStore.data
        .map { prefs -> sanitizeGameMenuSectionOrder(prefs[GAME_MENU_SECTION_ORDER]) }
        .distinctUntilChanged()

    val hiddenGameMenuSections: Flow<Set<GameMenuSectionId>> = context.dataStore.data
        .map { prefs -> sanitizeHiddenGameMenuSections(prefs[HIDDEN_GAME_MENU_SECTIONS]) }
        .distinctUntilChanged()

    suspend fun setThemeMode(mode: ThemeMode) {
        context.dataStore.edit { prefs ->
            if (mode == ThemeMode.PRO && prefs[PRO_UNLOCKED] != true) return@edit
            prefs[THEME_MODE] = when (mode) {
                ThemeMode.SYSTEM -> 0
                ThemeMode.LIGHT -> 1
                ThemeMode.DARK -> 2
                ThemeMode.PRO -> 3
            }
        }
    }

    suspend fun setAppFontChoice(choice: AppFontChoice) {
        context.dataStore.edit { it[APP_FONT_CHOICE] = choice.preferenceValue }
    }

    suspend fun setCustomFontInstalled(displayName: String?) {
        context.dataStore.edit { prefs ->
            val cleanName = displayName?.trim()?.takeIf(String::isNotEmpty)
            if (cleanName == null) prefs.remove(CUSTOM_FONT_NAME) else prefs[CUSTOM_FONT_NAME] = cleanName
            prefs[CUSTOM_FONT_REVISION] = (prefs[CUSTOM_FONT_REVISION] ?: 0) + 1
            prefs[APP_FONT_CHOICE] = AppFontChoice.CUSTOM.preferenceValue
        }
    }

    suspend fun clearCustomFont() {
        context.dataStore.edit { prefs ->
            prefs.remove(CUSTOM_FONT_NAME)
            prefs[CUSTOM_FONT_REVISION] = (prefs[CUSTOM_FONT_REVISION] ?: 0) + 1
            if (AppFontChoice.fromPreference(prefs[APP_FONT_CHOICE]) == AppFontChoice.CUSTOM) {
                prefs[APP_FONT_CHOICE] = AppFontChoice.SYSTEM.preferenceValue
            }
        }
    }

    suspend fun setAppFontScale(scale: Float) {
        context.dataStore.edit { it[APP_FONT_SCALE] = scale.coerceIn(MIN_APP_FONT_SCALE, MAX_APP_FONT_SCALE) }
    }

    suspend fun setHomeGridScale(scale: Float) {
        context.dataStore.edit { it[HOME_GRID_SCALE] = scale.coerceIn(MIN_HOME_GRID_SCALE, MAX_HOME_GRID_SCALE) }
    }

    suspend fun setHomeBackgroundType(type: HomeBackgroundType) {
        context.dataStore.edit { prefs ->
            prefs[HOME_BACKGROUND_TYPE] = type.preferenceValue
            // A separate revision makes replacing a file with the same type/path observable.
            prefs[HOME_BACKGROUND_REVISION] = (prefs[HOME_BACKGROUND_REVISION] ?: 0) + 1
        }
    }

    suspend fun setHomeBackgroundDim(dim: Int) {
        context.dataStore.edit { it[HOME_BACKGROUND_DIM] = dim.coerceIn(0, 85) }
    }

    suspend fun setTouchControlVisualStyle(style: TouchControlVisualStyle) {
        context.dataStore.edit { it[TOUCH_CONTROL_VISUAL_STYLE] = style.preferenceValue }
    }

    suspend fun setTouchControlPressEffect(effect: TouchControlPressEffect) {
        context.dataStore.edit { it[TOUCH_CONTROL_PRESS_EFFECT] = effect.preferenceValue }
    }

    suspend fun setGameMenuLayoutStyle(style: GameMenuLayoutStyle) {
        context.dataStore.edit { it[GAME_MENU_LAYOUT_STYLE] = style.preferenceValue }
    }

    suspend fun setDrawerVisualStyle(style: DrawerVisualStyle) {
        context.dataStore.edit { it[DRAWER_VISUAL_STYLE] = style.preferenceValue }
    }

    suspend fun setHiddenDrawerItems(hidden: Set<DrawerItemId>) {
        val normalized = hidden.filterNot(DrawerItemId::required).toSet()
        context.dataStore.edit {
            it[HIDDEN_DRAWER_ITEMS] = normalized.joinToString(",") { item -> item.name }
        }
    }

    suspend fun setGameMenuTabOrder(order: List<GameMenuTabId>) {
        val normalized = (order + DefaultGameMenuTabOrder).distinct()
        context.dataStore.edit { it[GAME_MENU_TAB_ORDER] = normalized.joinToString(",") { tab -> tab.name } }
    }

    suspend fun setHiddenGameMenuTabs(hidden: Set<GameMenuTabId>) {
        val normalized = hidden.filterNot { it == GameMenuTabId.SESSION }.toSet()
        context.dataStore.edit { it[HIDDEN_GAME_MENU_TABS] = normalized.joinToString(",") { tab -> tab.name } }
    }

    suspend fun setGameMenuSectionOrder(order: List<GameMenuSectionId>) {
        val normalized = sanitizeGameMenuSectionOrder(order.joinToString(",") { it.name })
        context.dataStore.edit {
            it[GAME_MENU_SECTION_ORDER] = normalized.joinToString(",") { section -> section.name }
        }
    }

    suspend fun setHiddenGameMenuSections(hidden: Set<GameMenuSectionId>) {
        context.dataStore.edit {
            it[HIDDEN_GAME_MENU_SECTIONS] = hidden.joinToString(",") { section -> section.name }
        }
    }


    val proUnlocked: Flow<Boolean> = context.dataStore.data
        .map { prefs -> prefs[PRO_UNLOCKED] ?: false }
        .distinctUntilChanged()

    fun getProUnlockedSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { prefs -> prefs[PRO_UNLOCKED] ?: false }.first()
        }
    }

    val welcomeDialogShown: Flow<Boolean> = context.dataStore.data
        .map { prefs -> prefs[WELCOME_DIALOG_SHOWN] ?: false }
        .distinctUntilChanged()

    val mediatekSettingsNoticeShown: Flow<Boolean> = context.dataStore.data
        .map { prefs -> prefs[MEDIATEK_SETTINGS_NOTICE_SHOWN] ?: false }
        .distinctUntilChanged()

    suspend fun setProUnlocked(unlocked: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[PRO_UNLOCKED] = unlocked
            if (!unlocked && prefs[THEME_MODE] == 3) {
                prefs[THEME_MODE] = 0
            }
        }
    }

    suspend fun setWelcomeDialogShown(shown: Boolean) {
        context.dataStore.edit { prefs -> prefs[WELCOME_DIALOG_SHOWN] = shown }
    }

    suspend fun markMediatekSettingsNoticeShown() {
        context.dataStore.edit { prefs -> prefs[MEDIATEK_SETTINGS_NOTICE_SHOWN] = true }
    }

    suspend fun recordInAppReviewSession(activePlayTimeMs: Long) {
        context.dataStore.edit { prefs ->
            val updated = InAppReviewPolicy.recordSession(
                progress = prefs.inAppReviewProgress(),
                activePlayTimeMs = activePlayTimeMs
            )
            prefs[IN_APP_REVIEW_QUALIFYING_SESSION_COUNT] = updated.qualifyingSessionCount
            prefs[IN_APP_REVIEW_TOTAL_ACTIVE_PLAY_TIME_MS] = updated.totalActivePlayTimeMs
        }
    }

    suspend fun claimInAppReviewAttempt(nowMs: Long = System.currentTimeMillis()): Long? {
        val claimedAtMs = nowMs.coerceAtLeast(1L)
        var claimed = false
        context.dataStore.edit { prefs ->
            if (!InAppReviewPolicy.canAttempt(prefs.inAppReviewProgress(), claimedAtMs)) return@edit
            prefs[IN_APP_REVIEW_LAST_ATTEMPT_AT_MS] = claimedAtMs
            claimed = true
        }
        return claimedAtMs.takeIf { claimed }
    }

    suspend fun releaseInAppReviewAttempt(claimedAtMs: Long) {
        context.dataStore.edit { prefs ->
            if (prefs[IN_APP_REVIEW_LAST_ATTEMPT_AT_MS] == claimedAtMs &&
                prefs[IN_APP_REVIEW_REQUESTED] != true
            ) {
                prefs.remove(IN_APP_REVIEW_LAST_ATTEMPT_AT_MS)
            }
        }
    }

    suspend fun markInAppReviewRequested(claimedAtMs: Long) {
        context.dataStore.edit { prefs ->
            if (prefs[IN_APP_REVIEW_LAST_ATTEMPT_AT_MS] == claimedAtMs) {
                prefs[IN_APP_REVIEW_REQUESTED] = true
            }
        }
    }

    private fun Preferences.inAppReviewProgress(): InAppReviewProgress = InAppReviewProgress(
        qualifyingSessionCount = this[IN_APP_REVIEW_QUALIFYING_SESSION_COUNT] ?: 0,
        totalActivePlayTimeMs = this[IN_APP_REVIEW_TOTAL_ACTIVE_PLAY_TIME_MS] ?: 0L,
        reviewRequested = this[IN_APP_REVIEW_REQUESTED] ?: false,
        lastAttemptAtMs = this[IN_APP_REVIEW_LAST_ATTEMPT_AT_MS] ?: 0L
    )

    val renderer: Flow<Int> = context.dataStore.data.map { prefs ->
        normalizeRendererPreference(prefs[RENDERER])
    }

    val performanceProfile: Flow<Int> = context.dataStore.data.map { prefs ->
        PerformanceProfiles.normalize(prefs[PERFORMANCE_PROFILE] ?: PerformanceProfiles.SAFE)
    }

    val gpuHardwareProfile: Flow<Int> = context.dataStore.data.map { prefs ->
        GpuHardwareProfiles.normalize(prefs[GPU_HARDWARE_PROFILE] ?: GpuHardwareProfiles.ADRENO)
    }


    val gpuDriverType: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[GPU_DRIVER_TYPE] ?: 0
    }

    val customDriverPath: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[CUSTOM_DRIVER_PATH]
    }

    suspend fun setRenderer(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[RENDERER] = normalizeRendererPreference(value)
        }
    }

    suspend fun setPerformanceProfile(value: Int) {
        context.dataStore.edit { it[PERFORMANCE_PROFILE] = PerformanceProfiles.normalize(value) }
    }

    private fun normalizeRendererPreference(value: Int?): Int {
        return RendererDefaults.normalizeAndroidRenderer(value ?: RendererDefaults.AUTO)
    }

    private fun resolvePerformanceProfile(prefs: Preferences): Int {
        return PerformanceProfiles.normalize(prefs[PERFORMANCE_PROFILE] ?: PerformanceProfiles.SAFE)
    }

    private fun resolvePerformanceProfileConfig(prefs: Preferences) =
        PerformanceProfiles.configFor(resolvePerformanceProfile(prefs))

    private fun resolveGpuHardwareProfile(): Int {
        return GpuHardwareProfiles.detectHardwareProfile()
    }

    suspend fun setGpuDriverType(value: Int) {
        context.dataStore.edit { it[GPU_DRIVER_TYPE] = value }
    }

    suspend fun setCustomDriverPath(path: String?) {
        context.dataStore.edit { prefs ->
            if (path == null) prefs.remove(CUSTOM_DRIVER_PATH)
            else prefs[CUSTOM_DRIVER_PATH] = path
        }
    }

    val frameLimitEnabled: Flow<Boolean> = context.dataStore.data.map { prefs ->
        if (prefs[ACHIEVEMENTS_HARDCORE] == true) true else prefs[FRAME_LIMIT_ENABLED] ?: true
    }

    val audioVolume: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceVolume(prefs[AUDIO_VOLUME] ?: AudioDefaults.VOLUME_DEFAULT)
    }

    suspend fun setAudioVolume(value: Int) {
        context.dataStore.edit { it[AUDIO_VOLUME] = AudioDefaults.coerceVolume(value) }
    }

    val audioFastForwardVolume: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceVolume(prefs[AUDIO_FAST_FORWARD_VOLUME] ?: AudioDefaults.VOLUME_DEFAULT)
    }

    suspend fun setAudioFastForwardVolume(value: Int) {
        context.dataStore.edit { it[AUDIO_FAST_FORWARD_VOLUME] = AudioDefaults.coerceVolume(value) }
    }

    val audioMuted: Flow<Boolean> = context.dataStore.data.map { prefs -> prefs[AUDIO_MUTED] ?: false }

    suspend fun setAudioMuted(muted: Boolean) {
        context.dataStore.edit { it[AUDIO_MUTED] = muted }
    }

    val audioInterpolation: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceInterpolation(prefs[AUDIO_INTERPOLATION] ?: AudioDefaults.INTERPOLATION_DEFAULT)
    }

    suspend fun setAudioInterpolation(value: Int) {
        context.dataStore.edit { it[AUDIO_INTERPOLATION] = AudioDefaults.coerceInterpolation(value) }
    }

    val audioSyncMode: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceSyncMode(prefs[AUDIO_SYNC_MODE] ?: AudioDefaults.SYNC_DEFAULT)
    }

    suspend fun setAudioSyncMode(value: Int) {
        context.dataStore.edit { it[AUDIO_SYNC_MODE] = AudioDefaults.coerceSyncMode(value) }
    }

    val audioLightweightSpu2: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[AUDIO_LIGHTWEIGHT_SPU2] ?: AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT
    }

    suspend fun setAudioLightweightSpu2(enabled: Boolean) {
        context.dataStore.edit { it[AUDIO_LIGHTWEIGHT_SPU2] = enabled }
    }

    val audioBackend: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceBackend(prefs[AUDIO_BACKEND] ?: AudioDefaults.BACKEND_DEFAULT)
    }

    suspend fun setAudioBackend(value: Int) {
        context.dataStore.edit { it[AUDIO_BACKEND] = AudioDefaults.coerceBackend(value) }
    }

    val audioBufferMs: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceBufferMs(prefs[AUDIO_BUFFER_MS] ?: AudioDefaults.BUFFER_MS_DEFAULT)
    }

    suspend fun setAudioBufferMs(value: Int) {
        context.dataStore.edit { it[AUDIO_BUFFER_MS] = AudioDefaults.coerceBufferMs(value) }
    }

    val audioOutputLatencyMs: Flow<Int> = context.dataStore.data.map { prefs ->
        AudioDefaults.coerceOutputLatencyMs(
            prefs[AUDIO_OUTPUT_LATENCY_MS] ?: AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT
        )
    }

    suspend fun setAudioOutputLatencyMs(value: Int) {
        context.dataStore.edit { it[AUDIO_OUTPUT_LATENCY_MS] = AudioDefaults.coerceOutputLatencyMs(value) }
    }

    val audioMinimalOutputLatency: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[AUDIO_MINIMAL_OUTPUT_LATENCY] ?: AudioDefaults.MINIMAL_OUTPUT_LATENCY_DEFAULT
    }

    suspend fun setAudioMinimalOutputLatency(enabled: Boolean) {
        context.dataStore.edit { it[AUDIO_MINIMAL_OUTPUT_LATENCY] = enabled }
    }

    suspend fun setFrameLimitEnabled(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[FRAME_LIMIT_ENABLED] = if (prefs[ACHIEVEMENTS_HARDCORE] == true) true else enabled
        }
    }

    val vSyncEnabled: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[VSYNC_ENABLED] ?: false
    }

    suspend fun setVSyncEnabled(enabled: Boolean) {
        context.dataStore.edit { it[VSYNC_ENABLED] = enabled }
    }

    val fastForwardSpeed: Flow<Float> = context.dataStore.data.map { prefs ->
        sanitizeFastForwardSpeed(prefs[FAST_FORWARD_SPEED])
    }

    suspend fun setFastForwardSpeed(value: Float) {
        context.dataStore.edit { it[FAST_FORWARD_SPEED] = sanitizeFastForwardSpeed(value) }
    }

    val targetFps: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TARGET_FPS] ?: 0
    }

    suspend fun setTargetFps(value: Int) {
        context.dataStore.edit { it[TARGET_FPS] = if (value <= 0) 0 else value.coerceIn(20, 120) }
    }

    val ntscFramerate: Flow<Float> = context.dataStore.data.map { prefs ->
        sanitizeRegionFramerate(prefs[NTSC_FRAMERATE], DEFAULT_NTSC_FRAMERATE)
    }

    suspend fun setNtscFramerate(value: Float) {
        context.dataStore.edit { it[NTSC_FRAMERATE] = sanitizeRegionFramerate(value, DEFAULT_NTSC_FRAMERATE) }
    }

    val palFramerate: Flow<Float> = context.dataStore.data.map { prefs ->
        sanitizeRegionFramerate(prefs[PAL_FRAMERATE], DEFAULT_PAL_FRAMERATE)
    }

    suspend fun setPalFramerate(value: Float) {
        context.dataStore.edit { it[PAL_FRAMERATE] = sanitizeRegionFramerate(value, DEFAULT_PAL_FRAMERATE) }
    }

    val autoSaveEnabled: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[AUTO_SAVE_ENABLED] ?: false
    }

    suspend fun setAutoSaveEnabled(enabled: Boolean) {
        context.dataStore.edit { it[AUTO_SAVE_ENABLED] = enabled }
    }

    val autoSaveIntervalMinutes: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[AUTO_SAVE_INTERVAL_MINUTES] ?: 1).coerceIn(1, 999)
    }

    suspend fun setAutoSaveIntervalMinutes(value: Int) {
        context.dataStore.edit { it[AUTO_SAVE_INTERVAL_MINUTES] = value.coerceIn(1, 999) }
    }

    val skipDuplicateFrames: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[SKIP_DUPLICATE_FRAMES] ?: true
    }

    suspend fun setSkipDuplicateFrames(enabled: Boolean) {
        context.dataStore.edit { it[SKIP_DUPLICATE_FRAMES] = enabled }
    }

    suspend fun resetAllSettings() {
        context.dataStore.edit { prefs ->
            // Acknowledged compatibility notices are not user settings. Preserve them so a
            // settings reset does not make a one-time notice appear again.
            val mediatekNoticeWasShown = prefs[MEDIATEK_SETTINGS_NOTICE_SHOWN] == true
            prefs.clear()
            if (mediatekNoticeWasShown) {
                prefs[MEDIATEK_SETTINGS_NOTICE_SHOWN] = true
            }
        }
        localePrefs.edit().remove("language_tag").apply()
    }

    suspend fun cleanupLegacyClampingPreferencesIfNeeded() {
        context.dataStore.edit { prefs ->
            LEGACY_CLAMPING_PREF_KEYS.forEach(prefs::remove)
        }
    }

    val memoryCardSlot1: Flow<String?> = context.dataStore.data.map { prefs -> prefs[MEMORY_CARD_SLOT1] }
    val memoryCardSlot2: Flow<String?> = context.dataStore.data.map { prefs -> prefs[MEMORY_CARD_SLOT2] }

    suspend fun setMemoryCardAssignments(slot1: String?, slot2: String?) {
        context.dataStore.edit { prefs ->
            slot1?.let { prefs[MEMORY_CARD_SLOT1] = it } ?: prefs.remove(MEMORY_CARD_SLOT1)
            slot2?.let { prefs[MEMORY_CARD_SLOT2] = it } ?: prefs.remove(MEMORY_CARD_SLOT2)
        }
    }

    val upscaleMultiplier: Flow<Float> = context.dataStore.data.map { prefs ->
        readUpscale(prefs)
    }

    suspend fun setUpscaleMultiplier(value: Float) {
        context.dataStore.edit { it[UPSCALE] = normalizeUpscale(value) }
    }

    // BIOS Path
    val biosPath: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[BIOS_PATH]
    }

    suspend fun setBiosPath(path: String) {
        context.dataStore.edit { it[BIOS_PATH] = path }
    }

    // Game Path
    val gamePaths: Flow<List<String>> = context.dataStore.data.map(::readGamePaths)

    val gamePath: Flow<String?> = gamePaths.map { it.firstOrNull() }

    suspend fun setGamePath(path: String) {
        setGamePaths(listOf(path))
    }

    suspend fun setGamePaths(paths: List<String>) {
        val normalized = paths.map(String::trim).filter(String::isNotBlank).distinct()
        context.dataStore.edit { prefs ->
            if (normalized.isEmpty()) {
                prefs.remove(GAME_PATHS)
                prefs.remove(GAME_PATH)
            } else {
                prefs[GAME_PATHS] = JSONArray(normalized).toString()
                prefs[GAME_PATH] = normalized.first()
            }
        }
    }

    suspend fun addGamePath(path: String) {
        val normalized = path.trim()
        if (normalized.isBlank()) return
        context.dataStore.edit { prefs ->
            val paths = (readGamePaths(prefs) + normalized).distinct()
            prefs[GAME_PATHS] = JSONArray(paths).toString()
            prefs[GAME_PATH] = paths.first()
        }
    }

    suspend fun removeGamePath(path: String) {
        context.dataStore.edit { prefs ->
            val paths = readGamePaths(prefs).filterNot { it == path }
            if (paths.isEmpty()) {
                prefs.remove(GAME_PATHS)
                prefs.remove(GAME_PATH)
            } else {
                prefs[GAME_PATHS] = JSONArray(paths).toString()
                prefs[GAME_PATH] = paths.first()
            }
        }
    }

    private fun readGamePaths(prefs: Preferences): List<String> {
        val stored = prefs[GAME_PATHS]
        val paths = stored?.let { encoded ->
            runCatching {
                val array = JSONArray(encoded)
                buildList {
                    for (index in 0 until array.length()) {
                        array.optString(index).trim().takeIf(String::isNotBlank)?.let(::add)
                    }
                }
            }.getOrNull()
        }.orEmpty()
        return (paths.ifEmpty { listOfNotNull(prefs[GAME_PATH]?.trim()?.takeIf(String::isNotBlank)) }).distinct()
    }

    val emulatorDataPath: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[EMULATOR_DATA_PATH]
    }

    suspend fun setEmulatorDataPath(path: String?) {
        context.dataStore.edit { prefs ->
            path?.takeIf { it.isNotBlank() }?.let {
                prefs[EMULATOR_DATA_PATH] = it
            } ?: prefs.remove(EMULATOR_DATA_PATH)
        }
    }

    fun getEmulatorDataPathSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[EMULATOR_DATA_PATH] }.first()
        }
    }

    val coverDownloadBaseUrl: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[COVER_DOWNLOAD_BASE_URL]
    }

    suspend fun setCoverDownloadBaseUrl(url: String?) {
        context.dataStore.edit { prefs ->
            if (url.isNullOrBlank()) {
                prefs.remove(COVER_DOWNLOAD_BASE_URL)
            } else {
                prefs[COVER_DOWNLOAD_BASE_URL] = url.trim().trimEnd('/')
            }
        }
    }

    fun getCoverDownloadBaseUrlSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[COVER_DOWNLOAD_BASE_URL] }.first()
        }
    }

    val coverArtStyle: Flow<Int> = context.dataStore.data.map { prefs ->
        when (prefs[COVER_ART_STYLE]) {
            COVER_ART_STYLE_DISABLED -> COVER_ART_STYLE_DISABLED
            COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
            else -> COVER_ART_STYLE_DEFAULT
        }
    }

    suspend fun setCoverArtStyle(style: Int) {
        context.dataStore.edit { prefs ->
            prefs[COVER_ART_STYLE] = when (style) {
                COVER_ART_STYLE_DISABLED -> COVER_ART_STYLE_DISABLED
                COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                else -> COVER_ART_STYLE_DEFAULT
            }
        }
    }

    fun getCoverArtStyleSync(): Int {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { prefs ->
                when (prefs[COVER_ART_STYLE]) {
                    COVER_ART_STYLE_DISABLED -> COVER_ART_STYLE_DISABLED
                    COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                    else -> COVER_ART_STYLE_DEFAULT
                }
            }.first()
        }
    }

    val onboardingCompleted: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ONBOARDING_COMPLETED] ?: false
    }

    suspend fun setOnboardingCompleted(completed: Boolean) {
        context.dataStore.edit { it[ONBOARDING_COMPLETED] = completed }
    }

    val languageTag: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[LANGUAGE_TAG]
    }

    suspend fun setLanguageTag(tag: String?) {
        localePrefs.edit().putString("language_tag", tag).apply()
        context.dataStore.edit { prefs ->
            if (tag.isNullOrBlank()) {
                prefs.remove(LANGUAGE_TAG)
            } else {
                prefs[LANGUAGE_TAG] = tag
            }
        }
    }

    fun getStoredLanguageTagSync(): String? {
        return localePrefs.getString("language_tag", null)
    }

    val settingsSnapshot: Flow<SettingsSnapshot> = context.dataStore.data
        .map { prefs ->
            val biosPath = prefs[BIOS_PATH]
            val performanceProfile = resolvePerformanceProfile(prefs)
            val profileConfig = resolvePerformanceProfileConfig(prefs)
            val gpuHardwareProfile = resolveGpuHardwareProfile()
            SettingsSnapshot(
                themeMode = readThemeMode(prefs),
                appFontChoice = AppFontChoice.fromPreference(prefs[APP_FONT_CHOICE]),
                appFontScale = (prefs[APP_FONT_SCALE] ?: DEFAULT_APP_FONT_SCALE)
                    .coerceIn(MIN_APP_FONT_SCALE, MAX_APP_FONT_SCALE),
                customFontName = prefs[CUSTOM_FONT_NAME]?.takeIf(String::isNotBlank),
                customFontRevision = (prefs[CUSTOM_FONT_REVISION] ?: 0).coerceAtLeast(0),
                homeGridScale = (prefs[HOME_GRID_SCALE] ?: DEFAULT_HOME_GRID_SCALE)
                    .coerceIn(MIN_HOME_GRID_SCALE, MAX_HOME_GRID_SCALE),
                homeBackgroundType = HomeBackgroundType.fromPreference(prefs[HOME_BACKGROUND_TYPE]),
                homeBackgroundRevision = (prefs[HOME_BACKGROUND_REVISION] ?: 0).coerceAtLeast(0),
                homeBackgroundDim = (prefs[HOME_BACKGROUND_DIM] ?: DEFAULT_HOME_BACKGROUND_DIM)
                    .coerceIn(0, 85),
                touchControlVisualStyle = TouchControlVisualStyle.fromPreference(prefs[TOUCH_CONTROL_VISUAL_STYLE]),
                touchControlPressEffect = TouchControlPressEffect.fromPreference(prefs[TOUCH_CONTROL_PRESS_EFFECT]),
                gameMenuLayoutStyle = GameMenuLayoutStyle.fromPreference(prefs[GAME_MENU_LAYOUT_STYLE]),
                drawerVisualStyle = DrawerVisualStyle.fromPreference(prefs[DRAWER_VISUAL_STYLE]),
                hiddenDrawerItems = sanitizeHiddenDrawerItems(prefs[HIDDEN_DRAWER_ITEMS]),
                gameMenuTabOrder = sanitizeGameMenuTabOrder(prefs[GAME_MENU_TAB_ORDER]),
                hiddenGameMenuTabs = sanitizeHiddenGameMenuTabs(prefs[HIDDEN_GAME_MENU_TABS]),
                gameMenuSectionOrder = sanitizeGameMenuSectionOrder(prefs[GAME_MENU_SECTION_ORDER]),
                hiddenGameMenuSections = sanitizeHiddenGameMenuSections(prefs[HIDDEN_GAME_MENU_SECTIONS]),
                proUnlocked = prefs[PRO_UNLOCKED] ?: false,
                languageTag = prefs[LANGUAGE_TAG],
                performanceProfile = performanceProfile,
                gpuHardwareProfile = gpuHardwareProfile,
                renderer = normalizeRendererPreference(prefs[RENDERER]),
                upscaleMultiplier = readUpscale(prefs),
                aspectRatio = normalizeAspectRatioPreference(prefs[ASPECT_RATIO]),
                audioVolume = AudioDefaults.coerceVolume(
                    prefs[AUDIO_VOLUME] ?: AudioDefaults.VOLUME_DEFAULT
                ),
                audioFastForwardVolume = AudioDefaults.coerceVolume(
                    prefs[AUDIO_FAST_FORWARD_VOLUME] ?: AudioDefaults.VOLUME_DEFAULT
                ),
                audioMuted = prefs[AUDIO_MUTED] ?: false,
                audioInterpolation = AudioDefaults.coerceInterpolation(
                    prefs[AUDIO_INTERPOLATION] ?: AudioDefaults.INTERPOLATION_DEFAULT
                ),
                audioSyncMode = AudioDefaults.coerceSyncMode(
                    prefs[AUDIO_SYNC_MODE] ?: AudioDefaults.SYNC_DEFAULT
                ),
                audioLightweightSpu2 = prefs[AUDIO_LIGHTWEIGHT_SPU2]
                    ?: AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT,
                audioBackend = AudioDefaults.coerceBackend(
                    prefs[AUDIO_BACKEND] ?: AudioDefaults.BACKEND_DEFAULT
                ),
                audioBufferMs = AudioDefaults.coerceBufferMs(
                    prefs[AUDIO_BUFFER_MS] ?: AudioDefaults.BUFFER_MS_DEFAULT
                ),
                audioOutputLatencyMs = AudioDefaults.coerceOutputLatencyMs(
                    prefs[AUDIO_OUTPUT_LATENCY_MS] ?: AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT
                ),
                audioMinimalOutputLatency = prefs[AUDIO_MINIMAL_OUTPUT_LATENCY]
                    ?: AudioDefaults.MINIMAL_OUTPUT_LATENCY_DEFAULT,
                autoProgressiveScan = prefs[AUTO_PROGRESSIVE_SCAN] ?: false,
                padVibration = prefs[PAD_VIBRATION] ?: true,
                padVibrationStrength = (prefs[PAD_VIBRATION_STRENGTH] ?: DEFAULT_PAD_VIBRATION_STRENGTH).coerceIn(0, 150),
                padVibrationFallback = prefs[PAD_VIBRATION_FALLBACK] ?: true,
                showFps = prefs[SHOW_FPS] ?: false,
                fpsOverlayMode = prefs[FPS_OVERLAY_MODE] ?: FPS_OVERLAY_MODE_DETAILED,
                fpsOverlayCorner = when (prefs[FPS_OVERLAY_CORNER]) {
                    FPS_OVERLAY_CORNER_TOP_LEFT,
                    FPS_OVERLAY_CORNER_TOP_RIGHT,
                    FPS_OVERLAY_CORNER_BOTTOM_LEFT,
                    FPS_OVERLAY_CORNER_BOTTOM_RIGHT -> prefs[FPS_OVERLAY_CORNER] ?: FPS_OVERLAY_CORNER_TOP_RIGHT
                    else -> FPS_OVERLAY_CORNER_TOP_RIGHT
                },
                fpsOverlayScale = (prefs[FPS_OVERLAY_SCALE] ?: DEFAULT_FPS_OVERLAY_SCALE).coerceIn(
                    MIN_FPS_OVERLAY_SCALE,
                    MAX_FPS_OVERLAY_SCALE
                ),
                fpsOverlayMetrics = PerformanceOverlayMetrics.sanitize(
                    prefs[FPS_OVERLAY_METRICS] ?: PerformanceOverlayMetrics.DEFAULT
                ),
                confirmSaveLoadActions = prefs[CONFIRM_SAVE_LOAD_ACTIONS] ?: true,
                backButtonExitsGame = prefs[BACK_BUTTON_EXITS_GAME] ?: false,
                compactControls = prefs[COMPACT_CONTROLS] ?: true,
                keepScreenOn = prefs[KEEP_SCREEN_ON] ?: true,
                showRecentGames = prefs[SHOW_RECENT_GAMES] ?: true,
                showHomeSearch = prefs[SHOW_HOME_SEARCH] ?: false,
                showDebugOptions = prefs[SHOW_DEBUG_OPTIONS] ?: false,
                preferEnglishGameTitles = prefs[PREFER_ENGLISH_GAME_TITLES] ?: false,
                biosPath = biosPath,
                gamePath = readGamePaths(prefs).firstOrNull(),
                gamePaths = readGamePaths(prefs),
                emulatorDataPath = prefs[EMULATOR_DATA_PATH],
                coverDownloadBaseUrl = prefs[COVER_DOWNLOAD_BASE_URL],
                coverArtStyle = when (prefs[COVER_ART_STYLE]) {
                    COVER_ART_STYLE_DISABLED -> COVER_ART_STYLE_DISABLED
                    COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                    else -> COVER_ART_STYLE_DEFAULT
                },
                setupComplete = prefs[ONBOARDING_COMPLETED] ?: false,
                enableFastBoot = prefs[ENABLE_FAST_BOOT] ?: true,
                eeCycleRate = prefs[EE_CYCLE_RATE] ?: profileConfig.eeCycleRate,
                eeCycleSkip = prefs[EE_CYCLE_SKIP] ?: profileConfig.eeCycleSkip,
                enableEeRecompiler = prefs[ENABLE_EE_RECOMPILER] ?: true,
                enableIopRecompiler = prefs[ENABLE_IOP_RECOMPILER] ?: true,
                enableVu0Recompiler = prefs[ENABLE_VU0_RECOMPILER] ?: true,
                enableVu1Recompiler = prefs[ENABLE_VU1_RECOMPILER] ?: true,
                eeFpuRoundMode = sanitizeFloatRoundMode(prefs[EE_FPU_ROUND_MODE], DEFAULT_EE_FPU_ROUND_MODE),
                vu0RoundMode = sanitizeFloatRoundMode(prefs[VU0_ROUND_MODE], DEFAULT_VU_ROUND_MODE),
                vu1RoundMode = sanitizeFloatRoundMode(prefs[VU1_ROUND_MODE], DEFAULT_VU_ROUND_MODE),
                eeFpuClampingMode = sanitizeClampingMode(prefs[EE_FPU_CLAMPING_MODE], DEFAULT_EE_FPU_CLAMPING_MODE),
                vu0ClampingMode = sanitizeClampingMode(prefs[VU0_CLAMPING_MODE], DEFAULT_VU0_CLAMPING_MODE),
                vu1ClampingMode = sanitizeClampingMode(prefs[VU1_CLAMPING_MODE], DEFAULT_VU1_CLAMPING_MODE),
                enableGameFixes = prefs[ENABLE_GAME_FIXES] ?: true,
                enableEeTimingHack = prefs[ENABLE_EE_TIMING_HACK] ?: false,
                enableWaitLoopSpeedhack = prefs[ENABLE_WAIT_LOOP_SPEEDHACK] ?: true,
                enableIntcStatSpeedhack = prefs[ENABLE_INTC_STAT_SPEEDHACK] ?: true,
                enableVuFlagHack = prefs[ENABLE_VU_FLAG_HACK] ?: true,
                enableInstantVu1 = prefs[ENABLE_INSTANT_VU1] ?: true,
                enableMtvu = prefs[ENABLE_MTVU] ?: true,
                enableThreadPinning = prefs[ENABLE_THREAD_PINNING] ?: DEFAULT_THREAD_PINNING,
                enableFastCdvd = prefs[ENABLE_FAST_CDVD] ?: false,
                enableCheats = prefs[ENABLE_CHEATS] ?: false,
                hwDownloadMode = GsHackDefaults.coerceHardwareDownloadMode(
                    prefs[HW_DOWNLOAD_MODE] ?: profileConfig.hwDownloadMode
                ),
                frameSkip = GsHackDefaults.coerceFrameSkip(
                    prefs[FRAME_SKIP] ?: GsHackDefaults.FRAME_SKIP_DEFAULT
                ),
                skipDuplicateFrames = prefs[SKIP_DUPLICATE_FRAMES] ?: true,
                textureFiltering = GsHackDefaults.coerceBilinearFiltering(
                    prefs[TEXTURE_FILTERING] ?: GsHackDefaults.BILINEAR_FILTERING_DEFAULT
                ),
                trilinearFiltering = prefs[TRILINEAR_FILTERING]?.let(GsHackDefaults::coerceTrilinearFiltering)
                    ?: GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
                blendingAccuracy = GsHackDefaults.coerceBlendingAccuracy(
                    prefs[BLENDING_ACCURACY] ?: GsHackDefaults.BLENDING_ACCURACY_DEFAULT
                ),
                texturePreloading = GsHackDefaults.coerceTexturePreloading(
                    prefs[TEXTURE_PRELOADING] ?: GsHackDefaults.TEXTURE_PRELOADING_DEFAULT
                ),
                enableFxaa = prefs[ENABLE_FXAA] ?: false,
                sgsrMode = (prefs[SGSR_MODE] ?: 0).coerceIn(0, 3),
                casMode = prefs[CAS_MODE] ?: 0,
                casSharpness = prefs[CAS_SHARPNESS] ?: 50,
                tvShader = prefs[TV_SHADER]?.let(GsHackDefaults::coerceTvShader) ?: GsHackDefaults.TV_SHADER_DEFAULT,
                shadeBoostEnabled = resolveShadeBoostEnabled(
                    explicitValue = prefs[SHADEBOOST_ENABLED],
                    brightness = prefs[SHADEBOOST_BRIGHTNESS] ?: 50,
                    contrast = prefs[SHADEBOOST_CONTRAST] ?: 50,
                    saturation = prefs[SHADEBOOST_SATURATION] ?: 50,
                    gamma = prefs[SHADEBOOST_GAMMA] ?: 50
                ),
                shadeBoostBrightness = prefs[SHADEBOOST_BRIGHTNESS] ?: 50,
                shadeBoostContrast = prefs[SHADEBOOST_CONTRAST] ?: 50,
                shadeBoostSaturation = prefs[SHADEBOOST_SATURATION] ?: 50,
                shadeBoostGamma = prefs[SHADEBOOST_GAMMA] ?: 50,
                enableWidescreenPatches = prefs[ENABLE_WIDESCREEN_PATCHES] ?: false,
                enableNoInterlacingPatches = prefs[ENABLE_NO_INTERLACING_PATCHES] ?: false,
                deinterlaceMode = GsHackDefaults.coerceDeinterlaceMode(
                    prefs[DEINTERLACE_MODE] ?: GsHackDefaults.DEINTERLACE_MODE_DEFAULT
                ),
                dithering = GsHackDefaults.coerceDithering(
                    prefs[DITHERING] ?: GsHackDefaults.DITHERING_DEFAULT
                ),
                antiBlur = prefs[ANTI_BLUR] ?: GsHackDefaults.ANTI_BLUR_DEFAULT,
                anisotropicFiltering = GsHackDefaults.coerceAnisotropicFiltering(
                    prefs[ANISOTROPIC_FILTERING] ?: GsHackDefaults.ANISOTROPIC_FILTERING_DEFAULT
                ),
                enableHwMipmapping = prefs[ENABLE_HW_MIPMAPPING] ?: GsHackDefaults.HW_MIPMAPPING_DEFAULT,
                cpuSpriteRenderSize = prefs[CPU_SPRITE_RENDER_SIZE] ?: GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT,
                cpuSpriteRenderLevel = prefs[CPU_SPRITE_RENDER_LEVEL] ?: GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT,
                softwareClutRender = prefs[SOFTWARE_CLUT_RENDER] ?: GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT,
                gpuTargetClutMode = prefs[GPU_TARGET_CLUT_MODE] ?: GsHackDefaults.GPU_TARGET_CLUT_DEFAULT,
                skipDrawStart = prefs[SKIP_DRAW_START] ?: 0,
                skipDrawEnd = prefs[SKIP_DRAW_END] ?: 0,
                autoFlushHardware = prefs[AUTO_FLUSH_HARDWARE] ?: GsHackDefaults.AUTO_FLUSH_DEFAULT,
                cpuFramebufferConversion = prefs[CPU_FRAMEBUFFER_CONVERSION] ?: false,
                disableDepthConversion = prefs[DISABLE_DEPTH_CONVERSION] ?: false,
                disableSafeFeatures = prefs[DISABLE_SAFE_FEATURES] ?: false,
                disableRenderFixes = prefs[DISABLE_RENDER_FIXES] ?: false,
                preloadFrameData = prefs[PRELOAD_FRAME_DATA] ?: false,
                disablePartialInvalidation = prefs[DISABLE_PARTIAL_INVALIDATION] ?: false,
                textureInsideRt = prefs[TEXTURE_INSIDE_RT] ?: GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT,
                readTargetsOnClose = prefs[READ_TARGETS_ON_CLOSE] ?: false,
                estimateTextureRegion = prefs[ESTIMATE_TEXTURE_REGION] ?: false,
                gpuPaletteConversion = prefs[GPU_PALETTE_CONVERSION] ?: false,
                halfPixelOffset = prefs[HALF_PIXEL_OFFSET] ?: GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT,
                nativeScaling = GsHackDefaults.coerceNativeScaling(
                    prefs[NATIVE_SCALING] ?: GsHackDefaults.NATIVE_SCALING_DEFAULT
                ),
                roundSprite = prefs[ROUND_SPRITE] ?: GsHackDefaults.ROUND_SPRITE_DEFAULT,
                bilinearUpscale = prefs[BILINEAR_UPSCALE] ?: GsHackDefaults.BILINEAR_UPSCALE_DEFAULT,
                textureOffsetX = prefs[TEXTURE_OFFSET_X] ?: 0,
                textureOffsetY = prefs[TEXTURE_OFFSET_Y] ?: 0,
                alignSprite = prefs[ALIGN_SPRITE] ?: false,
                mergeSprite = prefs[MERGE_SPRITE] ?: false,
                forceEvenSpritePosition = prefs[FORCE_EVEN_SPRITE_POSITION] ?: false,
                nativePaletteDraw = prefs[NATIVE_PALETTE_DRAW] ?: false,
                performancePreset = PerformancePresets.CUSTOM,
                overlayScale = prefs[OVERLAY_SCALE] ?: 100,
                overlayOpacity = (prefs[OVERLAY_OPACITY] ?: DEFAULT_OVERLAY_OPACITY)
                    .coerceIn(OVERLAY_OPACITY_MIN, OVERLAY_OPACITY_MAX),
                overlayShow = prefs[OVERLAY_SHOW] ?: true,
                racingMode = prefs[RACING_MODE] ?: false,
                touchscreenRightStick = prefs[TOUCHSCREEN_RIGHT_STICK] ?: DEFAULT_TOUCHSCREEN_RIGHT_STICK,
                touchscreenRightStickSensitivity = (prefs[TOUCHSCREEN_RIGHT_STICK_SENSITIVITY]
                    ?: DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY).coerceIn(
                    TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN,
                    TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX
                ),
                touchHaptics = prefs[TOUCH_HAPTICS] ?: false,
                touchHapticsPreset = (prefs[TOUCH_HAPTICS_PRESET] ?: DEFAULT_TOUCH_HAPTICS_PRESET).coerceIn(TOUCH_HAPTICS_PRESET_SOFT, TOUCH_HAPTICS_PRESET_STRONG),
                touchHapticsStrength = (prefs[TOUCH_HAPTICS_STRENGTH] ?: DEFAULT_TOUCH_HAPTICS_STRENGTH).coerceIn(10, 100),
                gyroMode = (prefs[GYRO_MODE] ?: GYRO_MODE_OFF).coerceIn(GYRO_MODE_OFF, GYRO_MODE_STEERING),
                gyroSensitivity = (prefs[GYRO_SENSITIVITY] ?: DEFAULT_GYRO_SENSITIVITY).coerceIn(25, 300),
                gyroSmoothing = (prefs[GYRO_SMOOTHING] ?: DEFAULT_GYRO_SMOOTHING).coerceIn(0, 90),
                gyroInvertX = prefs[GYRO_INVERT_X] ?: false,
                gyroInvertY = prefs[GYRO_INVERT_Y] ?: false,
                leftStickSensitivity = prefs[LEFT_STICK_SENSITIVITY] ?: DEFAULT_STICK_SENSITIVITY,
                rightStickSensitivity = prefs[RIGHT_STICK_SENSITIVITY] ?: DEFAULT_STICK_SENSITIVITY,
                invertLeftStick = prefs[INVERT_LEFT_STICK] ?: false,
                invertRightStick = prefs[INVERT_RIGHT_STICK] ?: false,
                invertLeftStickHorizontal = prefs[INVERT_LEFT_STICK_HORIZONTAL] ?: false,
                invertRightStickHorizontal = prefs[INVERT_RIGHT_STICK_HORIZONTAL] ?: false,
                enableAutoGamepad = prefs[ENABLE_AUTO_GAMEPAD] ?: true,
                hideOverlayOnGamepad = prefs[HIDE_OVERLAY_ON_GAMEPAD] ?: true,
                gamepadStickDeadzone = prefs[GAMEPAD_STICK_DEADZONE] ?: DEFAULT_GAMEPAD_STICK_DEADZONE,
                gamepadLeftStickSensitivity = prefs[GAMEPAD_LEFT_STICK_SENSITIVITY] ?: DEFAULT_GAMEPAD_STICK_SENSITIVITY,
                gamepadRightStickSensitivity = prefs[GAMEPAD_RIGHT_STICK_SENSITIVITY] ?: DEFAULT_GAMEPAD_STICK_SENSITIVITY,
                gamepadRightStickUpToR2 = prefs[GAMEPAD_RIGHT_STICK_UP_TO_R2] ?: false,
                gamepadRightStickDownToL2 = prefs[GAMEPAD_RIGHT_STICK_DOWN_TO_L2] ?: false,
                gamepadButtonHaptics = prefs[GAMEPAD_BUTTON_HAPTICS] ?: false,
                pressureModifierAmount = (prefs[PRESSURE_MODIFIER_AMOUNT] ?: DEFAULT_PRESSURE_MODIFIER_AMOUNT).coerceIn(1, 100),
                gamepadBindings = decodeGamepadBindings(prefs[GAMEPAD_BINDINGS]),
                gamepadBindingsByPad = decodeGamepadBindingsByPad(prefs[GAMEPAD_BINDINGS]),
                gpuDriverType = prefs[GPU_DRIVER_TYPE] ?: 0,
                mediatekAngleOpenGl = prefs[MEDIATEK_ANGLE_OPENGL] ?: false,
                customDriverPath = prefs[CUSTOM_DRIVER_PATH],
                dev9EthernetEnabled = prefs[DEV9_ETHERNET_ENABLED] ?: false,
                dev9EthernetDevice = prefs[DEV9_ETHERNET_DEVICE]?.takeIf(String::isNotBlank) ?: "Auto",
                dev9InterceptDhcp = prefs[DEV9_INTERCEPT_DHCP] ?: false,
                dev9Dns1Mode = sanitizeDev9DnsMode(prefs[DEV9_DNS1_MODE]),
                dev9Dns1 = sanitizeIpv4(prefs[DEV9_DNS1]),
                dev9Dns2Mode = sanitizeDev9DnsMode(prefs[DEV9_DNS2_MODE]),
                dev9Dns2 = sanitizeIpv4(prefs[DEV9_DNS2]),
                dev9LogDhcp = prefs[DEV9_LOG_DHCP] ?: false,
                dev9LogDns = prefs[DEV9_LOG_DNS] ?: false,
                dev9LocalLinkMode = sanitizeLocalLinkMode(prefs[DEV9_LOCAL_LINK_MODE]),
                dev9LocalLinkAddress = sanitizeIpv4(prefs[DEV9_LOCAL_LINK_ADDRESS], "192.168.43.1"),
                dev9LocalLinkPort = (prefs[DEV9_LOCAL_LINK_PORT] ?: DEFAULT_LOCAL_LINK_PORT).coerceIn(1024, 65535),
                dev9LocalLinkPeerId = (prefs[DEV9_LOCAL_LINK_PEER_ID] ?: defaultLocalLinkPeerId()).coerceIn(2, 65533),
                dev9LocalLinkRoomCode = sanitizeLocalLinkRoomCode(prefs[DEV9_LOCAL_LINK_ROOM_CODE]),
                frameLimitEnabled = prefs[FRAME_LIMIT_ENABLED] ?: true,
                vSyncEnabled = prefs[VSYNC_ENABLED] ?: false,
                fastForwardSpeed = sanitizeFastForwardSpeed(prefs[FAST_FORWARD_SPEED]),
                targetFps = prefs[TARGET_FPS] ?: 0,
                ntscFramerate = sanitizeRegionFramerate(prefs[NTSC_FRAMERATE], DEFAULT_NTSC_FRAMERATE),
                palFramerate = sanitizeRegionFramerate(prefs[PAL_FRAMERATE], DEFAULT_PAL_FRAMERATE),
                achievementsEnabled = prefs[ACHIEVEMENTS_ENABLED] ?: false,
                achievementsHardcore = prefs[ACHIEVEMENTS_HARDCORE] ?: false,
                achievementsNotifications = prefs[ACHIEVEMENTS_NOTIFICATIONS] ?: true,
                achievementsLeaderboardNotifications = prefs[ACHIEVEMENTS_LEADERBOARD_NOTIFICATIONS] ?: true,
                achievementsIndicators = prefs[ACHIEVEMENTS_INDICATORS] ?: true,
                achievementsLeaderboardTrackers = prefs[ACHIEVEMENTS_LEADERBOARD_TRACKERS] ?: true,
                achievementsSoundEffects = prefs[ACHIEVEMENTS_SOUND_EFFECTS] ?: true,
                achievementsUnlockSoundPath = prefs[ACHIEVEMENTS_UNLOCK_SOUND_PATH],
                achievementsUnlockSoundName = prefs[ACHIEVEMENTS_UNLOCK_SOUND_NAME],
                achievementsUsername = prefs[ACHIEVEMENTS_USERNAME],
                achievementsToken = prefs[ACHIEVEMENTS_TOKEN]
            )
        }
        .distinctUntilChanged()

    suspend fun setDev9EthernetEnabled(enabled: Boolean) = context.dataStore.edit { it[DEV9_ETHERNET_ENABLED] = enabled }
    suspend fun setDev9EthernetDevice(device: String) = context.dataStore.edit {
        it[DEV9_ETHERNET_DEVICE] = device.ifBlank { "Auto" }
    }
    suspend fun setDev9InterceptDhcp(enabled: Boolean) = context.dataStore.edit { it[DEV9_INTERCEPT_DHCP] = enabled }
    suspend fun setDev9Dns1Mode(mode: String) = context.dataStore.edit { it[DEV9_DNS1_MODE] = sanitizeDev9DnsMode(mode) }
    suspend fun setDev9Dns1(address: String) = context.dataStore.edit { it[DEV9_DNS1] = sanitizeIpv4(address) }
    suspend fun setDev9Dns2Mode(mode: String) = context.dataStore.edit { it[DEV9_DNS2_MODE] = sanitizeDev9DnsMode(mode) }
    suspend fun setDev9Dns2(address: String) = context.dataStore.edit { it[DEV9_DNS2] = sanitizeIpv4(address) }
    suspend fun setDev9LogDhcp(enabled: Boolean) = context.dataStore.edit { it[DEV9_LOG_DHCP] = enabled }
    suspend fun setDev9LogDns(enabled: Boolean) = context.dataStore.edit { it[DEV9_LOG_DNS] = enabled }
    suspend fun setDev9LocalLinkMode(mode: Int) = context.dataStore.edit { prefs ->
        prefs[DEV9_LOCAL_LINK_MODE] = sanitizeLocalLinkMode(mode)
        if (mode != DEV9_LOCAL_LINK_OFF && sanitizeLocalLinkRoomCode(prefs[DEV9_LOCAL_LINK_ROOM_CODE]).isBlank()) {
            prefs[DEV9_LOCAL_LINK_ROOM_CODE] = UUID.randomUUID().toString()
                .filter(Char::isLetterOrDigit)
                .take(12)
                .uppercase()
        }
        if (!prefs.contains(DEV9_LOCAL_LINK_PEER_ID)) prefs[DEV9_LOCAL_LINK_PEER_ID] = defaultLocalLinkPeerId()
    }
    suspend fun setDev9LocalLinkAddress(address: String) = context.dataStore.edit {
        it[DEV9_LOCAL_LINK_ADDRESS] = sanitizeIpv4(address, "192.168.43.1")
    }
    suspend fun setDev9LocalLinkPort(port: Int) = context.dataStore.edit {
        it[DEV9_LOCAL_LINK_PORT] = port.coerceIn(1024, 65535)
    }
    suspend fun setDev9LocalLinkRoomCode(code: String) = context.dataStore.edit {
        val sanitized = sanitizeLocalLinkRoomCode(code)
        if (sanitized.length in 4..12) it[DEV9_LOCAL_LINK_ROOM_CODE] = sanitized
    }

    private fun sanitizeDev9DnsMode(mode: String?): String = when (mode) {
        DEV9_DNS_MODE_MANUAL, DEV9_DNS_MODE_INTERNAL -> mode
        else -> DEV9_DNS_MODE_AUTO
    }

    private fun sanitizeLocalLinkMode(mode: Int?): Int = when (mode) {
        DEV9_LOCAL_LINK_HOST, DEV9_LOCAL_LINK_JOIN -> mode
        else -> DEV9_LOCAL_LINK_OFF
    }

    private fun sanitizeLocalLinkRoomCode(code: String?): String = code.orEmpty()
        .filter(Char::isLetterOrDigit)
        .take(12)
        .uppercase()

    private fun defaultLocalLinkPeerId(): Int {
        val androidId = Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID).orEmpty()
        return 2 + ((androidId.hashCode().toLong() and 0x7fffffffL) % 65532L).toInt()
    }

    private fun sanitizeIpv4(address: String?, fallback: String = "0.0.0.0"): String {
        val parts = address.orEmpty().trim().split('.')
        if (parts.size != 4 || parts.any { it.toIntOrNull() !in 0..255 }) return fallback
        return parts.joinToString(".") { it.toInt().toString() }
    }

    val overlayLayoutSnapshot: Flow<OverlayLayoutSnapshot> = context.dataStore.data
        .map { prefs ->
            OverlayLayoutSnapshot(
                overlayScale = prefs[OVERLAY_SCALE] ?: 100,
                overlayOpacity = (prefs[OVERLAY_OPACITY] ?: DEFAULT_OVERLAY_OPACITY)
                    .coerceIn(OVERLAY_OPACITY_MIN, OVERLAY_OPACITY_MAX),
                hideOverlayOnGamepad = prefs[HIDE_OVERLAY_ON_GAMEPAD] ?: true,
                dpadOffset = parseOffsetStr(
                    prefs[DPAD_OFFSET],
                    DEFAULT_DPAD_OFFSET_X to DEFAULT_DPAD_OFFSET_Y
                ),
                lstickOffset = parseOffsetStr(
                    prefs[LSTICK_OFFSET],
                    DEFAULT_LSTICK_OFFSET_X to DEFAULT_LSTICK_OFFSET_Y
                ),
                rstickOffset = parseOffsetStr(
                    prefs[RSTICK_OFFSET],
                    DEFAULT_RSTICK_OFFSET_X to DEFAULT_RSTICK_OFFSET_Y
                ),
                actionOffset = parseOffsetStr(
                    prefs[ACTION_OFFSET],
                    DEFAULT_ACTION_OFFSET_X to DEFAULT_ACTION_OFFSET_Y
                ),
                lbtnOffset = parseOffsetStr(
                    prefs[LBTN_OFFSET],
                    DEFAULT_LBTN_OFFSET_X to DEFAULT_LBTN_OFFSET_Y
                ),
                rbtnOffset = parseOffsetStr(
                    prefs[RBTN_OFFSET],
                    DEFAULT_RBTN_OFFSET_X to DEFAULT_RBTN_OFFSET_Y
                ),
                centerOffset = parseOffsetStr(
                    prefs[CENTER_OFFSET],
                    DEFAULT_CENTER_OFFSET_X to DEFAULT_CENTER_OFFSET_Y
                ),
                stickScale = (prefs[STICK_SCALE] ?: OVERLAY_CONTROL_SCALE_DEFAULT)
                    .coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX),
                leftStickSensitivity = prefs[LEFT_STICK_SENSITIVITY] ?: 100,
                rightStickSensitivity = prefs[RIGHT_STICK_SENSITIVITY] ?: 100,
                invertLeftStick = prefs[INVERT_LEFT_STICK] ?: false,
                invertRightStick = prefs[INVERT_RIGHT_STICK] ?: false,
                invertLeftStickHorizontal = prefs[INVERT_LEFT_STICK_HORIZONTAL] ?: false,
                invertRightStickHorizontal = prefs[INVERT_RIGHT_STICK_HORIZONTAL] ?: false,
                stickSurfaceMode = prefs[STICK_SURFACE_MODE] ?: false,
                controlLayouts = decodeControlLayouts(prefs[CONTROL_LAYOUTS])
            )
        }
        .distinctUntilChanged()

    val aspectRatio: Flow<Int> = context.dataStore.data.map { prefs ->
        normalizeAspectRatioPreference(prefs[ASPECT_RATIO])
    }

    suspend fun setAspectRatio(value: Int) {
        context.dataStore.edit { it[ASPECT_RATIO] = normalizeAspectRatioPreference(value) }
    }

    val autoProgressiveScan: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[AUTO_PROGRESSIVE_SCAN] ?: false
    }

    suspend fun setAutoProgressiveScan(enabled: Boolean) {
        context.dataStore.edit { it[AUTO_PROGRESSIVE_SCAN] = enabled }
    }

    private fun normalizeAspectRatioPreference(value: Int?): Int {
        return when (value) {
            0, 1, 2, 3, 4 -> value
            else -> 1
        }
    }

    val padVibration: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[PAD_VIBRATION] ?: true
    }

    suspend fun setPadVibration(enabled: Boolean) {
        context.dataStore.edit { it[PAD_VIBRATION] = enabled }
    }

    val padVibrationStrength: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[PAD_VIBRATION_STRENGTH] ?: DEFAULT_PAD_VIBRATION_STRENGTH).coerceIn(0, 150)
    }

    suspend fun setPadVibrationStrength(value: Int) {
        context.dataStore.edit { it[PAD_VIBRATION_STRENGTH] = value.coerceIn(0, 150) }
    }

    val padVibrationFallback: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[PAD_VIBRATION_FALLBACK] ?: true
    }

    suspend fun setPadVibrationFallback(enabled: Boolean) {
        context.dataStore.edit { it[PAD_VIBRATION_FALLBACK] = enabled }
    }

    val touchHaptics: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[TOUCH_HAPTICS] ?: false
    }

    suspend fun setTouchHaptics(enabled: Boolean) {
        context.dataStore.edit { it[TOUCH_HAPTICS] = enabled }
    }

    val touchHapticsPreset: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[TOUCH_HAPTICS_PRESET] ?: DEFAULT_TOUCH_HAPTICS_PRESET)
            .coerceIn(TOUCH_HAPTICS_PRESET_SOFT, TOUCH_HAPTICS_PRESET_STRONG)
    }

    suspend fun setTouchHapticsPreset(value: Int) {
        context.dataStore.edit {
            it[TOUCH_HAPTICS_PRESET] = value.coerceIn(TOUCH_HAPTICS_PRESET_SOFT, TOUCH_HAPTICS_PRESET_STRONG)
        }
    }

    val touchHapticsStrength: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[TOUCH_HAPTICS_STRENGTH] ?: DEFAULT_TOUCH_HAPTICS_STRENGTH).coerceIn(10, 100)
    }

    suspend fun setTouchHapticsStrength(value: Int) {
        context.dataStore.edit { it[TOUCH_HAPTICS_STRENGTH] = value.coerceIn(10, 100) }
    }

    val gyroMode: Flow<Int> = context.dataStore.data.map { (it[GYRO_MODE] ?: GYRO_MODE_OFF).coerceIn(GYRO_MODE_OFF, GYRO_MODE_STEERING) }
    suspend fun setGyroMode(value: Int) { context.dataStore.edit { it[GYRO_MODE] = value.coerceIn(GYRO_MODE_OFF, GYRO_MODE_STEERING) } }
    val gyroSensitivity: Flow<Int> = context.dataStore.data.map { (it[GYRO_SENSITIVITY] ?: DEFAULT_GYRO_SENSITIVITY).coerceIn(25, 300) }
    suspend fun setGyroSensitivity(value: Int) { context.dataStore.edit { it[GYRO_SENSITIVITY] = value.coerceIn(25, 300) } }
    val gyroSmoothing: Flow<Int> = context.dataStore.data.map { (it[GYRO_SMOOTHING] ?: DEFAULT_GYRO_SMOOTHING).coerceIn(0, 90) }
    suspend fun setGyroSmoothing(value: Int) { context.dataStore.edit { it[GYRO_SMOOTHING] = value.coerceIn(0, 90) } }
    val gyroInvertX: Flow<Boolean> = context.dataStore.data.map { it[GYRO_INVERT_X] ?: false }
    suspend fun setGyroInvertX(value: Boolean) { context.dataStore.edit { it[GYRO_INVERT_X] = value } }
    val gyroInvertY: Flow<Boolean> = context.dataStore.data.map { it[GYRO_INVERT_Y] ?: false }
    suspend fun setGyroInvertY(value: Boolean) { context.dataStore.edit { it[GYRO_INVERT_Y] = value } }

    val gamepadButtonHaptics: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[GAMEPAD_BUTTON_HAPTICS] ?: false
    }

    suspend fun setGamepadButtonHaptics(enabled: Boolean) {
        context.dataStore.edit { it[GAMEPAD_BUTTON_HAPTICS] = enabled }
    }

    val pressureModifierAmount: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[PRESSURE_MODIFIER_AMOUNT] ?: DEFAULT_PRESSURE_MODIFIER_AMOUNT).coerceIn(1, 100)
    }

    suspend fun setPressureModifierAmount(value: Int) {
        context.dataStore.edit { it[PRESSURE_MODIFIER_AMOUNT] = value.coerceIn(1, 100) }
    }

    val showFps: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[SHOW_FPS] ?: false
    }

    suspend fun setShowFps(enabled: Boolean) {
        context.dataStore.edit { it[SHOW_FPS] = enabled }
    }

    val fpsOverlayMode: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[FPS_OVERLAY_MODE] ?: FPS_OVERLAY_MODE_DETAILED
    }

    suspend fun setFpsOverlayMode(mode: Int) {
        context.dataStore.edit { it[FPS_OVERLAY_MODE] = mode }
    }

    val fpsOverlayCorner: Flow<Int> = context.dataStore.data.map { prefs ->
        when (prefs[FPS_OVERLAY_CORNER]) {
            FPS_OVERLAY_CORNER_TOP_LEFT,
            FPS_OVERLAY_CORNER_TOP_RIGHT,
            FPS_OVERLAY_CORNER_BOTTOM_LEFT,
            FPS_OVERLAY_CORNER_BOTTOM_RIGHT -> prefs[FPS_OVERLAY_CORNER] ?: FPS_OVERLAY_CORNER_TOP_RIGHT
            else -> FPS_OVERLAY_CORNER_TOP_RIGHT
        }
    }

    suspend fun setFpsOverlayCorner(corner: Int) {
        context.dataStore.edit {
            it[FPS_OVERLAY_CORNER] = when (corner) {
                FPS_OVERLAY_CORNER_TOP_LEFT,
                FPS_OVERLAY_CORNER_TOP_RIGHT,
                FPS_OVERLAY_CORNER_BOTTOM_LEFT,
                FPS_OVERLAY_CORNER_BOTTOM_RIGHT -> corner
                else -> FPS_OVERLAY_CORNER_TOP_RIGHT
            }
        }
    }

    val fpsOverlayScale: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[FPS_OVERLAY_SCALE] ?: DEFAULT_FPS_OVERLAY_SCALE).coerceIn(
            MIN_FPS_OVERLAY_SCALE,
            MAX_FPS_OVERLAY_SCALE
        )
    }.distinctUntilChanged()

    suspend fun setFpsOverlayScale(scale: Int) {
        context.dataStore.edit {
            it[FPS_OVERLAY_SCALE] = scale.coerceIn(MIN_FPS_OVERLAY_SCALE, MAX_FPS_OVERLAY_SCALE)
        }
    }

    val fpsOverlayMetrics: Flow<Int> = context.dataStore.data.map { prefs ->
        PerformanceOverlayMetrics.sanitize(
            prefs[FPS_OVERLAY_METRICS] ?: PerformanceOverlayMetrics.DEFAULT
        )
    }.distinctUntilChanged()

    suspend fun setFpsOverlayMetrics(metrics: Int) {
        context.dataStore.edit {
            it[FPS_OVERLAY_METRICS] = PerformanceOverlayMetrics.sanitize(metrics)
        }
    }

    val confirmSaveLoadActions: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[CONFIRM_SAVE_LOAD_ACTIONS] ?: true
    }

    suspend fun setConfirmSaveLoadActions(enabled: Boolean) {
        context.dataStore.edit { it[CONFIRM_SAVE_LOAD_ACTIONS] = enabled }
    }

    val backButtonExitsGame: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[BACK_BUTTON_EXITS_GAME] ?: false
    }.distinctUntilChanged()

    suspend fun setBackButtonExitsGame(enabled: Boolean) {
        context.dataStore.edit { it[BACK_BUTTON_EXITS_GAME] = enabled }
    }

    val compactControls: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[COMPACT_CONTROLS] ?: true
    }

    suspend fun setCompactControls(enabled: Boolean) {
        context.dataStore.edit { it[COMPACT_CONTROLS] = enabled }
    }

    val keepScreenOn: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[KEEP_SCREEN_ON] ?: true
    }

    suspend fun setKeepScreenOn(enabled: Boolean) {
        context.dataStore.edit { it[KEEP_SCREEN_ON] = enabled }
    }

    val showRecentGames: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[SHOW_RECENT_GAMES] ?: true
    }

    suspend fun setShowRecentGames(enabled: Boolean) {
        context.dataStore.edit { it[SHOW_RECENT_GAMES] = enabled }
    }

    val showHomeSearch: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[SHOW_HOME_SEARCH] ?: false
    }

    suspend fun setShowHomeSearch(enabled: Boolean) {
        context.dataStore.edit { it[SHOW_HOME_SEARCH] = enabled }
    }

    val showDebugOptions: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[SHOW_DEBUG_OPTIONS] ?: false
    }

    suspend fun setShowDebugOptions(enabled: Boolean) {
        context.dataStore.edit { it[SHOW_DEBUG_OPTIONS] = enabled }
    }

    val preferEnglishGameTitles: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[PREFER_ENGLISH_GAME_TITLES] ?: false
    }

    suspend fun setPreferEnglishGameTitles(enabled: Boolean) {
        context.dataStore.edit { it[PREFER_ENGLISH_GAME_TITLES] = enabled }
    }

    val recentGames: Flow<List<RecentGameEntry>> = context.dataStore.data.map { prefs ->
        decodeRecentGames(prefs[RECENT_GAMES])
    }

    val homeLibraryViewMode: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[HOME_LIBRARY_VIEW_MODE] ?: 0
    }

    suspend fun setHomeLibraryViewMode(mode: Int) {
        context.dataStore.edit { it[HOME_LIBRARY_VIEW_MODE] = mode.coerceIn(0, 2) }
    }

    suspend fun markGameLaunched(path: String, title: String, serial: String? = null) {
        context.dataStore.edit { prefs ->
            val cleanTitle = sanitizeRecentTitle(path, title, serial)
            val updated = buildList {
                add(
                    RecentGameEntry(
                        path = path,
                        title = cleanTitle,
                        lastPlayedAt = System.currentTimeMillis(),
                        serial = serial
                    )
                )
                addAll(
                    decodeRecentGames(prefs[RECENT_GAMES]).filterNot { it.path == path }
                )
            }.take(MAX_RECENT_GAMES)
            prefs[RECENT_GAMES] = encodeRecentGames(updated)
        }
    }

    private fun decodeRecentGames(raw: String?): List<RecentGameEntry> {
        if (raw.isNullOrBlank()) return emptyList()
        return runCatching {
            val array = JSONArray(raw)
            buildList {
                for (index in 0 until array.length()) {
                    val item = array.optJSONObject(index) ?: continue
                    val path = item.optString("path")
                    val serial = item.optString("serial").takeIf { it.isNotBlank() }
                    if (path.isBlank()) continue
                    add(
                        RecentGameEntry(
                            path = path,
                            title = sanitizeRecentTitle(path, item.optString("title"), serial),
                            lastPlayedAt = item.optLong("lastPlayedAt"),
                            serial = serial
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
    }

    private fun sanitizeRecentTitle(path: String, rawTitle: String, serial: String?): String {
        return Pcsx2CompatibilityRepository(context).findBySerial(serial)?.title
            ?: EmulatorBridge.cleanGameDisplayTitle(rawTitle, path)
    }

    private fun encodeRecentGames(items: List<RecentGameEntry>): String {
        val array = JSONArray()
        items.forEach { item ->
            array.put(
                JSONObject().apply {
                    put("path", item.path)
                    put("title", item.title)
                    put("lastPlayedAt", item.lastPlayedAt)
                    put("serial", item.serial ?: "")
                }
            )
        }
        return array.toString()
    }

    private fun decodeGamepadBindingsByPad(raw: String?): Map<Int, Map<String, Int>> {
        if (raw.isNullOrBlank()) return emptyMap()
        return runCatching {
            val json = JSONObject(raw)
            val nested = buildMap {
                json.keys().forEach { key ->
                    val padIndex = key.toIntOrNull() ?: return@forEach
                    val valueObject = json.optJSONObject(key) ?: return@forEach
                    val bindings = buildMap {
                        valueObject.keys().forEach { actionId ->
                            val keyCode = valueObject.optInt(actionId, Int.MIN_VALUE)
                            if (keyCode != Int.MIN_VALUE) put(actionId, keyCode)
                        }
                    }
                    if (bindings.isNotEmpty()) put(padIndex.coerceIn(0, 1), bindings)
                }
            }
            nested.ifEmpty {
                val legacy = buildMap {
                    json.keys().forEach { key ->
                        val value = json.optInt(key, Int.MIN_VALUE)
                        if (value != Int.MIN_VALUE) put(key, value)
                    }
                }
                if (legacy.isEmpty()) emptyMap() else mapOf(0 to legacy)
            }
        }.getOrDefault(emptyMap())
    }

    private fun decodeGamepadBindings(raw: String?): Map<String, Int> {
        return decodeGamepadBindingsByPad(raw)[0].orEmpty()
    }

    private fun encodeGamepadBindingsByPad(bindingsByPad: Map<Int, Map<String, Int>>): String {
        return JSONObject().apply {
            bindingsByPad.toSortedMap().forEach { (padIndex, bindings) ->
                if (bindings.isEmpty()) return@forEach
                put(
                    padIndex.toString(),
                    JSONObject().apply {
                        bindings.toSortedMap().forEach { (actionId, keyCode) ->
                            put(actionId, keyCode)
                        }
                    }
                )
            }
        }.toString()
    }

    private fun normalizeGamepadPadIndex(padIndex: Int): Int = padIndex.coerceIn(0, 1)

    private fun updateGamepadBindingsForPad(
        prefs: MutablePreferences,
        padIndex: Int,
        transform: (MutableMap<String, Int>) -> Unit
    ) {
        val normalizedPadIndex = normalizeGamepadPadIndex(padIndex)
        val updated = decodeGamepadBindingsByPad(prefs[GAMEPAD_BINDINGS])
            .mapValues { (_, bindings) -> bindings.toMutableMap() }
            .toMutableMap()
        val padBindings = updated[normalizedPadIndex]?.toMutableMap() ?: mutableMapOf()
        transform(padBindings)
        if (padBindings.isEmpty()) {
            updated.remove(normalizedPadIndex)
        } else {
            updated[normalizedPadIndex] = padBindings
        }
        if (updated.isEmpty()) {
            prefs.remove(GAMEPAD_BINDINGS)
        } else {
            prefs[GAMEPAD_BINDINGS] = encodeGamepadBindingsByPad(updated)
        }
    }

    // Overlay customization
    val overlayScale: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[OVERLAY_SCALE] ?: 100
    }

    suspend fun setOverlayScale(scale: Int) {
        context.dataStore.edit { it[OVERLAY_SCALE] = scale.coerceIn(50, 150) }
    }

    val overlayOpacity: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[OVERLAY_OPACITY] ?: DEFAULT_OVERLAY_OPACITY)
            .coerceIn(OVERLAY_OPACITY_MIN, OVERLAY_OPACITY_MAX)
    }

    suspend fun setOverlayOpacity(opacity: Int) {
        context.dataStore.edit {
            it[OVERLAY_OPACITY] = opacity.coerceIn(OVERLAY_OPACITY_MIN, OVERLAY_OPACITY_MAX)
        }
    }

    val overlayShow: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[OVERLAY_SHOW] ?: true
    }

    suspend fun setOverlayShow(enabled: Boolean) {
        context.dataStore.edit { it[OVERLAY_SHOW] = enabled }
    }

    val racingMode: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[RACING_MODE] ?: false
    }

    suspend fun setRacingMode(enabled: Boolean) {
        context.dataStore.edit { it[RACING_MODE] = enabled }
    }

    val touchscreenRightStick: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[TOUCHSCREEN_RIGHT_STICK] ?: DEFAULT_TOUCHSCREEN_RIGHT_STICK
    }

    suspend fun setTouchscreenRightStick(enabled: Boolean) {
        context.dataStore.edit { it[TOUCHSCREEN_RIGHT_STICK] = enabled }
    }

    val touchscreenRightStickSensitivity: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[TOUCHSCREEN_RIGHT_STICK_SENSITIVITY]
            ?: DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY).coerceIn(
            TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN,
            TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX
        )
    }

    suspend fun setTouchscreenRightStickSensitivity(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[TOUCHSCREEN_RIGHT_STICK_SENSITIVITY] = value.coerceIn(
                TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN,
                TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX
            )
        }
    }

    val gamepadStickDeadzone: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[GAMEPAD_STICK_DEADZONE] ?: DEFAULT_GAMEPAD_STICK_DEADZONE
    }

    suspend fun setGamepadStickDeadzone(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[GAMEPAD_STICK_DEADZONE] = value.coerceIn(0, 35)
        }
    }

    val gamepadLeftStickSensitivity: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[GAMEPAD_LEFT_STICK_SENSITIVITY] ?: DEFAULT_GAMEPAD_STICK_SENSITIVITY
    }

    suspend fun setGamepadLeftStickSensitivity(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[GAMEPAD_LEFT_STICK_SENSITIVITY] = value.coerceIn(50, 200)
        }
    }

    val gamepadRightStickSensitivity: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[GAMEPAD_RIGHT_STICK_SENSITIVITY] ?: DEFAULT_GAMEPAD_STICK_SENSITIVITY
    }

    suspend fun setGamepadRightStickSensitivity(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[GAMEPAD_RIGHT_STICK_SENSITIVITY] = value.coerceIn(50, 200)
        }
    }

    val gamepadRightStickUpToR2: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[GAMEPAD_RIGHT_STICK_UP_TO_R2] ?: false
    }

    suspend fun setGamepadRightStickUpToR2(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[GAMEPAD_RIGHT_STICK_UP_TO_R2] = enabled
        }
    }

    val gamepadRightStickDownToL2: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[GAMEPAD_RIGHT_STICK_DOWN_TO_L2] ?: false
    }

    suspend fun setGamepadRightStickDownToL2(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[GAMEPAD_RIGHT_STICK_DOWN_TO_L2] = enabled
        }
    }

    val eeCycleRate: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[EE_CYCLE_RATE] ?: resolvePerformanceProfileConfig(prefs).eeCycleRate
    }

    suspend fun setEeCycleRate(value: Int) {
        context.dataStore.edit { it[EE_CYCLE_RATE] = value.coerceIn(-3, 3) }
    }

    val eeCycleSkip: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[EE_CYCLE_SKIP] ?: resolvePerformanceProfileConfig(prefs).eeCycleSkip
    }

    suspend fun setEeCycleSkip(value: Int) {
        context.dataStore.edit { it[EE_CYCLE_SKIP] = value.coerceIn(0, 3) }
    }

    val enableFastBoot: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_FAST_BOOT] ?: true
    }

    suspend fun setEnableFastBoot(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_FAST_BOOT] = enabled }
    }

    val enableEeRecompiler: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_EE_RECOMPILER] ?: true
    }

    suspend fun setEnableEeRecompiler(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_EE_RECOMPILER] = enabled }
    }

    val enableIopRecompiler: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_IOP_RECOMPILER] ?: true
    }

    suspend fun setEnableIopRecompiler(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_IOP_RECOMPILER] = enabled }
    }

    val enableVu0Recompiler: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_VU0_RECOMPILER] ?: true
    }

    suspend fun setEnableVu0Recompiler(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_VU0_RECOMPILER] = enabled }
    }

    val enableVu1Recompiler: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_VU1_RECOMPILER] ?: true
    }

    suspend fun setEnableVu1Recompiler(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_VU1_RECOMPILER] = enabled }
    }

    val eeFpuRoundMode: Flow<Int> = context.dataStore.data.map { prefs ->
        sanitizeFloatRoundMode(prefs[EE_FPU_ROUND_MODE], DEFAULT_EE_FPU_ROUND_MODE)
    }

    suspend fun setEeFpuRoundMode(value: Int) {
        context.dataStore.edit { it[EE_FPU_ROUND_MODE] = sanitizeFloatRoundMode(value, DEFAULT_EE_FPU_ROUND_MODE) }
    }

    val vu0RoundMode: Flow<Int> = context.dataStore.data.map { prefs ->
        sanitizeFloatRoundMode(prefs[VU0_ROUND_MODE], DEFAULT_VU_ROUND_MODE)
    }

    suspend fun setVu0RoundMode(value: Int) {
        context.dataStore.edit { it[VU0_ROUND_MODE] = sanitizeFloatRoundMode(value, DEFAULT_VU_ROUND_MODE) }
    }

    val vu1RoundMode: Flow<Int> = context.dataStore.data.map { prefs ->
        sanitizeFloatRoundMode(prefs[VU1_ROUND_MODE], DEFAULT_VU_ROUND_MODE)
    }

    suspend fun setVu1RoundMode(value: Int) {
        context.dataStore.edit { it[VU1_ROUND_MODE] = sanitizeFloatRoundMode(value, DEFAULT_VU_ROUND_MODE) }
    }

    val eeFpuClampingMode: Flow<Int> = context.dataStore.data.map { prefs ->
        sanitizeClampingMode(prefs[EE_FPU_CLAMPING_MODE], DEFAULT_EE_FPU_CLAMPING_MODE)
    }

    suspend fun setEeFpuClampingMode(value: Int) {
        context.dataStore.edit { it[EE_FPU_CLAMPING_MODE] = sanitizeClampingMode(value, DEFAULT_EE_FPU_CLAMPING_MODE) }
    }

    val vu0ClampingMode: Flow<Int> = context.dataStore.data.map { prefs ->
        sanitizeClampingMode(prefs[VU0_CLAMPING_MODE], DEFAULT_VU0_CLAMPING_MODE)
    }

    suspend fun setVu0ClampingMode(value: Int) {
        context.dataStore.edit { it[VU0_CLAMPING_MODE] = sanitizeClampingMode(value, DEFAULT_VU0_CLAMPING_MODE) }
    }

    val vu1ClampingMode: Flow<Int> = context.dataStore.data.map { prefs ->
        sanitizeClampingMode(prefs[VU1_CLAMPING_MODE], DEFAULT_VU1_CLAMPING_MODE)
    }

    suspend fun setVu1ClampingMode(value: Int) {
        context.dataStore.edit { it[VU1_CLAMPING_MODE] = sanitizeClampingMode(value, DEFAULT_VU1_CLAMPING_MODE) }
    }

    val enableGameFixes: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_GAME_FIXES] ?: true
    }

    suspend fun setEnableGameFixes(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_GAME_FIXES] = enabled }
    }

    val enableEeTimingHack: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_EE_TIMING_HACK] ?: false
    }

    suspend fun setEnableEeTimingHack(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_EE_TIMING_HACK] = enabled }
    }

    val enableWaitLoopSpeedhack: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_WAIT_LOOP_SPEEDHACK] ?: true
    }

    suspend fun setEnableWaitLoopSpeedhack(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_WAIT_LOOP_SPEEDHACK] = enabled }
    }

    val enableIntcStatSpeedhack: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_INTC_STAT_SPEEDHACK] ?: true
    }

    suspend fun setEnableIntcStatSpeedhack(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_INTC_STAT_SPEEDHACK] = enabled }
    }

    val enableVuFlagHack: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_VU_FLAG_HACK] ?: true
    }

    suspend fun setEnableVuFlagHack(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_VU_FLAG_HACK] = enabled }
    }

    val enableInstantVu1: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_INSTANT_VU1] ?: true
    }

    suspend fun setEnableInstantVu1(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_INSTANT_VU1] = enabled }
    }

    // MTVU (Multi-Threaded VU1)
    val enableMtvu: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_MTVU] ?: true
    }

    suspend fun setEnableMtvu(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_MTVU] = enabled }
    }

    val enableThreadPinning: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_THREAD_PINNING] ?: DEFAULT_THREAD_PINNING
    }

    suspend fun setEnableThreadPinning(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_THREAD_PINNING] = enabled }
    }

    // Fast CDVD
    val enableFastCdvd: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_FAST_CDVD] ?: false
    }

    suspend fun setEnableFastCdvd(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_FAST_CDVD] = enabled }
    }

    val enableCheats: Flow<Boolean> = context.dataStore.data.map { prefs ->
        if (prefs[ACHIEVEMENTS_HARDCORE] == true) false else prefs[ENABLE_CHEATS] ?: false
    }

    suspend fun setEnableCheats(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[ENABLE_CHEATS] = enabled && prefs[ACHIEVEMENTS_HARDCORE] != true
        }
    }

    // Hardware Download Mode
    val hwDownloadMode: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceHardwareDownloadMode(
            prefs[HW_DOWNLOAD_MODE] ?: resolvePerformanceProfileConfig(prefs).hwDownloadMode
        )
    }

    suspend fun setHwDownloadMode(value: Int) {
        context.dataStore.edit { it[HW_DOWNLOAD_MODE] = GsHackDefaults.coerceHardwareDownloadMode(value) }
    }

    // Frame Skip: 0 = off, 1-4
    val frameSkip: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceFrameSkip(prefs[FRAME_SKIP] ?: GsHackDefaults.FRAME_SKIP_DEFAULT)
    }

    suspend fun setFrameSkip(value: Int) {
        context.dataStore.edit { it[FRAME_SKIP] = GsHackDefaults.coerceFrameSkip(value) }
    }

    // Texture Filtering: 0 = Nearest, 1 = Bilinear, 2 = Trilinear
    val textureFiltering: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceBilinearFiltering(
            prefs[TEXTURE_FILTERING] ?: GsHackDefaults.BILINEAR_FILTERING_DEFAULT
        )
    }

    suspend fun setTextureFiltering(value: Int) {
        context.dataStore.edit { it[TEXTURE_FILTERING] = GsHackDefaults.coerceBilinearFiltering(value) }
    }

    val trilinearFiltering: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TRILINEAR_FILTERING]?.let(GsHackDefaults::coerceTrilinearFiltering)
            ?: GsHackDefaults.TRILINEAR_FILTERING_DEFAULT
    }

    suspend fun setTrilinearFiltering(value: Int) {
        context.dataStore.edit { it[TRILINEAR_FILTERING] = GsHackDefaults.coerceTrilinearFiltering(value) }
    }

    val blendingAccuracy: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceBlendingAccuracy(
            prefs[BLENDING_ACCURACY] ?: GsHackDefaults.BLENDING_ACCURACY_DEFAULT
        )
    }

    suspend fun setBlendingAccuracy(value: Int) {
        context.dataStore.edit { it[BLENDING_ACCURACY] = GsHackDefaults.coerceBlendingAccuracy(value) }
    }

    val mediatekAngleOpenGl: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[MEDIATEK_ANGLE_OPENGL] ?: false
    }

    suspend fun setMediatekAngleOpenGl(enabled: Boolean) {
        context.dataStore.edit { it[MEDIATEK_ANGLE_OPENGL] = enabled }
    }

    val texturePreloading: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceTexturePreloading(
            prefs[TEXTURE_PRELOADING] ?: GsHackDefaults.TEXTURE_PRELOADING_DEFAULT
        )
    }

    suspend fun setTexturePreloading(value: Int) {
        context.dataStore.edit { it[TEXTURE_PRELOADING] = GsHackDefaults.coerceTexturePreloading(value) }
    }

    val textureReplacementsEnabled: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_REPLACEMENTS_ENABLED] ?: false
    }

    suspend fun setTextureReplacementsEnabled(enabled: Boolean) {
        context.dataStore.edit { it[TEXTURE_REPLACEMENTS_ENABLED] = enabled }
    }

    val textureReplacementsAsync: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_REPLACEMENTS_ASYNC] ?: true
    }

    suspend fun setTextureReplacementsAsync(enabled: Boolean) {
        context.dataStore.edit { it[TEXTURE_REPLACEMENTS_ASYNC] = enabled }
    }

    val textureReplacementsPrecache: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_REPLACEMENTS_PRECACHE] ?: false
    }

    suspend fun setTextureReplacementsPrecache(enabled: Boolean) {
        context.dataStore.edit { it[TEXTURE_REPLACEMENTS_PRECACHE] = enabled }
    }

    val textureDumpingEnabled: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_DUMPING_ENABLED] ?: false
    }

    suspend fun setTextureDumpingEnabled(enabled: Boolean) {
        context.dataStore.edit { it[TEXTURE_DUMPING_ENABLED] = enabled }
    }

    val enableFxaa: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_FXAA] ?: false
    }

    suspend fun setEnableFxaa(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_FXAA] = enabled }
    }

    val sgsrMode: Flow<Int> = context.dataStore.data.map { prefs ->
        (prefs[SGSR_MODE] ?: 0).coerceIn(0, 3)
    }

    suspend fun setSgsrMode(value: Int) {
        context.dataStore.edit { it[SGSR_MODE] = value.coerceIn(0, 3) }
    }

    val casMode: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[CAS_MODE] ?: 0
    }

    suspend fun setCasMode(value: Int) {
        context.dataStore.edit { it[CAS_MODE] = value.coerceIn(0, 2) }
    }

    val casSharpness: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[CAS_SHARPNESS] ?: 50
    }

    suspend fun setCasSharpness(value: Int) {
        context.dataStore.edit { it[CAS_SHARPNESS] = value.coerceIn(0, 100) }
    }

    val tvShader: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TV_SHADER]?.let(GsHackDefaults::coerceTvShader) ?: GsHackDefaults.TV_SHADER_DEFAULT
    }

    suspend fun setTvShader(value: Int) {
        context.dataStore.edit { it[TV_SHADER] = GsHackDefaults.coerceTvShader(value) }
    }

    val shadeBoostEnabled: Flow<Boolean> = context.dataStore.data.map { prefs ->
        resolveShadeBoostEnabled(
            explicitValue = prefs[SHADEBOOST_ENABLED],
            brightness = prefs[SHADEBOOST_BRIGHTNESS] ?: 50,
            contrast = prefs[SHADEBOOST_CONTRAST] ?: 50,
            saturation = prefs[SHADEBOOST_SATURATION] ?: 50,
            gamma = prefs[SHADEBOOST_GAMMA] ?: 50
        )
    }

    suspend fun setShadeBoostEnabled(enabled: Boolean) {
        context.dataStore.edit { it[SHADEBOOST_ENABLED] = enabled }
    }

    val shadeBoostBrightness: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SHADEBOOST_BRIGHTNESS] ?: 50
    }

    suspend fun setShadeBoostBrightness(value: Int) {
        context.dataStore.edit { it[SHADEBOOST_BRIGHTNESS] = value.coerceIn(1, 100) }
    }

    val shadeBoostContrast: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SHADEBOOST_CONTRAST] ?: 50
    }

    suspend fun setShadeBoostContrast(value: Int) {
        context.dataStore.edit { it[SHADEBOOST_CONTRAST] = value.coerceIn(1, 100) }
    }

    val shadeBoostSaturation: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SHADEBOOST_SATURATION] ?: 50
    }

    suspend fun setShadeBoostSaturation(value: Int) {
        context.dataStore.edit { it[SHADEBOOST_SATURATION] = value.coerceIn(1, 100) }
    }

    val shadeBoostGamma: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SHADEBOOST_GAMMA] ?: 50
    }

    suspend fun setShadeBoostGamma(value: Int) {
        context.dataStore.edit { it[SHADEBOOST_GAMMA] = value.coerceIn(1, 100) }
    }

    // Widescreen Patches
    val enableWidescreenPatches: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_WIDESCREEN_PATCHES] ?: false
    }

    suspend fun setEnableWidescreenPatches(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_WIDESCREEN_PATCHES] = enabled }
    }

    val enableNoInterlacingPatches: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_NO_INTERLACING_PATCHES] ?: false
    }

    suspend fun setEnableNoInterlacingPatches(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_NO_INTERLACING_PATCHES] = enabled }
    }

    val deinterlaceMode: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceDeinterlaceMode(
            prefs[DEINTERLACE_MODE] ?: GsHackDefaults.DEINTERLACE_MODE_DEFAULT
        )
    }

    suspend fun setDeinterlaceMode(value: Int) {
        context.dataStore.edit { it[DEINTERLACE_MODE] = GsHackDefaults.coerceDeinterlaceMode(value) }
    }

    val dithering: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceDithering(prefs[DITHERING] ?: GsHackDefaults.DITHERING_DEFAULT)
    }

    suspend fun setDithering(value: Int) {
        context.dataStore.edit { it[DITHERING] = GsHackDefaults.coerceDithering(value) }
    }

    val antiBlur: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ANTI_BLUR] ?: GsHackDefaults.ANTI_BLUR_DEFAULT
    }

    suspend fun setAntiBlur(enabled: Boolean) {
        context.dataStore.edit { it[ANTI_BLUR] = enabled }
    }

    // Anisotropic Filtering: 0 = off, 2, 4, 8, 16
    val anisotropicFiltering: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceAnisotropicFiltering(
            prefs[ANISOTROPIC_FILTERING] ?: GsHackDefaults.ANISOTROPIC_FILTERING_DEFAULT
        )
    }

    suspend fun setAnisotropicFiltering(value: Int) {
        context.dataStore.edit { it[ANISOTROPIC_FILTERING] = GsHackDefaults.coerceAnisotropicFiltering(value) }
    }

    val enableHwMipmapping: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_HW_MIPMAPPING] ?: GsHackDefaults.HW_MIPMAPPING_DEFAULT
    }

    suspend fun setEnableHwMipmapping(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_HW_MIPMAPPING] = enabled }
    }

    val cpuSpriteRenderSize: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[CPU_SPRITE_RENDER_SIZE] ?: GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT
    }

    suspend fun setCpuSpriteRenderSize(value: Int) {
        context.dataStore.edit { it[CPU_SPRITE_RENDER_SIZE] = value.coerceIn(0, 10) }
    }

    val cpuSpriteRenderLevel: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[CPU_SPRITE_RENDER_LEVEL] ?: GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT
    }

    suspend fun setCpuSpriteRenderLevel(value: Int) {
        context.dataStore.edit { it[CPU_SPRITE_RENDER_LEVEL] = value.coerceIn(0, 2) }
    }

    val softwareClutRender: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SOFTWARE_CLUT_RENDER] ?: GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT
    }

    suspend fun setSoftwareClutRender(value: Int) {
        context.dataStore.edit { it[SOFTWARE_CLUT_RENDER] = value.coerceIn(0, 2) }
    }

    val gpuTargetClutMode: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[GPU_TARGET_CLUT_MODE] ?: GsHackDefaults.GPU_TARGET_CLUT_DEFAULT
    }

    suspend fun setGpuTargetClutMode(value: Int) {
        context.dataStore.edit { it[GPU_TARGET_CLUT_MODE] = value.coerceIn(0, 2) }
    }

    val skipDrawStart: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SKIP_DRAW_START] ?: 0
    }

    suspend fun setSkipDrawStart(value: Int) {
        context.dataStore.edit { it[SKIP_DRAW_START] = value.coerceIn(0, 5000) }
    }

    val skipDrawEnd: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[SKIP_DRAW_END] ?: 0
    }

    suspend fun setSkipDrawEnd(value: Int) {
        context.dataStore.edit { it[SKIP_DRAW_END] = value.coerceIn(0, 5000) }
    }

    val autoFlushHardware: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[AUTO_FLUSH_HARDWARE] ?: GsHackDefaults.AUTO_FLUSH_DEFAULT
    }

    suspend fun setAutoFlushHardware(value: Int) {
        context.dataStore.edit { it[AUTO_FLUSH_HARDWARE] = value.coerceIn(0, 2) }
    }

    val cpuFramebufferConversion: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[CPU_FRAMEBUFFER_CONVERSION] ?: false
    }

    suspend fun setCpuFramebufferConversion(enabled: Boolean) {
        context.dataStore.edit { it[CPU_FRAMEBUFFER_CONVERSION] = enabled }
    }

    val disableDepthConversion: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[DISABLE_DEPTH_CONVERSION] ?: false
    }

    suspend fun setDisableDepthConversion(enabled: Boolean) {
        context.dataStore.edit { it[DISABLE_DEPTH_CONVERSION] = enabled }
    }

    val disableSafeFeatures: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[DISABLE_SAFE_FEATURES] ?: false
    }

    suspend fun setDisableSafeFeatures(enabled: Boolean) {
        context.dataStore.edit { it[DISABLE_SAFE_FEATURES] = enabled }
    }

    val disableRenderFixes: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[DISABLE_RENDER_FIXES] ?: false
    }

    suspend fun setDisableRenderFixes(enabled: Boolean) {
        context.dataStore.edit { it[DISABLE_RENDER_FIXES] = enabled }
    }

    val preloadFrameData: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[PRELOAD_FRAME_DATA] ?: false
    }

    suspend fun setPreloadFrameData(enabled: Boolean) {
        context.dataStore.edit { it[PRELOAD_FRAME_DATA] = enabled }
    }

    val disablePartialInvalidation: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[DISABLE_PARTIAL_INVALIDATION] ?: false
    }

    suspend fun setDisablePartialInvalidation(enabled: Boolean) {
        context.dataStore.edit { it[DISABLE_PARTIAL_INVALIDATION] = enabled }
    }

    val textureInsideRt: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_INSIDE_RT] ?: GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT
    }

    suspend fun setTextureInsideRt(value: Int) {
        context.dataStore.edit { it[TEXTURE_INSIDE_RT] = value.coerceIn(0, 2) }
    }

    val readTargetsOnClose: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[READ_TARGETS_ON_CLOSE] ?: false
    }

    suspend fun setReadTargetsOnClose(enabled: Boolean) {
        context.dataStore.edit { it[READ_TARGETS_ON_CLOSE] = enabled }
    }

    val estimateTextureRegion: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ESTIMATE_TEXTURE_REGION] ?: false
    }

    suspend fun setEstimateTextureRegion(enabled: Boolean) {
        context.dataStore.edit { it[ESTIMATE_TEXTURE_REGION] = enabled }
    }

    val gpuPaletteConversion: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[GPU_PALETTE_CONVERSION] ?: false
    }

    suspend fun setGpuPaletteConversion(enabled: Boolean) {
        context.dataStore.edit { it[GPU_PALETTE_CONVERSION] = enabled }
    }

    val halfPixelOffset: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[HALF_PIXEL_OFFSET] ?: GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT
    }

    suspend fun setHalfPixelOffset(value: Int) {
        context.dataStore.edit { it[HALF_PIXEL_OFFSET] = value.coerceIn(0, 5) }
    }

    val nativeScaling: Flow<Int> = context.dataStore.data.map { prefs ->
        GsHackDefaults.coerceNativeScaling(
            prefs[NATIVE_SCALING] ?: GsHackDefaults.NATIVE_SCALING_DEFAULT
        )
    }

    suspend fun setNativeScaling(value: Int) {
        context.dataStore.edit { it[NATIVE_SCALING] = GsHackDefaults.coerceNativeScaling(value) }
    }

    val roundSprite: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[ROUND_SPRITE] ?: GsHackDefaults.ROUND_SPRITE_DEFAULT
    }

    suspend fun setRoundSprite(value: Int) {
        context.dataStore.edit { it[ROUND_SPRITE] = value.coerceIn(0, 2) }
    }

    val bilinearUpscale: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[BILINEAR_UPSCALE] ?: GsHackDefaults.BILINEAR_UPSCALE_DEFAULT
    }

    suspend fun setBilinearUpscale(value: Int) {
        context.dataStore.edit { it[BILINEAR_UPSCALE] = value.coerceIn(0, 2) }
    }

    val textureOffsetX: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_OFFSET_X] ?: 0
    }

    suspend fun setTextureOffsetX(value: Int) {
        context.dataStore.edit { it[TEXTURE_OFFSET_X] = value.coerceIn(-4096, 4096) }
    }

    val textureOffsetY: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_OFFSET_Y] ?: 0
    }

    suspend fun setTextureOffsetY(value: Int) {
        context.dataStore.edit { it[TEXTURE_OFFSET_Y] = value.coerceIn(-4096, 4096) }
    }

    val alignSprite: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ALIGN_SPRITE] ?: false
    }

    suspend fun setAlignSprite(enabled: Boolean) {
        context.dataStore.edit { it[ALIGN_SPRITE] = enabled }
    }

    val mergeSprite: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[MERGE_SPRITE] ?: false
    }

    suspend fun setMergeSprite(enabled: Boolean) {
        context.dataStore.edit { it[MERGE_SPRITE] = enabled }
    }

    val forceEvenSpritePosition: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[FORCE_EVEN_SPRITE_POSITION] ?: false
    }

    suspend fun setForceEvenSpritePosition(enabled: Boolean) {
        context.dataStore.edit { it[FORCE_EVEN_SPRITE_POSITION] = enabled }
    }

    val nativePaletteDraw: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[NATIVE_PALETTE_DRAW] ?: false
    }

    suspend fun setNativePaletteDraw(enabled: Boolean) {
        context.dataStore.edit { it[NATIVE_PALETTE_DRAW] = enabled }
    }

    val performancePreset: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[PERFORMANCE_PRESET] ?: PerformancePresets.CUSTOM
    }

    suspend fun setPerformancePreset(value: Int) {
        context.dataStore.edit { it[PERFORMANCE_PRESET] = value }
    }

    // Gamepad auto-detect
    val enableAutoGamepad: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_AUTO_GAMEPAD] ?: true
    }

    suspend fun setEnableAutoGamepad(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_AUTO_GAMEPAD] = enabled }
    }

    // Hide overlay when gamepad connected
    val hideOverlayOnGamepad: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[HIDE_OVERLAY_ON_GAMEPAD] ?: true
    }

    suspend fun setHideOverlayOnGamepad(enabled: Boolean) {
        context.dataStore.edit { it[HIDE_OVERLAY_ON_GAMEPAD] = enabled }
    }

    val gamepadBindingsByPad: Flow<Map<Int, Map<String, Int>>> = context.dataStore.data.map { prefs ->
        decodeGamepadBindingsByPad(prefs[GAMEPAD_BINDINGS])
    }

    suspend fun setGamepadBinding(padIndex: Int, actionId: String, keyCode: Int) {
        context.dataStore.edit { prefs ->
            updateGamepadBindingsForPad(prefs, padIndex) { updated ->
                updated.entries.removeAll { it.value == keyCode }
                updated[actionId] = keyCode
            }
        }
    }

    suspend fun clearGamepadBinding(padIndex: Int, actionId: String) {
        context.dataStore.edit { prefs ->
            updateGamepadBindingsForPad(prefs, padIndex) { updated ->
                updated.remove(actionId)
            }
        }
    }

    suspend fun resetGamepadBindingsForPad(padIndex: Int) {
        context.dataStore.edit { prefs ->
            updateGamepadBindingsForPad(prefs, padIndex) { updated ->
                updated.clear()
            }
        }
    }

    // Custom Layout Offsets
    private fun parseOffsetStr(raw: String?, default: Pair<Float, Float> = 0f to 0f): Pair<Float, Float> {
        if (raw.isNullOrBlank()) return default
        val parts = raw.split(",")
        if (parts.size != 2) return default
        return (parts[0].toFloatOrNull() ?: default.first) to (parts[1].toFloatOrNull() ?: default.second)
    }

    private fun formatOffsetStr(x: Float, y: Float): String = "$x,$y"

    private fun decodeControlLayouts(raw: String?): Map<String, OverlayControlLayout> {
        if (raw.isNullOrBlank()) return emptyMap()
        return runCatching {
            val json = JSONObject(raw)
            buildMap {
                val keys = json.keys()
                while (keys.hasNext()) {
                    val id = keys.next()
                    val item = json.optJSONObject(id) ?: continue
                    put(
                        id,
                        OverlayControlLayout(
                            offset = (item.optDouble("x", 0.0).toFloat()) to (item.optDouble("y", 0.0).toFloat()),
                            scale = item.optInt("scale", OVERLAY_CONTROL_SCALE_DEFAULT)
                                .coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX),
                            widthScale = item.optInt(
                                "widthScale",
                                if (id.contains("stick")) 160 else 100
                            ).coerceIn(100, 240),
                            opacity = item.optInt("opacity", OVERLAY_CONTROL_OPACITY_DEFAULT)
                                .coerceIn(OVERLAY_CONTROL_OPACITY_MIN, OVERLAY_CONTROL_OPACITY_MAX),
                            visible = item.optBoolean("visible", true),
                            surfaceOnly = item.optBoolean("surfaceOnly", false)
                        )
                    )
                }
            }
        }.getOrDefault(emptyMap())
    }

    private fun encodeControlLayouts(layouts: Map<String, OverlayControlLayout>): String? {
        if (layouts.isEmpty()) return null
        return JSONObject().apply {
            layouts.toSortedMap().forEach { (id, layout) ->
                put(
                    id,
                    JSONObject().apply {
                        put("x", layout.offset.first.toDouble())
                        put("y", layout.offset.second.toDouble())
                        put("scale", layout.scale.coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX))
                        put("widthScale", layout.widthScale.coerceIn(100, 240))
                        put(
                            "opacity",
                            layout.opacity.coerceIn(OVERLAY_CONTROL_OPACITY_MIN, OVERLAY_CONTROL_OPACITY_MAX)
                        )
                        put("visible", layout.visible)
                        put("surfaceOnly", layout.surfaceOnly)
                    }
                )
            }
        }.toString()
    }

    private fun migrateGlobalStickSurfaceMode(prefs: MutablePreferences) {
        if (prefs[STICK_SURFACE_MODE] == true) {
            val stickScale = (prefs[STICK_SCALE] ?: OVERLAY_CONTROL_SCALE_DEFAULT)
                .coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX)
            val defaults = defaultOverlayControlLayouts(stickScale)
            val layouts = decodeControlLayouts(prefs[CONTROL_LAYOUTS]).toMutableMap()
            listOf("left_stick", "right_stick").forEach { id ->
                val current = layouts[id] ?: defaults[id] ?: OverlayControlLayout(scale = stickScale)
                layouts[id] = current.copy(surfaceOnly = true)
            }
            encodeControlLayouts(layouts)?.let { prefs[CONTROL_LAYOUTS] = it }
        }
        prefs.remove(STICK_SURFACE_MODE)
    }

    val dpadOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[DPAD_OFFSET], DEFAULT_DPAD_OFFSET_X to DEFAULT_DPAD_OFFSET_Y)
    }
    val lstickOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[LSTICK_OFFSET], DEFAULT_LSTICK_OFFSET_X to DEFAULT_LSTICK_OFFSET_Y)
    }
    val rstickOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[RSTICK_OFFSET], DEFAULT_RSTICK_OFFSET_X to DEFAULT_RSTICK_OFFSET_Y)
    }
    val actionOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[ACTION_OFFSET], DEFAULT_ACTION_OFFSET_X to DEFAULT_ACTION_OFFSET_Y)
    }
    val lbtnOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[LBTN_OFFSET], DEFAULT_LBTN_OFFSET_X to DEFAULT_LBTN_OFFSET_Y)
    }
    val rbtnOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[RBTN_OFFSET], DEFAULT_RBTN_OFFSET_X to DEFAULT_RBTN_OFFSET_Y)
    }
    val centerOffset: Flow<Pair<Float, Float>> = context.dataStore.data.map {
        parseOffsetStr(it[CENTER_OFFSET], DEFAULT_CENTER_OFFSET_X to DEFAULT_CENTER_OFFSET_Y)
    }
    
    val stickScale: Flow<Int> = context.dataStore.data.map {
        (it[STICK_SCALE] ?: OVERLAY_CONTROL_SCALE_DEFAULT)
            .coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX)
    }

    val leftStickSensitivity: Flow<Int> = context.dataStore.data.map { it[LEFT_STICK_SENSITIVITY] ?: 100 }

    suspend fun setLeftStickSensitivity(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[LEFT_STICK_SENSITIVITY] = value.coerceIn(50, 200)
        }
    }

    val rightStickSensitivity: Flow<Int> = context.dataStore.data.map { it[RIGHT_STICK_SENSITIVITY] ?: 100 }

    suspend fun setRightStickSensitivity(value: Int) {
        context.dataStore.edit { prefs ->
            prefs[RIGHT_STICK_SENSITIVITY] = value.coerceIn(50, 200)
        }
    }

    val invertLeftStick: Flow<Boolean> = context.dataStore.data.map { it[INVERT_LEFT_STICK] ?: false }

    suspend fun setInvertLeftStick(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[INVERT_LEFT_STICK] = enabled
        }
    }

    val invertRightStick: Flow<Boolean> = context.dataStore.data.map { it[INVERT_RIGHT_STICK] ?: false }

    suspend fun setInvertRightStick(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[INVERT_RIGHT_STICK] = enabled
        }
    }

    val invertLeftStickHorizontal: Flow<Boolean> = context.dataStore.data.map { it[INVERT_LEFT_STICK_HORIZONTAL] ?: false }

    suspend fun setInvertLeftStickHorizontal(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[INVERT_LEFT_STICK_HORIZONTAL] = enabled
        }
    }

    val invertRightStickHorizontal: Flow<Boolean> = context.dataStore.data.map { it[INVERT_RIGHT_STICK_HORIZONTAL] ?: false }

    suspend fun setInvertRightStickHorizontal(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[INVERT_RIGHT_STICK_HORIZONTAL] = enabled
        }
    }

    suspend fun setControlsLayout(
        dpadX: Float, dpadY: Float,
        lstickX: Float, lstickY: Float,
        rstickX: Float, rstickY: Float,
        actionX: Float, actionY: Float,
        lbtnX: Float, lbtnY: Float,
        rbtnX: Float, rbtnY: Float,
        centerX: Float, centerY: Float,
        stickScaleVal: Int,
        controlLayouts: Map<String, OverlayControlLayout> = emptyMap()
    ) {
        context.dataStore.edit { prefs ->
            prefs[DPAD_OFFSET] = formatOffsetStr(dpadX, dpadY)
            prefs[LSTICK_OFFSET] = formatOffsetStr(lstickX, lstickY)
            prefs[RSTICK_OFFSET] = formatOffsetStr(rstickX, rstickY)
            prefs[ACTION_OFFSET] = formatOffsetStr(actionX, actionY)
            prefs[LBTN_OFFSET] = formatOffsetStr(lbtnX, lbtnY)
            prefs[RBTN_OFFSET] = formatOffsetStr(rbtnX, rbtnY)
            prefs[CENTER_OFFSET] = formatOffsetStr(centerX, centerY)
            prefs[STICK_SCALE] = stickScaleVal.coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX)
            prefs[OVERLAY_LAYOUT_VERSION] = CURRENT_OVERLAY_LAYOUT_VERSION
            encodeControlLayouts(controlLayouts)?.let { prefs[CONTROL_LAYOUTS] = it } ?: prefs.remove(CONTROL_LAYOUTS)
        }
    }

    suspend fun resetControlsLayout() {
        context.dataStore.edit { prefs ->
            prefs.remove(DPAD_OFFSET)
            prefs.remove(LSTICK_OFFSET)
            prefs.remove(RSTICK_OFFSET)
            prefs.remove(ACTION_OFFSET)
            prefs.remove(LBTN_OFFSET)
            prefs.remove(RBTN_OFFSET)
            prefs.remove(CENTER_OFFSET)
            prefs.remove(STICK_SCALE)
            prefs.remove(STICK_SURFACE_MODE)
            prefs.remove(CONTROL_LAYOUTS)
            prefs[OVERLAY_LAYOUT_VERSION] = CURRENT_OVERLAY_LAYOUT_VERSION
        }
    }

    suspend fun migrateOverlayLayoutIfNeeded() {
        context.dataStore.edit { prefs ->
            val currentVersion = prefs[OVERLAY_LAYOUT_VERSION] ?: 0
            if (currentVersion >= CURRENT_OVERLAY_LAYOUT_VERSION) return@edit

            if (currentVersion < 10) {
                prefs.remove(DPAD_OFFSET)
                prefs.remove(LSTICK_OFFSET)
                prefs.remove(RSTICK_OFFSET)
                prefs.remove(ACTION_OFFSET)
                prefs.remove(LBTN_OFFSET)
                prefs.remove(RBTN_OFFSET)
                prefs.remove(CENTER_OFFSET)
                prefs.remove(STICK_SCALE)
                prefs.remove(LEFT_STICK_SENSITIVITY)
                prefs.remove(RIGHT_STICK_SENSITIVITY)
                prefs.remove(TOUCHSCREEN_RIGHT_STICK_SENSITIVITY)
                prefs.remove(STICK_SURFACE_MODE)
                prefs.remove(CONTROL_LAYOUTS)
            } else {
                val savedLeftStickOffset = parseOffsetStr(
                    prefs[LSTICK_OFFSET],
                    LEGACY_DEFAULT_LSTICK_OFFSET_X to LEGACY_DEFAULT_LSTICK_OFFSET_Y
                )
                if (savedLeftStickOffset == (LEGACY_DEFAULT_LSTICK_OFFSET_X to LEGACY_DEFAULT_LSTICK_OFFSET_Y)) {
                    prefs[LSTICK_OFFSET] = formatOffsetStr(DEFAULT_LSTICK_OFFSET_X, DEFAULT_LSTICK_OFFSET_Y)
                }
            }
            if (currentVersion >= 11) {
                val savedDpadOffset = parseOffsetStr(
                    prefs[DPAD_OFFSET],
                    PREVIOUS_DEFAULT_DPAD_OFFSET_X - LEFT_SIDE_LAYOUT_SHIFT_X to DEFAULT_DPAD_OFFSET_Y
                )
                val savedLstickOffset = parseOffsetStr(
                    prefs[LSTICK_OFFSET],
                    PREVIOUS_DEFAULT_LSTICK_OFFSET_X - LEFT_SIDE_LAYOUT_SHIFT_X to DEFAULT_LSTICK_OFFSET_Y
                )
                prefs[DPAD_OFFSET] = formatOffsetStr(savedDpadOffset.first + LEFT_SIDE_LAYOUT_SHIFT_X, savedDpadOffset.second)
                prefs[LSTICK_OFFSET] = formatOffsetStr(savedLstickOffset.first + LEFT_SIDE_LAYOUT_SHIFT_X, savedLstickOffset.second)
            }
            if (currentVersion < 13) {
                val savedDpadOffset = parseOffsetStr(
                    prefs[DPAD_OFFSET],
                    DEFAULT_DPAD_OFFSET_X to DEFAULT_DPAD_OFFSET_Y
                )
                val savedLstickOffset = parseOffsetStr(
                    prefs[LSTICK_OFFSET],
                    DEFAULT_LSTICK_OFFSET_X to DEFAULT_LSTICK_OFFSET_Y
                )
                if (savedDpadOffset == (PREVIOUS_DEFAULT_DPAD_OFFSET_X + LEFT_SIDE_LAYOUT_SHIFT_X to DEFAULT_DPAD_OFFSET_Y) ||
                    savedDpadOffset == (12f to DEFAULT_DPAD_OFFSET_Y) ||
                    savedDpadOffset == (8f to DEFAULT_DPAD_OFFSET_Y)) {
                    prefs[DPAD_OFFSET] = formatOffsetStr(DEFAULT_DPAD_OFFSET_X, DEFAULT_DPAD_OFFSET_Y)
                }
                if (savedLstickOffset == (PREVIOUS_DEFAULT_LSTICK_OFFSET_X + LEFT_SIDE_LAYOUT_SHIFT_X to DEFAULT_LSTICK_OFFSET_Y) ||
                    savedLstickOffset == (12f to DEFAULT_LSTICK_OFFSET_Y) ||
                    savedLstickOffset == (8f to DEFAULT_LSTICK_OFFSET_Y)) {
                    prefs[LSTICK_OFFSET] = formatOffsetStr(DEFAULT_LSTICK_OFFSET_X, DEFAULT_LSTICK_OFFSET_Y)
                }
            }
            if (currentVersion < 14) {
                val savedCenterOffset = parseOffsetStr(
                    prefs[CENTER_OFFSET],
                    PREVIOUS_DEFAULT_CENTER_OFFSET_X to PREVIOUS_DEFAULT_CENTER_OFFSET_Y
                )
                if (savedCenterOffset == (PREVIOUS_DEFAULT_CENTER_OFFSET_X to PREVIOUS_DEFAULT_CENTER_OFFSET_Y)) {
                    prefs[CENTER_OFFSET] = formatOffsetStr(DEFAULT_CENTER_OFFSET_X, DEFAULT_CENTER_OFFSET_Y)
                }
            }
            if (currentVersion < 15) {
                val layouts = decodeControlLayouts(prefs[CONTROL_LAYOUTS]).toMutableMap()
                val select = layouts["select"]
                val toggle = layouts["left_input_toggle"]
                val start = layouts["start"]

                val hasDefaultCenterControls = listOf(select, toggle, start).all { layout ->
                    layout == null || (
                        layout.offset == (0f to 0f) &&
                            layout.scale == 80 &&
                            layout.visible
                        )
                }

                if (hasDefaultCenterControls) {
                    prefs.remove(CENTER_OFFSET)
                    prefs.remove(CONTROL_LAYOUTS)
                }
            }
            migrateGlobalStickSurfaceMode(prefs)
            prefs[OVERLAY_LAYOUT_VERSION] = CURRENT_OVERLAY_LAYOUT_VERSION
        }
    }

    suspend fun exportJson(): JSONObject {
        val prefs = context.dataStore.data.first()
        val gpuHardwareProfile = resolveGpuHardwareProfile()
        return JSONObject().apply {
            put("themeMode", prefs[THEME_MODE] ?: 0)
            put("appFontChoice", prefs[APP_FONT_CHOICE] ?: AppFontChoice.SYSTEM.preferenceValue)
            put("appFontScale", (prefs[APP_FONT_SCALE] ?: DEFAULT_APP_FONT_SCALE).toDouble())
            put("customFontName", prefs[CUSTOM_FONT_NAME])
            put("homeGridScale", (prefs[HOME_GRID_SCALE] ?: DEFAULT_HOME_GRID_SCALE).toDouble())
            put("homeBackgroundDim", prefs[HOME_BACKGROUND_DIM] ?: DEFAULT_HOME_BACKGROUND_DIM)
            put("homeBackgroundType", prefs[HOME_BACKGROUND_TYPE] ?: HomeBackgroundType.NONE.preferenceValue)
            put("touchControlVisualStyle", prefs[TOUCH_CONTROL_VISUAL_STYLE] ?: TouchControlVisualStyle.CLASSIC.preferenceValue)
            put("touchControlPressEffect", prefs[TOUCH_CONTROL_PRESS_EFFECT] ?: TouchControlPressEffect.GROW.preferenceValue)
            put("gameMenuLayoutStyle", prefs[GAME_MENU_LAYOUT_STYLE] ?: GameMenuLayoutStyle.SIDEBAR.preferenceValue)
            put("drawerVisualStyle", prefs[DRAWER_VISUAL_STYLE] ?: DrawerVisualStyle.CLASSIC.preferenceValue)
            put("hiddenDrawerItems", prefs[HIDDEN_DRAWER_ITEMS] ?: "")
            put("gameMenuTabOrder", prefs[GAME_MENU_TAB_ORDER] ?: DefaultGameMenuTabOrder.joinToString(",") { it.name })
            put("hiddenGameMenuTabs", prefs[HIDDEN_GAME_MENU_TABS] ?: "")
            put("gameMenuSectionOrder", prefs[GAME_MENU_SECTION_ORDER] ?: DefaultGameMenuSectionOrder.joinToString(",") { it.name })
            put("hiddenGameMenuSections", prefs[HIDDEN_GAME_MENU_SECTIONS] ?: "")
            put("performanceProfile", resolvePerformanceProfile(prefs))
            put("gpuHardwareProfile", gpuHardwareProfile)
            put("renderer", normalizeRendererPreference(prefs[RENDERER]))
            put("mediatekAngleOpenGl", prefs[MEDIATEK_ANGLE_OPENGL] ?: false)
            put("upscaleMultiplier", readUpscale(prefs).toDouble())
            put("biosPath", prefs[BIOS_PATH])
            put("gamePath", prefs[GAME_PATH])
            put("gamePaths", JSONArray(readGamePaths(prefs)))
            put("emulatorDataPath", prefs[EMULATOR_DATA_PATH])
            put("coverDownloadBaseUrl", prefs[COVER_DOWNLOAD_BASE_URL])
            put("coverArtStyle", prefs[COVER_ART_STYLE] ?: COVER_ART_STYLE_DEFAULT)
            put("onboardingCompleted", prefs[ONBOARDING_COMPLETED] ?: false)
            put("languageTag", prefs[LANGUAGE_TAG])
            put("aspectRatio", normalizeAspectRatioPreference(prefs[ASPECT_RATIO]))
            put("audioVolume", AudioDefaults.coerceVolume(prefs[AUDIO_VOLUME] ?: AudioDefaults.VOLUME_DEFAULT))
            put("audioFastForwardVolume", AudioDefaults.coerceVolume(prefs[AUDIO_FAST_FORWARD_VOLUME] ?: AudioDefaults.VOLUME_DEFAULT))
            put("audioMuted", prefs[AUDIO_MUTED] ?: false)
            put("audioInterpolation", AudioDefaults.coerceInterpolation(prefs[AUDIO_INTERPOLATION] ?: AudioDefaults.INTERPOLATION_DEFAULT))
            put("audioSyncMode", AudioDefaults.coerceSyncMode(prefs[AUDIO_SYNC_MODE] ?: AudioDefaults.SYNC_DEFAULT))
            put("audioLightweightSpu2", prefs[AUDIO_LIGHTWEIGHT_SPU2] ?: AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT)
            put("audioBackend", AudioDefaults.coerceBackend(prefs[AUDIO_BACKEND] ?: AudioDefaults.BACKEND_DEFAULT))
            put("audioBufferMs", AudioDefaults.coerceBufferMs(prefs[AUDIO_BUFFER_MS] ?: AudioDefaults.BUFFER_MS_DEFAULT))
            put("audioOutputLatencyMs", AudioDefaults.coerceOutputLatencyMs(prefs[AUDIO_OUTPUT_LATENCY_MS] ?: AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT))
            put("audioMinimalOutputLatency", prefs[AUDIO_MINIMAL_OUTPUT_LATENCY] ?: AudioDefaults.MINIMAL_OUTPUT_LATENCY_DEFAULT)
            put("autoProgressiveScan", prefs[AUTO_PROGRESSIVE_SCAN] ?: false)
            put("padVibration", prefs[PAD_VIBRATION] ?: true)
            put("padVibrationStrength", (prefs[PAD_VIBRATION_STRENGTH] ?: DEFAULT_PAD_VIBRATION_STRENGTH).coerceIn(0, 150))
            put("padVibrationFallback", prefs[PAD_VIBRATION_FALLBACK] ?: true)
            put("showFps", prefs[SHOW_FPS] ?: false)
            put("fpsOverlayMode", prefs[FPS_OVERLAY_MODE] ?: FPS_OVERLAY_MODE_DETAILED)
            put("fpsOverlayCorner", prefs[FPS_OVERLAY_CORNER] ?: FPS_OVERLAY_CORNER_TOP_RIGHT)
            put("fpsOverlayScale", (prefs[FPS_OVERLAY_SCALE] ?: DEFAULT_FPS_OVERLAY_SCALE).coerceIn(MIN_FPS_OVERLAY_SCALE, MAX_FPS_OVERLAY_SCALE))
            put("fpsOverlayMetrics", PerformanceOverlayMetrics.sanitize(prefs[FPS_OVERLAY_METRICS] ?: PerformanceOverlayMetrics.DEFAULT))
            put("confirmSaveLoadActions", prefs[CONFIRM_SAVE_LOAD_ACTIONS] ?: true)
            put("backButtonExitsGame", prefs[BACK_BUTTON_EXITS_GAME] ?: false)
            put("compactControls", prefs[COMPACT_CONTROLS] ?: true)
            put("keepScreenOn", prefs[KEEP_SCREEN_ON] ?: true)
            put("showRecentGames", prefs[SHOW_RECENT_GAMES] ?: true)
            put("showHomeSearch", prefs[SHOW_HOME_SEARCH] ?: false)
            put("showDebugOptions", prefs[SHOW_DEBUG_OPTIONS] ?: false)
            put("preferEnglishGameTitles", prefs[PREFER_ENGLISH_GAME_TITLES] ?: false)
            put("recentGames", prefs[RECENT_GAMES] ?: "[]")
            put("homeLibraryViewMode", prefs[HOME_LIBRARY_VIEW_MODE] ?: 0)
            put("overlayScale", prefs[OVERLAY_SCALE] ?: 100)
            put(
                "overlayOpacity",
                (prefs[OVERLAY_OPACITY] ?: DEFAULT_OVERLAY_OPACITY)
                    .coerceIn(OVERLAY_OPACITY_MIN, OVERLAY_OPACITY_MAX)
            )
            put("overlayShow", prefs[OVERLAY_SHOW] ?: true)
            put("racingMode", prefs[RACING_MODE] ?: false)
            put(
                "touchscreenRightStick",
                prefs[TOUCHSCREEN_RIGHT_STICK] ?: DEFAULT_TOUCHSCREEN_RIGHT_STICK
            )
            put(
                "touchscreenRightStickSensitivity",
                (prefs[TOUCHSCREEN_RIGHT_STICK_SENSITIVITY]
                    ?: DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY).coerceIn(
                    TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN,
                    TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX
                )
            )
            put("touchHaptics", prefs[TOUCH_HAPTICS] ?: false)
            put("touchHapticsPreset", (prefs[TOUCH_HAPTICS_PRESET] ?: DEFAULT_TOUCH_HAPTICS_PRESET).coerceIn(TOUCH_HAPTICS_PRESET_SOFT, TOUCH_HAPTICS_PRESET_STRONG))
            put("touchHapticsStrength", (prefs[TOUCH_HAPTICS_STRENGTH] ?: DEFAULT_TOUCH_HAPTICS_STRENGTH).coerceIn(10, 100))
            put("gyroMode", (prefs[GYRO_MODE] ?: GYRO_MODE_OFF).coerceIn(GYRO_MODE_OFF, GYRO_MODE_STEERING))
            put("gyroSensitivity", (prefs[GYRO_SENSITIVITY] ?: DEFAULT_GYRO_SENSITIVITY).coerceIn(25, 300))
            put("gyroSmoothing", (prefs[GYRO_SMOOTHING] ?: DEFAULT_GYRO_SMOOTHING).coerceIn(0, 90))
            put("gyroInvertX", prefs[GYRO_INVERT_X] ?: false)
            put("gyroInvertY", prefs[GYRO_INVERT_Y] ?: false)
            put("gamepadStickDeadzone", prefs[GAMEPAD_STICK_DEADZONE] ?: DEFAULT_GAMEPAD_STICK_DEADZONE)
            put("gamepadLeftStickSensitivity", prefs[GAMEPAD_LEFT_STICK_SENSITIVITY] ?: DEFAULT_GAMEPAD_STICK_SENSITIVITY)
            put("gamepadRightStickSensitivity", prefs[GAMEPAD_RIGHT_STICK_SENSITIVITY] ?: DEFAULT_GAMEPAD_STICK_SENSITIVITY)
            put("gamepadRightStickUpToR2", prefs[GAMEPAD_RIGHT_STICK_UP_TO_R2] ?: false)
            put("gamepadRightStickDownToL2", prefs[GAMEPAD_RIGHT_STICK_DOWN_TO_L2] ?: false)
            put("gamepadButtonHaptics", prefs[GAMEPAD_BUTTON_HAPTICS] ?: false)
            put("pressureModifierAmount", (prefs[PRESSURE_MODIFIER_AMOUNT] ?: DEFAULT_PRESSURE_MODIFIER_AMOUNT).coerceIn(1, 100))
            put("enableFastBoot", prefs[ENABLE_FAST_BOOT] ?: true)
            put("eeCycleRate", prefs[EE_CYCLE_RATE] ?: 0)
            put("eeCycleSkip", prefs[EE_CYCLE_SKIP] ?: 0)
            put("enableEeRecompiler", prefs[ENABLE_EE_RECOMPILER] ?: true)
            put("enableIopRecompiler", prefs[ENABLE_IOP_RECOMPILER] ?: true)
            put("enableVu0Recompiler", prefs[ENABLE_VU0_RECOMPILER] ?: true)
            put("enableVu1Recompiler", prefs[ENABLE_VU1_RECOMPILER] ?: true)
            put("eeFpuRoundMode", sanitizeFloatRoundMode(prefs[EE_FPU_ROUND_MODE], DEFAULT_EE_FPU_ROUND_MODE))
            put("vu0RoundMode", sanitizeFloatRoundMode(prefs[VU0_ROUND_MODE], DEFAULT_VU_ROUND_MODE))
            put("vu1RoundMode", sanitizeFloatRoundMode(prefs[VU1_ROUND_MODE], DEFAULT_VU_ROUND_MODE))
            put("eeFpuClampingMode", sanitizeClampingMode(prefs[EE_FPU_CLAMPING_MODE], DEFAULT_EE_FPU_CLAMPING_MODE))
            put("vu0ClampingMode", sanitizeClampingMode(prefs[VU0_CLAMPING_MODE], DEFAULT_VU0_CLAMPING_MODE))
            put("vu1ClampingMode", sanitizeClampingMode(prefs[VU1_CLAMPING_MODE], DEFAULT_VU1_CLAMPING_MODE))
            put("enableGameFixes", prefs[ENABLE_GAME_FIXES] ?: true)
            put("enableEeTimingHack", prefs[ENABLE_EE_TIMING_HACK] ?: false)
            put("enableWaitLoopSpeedhack", prefs[ENABLE_WAIT_LOOP_SPEEDHACK] ?: true)
            put("enableIntcStatSpeedhack", prefs[ENABLE_INTC_STAT_SPEEDHACK] ?: true)
            put("enableVuFlagHack", prefs[ENABLE_VU_FLAG_HACK] ?: true)
            put("enableInstantVu1", prefs[ENABLE_INSTANT_VU1] ?: true)
            put("enableMtvu", prefs[ENABLE_MTVU] ?: true)
            put("enableThreadPinning", prefs[ENABLE_THREAD_PINNING] ?: DEFAULT_THREAD_PINNING)
            put("enableFastCdvd", prefs[ENABLE_FAST_CDVD] ?: false)
            put("hwDownloadMode", GsHackDefaults.coerceHardwareDownloadMode(
                prefs[HW_DOWNLOAD_MODE] ?: GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT
            ))
            put("frameSkip", GsHackDefaults.coerceFrameSkip(
                prefs[FRAME_SKIP] ?: GsHackDefaults.FRAME_SKIP_DEFAULT
            ))
            put("skipDuplicateFrames", prefs[SKIP_DUPLICATE_FRAMES] ?: true)
            put("textureFiltering", GsHackDefaults.coerceBilinearFiltering(
                prefs[TEXTURE_FILTERING] ?: GsHackDefaults.BILINEAR_FILTERING_DEFAULT
            ))
            put(
                "trilinearFiltering",
                prefs[TRILINEAR_FILTERING]?.let(GsHackDefaults::coerceTrilinearFiltering)
                    ?: GsHackDefaults.TRILINEAR_FILTERING_DEFAULT
            )
            put("blendingAccuracy", GsHackDefaults.coerceBlendingAccuracy(
                prefs[BLENDING_ACCURACY] ?: GsHackDefaults.BLENDING_ACCURACY_DEFAULT
            ))
            put("texturePreloading", GsHackDefaults.coerceTexturePreloading(
                prefs[TEXTURE_PRELOADING] ?: GsHackDefaults.TEXTURE_PRELOADING_DEFAULT
            ))
            put("textureReplacementsEnabled", prefs[TEXTURE_REPLACEMENTS_ENABLED] ?: false)
            put("textureReplacementsAsync", prefs[TEXTURE_REPLACEMENTS_ASYNC] ?: true)
            put("textureReplacementsPrecache", prefs[TEXTURE_REPLACEMENTS_PRECACHE] ?: false)
            put("textureDumpingEnabled", prefs[TEXTURE_DUMPING_ENABLED] ?: false)
            put("enableFxaa", prefs[ENABLE_FXAA] ?: false)
            put("sgsrMode", (prefs[SGSR_MODE] ?: 0).coerceIn(0, 3))
            put("casMode", prefs[CAS_MODE] ?: 0)
            put("casSharpness", prefs[CAS_SHARPNESS] ?: 50)
            put("tvShader", prefs[TV_SHADER]?.let(GsHackDefaults::coerceTvShader) ?: GsHackDefaults.TV_SHADER_DEFAULT)
            put("enableWidescreenPatches", prefs[ENABLE_WIDESCREEN_PATCHES] ?: false)
            put("enableNoInterlacingPatches", prefs[ENABLE_NO_INTERLACING_PATCHES] ?: false)
            put("deinterlaceMode", GsHackDefaults.coerceDeinterlaceMode(
                prefs[DEINTERLACE_MODE] ?: GsHackDefaults.DEINTERLACE_MODE_DEFAULT
            ))
            put("dithering", GsHackDefaults.coerceDithering(
                prefs[DITHERING] ?: GsHackDefaults.DITHERING_DEFAULT
            ))
            put("antiBlur", prefs[ANTI_BLUR] ?: GsHackDefaults.ANTI_BLUR_DEFAULT)
            put("anisotropicFiltering", GsHackDefaults.coerceAnisotropicFiltering(
                prefs[ANISOTROPIC_FILTERING] ?: GsHackDefaults.ANISOTROPIC_FILTERING_DEFAULT
            ))
            put("enableHwMipmapping", prefs[ENABLE_HW_MIPMAPPING] ?: GsHackDefaults.HW_MIPMAPPING_DEFAULT)
            put("cpuSpriteRenderSize", prefs[CPU_SPRITE_RENDER_SIZE] ?: GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT)
            put("cpuSpriteRenderLevel", prefs[CPU_SPRITE_RENDER_LEVEL] ?: GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT)
            put("softwareClutRender", prefs[SOFTWARE_CLUT_RENDER] ?: GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT)
            put("gpuTargetClutMode", prefs[GPU_TARGET_CLUT_MODE] ?: GsHackDefaults.GPU_TARGET_CLUT_DEFAULT)
            put("skipDrawStart", prefs[SKIP_DRAW_START] ?: 0)
            put("skipDrawEnd", prefs[SKIP_DRAW_END] ?: 0)
            put("autoFlushHardware", prefs[AUTO_FLUSH_HARDWARE] ?: GsHackDefaults.AUTO_FLUSH_DEFAULT)
            put("cpuFramebufferConversion", prefs[CPU_FRAMEBUFFER_CONVERSION] ?: false)
            put("disableDepthConversion", prefs[DISABLE_DEPTH_CONVERSION] ?: false)
            put("disableSafeFeatures", prefs[DISABLE_SAFE_FEATURES] ?: false)
            put("disableRenderFixes", prefs[DISABLE_RENDER_FIXES] ?: false)
            put("preloadFrameData", prefs[PRELOAD_FRAME_DATA] ?: false)
            put("disablePartialInvalidation", prefs[DISABLE_PARTIAL_INVALIDATION] ?: false)
            put("textureInsideRt", prefs[TEXTURE_INSIDE_RT] ?: GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT)
            put("readTargetsOnClose", prefs[READ_TARGETS_ON_CLOSE] ?: false)
            put("estimateTextureRegion", prefs[ESTIMATE_TEXTURE_REGION] ?: false)
            put("gpuPaletteConversion", prefs[GPU_PALETTE_CONVERSION] ?: false)
            put("halfPixelOffset", prefs[HALF_PIXEL_OFFSET] ?: GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT)
            put("nativeScaling", GsHackDefaults.coerceNativeScaling(
                prefs[NATIVE_SCALING] ?: GsHackDefaults.NATIVE_SCALING_DEFAULT
            ))
            put("roundSprite", prefs[ROUND_SPRITE] ?: GsHackDefaults.ROUND_SPRITE_DEFAULT)
            put("bilinearUpscale", prefs[BILINEAR_UPSCALE] ?: GsHackDefaults.BILINEAR_UPSCALE_DEFAULT)
            put("textureOffsetX", prefs[TEXTURE_OFFSET_X] ?: 0)
            put("textureOffsetY", prefs[TEXTURE_OFFSET_Y] ?: 0)
            put("alignSprite", prefs[ALIGN_SPRITE] ?: false)
            put("mergeSprite", prefs[MERGE_SPRITE] ?: false)
            put("forceEvenSpritePosition", prefs[FORCE_EVEN_SPRITE_POSITION] ?: false)
            put("nativePaletteDraw", prefs[NATIVE_PALETTE_DRAW] ?: false)
            put("performancePreset", PerformancePresets.CUSTOM)
            put("enableAutoGamepad", prefs[ENABLE_AUTO_GAMEPAD] ?: true)
            put("hideOverlayOnGamepad", prefs[HIDE_OVERLAY_ON_GAMEPAD] ?: true)
            put("gamepadBindings", prefs[GAMEPAD_BINDINGS])
            put("gpuDriverType", prefs[GPU_DRIVER_TYPE] ?: 0)
            put("customDriverPath", prefs[CUSTOM_DRIVER_PATH])
            put("dev9EthernetEnabled", prefs[DEV9_ETHERNET_ENABLED] ?: false)
            put("dev9EthernetDevice", prefs[DEV9_ETHERNET_DEVICE] ?: "Auto")
            put("dev9InterceptDhcp", prefs[DEV9_INTERCEPT_DHCP] ?: false)
            put("dev9Dns1Mode", sanitizeDev9DnsMode(prefs[DEV9_DNS1_MODE]))
            put("dev9Dns1", sanitizeIpv4(prefs[DEV9_DNS1]))
            put("dev9Dns2Mode", sanitizeDev9DnsMode(prefs[DEV9_DNS2_MODE]))
            put("dev9Dns2", sanitizeIpv4(prefs[DEV9_DNS2]))
            put("dev9LogDhcp", prefs[DEV9_LOG_DHCP] ?: false)
            put("dev9LogDns", prefs[DEV9_LOG_DNS] ?: false)
            put("dev9LocalLinkMode", sanitizeLocalLinkMode(prefs[DEV9_LOCAL_LINK_MODE]))
            put("dev9LocalLinkAddress", sanitizeIpv4(prefs[DEV9_LOCAL_LINK_ADDRESS], "192.168.43.1"))
            put("dev9LocalLinkPort", (prefs[DEV9_LOCAL_LINK_PORT] ?: DEFAULT_LOCAL_LINK_PORT).coerceIn(1024, 65535))
            put("dev9LocalLinkPeerId", (prefs[DEV9_LOCAL_LINK_PEER_ID] ?: defaultLocalLinkPeerId()).coerceIn(2, 65533))
            put("dev9LocalLinkRoomCode", sanitizeLocalLinkRoomCode(prefs[DEV9_LOCAL_LINK_ROOM_CODE]))
            put("frameLimitEnabled", prefs[FRAME_LIMIT_ENABLED] ?: true)
            put("vSyncEnabled", prefs[VSYNC_ENABLED] ?: false)
            put("fastForwardSpeed", sanitizeFastForwardSpeed(prefs[FAST_FORWARD_SPEED]).toDouble())
            put("targetFps", prefs[TARGET_FPS] ?: 0)
            put("ntscFramerate", sanitizeRegionFramerate(prefs[NTSC_FRAMERATE], DEFAULT_NTSC_FRAMERATE).toDouble())
            put("palFramerate", sanitizeRegionFramerate(prefs[PAL_FRAMERATE], DEFAULT_PAL_FRAMERATE).toDouble())
            put("autoSaveEnabled", prefs[AUTO_SAVE_ENABLED] ?: false)
            put("autoSaveIntervalMinutes", (prefs[AUTO_SAVE_INTERVAL_MINUTES] ?: 1).coerceIn(1, 999))
            put("overlayLayoutVersion", prefs[OVERLAY_LAYOUT_VERSION] ?: 0)
            put("dpadOffset", prefs[DPAD_OFFSET])
            put("lstickOffset", prefs[LSTICK_OFFSET])
            put("rstickOffset", prefs[RSTICK_OFFSET])
            put("actionOffset", prefs[ACTION_OFFSET])
            put("lbtnOffset", prefs[LBTN_OFFSET])
            put("rbtnOffset", prefs[RBTN_OFFSET])
            put("centerOffset", prefs[CENTER_OFFSET])
            put(
                "stickScale",
                (prefs[STICK_SCALE] ?: OVERLAY_CONTROL_SCALE_DEFAULT)
                    .coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX)
            )
            put("leftStickSensitivity", prefs[LEFT_STICK_SENSITIVITY] ?: 100)
            put("rightStickSensitivity", prefs[RIGHT_STICK_SENSITIVITY] ?: 100)
            put("invertLeftStick", prefs[INVERT_LEFT_STICK] ?: false)
            put("invertRightStick", prefs[INVERT_RIGHT_STICK] ?: false)
            put("invertLeftStickHorizontal", prefs[INVERT_LEFT_STICK_HORIZONTAL] ?: false)
            put("invertRightStickHorizontal", prefs[INVERT_RIGHT_STICK_HORIZONTAL] ?: false)
            put("stickSurfaceMode", prefs[STICK_SURFACE_MODE] ?: false)
            put("controlLayouts", prefs[CONTROL_LAYOUTS])
            put("memoryCardSlot1", prefs[MEMORY_CARD_SLOT1])
            put("memoryCardSlot2", prefs[MEMORY_CARD_SLOT2])
        }
    }

    suspend fun importJson(json: JSONObject) {
        val languageTag = json.optString("languageTag").takeIf { it.isNotBlank() }
        localePrefs.edit().putString("language_tag", languageTag).apply()
        context.dataStore.edit { prefs ->
            prefs[THEME_MODE] = json.optInt("themeMode", 0)
            prefs[APP_FONT_CHOICE] = AppFontChoice.fromPreference(
                json.optInt("appFontChoice", AppFontChoice.SYSTEM.preferenceValue)
            ).preferenceValue
            prefs[APP_FONT_SCALE] = json.optDouble("appFontScale", DEFAULT_APP_FONT_SCALE.toDouble())
                .toFloat().coerceIn(MIN_APP_FONT_SCALE, MAX_APP_FONT_SCALE)
            json.optString("customFontName").trim().takeIf(String::isNotEmpty)?.let {
                prefs[CUSTOM_FONT_NAME] = it
            } ?: prefs.remove(CUSTOM_FONT_NAME)
            prefs[HOME_GRID_SCALE] = json.optDouble("homeGridScale", DEFAULT_HOME_GRID_SCALE.toDouble())
                .toFloat().coerceIn(MIN_HOME_GRID_SCALE, MAX_HOME_GRID_SCALE)
            prefs[HOME_BACKGROUND_DIM] = json.optInt("homeBackgroundDim", DEFAULT_HOME_BACKGROUND_DIM)
                .coerceIn(0, 85)
            prefs[HOME_BACKGROUND_TYPE] = HomeBackgroundType.fromPreference(
                json.optInt("homeBackgroundType", HomeBackgroundType.NONE.preferenceValue)
            ).preferenceValue
            prefs[TOUCH_CONTROL_VISUAL_STYLE] = TouchControlVisualStyle.fromPreference(
                json.optInt("touchControlVisualStyle", TouchControlVisualStyle.CLASSIC.preferenceValue)
            ).preferenceValue
            prefs[TOUCH_CONTROL_PRESS_EFFECT] = TouchControlPressEffect.fromPreference(
                json.optInt("touchControlPressEffect", TouchControlPressEffect.GROW.preferenceValue)
            ).preferenceValue
            prefs[GAME_MENU_LAYOUT_STYLE] = GameMenuLayoutStyle.fromPreference(
                json.optInt("gameMenuLayoutStyle", GameMenuLayoutStyle.SIDEBAR.preferenceValue)
            ).preferenceValue
            prefs[DRAWER_VISUAL_STYLE] = DrawerVisualStyle.fromPreference(
                json.optInt("drawerVisualStyle", DrawerVisualStyle.CLASSIC.preferenceValue)
            ).preferenceValue
            prefs[HIDDEN_DRAWER_ITEMS] = sanitizeHiddenDrawerItems(json.optString("hiddenDrawerItems"))
                .joinToString(",") { it.name }
            prefs[GAME_MENU_TAB_ORDER] = sanitizeGameMenuTabOrder(json.optString("gameMenuTabOrder"))
                .joinToString(",") { it.name }
            prefs[HIDDEN_GAME_MENU_TABS] = sanitizeHiddenGameMenuTabs(json.optString("hiddenGameMenuTabs"))
                .joinToString(",") { it.name }
            prefs[GAME_MENU_SECTION_ORDER] = sanitizeGameMenuSectionOrder(json.optString("gameMenuSectionOrder"))
                .joinToString(",") { it.name }
            prefs[HIDDEN_GAME_MENU_SECTIONS] = sanitizeHiddenGameMenuSections(json.optString("hiddenGameMenuSections"))
                .joinToString(",") { it.name }
            prefs[PERFORMANCE_PROFILE] = PerformanceProfiles.normalize(
                json.optInt("performanceProfile", PerformanceProfiles.SAFE)
            )
            val gpuHardwareProfile = GpuHardwareProfiles.normalize(
                json.optInt("gpuHardwareProfile", GpuHardwareProfiles.ADRENO)
            )
            prefs[GPU_HARDWARE_PROFILE] = gpuHardwareProfile
            val importedRenderer = normalizeRendererPreference(
                if (json.has("renderer")) json.optInt("renderer") else null
            )
            prefs[RENDERER] = importedRenderer
            prefs[MEDIATEK_ANGLE_OPENGL] = json.optBoolean("mediatekAngleOpenGl", false) &&
                GpuHardwareProfiles.isMediatekProfile(gpuHardwareProfile)
            prefs[UPSCALE] = json.readUpscaleMultiplier()
            json.optString("biosPath").takeIf { it.isNotBlank() }?.let { prefs[BIOS_PATH] = it } ?: prefs.remove(BIOS_PATH)
            val importedGamePaths = json.optJSONArray("gamePaths")?.let { array ->
                buildList {
                    for (index in 0 until array.length()) {
                        array.optString(index).trim().takeIf(String::isNotBlank)?.let(::add)
                    }
                }
            }.orEmpty().ifEmpty {
                listOfNotNull(json.optString("gamePath").trim().takeIf(String::isNotBlank))
            }.distinct()
            if (importedGamePaths.isEmpty()) {
                prefs.remove(GAME_PATHS)
                prefs.remove(GAME_PATH)
            } else {
                prefs[GAME_PATHS] = JSONArray(importedGamePaths).toString()
                prefs[GAME_PATH] = importedGamePaths.first()
            }
            json.optString("emulatorDataPath").takeIf { it.isNotBlank() }?.let {
                prefs[EMULATOR_DATA_PATH] = it
            } ?: prefs.remove(EMULATOR_DATA_PATH)
            json.optString("coverDownloadBaseUrl").takeIf { it.isNotBlank() }?.let {
                prefs[COVER_DOWNLOAD_BASE_URL] = it.trim().trimEnd('/')
            } ?: prefs.remove(COVER_DOWNLOAD_BASE_URL)
            prefs[COVER_ART_STYLE] = when (json.optInt("coverArtStyle", COVER_ART_STYLE_DEFAULT)) {
                COVER_ART_STYLE_DISABLED -> COVER_ART_STYLE_DISABLED
                COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                else -> COVER_ART_STYLE_DEFAULT
            }
            prefs[ONBOARDING_COMPLETED] = json.optBoolean("onboardingCompleted", false)
            languageTag?.let { prefs[LANGUAGE_TAG] = it } ?: prefs.remove(LANGUAGE_TAG)
            prefs[ASPECT_RATIO] = normalizeAspectRatioPreference(json.optInt("aspectRatio", 1))
            prefs[AUDIO_VOLUME] = AudioDefaults.coerceVolume(json.optInt("audioVolume", AudioDefaults.VOLUME_DEFAULT))
            prefs[AUDIO_FAST_FORWARD_VOLUME] = AudioDefaults.coerceVolume(
                json.optInt("audioFastForwardVolume", AudioDefaults.VOLUME_DEFAULT)
            )
            prefs[AUDIO_MUTED] = json.optBoolean("audioMuted", false)
            prefs[AUDIO_INTERPOLATION] = AudioDefaults.coerceInterpolation(
                json.optInt("audioInterpolation", AudioDefaults.INTERPOLATION_DEFAULT)
            )
            prefs[AUDIO_SYNC_MODE] = AudioDefaults.coerceSyncMode(
                json.optInt("audioSyncMode", AudioDefaults.SYNC_DEFAULT)
            )
            prefs[AUDIO_LIGHTWEIGHT_SPU2] = json.optBoolean(
                "audioLightweightSpu2",
                AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT
            )
            prefs[AUDIO_BACKEND] = AudioDefaults.coerceBackend(
                json.optInt("audioBackend", AudioDefaults.BACKEND_DEFAULT)
            )
            prefs[AUDIO_BUFFER_MS] = AudioDefaults.coerceBufferMs(
                json.optInt("audioBufferMs", AudioDefaults.BUFFER_MS_DEFAULT)
            )
            prefs[AUDIO_OUTPUT_LATENCY_MS] = AudioDefaults.coerceOutputLatencyMs(
                json.optInt("audioOutputLatencyMs", AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT)
            )
            prefs[AUDIO_MINIMAL_OUTPUT_LATENCY] = json.optBoolean(
                "audioMinimalOutputLatency",
                AudioDefaults.MINIMAL_OUTPUT_LATENCY_DEFAULT
            )
            prefs[AUTO_PROGRESSIVE_SCAN] = json.optBoolean("autoProgressiveScan", false)
            prefs[PAD_VIBRATION] = json.optBoolean("padVibration", true)
            prefs[PAD_VIBRATION_STRENGTH] = json.optInt("padVibrationStrength", DEFAULT_PAD_VIBRATION_STRENGTH).coerceIn(0, 150)
            prefs[PAD_VIBRATION_FALLBACK] = json.optBoolean("padVibrationFallback", true)
            prefs[SHOW_FPS] = json.optBoolean("showFps", false)
            prefs[FPS_OVERLAY_MODE] = json.optInt("fpsOverlayMode", FPS_OVERLAY_MODE_DETAILED)
            prefs[FPS_OVERLAY_CORNER] = json.optInt("fpsOverlayCorner", FPS_OVERLAY_CORNER_TOP_RIGHT).coerceIn(
                FPS_OVERLAY_CORNER_TOP_LEFT,
                FPS_OVERLAY_CORNER_BOTTOM_RIGHT
            )
            prefs[FPS_OVERLAY_SCALE] = json.optInt("fpsOverlayScale", DEFAULT_FPS_OVERLAY_SCALE).coerceIn(
                MIN_FPS_OVERLAY_SCALE,
                MAX_FPS_OVERLAY_SCALE
            )
            prefs[FPS_OVERLAY_METRICS] = PerformanceOverlayMetrics.sanitize(
                json.optInt("fpsOverlayMetrics", PerformanceOverlayMetrics.DEFAULT)
            )
            prefs[CONFIRM_SAVE_LOAD_ACTIONS] = json.optBoolean("confirmSaveLoadActions", true)
            prefs[BACK_BUTTON_EXITS_GAME] = json.optBoolean("backButtonExitsGame", false)
            prefs[COMPACT_CONTROLS] = json.optBoolean("compactControls", true)
            prefs[KEEP_SCREEN_ON] = json.optBoolean("keepScreenOn", true)
            prefs[SHOW_RECENT_GAMES] = json.optBoolean("showRecentGames", true)
            prefs[SHOW_HOME_SEARCH] = json.optBoolean("showHomeSearch", false)
            prefs[SHOW_DEBUG_OPTIONS] = json.optBoolean("showDebugOptions", false)
            prefs[PREFER_ENGLISH_GAME_TITLES] = json.optBoolean("preferEnglishGameTitles", false)
            prefs[RECENT_GAMES] = json.optString("recentGames", "[]")
            prefs[HOME_LIBRARY_VIEW_MODE] = json.optInt("homeLibraryViewMode", 0).coerceIn(0, 2)
            prefs[OVERLAY_SCALE] = json.optInt("overlayScale", 100)
            prefs[OVERLAY_OPACITY] = json.optInt("overlayOpacity", DEFAULT_OVERLAY_OPACITY)
                .coerceIn(OVERLAY_OPACITY_MIN, OVERLAY_OPACITY_MAX)
            prefs[OVERLAY_SHOW] = json.optBoolean("overlayShow", true)
            prefs[RACING_MODE] = json.optBoolean("racingMode", false)
            prefs[TOUCHSCREEN_RIGHT_STICK] = json.optBoolean(
                "touchscreenRightStick",
                DEFAULT_TOUCHSCREEN_RIGHT_STICK
            )
            prefs[TOUCHSCREEN_RIGHT_STICK_SENSITIVITY] = json.optInt(
                "touchscreenRightStickSensitivity",
                DEFAULT_TOUCHSCREEN_RIGHT_STICK_SENSITIVITY
            ).coerceIn(
                TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MIN,
                TOUCHSCREEN_RIGHT_STICK_SENSITIVITY_MAX
            )
            prefs[TOUCH_HAPTICS] = json.optBoolean("touchHaptics", false)
            prefs[TOUCH_HAPTICS_PRESET] = json.optInt("touchHapticsPreset", DEFAULT_TOUCH_HAPTICS_PRESET).coerceIn(TOUCH_HAPTICS_PRESET_SOFT, TOUCH_HAPTICS_PRESET_STRONG)
            prefs[TOUCH_HAPTICS_STRENGTH] = json.optInt("touchHapticsStrength", DEFAULT_TOUCH_HAPTICS_STRENGTH).coerceIn(10, 100)
            prefs[GYRO_MODE] = json.optInt("gyroMode", GYRO_MODE_OFF).coerceIn(GYRO_MODE_OFF, GYRO_MODE_STEERING)
            prefs[GYRO_SENSITIVITY] = json.optInt("gyroSensitivity", DEFAULT_GYRO_SENSITIVITY).coerceIn(25, 300)
            prefs[GYRO_SMOOTHING] = json.optInt("gyroSmoothing", DEFAULT_GYRO_SMOOTHING).coerceIn(0, 90)
            prefs[GYRO_INVERT_X] = json.optBoolean("gyroInvertX", false)
            prefs[GYRO_INVERT_Y] = json.optBoolean("gyroInvertY", false)
            prefs[GAMEPAD_STICK_DEADZONE] = json.optInt("gamepadStickDeadzone", DEFAULT_GAMEPAD_STICK_DEADZONE).coerceIn(0, 35)
            prefs[GAMEPAD_LEFT_STICK_SENSITIVITY] = json.optInt("gamepadLeftStickSensitivity", DEFAULT_GAMEPAD_STICK_SENSITIVITY).coerceIn(50, 200)
            prefs[GAMEPAD_RIGHT_STICK_SENSITIVITY] = json.optInt("gamepadRightStickSensitivity", DEFAULT_GAMEPAD_STICK_SENSITIVITY).coerceIn(50, 200)
            prefs[GAMEPAD_RIGHT_STICK_UP_TO_R2] = json.optBoolean("gamepadRightStickUpToR2", false)
            prefs[GAMEPAD_RIGHT_STICK_DOWN_TO_L2] = json.optBoolean("gamepadRightStickDownToL2", false)
            prefs[GAMEPAD_BUTTON_HAPTICS] = json.optBoolean("gamepadButtonHaptics", false)
            prefs[PRESSURE_MODIFIER_AMOUNT] = json.optInt("pressureModifierAmount", DEFAULT_PRESSURE_MODIFIER_AMOUNT).coerceIn(1, 100)
            prefs[ENABLE_FAST_BOOT] = json.optBoolean("enableFastBoot", true)
            prefs[EE_CYCLE_RATE] = json.optInt("eeCycleRate", 0)
            prefs[EE_CYCLE_SKIP] = json.optInt("eeCycleSkip", 0)
            prefs[ENABLE_EE_RECOMPILER] = json.optBoolean("enableEeRecompiler", true)
            prefs[ENABLE_IOP_RECOMPILER] = json.optBoolean("enableIopRecompiler", true)
            prefs[ENABLE_VU0_RECOMPILER] = json.optBoolean("enableVu0Recompiler", true)
            prefs[ENABLE_VU1_RECOMPILER] = json.optBoolean("enableVu1Recompiler", true)
            prefs[EE_FPU_ROUND_MODE] = sanitizeFloatRoundMode(json.optInt("eeFpuRoundMode", DEFAULT_EE_FPU_ROUND_MODE), DEFAULT_EE_FPU_ROUND_MODE)
            prefs[VU0_ROUND_MODE] = sanitizeFloatRoundMode(json.optInt("vu0RoundMode", DEFAULT_VU_ROUND_MODE), DEFAULT_VU_ROUND_MODE)
            prefs[VU1_ROUND_MODE] = sanitizeFloatRoundMode(json.optInt("vu1RoundMode", DEFAULT_VU_ROUND_MODE), DEFAULT_VU_ROUND_MODE)
            prefs[EE_FPU_CLAMPING_MODE] = sanitizeClampingMode(json.optInt("eeFpuClampingMode", DEFAULT_EE_FPU_CLAMPING_MODE), DEFAULT_EE_FPU_CLAMPING_MODE)
            val legacyVuClampingMode = json.optInt("vuClampingMode", DEFAULT_VU0_CLAMPING_MODE)
            prefs[VU0_CLAMPING_MODE] = sanitizeClampingMode(json.optInt("vu0ClampingMode", legacyVuClampingMode), DEFAULT_VU0_CLAMPING_MODE)
            prefs[VU1_CLAMPING_MODE] = sanitizeClampingMode(json.optInt("vu1ClampingMode", DEFAULT_VU1_CLAMPING_MODE), DEFAULT_VU1_CLAMPING_MODE)
            prefs[ENABLE_GAME_FIXES] = json.optBoolean("enableGameFixes", true)
            prefs[ENABLE_EE_TIMING_HACK] = json.optBoolean("enableEeTimingHack", false)
            prefs[ENABLE_WAIT_LOOP_SPEEDHACK] = json.optBoolean("enableWaitLoopSpeedhack", true)
            prefs[ENABLE_INTC_STAT_SPEEDHACK] = json.optBoolean("enableIntcStatSpeedhack", true)
            prefs[ENABLE_VU_FLAG_HACK] = json.optBoolean("enableVuFlagHack", true)
            prefs[ENABLE_INSTANT_VU1] = json.optBoolean("enableInstantVu1", true)
            prefs[ENABLE_MTVU] = json.optBoolean("enableMtvu", true)
            prefs[ENABLE_THREAD_PINNING] = json.optBoolean("enableThreadPinning", DEFAULT_THREAD_PINNING)
            prefs[ENABLE_FAST_CDVD] = json.optBoolean("enableFastCdvd", false)
            prefs[HW_DOWNLOAD_MODE] = GsHackDefaults.coerceHardwareDownloadMode(
                json.optInt("hwDownloadMode", GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT)
            )
            prefs[FRAME_SKIP] = GsHackDefaults.coerceFrameSkip(
                json.optInt("frameSkip", GsHackDefaults.FRAME_SKIP_DEFAULT)
            )
            prefs[SKIP_DUPLICATE_FRAMES] = json.optBoolean("skipDuplicateFrames", true)
            prefs[TEXTURE_FILTERING] = GsHackDefaults.coerceBilinearFiltering(
                json.optInt("textureFiltering", GsHackDefaults.BILINEAR_FILTERING_DEFAULT)
            )
            prefs[TRILINEAR_FILTERING] = GsHackDefaults.coerceTrilinearFiltering(
                json.optInt("trilinearFiltering", GsHackDefaults.TRILINEAR_FILTERING_DEFAULT)
            )
            val importedBlendingAccuracy = if (json.has("blendingAccuracy")) {
                json.optInt("blendingAccuracy")
            } else {
                GsHackDefaults.BLENDING_ACCURACY_DEFAULT
            }
            prefs[BLENDING_ACCURACY] = GsHackDefaults.coerceBlendingAccuracy(importedBlendingAccuracy)
            prefs[TEXTURE_PRELOADING] = GsHackDefaults.coerceTexturePreloading(
                json.optInt("texturePreloading", GsHackDefaults.TEXTURE_PRELOADING_DEFAULT)
            )
            prefs[TEXTURE_REPLACEMENTS_ENABLED] = json.optBoolean("textureReplacementsEnabled", false)
            prefs[TEXTURE_REPLACEMENTS_ASYNC] = json.optBoolean("textureReplacementsAsync", true)
            prefs[TEXTURE_REPLACEMENTS_PRECACHE] = json.optBoolean("textureReplacementsPrecache", false)
            prefs[TEXTURE_DUMPING_ENABLED] = json.optBoolean("textureDumpingEnabled", false)
            prefs[ENABLE_FXAA] = json.optBoolean("enableFxaa", false)
            prefs[SGSR_MODE] = json.optInt("sgsrMode", 0).coerceIn(0, 3)
            prefs[CAS_MODE] = json.optInt("casMode", 0).coerceIn(0, 2)
            prefs[CAS_SHARPNESS] = json.optInt("casSharpness", 50).coerceIn(0, 100)
            prefs[TV_SHADER] = GsHackDefaults.coerceTvShader(
                json.optInt("tvShader", GsHackDefaults.TV_SHADER_DEFAULT)
            )
            prefs[ENABLE_WIDESCREEN_PATCHES] = json.optBoolean("enableWidescreenPatches", false)
            prefs[ENABLE_NO_INTERLACING_PATCHES] = json.optBoolean("enableNoInterlacingPatches", false)
            prefs[DEINTERLACE_MODE] = GsHackDefaults.coerceDeinterlaceMode(
                json.optInt("deinterlaceMode", GsHackDefaults.DEINTERLACE_MODE_DEFAULT)
            )
            prefs[DITHERING] = GsHackDefaults.coerceDithering(
                json.optInt("dithering", GsHackDefaults.DITHERING_DEFAULT)
            )
            prefs[ANTI_BLUR] = json.optBoolean("antiBlur", GsHackDefaults.ANTI_BLUR_DEFAULT)
            prefs[ANISOTROPIC_FILTERING] = GsHackDefaults.coerceAnisotropicFiltering(
                json.optInt("anisotropicFiltering", GsHackDefaults.ANISOTROPIC_FILTERING_DEFAULT)
            )
            prefs[ENABLE_HW_MIPMAPPING] = json.optBoolean("enableHwMipmapping", GsHackDefaults.HW_MIPMAPPING_DEFAULT)
            prefs[CPU_SPRITE_RENDER_SIZE] = json.optInt("cpuSpriteRenderSize", GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT).coerceIn(0, 10)
            prefs[CPU_SPRITE_RENDER_LEVEL] = json.optInt("cpuSpriteRenderLevel", GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT).coerceIn(0, 2)
            prefs[SOFTWARE_CLUT_RENDER] = json.optInt("softwareClutRender", GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT).coerceIn(0, 2)
            prefs[GPU_TARGET_CLUT_MODE] = json.optInt("gpuTargetClutMode", GsHackDefaults.GPU_TARGET_CLUT_DEFAULT).coerceIn(0, 2)
            prefs[SKIP_DRAW_START] = json.optInt("skipDrawStart", 0).coerceIn(0, 5000)
            prefs[SKIP_DRAW_END] = json.optInt("skipDrawEnd", json.optInt("skipDraw", 0)).coerceIn(0, 5000)
            prefs[AUTO_FLUSH_HARDWARE] = json.optInt("autoFlushHardware", GsHackDefaults.AUTO_FLUSH_DEFAULT).coerceIn(0, 2)
            prefs[CPU_FRAMEBUFFER_CONVERSION] = json.optBoolean("cpuFramebufferConversion", false)
            prefs[DISABLE_DEPTH_CONVERSION] = json.optBoolean("disableDepthConversion", false)
            prefs[DISABLE_SAFE_FEATURES] = json.optBoolean("disableSafeFeatures", false)
            prefs[DISABLE_RENDER_FIXES] = json.optBoolean("disableRenderFixes", false)
            prefs[PRELOAD_FRAME_DATA] = json.optBoolean("preloadFrameData", false)
            prefs[DISABLE_PARTIAL_INVALIDATION] = json.optBoolean("disablePartialInvalidation", false)
            prefs[TEXTURE_INSIDE_RT] = json.optInt("textureInsideRt", GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT).coerceIn(0, 2)
            prefs[READ_TARGETS_ON_CLOSE] = json.optBoolean("readTargetsOnClose", false)
            prefs[ESTIMATE_TEXTURE_REGION] = json.optBoolean("estimateTextureRegion", false)
            prefs[GPU_PALETTE_CONVERSION] = json.optBoolean("gpuPaletteConversion", false)
            prefs[HALF_PIXEL_OFFSET] = json.optInt("halfPixelOffset", GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT).coerceIn(0, 5)
            prefs[NATIVE_SCALING] = GsHackDefaults.coerceNativeScaling(
                json.optInt("nativeScaling", GsHackDefaults.NATIVE_SCALING_DEFAULT)
            )
            prefs[ROUND_SPRITE] = json.optInt("roundSprite", GsHackDefaults.ROUND_SPRITE_DEFAULT).coerceIn(0, 2)
            prefs[BILINEAR_UPSCALE] = json.optInt("bilinearUpscale", GsHackDefaults.BILINEAR_UPSCALE_DEFAULT).coerceIn(0, 2)
            prefs[TEXTURE_OFFSET_X] = json.optInt("textureOffsetX", 0).coerceIn(-4096, 4096)
            prefs[TEXTURE_OFFSET_Y] = json.optInt("textureOffsetY", 0).coerceIn(-4096, 4096)
            prefs[ALIGN_SPRITE] = json.optBoolean("alignSprite", false)
            prefs[MERGE_SPRITE] = json.optBoolean("mergeSprite", false)
            prefs[FORCE_EVEN_SPRITE_POSITION] = json.optBoolean("forceEvenSpritePosition", false)
            prefs[NATIVE_PALETTE_DRAW] = json.optBoolean("nativePaletteDraw", false)
            prefs[ENABLE_AUTO_GAMEPAD] = json.optBoolean("enableAutoGamepad", true)
            prefs[HIDE_OVERLAY_ON_GAMEPAD] = json.optBoolean("hideOverlayOnGamepad", true)
            json.optString("gamepadBindings").takeIf { it.isNotBlank() }?.let { prefs[GAMEPAD_BINDINGS] = it } ?: prefs.remove(GAMEPAD_BINDINGS)
            prefs[GPU_DRIVER_TYPE] = json.optInt("gpuDriverType", 0)
            json.optString("customDriverPath").takeIf { it.isNotBlank() }?.let { prefs[CUSTOM_DRIVER_PATH] = it } ?: prefs.remove(CUSTOM_DRIVER_PATH)
            prefs[DEV9_ETHERNET_ENABLED] = json.optBoolean("dev9EthernetEnabled", false)
            prefs[DEV9_ETHERNET_DEVICE] = json.optString("dev9EthernetDevice", "Auto").ifBlank { "Auto" }
            prefs[DEV9_INTERCEPT_DHCP] = json.optBoolean("dev9InterceptDhcp", false)
            prefs[DEV9_DNS1_MODE] = sanitizeDev9DnsMode(json.optString("dev9Dns1Mode", DEV9_DNS_MODE_AUTO))
            prefs[DEV9_DNS1] = sanitizeIpv4(json.optString("dev9Dns1", "0.0.0.0"))
            prefs[DEV9_DNS2_MODE] = sanitizeDev9DnsMode(json.optString("dev9Dns2Mode", DEV9_DNS_MODE_AUTO))
            prefs[DEV9_DNS2] = sanitizeIpv4(json.optString("dev9Dns2", "0.0.0.0"))
            prefs[DEV9_LOG_DHCP] = json.optBoolean("dev9LogDhcp", false)
            prefs[DEV9_LOG_DNS] = json.optBoolean("dev9LogDns", false)
            prefs[DEV9_LOCAL_LINK_MODE] = sanitizeLocalLinkMode(json.optInt("dev9LocalLinkMode", DEV9_LOCAL_LINK_OFF))
            prefs[DEV9_LOCAL_LINK_ADDRESS] = sanitizeIpv4(json.optString("dev9LocalLinkAddress", "192.168.43.1"), "192.168.43.1")
            prefs[DEV9_LOCAL_LINK_PORT] = json.optInt("dev9LocalLinkPort", DEFAULT_LOCAL_LINK_PORT).coerceIn(1024, 65535)
            prefs[DEV9_LOCAL_LINK_PEER_ID] = json.optInt("dev9LocalLinkPeerId", defaultLocalLinkPeerId()).coerceIn(2, 65533)
            prefs[DEV9_LOCAL_LINK_ROOM_CODE] = sanitizeLocalLinkRoomCode(json.optString("dev9LocalLinkRoomCode", ""))
            prefs[FRAME_LIMIT_ENABLED] = json.optBoolean("frameLimitEnabled", true)
            prefs[VSYNC_ENABLED] = json.optBoolean("vSyncEnabled", false)
            prefs[FAST_FORWARD_SPEED] = sanitizeFastForwardSpeed(json.optDouble("fastForwardSpeed", DEFAULT_FAST_FORWARD_SPEED.toDouble()).toFloat())
            prefs[TARGET_FPS] = json.optInt("targetFps", 0).let { if (it <= 0) 0 else it.coerceIn(20, 120) }
            prefs[NTSC_FRAMERATE] = sanitizeRegionFramerate(json.optDouble("ntscFramerate", DEFAULT_NTSC_FRAMERATE.toDouble()).toFloat(), DEFAULT_NTSC_FRAMERATE)
            prefs[PAL_FRAMERATE] = sanitizeRegionFramerate(json.optDouble("palFramerate", DEFAULT_PAL_FRAMERATE.toDouble()).toFloat(), DEFAULT_PAL_FRAMERATE)
            prefs[AUTO_SAVE_ENABLED] = json.optBoolean("autoSaveEnabled", false)
            prefs[AUTO_SAVE_INTERVAL_MINUTES] = json.optInt("autoSaveIntervalMinutes", 1).coerceIn(1, 999)
            val importedOverlayVersion = json.optInt("overlayLayoutVersion", 0)
            json.optString("dpadOffset").takeIf { it.isNotBlank() }?.let {
                prefs[DPAD_OFFSET] = if (importedOverlayVersion >= 12) {
                    it
                } else {
                    val (x, y) = parseOffsetStr(it, DEFAULT_DPAD_OFFSET_X to DEFAULT_DPAD_OFFSET_Y)
                    formatOffsetStr(x + LEFT_SIDE_LAYOUT_SHIFT_X, y)
                }
            } ?: prefs.remove(DPAD_OFFSET)
            json.optString("lstickOffset").takeIf { it.isNotBlank() }?.let {
                prefs[LSTICK_OFFSET] = if (importedOverlayVersion >= 12) {
                    it
                } else {
                    val (x, y) = parseOffsetStr(it, DEFAULT_LSTICK_OFFSET_X to DEFAULT_LSTICK_OFFSET_Y)
                    formatOffsetStr(x + LEFT_SIDE_LAYOUT_SHIFT_X, y)
                }
            } ?: prefs.remove(LSTICK_OFFSET)
            json.optString("rstickOffset").takeIf { it.isNotBlank() }?.let { prefs[RSTICK_OFFSET] = it } ?: prefs.remove(RSTICK_OFFSET)
            json.optString("actionOffset").takeIf { it.isNotBlank() }?.let { prefs[ACTION_OFFSET] = it } ?: prefs.remove(ACTION_OFFSET)
            json.optString("lbtnOffset").takeIf { it.isNotBlank() }?.let { prefs[LBTN_OFFSET] = it } ?: prefs.remove(LBTN_OFFSET)
            json.optString("rbtnOffset").takeIf { it.isNotBlank() }?.let { prefs[RBTN_OFFSET] = it } ?: prefs.remove(RBTN_OFFSET)
            json.optString("centerOffset").takeIf { it.isNotBlank() }?.let { prefs[CENTER_OFFSET] = it } ?: prefs.remove(CENTER_OFFSET)
            prefs[STICK_SCALE] = json.optInt("stickScale", OVERLAY_CONTROL_SCALE_DEFAULT)
                .coerceIn(OVERLAY_CONTROL_SCALE_MIN, OVERLAY_CONTROL_SCALE_MAX)
            prefs[LEFT_STICK_SENSITIVITY] = json.optInt("leftStickSensitivity", 100).coerceIn(50, 200)
            prefs[RIGHT_STICK_SENSITIVITY] = json.optInt("rightStickSensitivity", 100).coerceIn(50, 200)
            prefs[INVERT_LEFT_STICK] = json.optBoolean("invertLeftStick", false)
            prefs[INVERT_RIGHT_STICK] = json.optBoolean("invertRightStick", false)
            prefs[INVERT_LEFT_STICK_HORIZONTAL] = json.optBoolean("invertLeftStickHorizontal", false)
            prefs[INVERT_RIGHT_STICK_HORIZONTAL] = json.optBoolean("invertRightStickHorizontal", false)
            prefs[STICK_SURFACE_MODE] = json.optBoolean("stickSurfaceMode", false)
            json.optString("controlLayouts").takeIf { it.isNotBlank() }?.let { prefs[CONTROL_LAYOUTS] = it } ?: prefs.remove(CONTROL_LAYOUTS)
            migrateGlobalStickSurfaceMode(prefs)
            json.optString("memoryCardSlot1").takeIf { it.isNotBlank() }?.let { prefs[MEMORY_CARD_SLOT1] = it } ?: prefs.remove(MEMORY_CARD_SLOT1)
            json.optString("memoryCardSlot2").takeIf { it.isNotBlank() }?.let { prefs[MEMORY_CARD_SLOT2] = it } ?: prefs.remove(MEMORY_CARD_SLOT2)
        }
    }

    private fun readUpscale(prefs: Preferences): Float {
        return (prefs[UPSCALE]
            ?: prefs[UPSCALE_LEGACY]?.toFloat()
            ?: 1f).let(::normalizeUpscale)
    }

    private fun sanitizeRegionFramerate(value: Float?, fallback: Float): Float {
        val raw = value ?: fallback
        return if (raw.isFinite()) raw.coerceIn(20f, 120f) else fallback
    }

    private fun sanitizeFloatRoundMode(value: Int?, fallback: Int): Int {
        return when (value) {
            FLOAT_ROUND_NEAREST,
            FLOAT_ROUND_NEGATIVE,
            FLOAT_ROUND_POSITIVE,
            FLOAT_ROUND_CHOP -> value
            else -> fallback
        }
    }

    private fun sanitizeClampingMode(value: Int?, fallback: Int): Int {
        return when (value) {
            CLAMPING_NONE,
            CLAMPING_NORMAL,
            CLAMPING_EXTRA,
            CLAMPING_FULL -> value
            else -> fallback
        }
    }

    private fun sanitizeFastForwardSpeed(value: Float?): Float {
        val raw = value ?: DEFAULT_FAST_FORWARD_SPEED
        return if (raw.isFinite()) raw.coerceIn(MIN_FAST_FORWARD_SPEED, MAX_FAST_FORWARD_SPEED) else DEFAULT_FAST_FORWARD_SPEED
    }

    suspend fun setAchievementsEnabled(enabled: Boolean) {
        context.dataStore.edit { it[ACHIEVEMENTS_ENABLED] = enabled }
    }

    suspend fun setAchievementsHardcore(enabled: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[ACHIEVEMENTS_HARDCORE] = enabled
            if (enabled) {
                prefs[ENABLE_CHEATS] = false
                prefs[FRAME_LIMIT_ENABLED] = true
            }
        }
    }

    suspend fun setAchievementsNotifications(enabled: Boolean) {
        context.dataStore.edit { it[ACHIEVEMENTS_NOTIFICATIONS] = enabled }
    }

    suspend fun setAchievementsLeaderboardNotifications(enabled: Boolean) {
        context.dataStore.edit { it[ACHIEVEMENTS_LEADERBOARD_NOTIFICATIONS] = enabled }
    }

    suspend fun setAchievementsIndicators(enabled: Boolean) {
        context.dataStore.edit { it[ACHIEVEMENTS_INDICATORS] = enabled }
    }

    suspend fun setAchievementsLeaderboardTrackers(enabled: Boolean) {
        context.dataStore.edit { it[ACHIEVEMENTS_LEADERBOARD_TRACKERS] = enabled }
    }

    suspend fun setAchievementsSoundEffects(enabled: Boolean) {
        context.dataStore.edit { it[ACHIEVEMENTS_SOUND_EFFECTS] = enabled }
    }

    suspend fun setAchievementsUnlockSound(path: String?, displayName: String?) {
        context.dataStore.edit { prefs ->
            if (path.isNullOrBlank() || displayName.isNullOrBlank()) {
                prefs.remove(ACHIEVEMENTS_UNLOCK_SOUND_PATH)
                prefs.remove(ACHIEVEMENTS_UNLOCK_SOUND_NAME)
            } else {
                prefs[ACHIEVEMENTS_UNLOCK_SOUND_PATH] = path
                prefs[ACHIEVEMENTS_UNLOCK_SOUND_NAME] = displayName
            }
        }
    }

    suspend fun setAchievementsUsername(username: String?) {
        context.dataStore.edit { prefs ->
            if (username == null) prefs.remove(ACHIEVEMENTS_USERNAME)
            else prefs[ACHIEVEMENTS_USERNAME] = username
        }
    }

    suspend fun setAchievementsToken(token: String?) {
        context.dataStore.edit { prefs ->
            if (token == null) prefs.remove(ACHIEVEMENTS_TOKEN)
            else prefs[ACHIEVEMENTS_TOKEN] = token
        }
    }

    suspend fun setAchievementsLoginTimestamp(timestamp: String?) {
        context.dataStore.edit { prefs ->
            if (timestamp == null) prefs.remove(ACHIEVEMENTS_LOGIN_TIMESTAMP)
            else prefs[ACHIEVEMENTS_LOGIN_TIMESTAMP] = timestamp
        }
    }

    suspend fun setAchievementsAvatarPath(avatarPath: String?) {
        context.dataStore.edit { prefs ->
            if (avatarPath.isNullOrBlank()) prefs.remove(ACHIEVEMENTS_AVATAR_PATH)
            else prefs[ACHIEVEMENTS_AVATAR_PATH] = avatarPath
        }
    }

    suspend fun setAchievementsProfileCache(cache: AchievementsProfileCache?) {
        context.dataStore.edit { prefs ->
            if (cache == null) {
                prefs.remove(ACHIEVEMENTS_PROFILE_USERNAME)
                prefs.remove(ACHIEVEMENTS_DISPLAY_NAME)
                prefs.remove(ACHIEVEMENTS_AVATAR_PATH)
                prefs.remove(ACHIEVEMENTS_POINTS)
                prefs.remove(ACHIEVEMENTS_SOFTCORE_POINTS)
                prefs.remove(ACHIEVEMENTS_UNREAD_MESSAGES)
                prefs.remove(ACHIEVEMENTS_PROFILE_UPDATED_AT)
            } else {
                prefs[ACHIEVEMENTS_PROFILE_USERNAME] = cache.username
                prefs[ACHIEVEMENTS_DISPLAY_NAME] = cache.displayName
                if (cache.avatarPath.isNullOrBlank()) prefs.remove(ACHIEVEMENTS_AVATAR_PATH)
                else prefs[ACHIEVEMENTS_AVATAR_PATH] = cache.avatarPath
                prefs[ACHIEVEMENTS_POINTS] = cache.points.coerceAtLeast(0)
                prefs[ACHIEVEMENTS_SOFTCORE_POINTS] = cache.softcorePoints.coerceAtLeast(0)
                prefs[ACHIEVEMENTS_UNREAD_MESSAGES] = cache.unreadMessages.coerceAtLeast(0)
                prefs[ACHIEVEMENTS_PROFILE_UPDATED_AT] = cache.updatedAtMillis
            }
        }
    }

    suspend fun setAchievementsRememberPassword(remember: Boolean) {
        context.dataStore.edit { prefs ->
            prefs[ACHIEVEMENTS_REMEMBER_PASSWORD] = remember
            if (!remember) {
                prefs.remove(ACHIEVEMENTS_PASSWORD)
            }
        }
    }

    suspend fun setAchievementsPassword(password: String?) {
        context.dataStore.edit { prefs ->
            if (password == null) prefs.remove(ACHIEVEMENTS_PASSWORD)
            else prefs[ACHIEVEMENTS_PASSWORD] = password
        }
    }

    suspend fun setAchievementsAccountProgressJson(json: String?) {
        context.dataStore.edit { prefs ->
            if (json.isNullOrBlank()) {
                prefs.remove(ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON)
                prefs.remove(ACHIEVEMENTS_ACCOUNT_PROGRESS_USERNAME)
                prefs.remove(ACHIEVEMENTS_ACCOUNT_PROGRESS_UPDATED_AT)
            } else {
                prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON] = json
            }
        }
    }

    suspend fun setAchievementsAccountProgressCache(cache: AchievementsAccountProgressCache?) {
        context.dataStore.edit { prefs ->
            if (cache == null) {
                prefs.remove(ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON)
                prefs.remove(ACHIEVEMENTS_ACCOUNT_PROGRESS_USERNAME)
                prefs.remove(ACHIEVEMENTS_ACCOUNT_PROGRESS_UPDATED_AT)
            } else {
                prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON] = cache.json
                prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_USERNAME] = cache.username
                prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_UPDATED_AT] = cache.updatedAtMillis
            }
        }
    }

    fun getAchievementsEnabledSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_ENABLED] ?: false }.first()
        }
    }

    fun getAchievementsHardcoreSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_HARDCORE] ?: false }.first()
        }
    }

    fun getAchievementsNotificationsSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_NOTIFICATIONS] ?: true }.first()
        }
    }

    fun getAchievementsLeaderboardNotificationsSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_LEADERBOARD_NOTIFICATIONS] ?: true }.first()
        }
    }

    fun getAchievementsIndicatorsSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_INDICATORS] ?: true }.first()
        }
    }

    fun getAchievementsLeaderboardTrackersSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_LEADERBOARD_TRACKERS] ?: true }.first()
        }
    }

    fun getAchievementsSoundEffectsSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_SOUND_EFFECTS] ?: true }.first()
        }
    }

    fun getAchievementsUnlockSoundPathSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_UNLOCK_SOUND_PATH] }.first()
        }?.takeIf { File(it).isFile }
    }

    fun getAchievementsUnlockSoundNameSync(): String? {
        if (getAchievementsUnlockSoundPathSync() == null) return null
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_UNLOCK_SOUND_NAME] }.first()
        }
    }

    fun getAchievementsUsernameSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_USERNAME] }.first()
        }
    }

    fun getAchievementsTokenSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_TOKEN] }.first()
        }
    }

    fun getAchievementsAvatarPathSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_AVATAR_PATH] }.first()
        }
    }

    fun getAchievementsProfileCacheSync(): AchievementsProfileCache? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { prefs ->
                val username = prefs[ACHIEVEMENTS_PROFILE_USERNAME]?.trim().orEmpty()
                if (username.isBlank()) return@map null
                AchievementsProfileCache(
                    username = username,
                    displayName = prefs[ACHIEVEMENTS_DISPLAY_NAME]?.takeIf { it.isNotBlank() } ?: username,
                    avatarPath = prefs[ACHIEVEMENTS_AVATAR_PATH]?.takeIf { it.isNotBlank() },
                    points = (prefs[ACHIEVEMENTS_POINTS] ?: 0).coerceAtLeast(0),
                    softcorePoints = (prefs[ACHIEVEMENTS_SOFTCORE_POINTS] ?: 0).coerceAtLeast(0),
                    unreadMessages = (prefs[ACHIEVEMENTS_UNREAD_MESSAGES] ?: 0).coerceAtLeast(0),
                    updatedAtMillis = prefs[ACHIEVEMENTS_PROFILE_UPDATED_AT] ?: 0L
                )
            }.first()
        }
    }

    fun getAchievementsRememberPasswordSync(): Boolean {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_REMEMBER_PASSWORD] ?: false }.first()
        }
    }

    fun getAchievementsPasswordSync(): String? {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { it[ACHIEVEMENTS_PASSWORD] }.first()
        }
    }

    suspend fun getAchievementsAccountProgressJson(): String? {
        return context.dataStore.data.map { it[ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON] }.first()
    }

    suspend fun getAchievementsAccountProgressCache(): AchievementsAccountProgressCache? {
        return context.dataStore.data.map { prefs ->
            val username = prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_USERNAME]?.trim().orEmpty()
            val json = prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_JSON].orEmpty()
            if (username.isBlank() || json.isBlank()) return@map null
            AchievementsAccountProgressCache(
                username = username,
                json = json,
                updatedAtMillis = prefs[ACHIEVEMENTS_ACCOUNT_PROGRESS_UPDATED_AT] ?: 0L
            )
        }.first()
    }

    private fun JSONObject.readUpscaleMultiplier(): Float {
        val doubleValue = optDouble("upscaleMultiplier", Double.NaN)
        return when {
            !doubleValue.isNaN() -> doubleValue.toFloat()
            has("upscaleMultiplier") -> optInt("upscaleMultiplier", 1).toFloat()
            else -> 1f
        }.let(::normalizeUpscale)
    }

    private fun resolveShadeBoostEnabled(
        explicitValue: Boolean?,
        brightness: Int,
        contrast: Int,
        saturation: Int,
        gamma: Int
    ): Boolean {
        return explicitValue == true || isShadeBoostActive(
            brightness = brightness,
            contrast = contrast,
            saturation = saturation,
            gamma = gamma
        )
    }

    private fun isShadeBoostActive(
        brightness: Int,
        contrast: Int,
        saturation: Int,
        gamma: Int
    ): Boolean {
        return brightness != 50 || contrast != 50 || saturation != 50 || gamma != 50
    }
}

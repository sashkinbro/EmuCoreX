package com.sbro.emucorex.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.MutablePreferences
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.floatPreferencesKey
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.core.normalizeUpscale
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.first
import org.json.JSONArray
import org.json.JSONObject

data class RecentGameEntry(
    val path: String,
    val title: String,
    val lastPlayedAt: Long,
    val serial: String? = null
)

data class SettingsSnapshot(
    val themeMode: ThemeMode = ThemeMode.SYSTEM,
    val languageTag: String? = null,
    val performanceProfile: Int = PerformanceProfiles.SAFE,
    val renderer: Int = EmulatorBridge.VULKAN_RENDERER,
    val upscaleMultiplier: Float = 1f,
    val aspectRatio: Int = 1,
    val padVibration: Boolean = true,
    val showFps: Boolean = false,
    val fpsOverlayMode: Int = AppPreferences.FPS_OVERLAY_MODE_DETAILED,
    val fpsOverlayCorner: Int = AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT,
    val compactControls: Boolean = true,
    val keepScreenOn: Boolean = true,
    val showRecentGames: Boolean = true,
    val showHomeSearch: Boolean = true,
    val preferEnglishGameTitles: Boolean = false,
    val biosPath: String? = null,
    val biosValid: Boolean = false,
    val gamePath: String? = null,
    val coverDownloadBaseUrl: String? = null,
    val coverArtStyle: Int = AppPreferences.COVER_ART_STYLE_DISABLED,
    val setupComplete: Boolean = false,
    val eeCycleRate: Int = PerformanceProfiles.safeConfig.eeCycleRate,
    val eeCycleSkip: Int = PerformanceProfiles.safeConfig.eeCycleSkip,
    val enableMtvu: Boolean = true,
    val enableFastCdvd: Boolean = false,
    val enableCheats: Boolean = true,
    val hwDownloadMode: Int = PerformanceProfiles.safeConfig.hwDownloadMode,
    val frameSkip: Int = 0,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val casMode: Int = 0,
    val casSharpness: Int = 50,
    val enableWidescreenPatches: Boolean = false,
    val enableNoInterlacingPatches: Boolean = false,
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
    val overlayOpacity: Int = 80,
    val overlayShow: Boolean = true,
    val enableAutoGamepad: Boolean = true,
    val hideOverlayOnGamepad: Boolean = true,
    val gamepadBindings: Map<String, Int> = emptyMap(),
    val gpuDriverType: Int = 0,
    val customDriverPath: String? = null,
    val frameLimitEnabled: Boolean = false,
    val targetFps: Int = 0
)

data class OverlayLayoutSnapshot(
    val overlayScale: Int = 100,
    val overlayOpacity: Int = 80,
    val hideOverlayOnGamepad: Boolean = true,
    val dpadOffset: Pair<Float, Float> = AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y,
    val lstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y,
    val rstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y,
    val actionOffset: Pair<Float, Float> = AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y,
    val lbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y,
    val rbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y,
    val centerOffset: Pair<Float, Float> = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
    val stickScale: Int = 100,
    val controlLayouts: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts()
)

data class OverlayControlLayout(
    val offset: Pair<Float, Float> = 0f to 0f,
    val scale: Int = 100,
    val visible: Boolean = true
)

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "settings")

class AppPreferences(private val context: Context) {

    private val localePrefs = context.getSharedPreferences("ui_locale", Context.MODE_PRIVATE)

    companion object {
        private const val CURRENT_OVERLAY_LAYOUT_VERSION = 15
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
        const val COVER_ART_STYLE_DISABLED = -1
        const val COVER_ART_STYLE_DEFAULT = 0
        const val COVER_ART_STYLE_3D = 1
        const val FPS_OVERLAY_MODE_SIMPLE = 0
        const val FPS_OVERLAY_MODE_DETAILED = 1
        const val FPS_OVERLAY_CORNER_TOP_LEFT = 0
        const val FPS_OVERLAY_CORNER_TOP_RIGHT = 1
        const val FPS_OVERLAY_CORNER_BOTTOM_LEFT = 2
        const val FPS_OVERLAY_CORNER_BOTTOM_RIGHT = 3

        fun defaultOverlayControlLayouts(stickScale: Int = 100): Map<String, OverlayControlLayout> = mapOf(
            "l2" to OverlayControlLayout(),
            "l1" to OverlayControlLayout(),
            "r2" to OverlayControlLayout(),
            "r1" to OverlayControlLayout(),
            "dpad_up" to OverlayControlLayout(visible = false),
            "dpad_down" to OverlayControlLayout(visible = false),
            "dpad_left" to OverlayControlLayout(visible = false),
            "dpad_right" to OverlayControlLayout(visible = false),
            "left_stick" to OverlayControlLayout(scale = stickScale, visible = true),
            "triangle" to OverlayControlLayout(),
            "cross" to OverlayControlLayout(),
            "square" to OverlayControlLayout(),
            "circle" to OverlayControlLayout(),
            "right_stick" to OverlayControlLayout(scale = stickScale, visible = false),
            "select" to OverlayControlLayout(scale = 80),
            "left_input_toggle" to OverlayControlLayout(scale = 80, visible = true),
            "start" to OverlayControlLayout(scale = 80),
            "l3" to OverlayControlLayout(visible = false),
            "r3" to OverlayControlLayout(visible = false)
        )

        private val THEME_MODE = intPreferencesKey("theme_mode")
        private val RENDERER = intPreferencesKey("renderer")
        private val UPSCALE = floatPreferencesKey("upscale_multiplier_v2")
        private val UPSCALE_LEGACY = intPreferencesKey("upscale_multiplier")
        private val BIOS_PATH = stringPreferencesKey("bios_path")
        private val GAME_PATH = stringPreferencesKey("game_path")
        private val COVER_DOWNLOAD_BASE_URL = stringPreferencesKey("cover_download_base_url")
        private val COVER_ART_STYLE = intPreferencesKey("cover_art_style")
        private val ONBOARDING_COMPLETED = booleanPreferencesKey("onboarding_completed")
        private val PERFORMANCE_PROFILE = intPreferencesKey("performance_profile")
        private val LANGUAGE_TAG = stringPreferencesKey("language_tag")
        private val ASPECT_RATIO = intPreferencesKey("aspect_ratio")
        private val PAD_VIBRATION = booleanPreferencesKey("pad_vibration")
        private val SHOW_FPS = booleanPreferencesKey("show_fps")
        private val FPS_OVERLAY_MODE = intPreferencesKey("fps_overlay_mode")
        private val FPS_OVERLAY_CORNER = intPreferencesKey("fps_overlay_corner")
        private val COMPACT_CONTROLS = booleanPreferencesKey("compact_controls")
        private val KEEP_SCREEN_ON = booleanPreferencesKey("keep_screen_on")
        private val SHOW_RECENT_GAMES = booleanPreferencesKey("show_recent_games")
        private val SHOW_HOME_SEARCH = booleanPreferencesKey("show_home_search")
        private val PREFER_ENGLISH_GAME_TITLES = booleanPreferencesKey("prefer_english_game_titles")
        private val RECENT_GAMES = stringPreferencesKey("recent_games")
        private val HOME_LIBRARY_VIEW_MODE = intPreferencesKey("home_library_view_mode")
        private const val MAX_RECENT_GAMES = 8
        // Overlay customization
        private val OVERLAY_SCALE = intPreferencesKey("overlay_scale")
        private val OVERLAY_OPACITY = intPreferencesKey("overlay_opacity")
        private val OVERLAY_SHOW = booleanPreferencesKey("overlay_show")
        // Extended emulator settings
        private val EE_CYCLE_RATE = intPreferencesKey("ee_cycle_rate")
        private val EE_CYCLE_SKIP = intPreferencesKey("ee_cycle_skip")
        private val ENABLE_MTVU = booleanPreferencesKey("enable_mtvu")
        private val ENABLE_FAST_CDVD = booleanPreferencesKey("enable_fast_cdvd")
        private val ENABLE_CHEATS = booleanPreferencesKey("enable_cheats")
        private val HW_DOWNLOAD_MODE = intPreferencesKey("hw_download_mode")
        private val FRAME_SKIP = intPreferencesKey("frame_skip")
        private val TEXTURE_FILTERING = intPreferencesKey("texture_filtering")
        private val TRILINEAR_FILTERING = intPreferencesKey("trilinear_filtering")
        private val BLENDING_ACCURACY = intPreferencesKey("blending_accuracy")
        private val TEXTURE_PRELOADING = intPreferencesKey("texture_preloading")
        private val ENABLE_FXAA = booleanPreferencesKey("enable_fxaa")
        private val CAS_MODE = intPreferencesKey("cas_mode")
        private val CAS_SHARPNESS = intPreferencesKey("cas_sharpness")
        private val ENABLE_WIDESCREEN_PATCHES = booleanPreferencesKey("enable_widescreen_patches")
        private val ENABLE_NO_INTERLACING_PATCHES = booleanPreferencesKey("enable_no_interlacing_patches")
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
        private val MANUAL_HACKS_BASELINE_VERSION = intPreferencesKey("manual_hacks_baseline_version")
        private val ENABLE_AUTO_GAMEPAD = booleanPreferencesKey("enable_auto_gamepad")
        private val HIDE_OVERLAY_ON_GAMEPAD = booleanPreferencesKey("hide_overlay_on_gamepad")
        private val GAMEPAD_BINDINGS = stringPreferencesKey("gamepad_bindings")
        private val GPU_DRIVER_TYPE = intPreferencesKey("gpu_driver_type")
        private val CUSTOM_DRIVER_PATH = stringPreferencesKey("custom_driver_path")
        private val FRAME_LIMIT_ENABLED = booleanPreferencesKey("frame_limit_enabled")
        private val TARGET_FPS = intPreferencesKey("target_fps")
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
        private val CONTROL_LAYOUTS = stringPreferencesKey("control_layouts")
        private val OVERLAY_LAYOUT_VERSION = intPreferencesKey("overlay_layout_version")
    }

    // Theme
    val themeMode: Flow<ThemeMode> = context.dataStore.data.map { prefs ->
        when (prefs[THEME_MODE]) {
            1 -> ThemeMode.LIGHT
            2 -> ThemeMode.DARK
            else -> ThemeMode.SYSTEM
        }
    }

    suspend fun setThemeMode(mode: ThemeMode) {
        context.dataStore.edit { prefs ->
            prefs[THEME_MODE] = when (mode) {
                ThemeMode.SYSTEM -> 0
                ThemeMode.LIGHT -> 1
                ThemeMode.DARK -> 2
            }
        }
    }


    val renderer: Flow<Int> = context.dataStore.data.map { prefs ->
        normalizeRendererPreference(prefs[RENDERER])
    }

    val performanceProfile: Flow<Int> = context.dataStore.data.map { prefs ->
        PerformanceProfiles.normalize(prefs[PERFORMANCE_PROFILE] ?: PerformanceProfiles.SAFE)
    }


    val gpuDriverType: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[GPU_DRIVER_TYPE] ?: 0
    }

    val customDriverPath: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[CUSTOM_DRIVER_PATH]
    }

    suspend fun setRenderer(value: Int) {
        context.dataStore.edit { it[RENDERER] = normalizeRendererPreference(value) }
    }

    suspend fun setPerformanceProfile(value: Int) {
        context.dataStore.edit { it[PERFORMANCE_PROFILE] = PerformanceProfiles.normalize(value) }
    }

    private fun normalizeRendererPreference(value: Int?): Int {
        return when (value) {
            null, 0, EmulatorBridge.AUTO_RENDERER -> EmulatorBridge.VULKAN_RENDERER
            else -> value
        }
    }

    private fun resolvePerformanceProfile(prefs: Preferences): Int {
        return PerformanceProfiles.normalize(prefs[PERFORMANCE_PROFILE] ?: PerformanceProfiles.SAFE)
    }

    private fun resolvePerformanceProfileConfig(prefs: Preferences) =
        PerformanceProfiles.configFor(resolvePerformanceProfile(prefs))

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
        prefs[FRAME_LIMIT_ENABLED] ?: false
    }

    suspend fun setFrameLimitEnabled(enabled: Boolean) {
        context.dataStore.edit { it[FRAME_LIMIT_ENABLED] = enabled }
    }

    val targetFps: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TARGET_FPS] ?: 0
    }

    suspend fun setTargetFps(value: Int) {
        context.dataStore.edit { it[TARGET_FPS] = if (value <= 0) 0 else value.coerceIn(20, 120) }
    }

    suspend fun resetAllSettings() {
        context.dataStore.edit { it.clear() }
        localePrefs.edit().remove("language_tag").apply()
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
    val gamePath: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[GAME_PATH]
    }

    suspend fun setGamePath(path: String) {
        context.dataStore.edit { it[GAME_PATH] = path }
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
            COVER_ART_STYLE_DEFAULT -> COVER_ART_STYLE_DEFAULT
            COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
            else -> COVER_ART_STYLE_DISABLED
        }
    }

    suspend fun setCoverArtStyle(style: Int) {
        context.dataStore.edit { prefs ->
            prefs[COVER_ART_STYLE] = when (style) {
                COVER_ART_STYLE_DEFAULT -> COVER_ART_STYLE_DEFAULT
                COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                else -> COVER_ART_STYLE_DISABLED
            }
        }
    }

    fun getCoverArtStyleSync(): Int {
        return kotlinx.coroutines.runBlocking {
            context.dataStore.data.map { prefs ->
                when (prefs[COVER_ART_STYLE]) {
                    COVER_ART_STYLE_DEFAULT -> COVER_ART_STYLE_DEFAULT
                    COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                    else -> COVER_ART_STYLE_DISABLED
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
            SettingsSnapshot(
                themeMode = when (prefs[THEME_MODE]) {
                    1 -> ThemeMode.LIGHT
                    2 -> ThemeMode.DARK
                    else -> ThemeMode.SYSTEM
                },
                languageTag = prefs[LANGUAGE_TAG],
                performanceProfile = performanceProfile,
                renderer = normalizeRendererPreference(prefs[RENDERER]),
                upscaleMultiplier = readUpscale(prefs),
                aspectRatio = prefs[ASPECT_RATIO] ?: 1,
                padVibration = prefs[PAD_VIBRATION] ?: true,
                showFps = prefs[SHOW_FPS] ?: false,
                fpsOverlayMode = prefs[FPS_OVERLAY_MODE] ?: FPS_OVERLAY_MODE_DETAILED,
                fpsOverlayCorner = when (prefs[FPS_OVERLAY_CORNER]) {
                    FPS_OVERLAY_CORNER_TOP_LEFT,
                    FPS_OVERLAY_CORNER_TOP_RIGHT,
                    FPS_OVERLAY_CORNER_BOTTOM_LEFT,
                    FPS_OVERLAY_CORNER_BOTTOM_RIGHT -> prefs[FPS_OVERLAY_CORNER] ?: FPS_OVERLAY_CORNER_TOP_RIGHT
                    else -> FPS_OVERLAY_CORNER_TOP_RIGHT
                },
                compactControls = prefs[COMPACT_CONTROLS] ?: true,
                keepScreenOn = prefs[KEEP_SCREEN_ON] ?: true,
                showRecentGames = prefs[SHOW_RECENT_GAMES] ?: true,
                showHomeSearch = prefs[SHOW_HOME_SEARCH] ?: true,
                preferEnglishGameTitles = prefs[PREFER_ENGLISH_GAME_TITLES] ?: false,
                biosPath = biosPath,
                biosValid = BiosValidator.hasUsableBiosFiles(context, biosPath),
                gamePath = prefs[GAME_PATH],
                coverDownloadBaseUrl = prefs[COVER_DOWNLOAD_BASE_URL],
                coverArtStyle = when (prefs[COVER_ART_STYLE]) {
                    COVER_ART_STYLE_DEFAULT -> COVER_ART_STYLE_DEFAULT
                    COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                    else -> COVER_ART_STYLE_DISABLED
                },
                setupComplete = prefs[ONBOARDING_COMPLETED] ?: false,
                eeCycleRate = prefs[EE_CYCLE_RATE] ?: profileConfig.eeCycleRate,
                eeCycleSkip = prefs[EE_CYCLE_SKIP] ?: profileConfig.eeCycleSkip,
                enableMtvu = prefs[ENABLE_MTVU] ?: true,
                enableFastCdvd = prefs[ENABLE_FAST_CDVD] ?: false,
                enableCheats = prefs[ENABLE_CHEATS] ?: true,
                hwDownloadMode = prefs[HW_DOWNLOAD_MODE] ?: profileConfig.hwDownloadMode,
                frameSkip = prefs[FRAME_SKIP] ?: 0,
                textureFiltering = prefs[TEXTURE_FILTERING] ?: GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
                trilinearFiltering = prefs[TRILINEAR_FILTERING] ?: GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
                blendingAccuracy = prefs[BLENDING_ACCURACY] ?: GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
                texturePreloading = prefs[TEXTURE_PRELOADING] ?: GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
                enableFxaa = prefs[ENABLE_FXAA] ?: false,
                casMode = prefs[CAS_MODE] ?: 0,
                casSharpness = prefs[CAS_SHARPNESS] ?: 50,
                enableWidescreenPatches = prefs[ENABLE_WIDESCREEN_PATCHES] ?: false,
                enableNoInterlacingPatches = prefs[ENABLE_NO_INTERLACING_PATCHES] ?: false,
                anisotropicFiltering = prefs[ANISOTROPIC_FILTERING] ?: 0,
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
                nativeScaling = prefs[NATIVE_SCALING] ?: GsHackDefaults.NATIVE_SCALING_DEFAULT,
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
                overlayOpacity = prefs[OVERLAY_OPACITY] ?: 80,
                overlayShow = prefs[OVERLAY_SHOW] ?: true,
                enableAutoGamepad = prefs[ENABLE_AUTO_GAMEPAD] ?: true,
                hideOverlayOnGamepad = prefs[HIDE_OVERLAY_ON_GAMEPAD] ?: true,
                gamepadBindings = decodeGamepadBindings(prefs[GAMEPAD_BINDINGS]),
                gpuDriverType = prefs[GPU_DRIVER_TYPE] ?: 0,
                customDriverPath = prefs[CUSTOM_DRIVER_PATH],
                frameLimitEnabled = prefs[FRAME_LIMIT_ENABLED] ?: false,
                targetFps = prefs[TARGET_FPS] ?: 0
            )
        }
        .distinctUntilChanged()

    val overlayLayoutSnapshot: Flow<OverlayLayoutSnapshot> = context.dataStore.data
        .map { prefs ->
            OverlayLayoutSnapshot(
                overlayScale = prefs[OVERLAY_SCALE] ?: 100,
                overlayOpacity = prefs[OVERLAY_OPACITY] ?: 80,
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
                stickScale = prefs[STICK_SCALE] ?: 100,
                controlLayouts = decodeControlLayouts(prefs[CONTROL_LAYOUTS])
            )
        }
        .distinctUntilChanged()

    val aspectRatio: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[ASPECT_RATIO] ?: 1
    }

    suspend fun setAspectRatio(value: Int) {
        context.dataStore.edit { it[ASPECT_RATIO] = value }
    }

    val padVibration: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[PAD_VIBRATION] ?: true
    }

    suspend fun setPadVibration(enabled: Boolean) {
        context.dataStore.edit { it[PAD_VIBRATION] = enabled }
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
        prefs[SHOW_HOME_SEARCH] ?: true
    }

    suspend fun setShowHomeSearch(enabled: Boolean) {
        context.dataStore.edit { it[SHOW_HOME_SEARCH] = enabled }
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
        context.dataStore.edit { it[HOME_LIBRARY_VIEW_MODE] = mode.coerceIn(0, 1) }
    }

    suspend fun markGameLaunched(path: String, title: String, serial: String? = null) {
        context.dataStore.edit { prefs ->
            val updated = buildList {
                add(
                    RecentGameEntry(
                        path = path,
                        title = title,
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
                    if (path.isBlank()) continue
                    add(
                        RecentGameEntry(
                            path = path,
                            title = item.optString("title"),
                            lastPlayedAt = item.optLong("lastPlayedAt"),
                            serial = item.optString("serial").takeIf { it.isNotBlank() }
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
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

    private fun decodeGamepadBindings(raw: String?): Map<String, Int> {
        if (raw.isNullOrBlank()) return emptyMap()
        return runCatching {
            val json = JSONObject(raw)
            buildMap {
                json.keys().forEach { key ->
                    val value = json.optInt(key, Int.MIN_VALUE)
                    if (value != Int.MIN_VALUE) put(key, value)
                }
            }
        }.getOrDefault(emptyMap())
    }

    private fun encodeGamepadBindings(bindings: Map<String, Int>): String {
        return JSONObject().apply {
            bindings.toSortedMap().forEach { (actionId, keyCode) ->
                put(actionId, keyCode)
            }
        }.toString()
    }

    // Overlay customization
    val overlayScale: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[OVERLAY_SCALE] ?: 100
    }

    suspend fun setOverlayScale(scale: Int) {
        context.dataStore.edit { it[OVERLAY_SCALE] = scale.coerceIn(50, 150) }
    }

    val overlayOpacity: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[OVERLAY_OPACITY] ?: 80
    }

    suspend fun setOverlayOpacity(opacity: Int) {
        context.dataStore.edit { it[OVERLAY_OPACITY] = opacity.coerceIn(20, 100) }
    }

    val overlayShow: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[OVERLAY_SHOW] ?: true
    }

    suspend fun setOverlayShow(enabled: Boolean) {
        context.dataStore.edit { it[OVERLAY_SHOW] = enabled }
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

    // MTVU (Multi-Threaded VU1)
    val enableMtvu: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_MTVU] ?: true
    }

    suspend fun setEnableMtvu(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_MTVU] = enabled }
    }

    // Fast CDVD
    val enableFastCdvd: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_FAST_CDVD] ?: false
    }

    suspend fun setEnableFastCdvd(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_FAST_CDVD] = enabled }
    }

    val enableCheats: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_CHEATS] ?: true
    }

    suspend fun setEnableCheats(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_CHEATS] = enabled }
    }

    // Hardware Download Mode
    val hwDownloadMode: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[HW_DOWNLOAD_MODE] ?: resolvePerformanceProfileConfig(prefs).hwDownloadMode
    }

    suspend fun setHwDownloadMode(value: Int) {
        context.dataStore.edit { it[HW_DOWNLOAD_MODE] = value.coerceIn(0, 3) }
    }

    // Frame Skip: 0 = off, 1-4
    val frameSkip: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[FRAME_SKIP] ?: 0
    }

    suspend fun setFrameSkip(value: Int) {
        context.dataStore.edit { it[FRAME_SKIP] = value.coerceIn(0, 4) }
    }

    // Texture Filtering: 0 = Nearest, 1 = Bilinear, 2 = Trilinear
    val textureFiltering: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_FILTERING] ?: GsHackDefaults.BILINEAR_FILTERING_DEFAULT
    }

    suspend fun setTextureFiltering(value: Int) {
        context.dataStore.edit { it[TEXTURE_FILTERING] = value.coerceIn(0, 3) }
    }

    val trilinearFiltering: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TRILINEAR_FILTERING] ?: GsHackDefaults.TRILINEAR_FILTERING_DEFAULT
    }

    suspend fun setTrilinearFiltering(value: Int) {
        context.dataStore.edit { it[TRILINEAR_FILTERING] = value.coerceIn(0, 3) }
    }

    val blendingAccuracy: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[BLENDING_ACCURACY] ?: GsHackDefaults.BLENDING_ACCURACY_DEFAULT
    }

    suspend fun setBlendingAccuracy(value: Int) {
        context.dataStore.edit { it[BLENDING_ACCURACY] = value.coerceIn(0, 5) }
    }

    val texturePreloading: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[TEXTURE_PRELOADING] ?: GsHackDefaults.TEXTURE_PRELOADING_DEFAULT
    }

    suspend fun setTexturePreloading(value: Int) {
        context.dataStore.edit { it[TEXTURE_PRELOADING] = value.coerceIn(0, 2) }
    }

    val enableFxaa: Flow<Boolean> = context.dataStore.data.map { prefs ->
        prefs[ENABLE_FXAA] ?: false
    }

    suspend fun setEnableFxaa(enabled: Boolean) {
        context.dataStore.edit { it[ENABLE_FXAA] = enabled }
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

    // Anisotropic Filtering: 0 = off, 2, 4, 8, 16
    val anisotropicFiltering: Flow<Int> = context.dataStore.data.map { prefs ->
        prefs[ANISOTROPIC_FILTERING] ?: 0
    }

    suspend fun setAnisotropicFiltering(value: Int) {
        context.dataStore.edit { it[ANISOTROPIC_FILTERING] = value }
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
        prefs[NATIVE_SCALING] ?: GsHackDefaults.NATIVE_SCALING_DEFAULT
    }

    suspend fun setNativeScaling(value: Int) {
        context.dataStore.edit { it[NATIVE_SCALING] = value.coerceIn(0, 2) }
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

    suspend fun ensureManualHardwareFixesBaseline(version: Int = 1): Boolean {
        var changed = false
        context.dataStore.edit { prefs ->
            if ((prefs[MANUAL_HACKS_BASELINE_VERSION] ?: 0) >= version) {
                return@edit
            }

            resetManualHardwareFixes(prefs)
            prefs[MANUAL_HACKS_BASELINE_VERSION] = version
            changed = true
        }
        return changed
    }

    private fun resetManualHardwareFixes(prefs: MutablePreferences) {
        prefs[CPU_SPRITE_RENDER_SIZE] = GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT
        prefs[CPU_SPRITE_RENDER_LEVEL] = GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT
        prefs[SOFTWARE_CLUT_RENDER] = GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT
        prefs[GPU_TARGET_CLUT_MODE] = GsHackDefaults.GPU_TARGET_CLUT_DEFAULT
        prefs[SKIP_DRAW_START] = 0
        prefs[SKIP_DRAW_END] = 0
        prefs[AUTO_FLUSH_HARDWARE] = GsHackDefaults.AUTO_FLUSH_DEFAULT
        prefs[CPU_FRAMEBUFFER_CONVERSION] = false
        prefs[DISABLE_DEPTH_CONVERSION] = false
        prefs[DISABLE_SAFE_FEATURES] = false
        prefs[DISABLE_RENDER_FIXES] = false
        prefs[PRELOAD_FRAME_DATA] = false
        prefs[DISABLE_PARTIAL_INVALIDATION] = false
        prefs[TEXTURE_INSIDE_RT] = GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT
        prefs[READ_TARGETS_ON_CLOSE] = false
        prefs[ESTIMATE_TEXTURE_REGION] = false
        prefs[GPU_PALETTE_CONVERSION] = false
        prefs[HALF_PIXEL_OFFSET] = GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT
        prefs[NATIVE_SCALING] = GsHackDefaults.NATIVE_SCALING_DEFAULT
        prefs[ROUND_SPRITE] = GsHackDefaults.ROUND_SPRITE_DEFAULT
        prefs[BILINEAR_UPSCALE] = GsHackDefaults.BILINEAR_UPSCALE_DEFAULT
        prefs[TEXTURE_OFFSET_X] = 0
        prefs[TEXTURE_OFFSET_Y] = 0
        prefs[ALIGN_SPRITE] = false
        prefs[MERGE_SPRITE] = false
        prefs[FORCE_EVEN_SPRITE_POSITION] = false
        prefs[NATIVE_PALETTE_DRAW] = false
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

    val gamepadBindings: Flow<Map<String, Int>> = context.dataStore.data.map { prefs ->
        decodeGamepadBindings(prefs[GAMEPAD_BINDINGS])
    }

    suspend fun setGamepadBinding(actionId: String, keyCode: Int) {
        context.dataStore.edit { prefs ->
            val updated = decodeGamepadBindings(prefs[GAMEPAD_BINDINGS]).toMutableMap()
            updated.entries.removeAll { it.value == keyCode }
            updated[actionId] = keyCode
            prefs[GAMEPAD_BINDINGS] = encodeGamepadBindings(updated)
        }
    }

    suspend fun clearGamepadBinding(actionId: String) {
        context.dataStore.edit { prefs ->
            val updated = decodeGamepadBindings(prefs[GAMEPAD_BINDINGS]).toMutableMap()
            updated.remove(actionId)
            if (updated.isEmpty()) prefs.remove(GAMEPAD_BINDINGS) else prefs[GAMEPAD_BINDINGS] = encodeGamepadBindings(updated)
        }
    }

    suspend fun resetGamepadBindings() {
        context.dataStore.edit { prefs ->
            prefs.remove(GAMEPAD_BINDINGS)
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
                            scale = item.optInt("scale", 100).coerceIn(50, 200),
                            visible = item.optBoolean("visible", true)
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
                        put("scale", layout.scale.coerceIn(50, 200))
                        put("visible", layout.visible)
                    }
                )
            }
        }.toString()
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
    
    val stickScale: Flow<Int> = context.dataStore.data.map { it[STICK_SCALE] ?: 100 }

    suspend fun setStickScale(scale: Int) {
        context.dataStore.edit { prefs ->
            prefs[STICK_SCALE] = scale.coerceIn(50, 200)
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
            prefs[STICK_SCALE] = stickScaleVal.coerceIn(50, 200)
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
            prefs[OVERLAY_LAYOUT_VERSION] = CURRENT_OVERLAY_LAYOUT_VERSION
        }
    }

    suspend fun exportJson(): JSONObject {
        val prefs = context.dataStore.data.first()
        return JSONObject().apply {
            put("themeMode", prefs[THEME_MODE] ?: 0)
            put("performanceProfile", resolvePerformanceProfile(prefs))
            put("renderer", normalizeRendererPreference(prefs[RENDERER]))
            put("upscaleMultiplier", readUpscale(prefs).toDouble())
            put("biosPath", prefs[BIOS_PATH])
            put("gamePath", prefs[GAME_PATH])
            put("coverDownloadBaseUrl", prefs[COVER_DOWNLOAD_BASE_URL])
            put("coverArtStyle", prefs[COVER_ART_STYLE] ?: COVER_ART_STYLE_DISABLED)
            put("onboardingCompleted", prefs[ONBOARDING_COMPLETED] ?: false)
            put("languageTag", prefs[LANGUAGE_TAG])
            put("aspectRatio", prefs[ASPECT_RATIO] ?: 1)
            put("padVibration", prefs[PAD_VIBRATION] ?: true)
            put("showFps", prefs[SHOW_FPS] ?: false)
            put("fpsOverlayMode", prefs[FPS_OVERLAY_MODE] ?: FPS_OVERLAY_MODE_DETAILED)
            put("fpsOverlayCorner", prefs[FPS_OVERLAY_CORNER] ?: FPS_OVERLAY_CORNER_TOP_RIGHT)
            put("compactControls", prefs[COMPACT_CONTROLS] ?: true)
            put("keepScreenOn", prefs[KEEP_SCREEN_ON] ?: true)
            put("showRecentGames", prefs[SHOW_RECENT_GAMES] ?: true)
            put("showHomeSearch", prefs[SHOW_HOME_SEARCH] ?: true)
            put("preferEnglishGameTitles", prefs[PREFER_ENGLISH_GAME_TITLES] ?: false)
            put("recentGames", prefs[RECENT_GAMES] ?: "[]")
            put("homeLibraryViewMode", prefs[HOME_LIBRARY_VIEW_MODE] ?: 0)
            put("overlayScale", prefs[OVERLAY_SCALE] ?: 100)
            put("overlayOpacity", prefs[OVERLAY_OPACITY] ?: 80)
            put("overlayShow", prefs[OVERLAY_SHOW] ?: true)
            put("eeCycleRate", prefs[EE_CYCLE_RATE] ?: 0)
            put("eeCycleSkip", prefs[EE_CYCLE_SKIP] ?: 0)
            put("enableMtvu", prefs[ENABLE_MTVU] ?: true)
            put("enableFastCdvd", prefs[ENABLE_FAST_CDVD] ?: false)
            put("hwDownloadMode", prefs[HW_DOWNLOAD_MODE] ?: 0)
            put("frameSkip", prefs[FRAME_SKIP] ?: 0)
            put("textureFiltering", prefs[TEXTURE_FILTERING] ?: GsHackDefaults.BILINEAR_FILTERING_DEFAULT)
            put("trilinearFiltering", prefs[TRILINEAR_FILTERING] ?: GsHackDefaults.TRILINEAR_FILTERING_DEFAULT)
            put("blendingAccuracy", prefs[BLENDING_ACCURACY] ?: GsHackDefaults.BLENDING_ACCURACY_DEFAULT)
            put("texturePreloading", prefs[TEXTURE_PRELOADING] ?: GsHackDefaults.TEXTURE_PRELOADING_DEFAULT)
            put("enableFxaa", prefs[ENABLE_FXAA] ?: false)
            put("casMode", prefs[CAS_MODE] ?: 0)
            put("casSharpness", prefs[CAS_SHARPNESS] ?: 50)
            put("enableWidescreenPatches", prefs[ENABLE_WIDESCREEN_PATCHES] ?: false)
            put("enableNoInterlacingPatches", prefs[ENABLE_NO_INTERLACING_PATCHES] ?: false)
            put("anisotropicFiltering", prefs[ANISOTROPIC_FILTERING] ?: 0)
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
            put("nativeScaling", prefs[NATIVE_SCALING] ?: GsHackDefaults.NATIVE_SCALING_DEFAULT)
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
            put("frameLimitEnabled", prefs[FRAME_LIMIT_ENABLED] ?: false)
            put("targetFps", prefs[TARGET_FPS] ?: 0)
            put("overlayLayoutVersion", prefs[OVERLAY_LAYOUT_VERSION] ?: 0)
            put("dpadOffset", prefs[DPAD_OFFSET])
            put("lstickOffset", prefs[LSTICK_OFFSET])
            put("rstickOffset", prefs[RSTICK_OFFSET])
            put("actionOffset", prefs[ACTION_OFFSET])
            put("lbtnOffset", prefs[LBTN_OFFSET])
            put("rbtnOffset", prefs[RBTN_OFFSET])
            put("centerOffset", prefs[CENTER_OFFSET])
            put("stickScale", prefs[STICK_SCALE] ?: 100)
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
            prefs[PERFORMANCE_PROFILE] = PerformanceProfiles.normalize(
                json.optInt("performanceProfile", PerformanceProfiles.SAFE)
            )
            prefs[RENDERER] = normalizeRendererPreference(json.optInt("renderer", EmulatorBridge.VULKAN_RENDERER))
            prefs[UPSCALE] = json.readUpscaleMultiplier()
            json.optString("biosPath").takeIf { it.isNotBlank() }?.let { prefs[BIOS_PATH] = it } ?: prefs.remove(BIOS_PATH)
            json.optString("gamePath").takeIf { it.isNotBlank() }?.let { prefs[GAME_PATH] = it } ?: prefs.remove(GAME_PATH)
            json.optString("coverDownloadBaseUrl").takeIf { it.isNotBlank() }?.let {
                prefs[COVER_DOWNLOAD_BASE_URL] = it.trim().trimEnd('/')
            } ?: prefs.remove(COVER_DOWNLOAD_BASE_URL)
            prefs[COVER_ART_STYLE] = when (json.optInt("coverArtStyle", COVER_ART_STYLE_DISABLED)) {
                COVER_ART_STYLE_DEFAULT -> COVER_ART_STYLE_DEFAULT
                COVER_ART_STYLE_3D -> COVER_ART_STYLE_3D
                else -> COVER_ART_STYLE_DISABLED
            }
            prefs[ONBOARDING_COMPLETED] = json.optBoolean("onboardingCompleted", false)
            languageTag?.let { prefs[LANGUAGE_TAG] = it } ?: prefs.remove(LANGUAGE_TAG)
            prefs[ASPECT_RATIO] = json.optInt("aspectRatio", 1)
            prefs[PAD_VIBRATION] = json.optBoolean("padVibration", true)
            prefs[SHOW_FPS] = json.optBoolean("showFps", false)
            prefs[FPS_OVERLAY_MODE] = json.optInt("fpsOverlayMode", FPS_OVERLAY_MODE_DETAILED)
            prefs[FPS_OVERLAY_CORNER] = json.optInt("fpsOverlayCorner", FPS_OVERLAY_CORNER_TOP_RIGHT).coerceIn(
                FPS_OVERLAY_CORNER_TOP_LEFT,
                FPS_OVERLAY_CORNER_BOTTOM_RIGHT
            )
            prefs[COMPACT_CONTROLS] = json.optBoolean("compactControls", true)
            prefs[KEEP_SCREEN_ON] = json.optBoolean("keepScreenOn", true)
            prefs[SHOW_RECENT_GAMES] = json.optBoolean("showRecentGames", true)
            prefs[SHOW_HOME_SEARCH] = json.optBoolean("showHomeSearch", true)
            prefs[PREFER_ENGLISH_GAME_TITLES] = json.optBoolean("preferEnglishGameTitles", false)
            prefs[RECENT_GAMES] = json.optString("recentGames", "[]")
            prefs[HOME_LIBRARY_VIEW_MODE] = json.optInt("homeLibraryViewMode", 0).coerceIn(0, 1)
            prefs[OVERLAY_SCALE] = json.optInt("overlayScale", 100)
            prefs[OVERLAY_OPACITY] = json.optInt("overlayOpacity", 80)
            prefs[OVERLAY_SHOW] = json.optBoolean("overlayShow", true)
            prefs[EE_CYCLE_RATE] = json.optInt("eeCycleRate", 0)
            prefs[EE_CYCLE_SKIP] = json.optInt("eeCycleSkip", 0)
            prefs[ENABLE_MTVU] = json.optBoolean("enableMtvu", true)
            prefs[ENABLE_FAST_CDVD] = json.optBoolean("enableFastCdvd", false)
            prefs[HW_DOWNLOAD_MODE] = json.optInt("hwDownloadMode", 0).coerceIn(0, 3)
            prefs[FRAME_SKIP] = json.optInt("frameSkip", 0)
            prefs[TEXTURE_FILTERING] = json.optInt("textureFiltering", GsHackDefaults.BILINEAR_FILTERING_DEFAULT).coerceIn(0, 3)
            prefs[TRILINEAR_FILTERING] = json.optInt("trilinearFiltering", GsHackDefaults.TRILINEAR_FILTERING_DEFAULT).coerceIn(0, 3)
            prefs[BLENDING_ACCURACY] = json.optInt("blendingAccuracy", GsHackDefaults.BLENDING_ACCURACY_DEFAULT).coerceIn(0, 5)
            prefs[TEXTURE_PRELOADING] = json.optInt("texturePreloading", GsHackDefaults.TEXTURE_PRELOADING_DEFAULT).coerceIn(0, 2)
            prefs[ENABLE_FXAA] = json.optBoolean("enableFxaa", false)
            prefs[CAS_MODE] = json.optInt("casMode", 0).coerceIn(0, 2)
            prefs[CAS_SHARPNESS] = json.optInt("casSharpness", 50).coerceIn(0, 100)
            prefs[ENABLE_WIDESCREEN_PATCHES] = json.optBoolean("enableWidescreenPatches", false)
            prefs[ENABLE_NO_INTERLACING_PATCHES] = json.optBoolean("enableNoInterlacingPatches", false)
            prefs[ANISOTROPIC_FILTERING] = json.optInt("anisotropicFiltering", 0)
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
            prefs[NATIVE_SCALING] = json.optInt("nativeScaling", GsHackDefaults.NATIVE_SCALING_DEFAULT).coerceIn(0, 2)
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
            prefs[FRAME_LIMIT_ENABLED] = json.optBoolean("frameLimitEnabled", false)
            prefs[TARGET_FPS] = json.optInt("targetFps", 0).let { if (it <= 0) 0 else it.coerceIn(20, 120) }
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
            prefs[STICK_SCALE] = json.optInt("stickScale", 100)
            json.optString("controlLayouts").takeIf { it.isNotBlank() }?.let { prefs[CONTROL_LAYOUTS] = it } ?: prefs.remove(CONTROL_LAYOUTS)
            json.optString("memoryCardSlot1").takeIf { it.isNotBlank() }?.let { prefs[MEMORY_CARD_SLOT1] = it } ?: prefs.remove(MEMORY_CARD_SLOT1)
            json.optString("memoryCardSlot2").takeIf { it.isNotBlank() }?.let { prefs[MEMORY_CARD_SLOT2] = it } ?: prefs.remove(MEMORY_CARD_SLOT2)
        }
    }

    private fun readUpscale(prefs: Preferences): Float {
        return (prefs[UPSCALE]
            ?: prefs[UPSCALE_LEGACY]?.toFloat()
            ?: 1f).let(::normalizeUpscale)
    }

    private fun JSONObject.readUpscaleMultiplier(): Float {
        val doubleValue = optDouble("upscaleMultiplier", Double.NaN)
        return when {
            !doubleValue.isNaN() -> doubleValue.toFloat()
            has("upscaleMultiplier") -> optInt("upscaleMultiplier", 1).toFloat()
            else -> 1f
        }.let(::normalizeUpscale)
    }
}

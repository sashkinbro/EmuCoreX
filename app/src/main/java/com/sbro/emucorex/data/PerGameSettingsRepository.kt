package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.RendererDefaults
import com.sbro.emucorex.core.normalizeUpscale
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class PerGameSettings(
    val gameKey: String,
    val gameTitle: String,
    val gameSerial: String? = null,
    val renderer: Int = EmulatorBridge.AUTO_RENDERER,
    val upscaleMultiplier: Float = 1f,
    val aspectRatio: Int = 1,
    val showFps: Boolean = false,
    val fpsOverlayMode: Int = AppPreferences.FPS_OVERLAY_MODE_DETAILED,
    val racingMode: Boolean = false,
    val touchHaptics: Boolean = false,
    val touchHapticsPreset: Int = AppPreferences.DEFAULT_TOUCH_HAPTICS_PRESET,
    val gyroMode: Int = AppPreferences.GYRO_MODE_OFF,
    val gyroSensitivity: Int = AppPreferences.DEFAULT_GYRO_SENSITIVITY,
    val gyroSmoothing: Int = AppPreferences.DEFAULT_GYRO_SMOOTHING,
    val gyroInvertX: Boolean = false,
    val gyroInvertY: Boolean = false,
    val gamepadRightStickUpToR2: Boolean = false,
    val gamepadRightStickDownToL2: Boolean = false,
    val gamepadButtonHaptics: Boolean = false,
    val pressureModifierAmount: Int = AppPreferences.DEFAULT_PRESSURE_MODIFIER_AMOUNT,
    val autoSaveOnExit: Boolean = false,
    val autoLoadOnStart: Boolean = false,
    val enableFastBoot: Boolean = true,
    val enableInstantVu1: Boolean = true,
    val enableMtvu: Boolean = true,
    val enableThreadPinning: Boolean = AppPreferences.DEFAULT_THREAD_PINNING,
    val enableFastCdvd: Boolean = false,
    val enableCheats: Boolean = false,
    val enableGameFixes: Boolean = true,
    val enableEeTimingHack: Boolean = false,
    val eeFpuRoundMode: Int = AppPreferences.DEFAULT_EE_FPU_ROUND_MODE,
    val vu0RoundMode: Int = AppPreferences.DEFAULT_VU_ROUND_MODE,
    val vu1RoundMode: Int = AppPreferences.DEFAULT_VU_ROUND_MODE,
    val eeFpuClampingMode: Int = AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE,
    val vu0ClampingMode: Int = AppPreferences.DEFAULT_VU0_CLAMPING_MODE,
    val vu1ClampingMode: Int = AppPreferences.DEFAULT_VU1_CLAMPING_MODE,
    val hwDownloadMode: Int = GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT,
    val eeCycleRate: Int = 0,
    val eeCycleSkip: Int = 0,
    val frameSkip: Int = 0,
    val skipDuplicateFrames: Boolean = true,
    val frameLimitEnabled: Boolean = true,
    val targetFps: Int = 0,
    val ntscFramerate: Float = AppPreferences.DEFAULT_NTSC_FRAMERATE,
    val palFramerate: Float = AppPreferences.DEFAULT_PAL_FRAMERATE,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val casMode: Int = 0,
    val sgsrMode: Int = 0,
    val casSharpness: Int = 50,
    val tvShader: Int = GsHackDefaults.TV_SHADER_DEFAULT,
    val shadeBoostEnabled: Boolean = false,
    val shadeBoostBrightness: Int = 50,
    val shadeBoostContrast: Int = 50,
    val shadeBoostSaturation: Int = 50,
    val shadeBoostGamma: Int = 50,
    val anisotropicFiltering: Int = 0,
    val enableHwMipmapping: Boolean = GsHackDefaults.HW_MIPMAPPING_DEFAULT,
    val antiBlur: Boolean = GsHackDefaults.ANTI_BLUR_DEFAULT,
    val deinterlaceMode: Int = GsHackDefaults.DEINTERLACE_MODE_DEFAULT,
    val dithering: Int = GsHackDefaults.DITHERING_DEFAULT,
    val enableWidescreenPatches: Boolean = false,
    val enableNoInterlacingPatches: Boolean = false,
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
    val touchControlVisualStyle: TouchControlVisualStyle? = null,
    val touchControlPressEffect: TouchControlPressEffect? = null,
    val touchControlsLayout: TouchControlsLayoutProfile? = null,
    val providedKeys: Set<String>? = null,
    val updatedAt: Long = System.currentTimeMillis()
)

data class TouchControlsLayoutProfile(
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

class PerGameSettingsRepository(context: Context) {
    private val file = File(EmulatorStorage.appStateDir(context), "per-game-settings.json")

    fun get(gameKey: String): PerGameSettings? = loadAll().firstOrNull { it.gameKey == gameKey }

    fun getAll(): List<PerGameSettings> = loadAll()

    fun save(settings: PerGameSettings) {
        val items = loadAll()
            .filterNot { it.gameKey == settings.gameKey } +
            settings.copy(updatedAt = System.currentTimeMillis())
        writeAll(items.sortedBy { it.gameTitle.lowercase() })
    }

    fun delete(gameKey: String) {
        writeAll(loadAll().filterNot { it.gameKey == gameKey })
    }

    fun deleteAll() {
        writeAll(emptyList())
    }

    fun exportJson(): JSONObject {
        return JSONObject().put(
            "profiles",
            JSONArray().apply {
                loadAll().forEach { put(it.toJson()) }
            }
        )
    }

    fun importJson(json: JSONObject) {
        val profiles = json.optJSONArray("profiles") ?: JSONArray()
        val items = buildList {
            for (index in 0 until profiles.length()) {
                val item = profiles.optJSONObject(index) ?: continue
                add(item.toPerGameSettings())
            }
        }
        writeAll(items.sortedBy { it.gameTitle.lowercase() })
    }

    private fun loadAll(): List<PerGameSettings> {
        if (!file.exists()) return emptyList()
        return runCatching {
            val root = JSONObject(file.readText())
            val profiles = root.optJSONArray("profiles") ?: JSONArray()
            var hadLegacyClampingKeys = false
            val items = buildList {
                for (index in 0 until profiles.length()) {
                    val item = profiles.optJSONObject(index) ?: continue
                    hadLegacyClampingKeys = hadLegacyClampingKeys || item.hasLegacyClampingKeys()
                    add(item.toPerGameSettings())
                }
            }
            if (hadLegacyClampingKeys) {
                writeAll(items.sortedBy { it.gameTitle.lowercase() })
            }
            items
        }.getOrDefault(emptyList())
    }

    private fun writeAll(items: List<PerGameSettings>) {
        file.parentFile?.mkdirs()
        file.writeText(
            JSONObject().put(
                "profiles",
                JSONArray().apply {
                    items.forEach { put(it.toJson()) }
                }
            ).toString(2)
        )
    }
}

private val legacyClampingProfileKeys = setOf(
    "enableEeClamping",
    "enableVu0Clamping",
    "enableVu1Clamping"
)

private fun JSONObject.hasLegacyClampingKeys(): Boolean {
    return legacyClampingProfileKeys.any(::has)
}

private fun JSONObject.toPerGameSettings(): PerGameSettings {
    val providedKeys = keys().asSequence().toSet() - legacyClampingProfileKeys
    return PerGameSettings(
        gameKey = optString("gameKey"),
        gameTitle = optString("gameTitle"),
        gameSerial = optString("gameSerial").takeIf { it.isNotBlank() },
        renderer = optInt("renderer", RendererDefaults.AUTO).let(::sanitizeRendererValue),
        upscaleMultiplier = readUpscaleMultiplier(),
        aspectRatio = optInt("aspectRatio", 1).let(::sanitizeAspectRatioValue),
        showFps = optBoolean("showFps", false),
        fpsOverlayMode = optInt("fpsOverlayMode", AppPreferences.FPS_OVERLAY_MODE_DETAILED),
        racingMode = optBoolean("racingMode", false),
        touchHaptics = optBoolean("touchHaptics", false),
        touchHapticsPreset = optInt("touchHapticsPreset", AppPreferences.DEFAULT_TOUCH_HAPTICS_PRESET)
            .coerceIn(AppPreferences.TOUCH_HAPTICS_PRESET_SOFT, AppPreferences.TOUCH_HAPTICS_PRESET_STRONG),
        gyroMode = optInt("gyroMode", AppPreferences.GYRO_MODE_OFF).coerceIn(AppPreferences.GYRO_MODE_OFF, AppPreferences.GYRO_MODE_STEERING),
        gyroSensitivity = optInt("gyroSensitivity", AppPreferences.DEFAULT_GYRO_SENSITIVITY).coerceIn(25, 300),
        gyroSmoothing = optInt("gyroSmoothing", AppPreferences.DEFAULT_GYRO_SMOOTHING).coerceIn(0, 90),
        gyroInvertX = optBoolean("gyroInvertX", false),
        gyroInvertY = optBoolean("gyroInvertY", false),
        gamepadRightStickUpToR2 = optBoolean("gamepadRightStickUpToR2", false),
        gamepadRightStickDownToL2 = optBoolean("gamepadRightStickDownToL2", false),
        gamepadButtonHaptics = optBoolean("gamepadButtonHaptics", false),
        pressureModifierAmount = optInt("pressureModifierAmount", AppPreferences.DEFAULT_PRESSURE_MODIFIER_AMOUNT).coerceIn(1, 100),
        autoSaveOnExit = optBoolean("autoSaveOnExit", false),
        autoLoadOnStart = optBoolean("autoLoadOnStart", false),
        enableFastBoot = optBoolean("enableFastBoot", true),
        enableInstantVu1 = optBoolean("enableInstantVu1", true),
        enableMtvu = optBoolean("enableMtvu", true),
        enableThreadPinning = optBoolean("enableThreadPinning", AppPreferences.DEFAULT_THREAD_PINNING),
        enableFastCdvd = optBoolean("enableFastCdvd", false),
        enableCheats = optBoolean("enableCheats", false),
        enableGameFixes = optBoolean("enableGameFixes", true),
        enableEeTimingHack = optBoolean("enableEeTimingHack", false),
        eeFpuRoundMode = sanitizeFloatRoundMode(optInt("eeFpuRoundMode", AppPreferences.DEFAULT_EE_FPU_ROUND_MODE), AppPreferences.DEFAULT_EE_FPU_ROUND_MODE),
        vu0RoundMode = sanitizeFloatRoundMode(optInt("vu0RoundMode", AppPreferences.DEFAULT_VU_ROUND_MODE), AppPreferences.DEFAULT_VU_ROUND_MODE),
        vu1RoundMode = sanitizeFloatRoundMode(optInt("vu1RoundMode", AppPreferences.DEFAULT_VU_ROUND_MODE), AppPreferences.DEFAULT_VU_ROUND_MODE),
        eeFpuClampingMode = sanitizeClampingMode(optInt("eeFpuClampingMode", AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE), AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE),
        vu0ClampingMode = sanitizeClampingMode(optInt("vu0ClampingMode", AppPreferences.DEFAULT_VU0_CLAMPING_MODE), AppPreferences.DEFAULT_VU0_CLAMPING_MODE),
        vu1ClampingMode = sanitizeClampingMode(optInt("vu1ClampingMode", AppPreferences.DEFAULT_VU1_CLAMPING_MODE), AppPreferences.DEFAULT_VU1_CLAMPING_MODE),
        hwDownloadMode = GsHackDefaults.coerceHardwareDownloadMode(
            optInt("hwDownloadMode", GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT)
        ),
        eeCycleRate = optInt("eeCycleRate", 0),
        eeCycleSkip = optInt("eeCycleSkip", 0),
        frameSkip = GsHackDefaults.coerceFrameSkip(
            optInt("frameSkip", GsHackDefaults.FRAME_SKIP_DEFAULT)
        ),
        skipDuplicateFrames = optBoolean("skipDuplicateFrames", true),
        frameLimitEnabled = optBoolean("frameLimitEnabled", true),
        targetFps = optInt("targetFps", 0).let { if (it <= 0) 0 else it.coerceIn(20, 120) },
        ntscFramerate = sanitizeRegionFramerate(
            optDouble("ntscFramerate", AppPreferences.DEFAULT_NTSC_FRAMERATE.toDouble()).toFloat(),
            AppPreferences.DEFAULT_NTSC_FRAMERATE
        ),
        palFramerate = sanitizeRegionFramerate(
            optDouble("palFramerate", AppPreferences.DEFAULT_PAL_FRAMERATE.toDouble()).toFloat(),
            AppPreferences.DEFAULT_PAL_FRAMERATE
        ),
        textureFiltering = GsHackDefaults.coerceBilinearFiltering(
            optInt("textureFiltering", GsHackDefaults.BILINEAR_FILTERING_DEFAULT)
        ),
        trilinearFiltering = readTrilinearFiltering(),
        blendingAccuracy = GsHackDefaults.coerceBlendingAccuracy(
            optInt("blendingAccuracy", GsHackDefaults.BLENDING_ACCURACY_DEFAULT)
        ),
        texturePreloading = GsHackDefaults.coerceTexturePreloading(
            optInt("texturePreloading", GsHackDefaults.TEXTURE_PRELOADING_DEFAULT)
        ),
        enableFxaa = optBoolean("enableFxaa", false),
        casMode = optInt("casMode", 0),
        sgsrMode = optInt("sgsrMode", 0).coerceIn(0, 3),
        casSharpness = optInt("casSharpness", 50),
        tvShader = optInt("tvShader", GsHackDefaults.TV_SHADER_DEFAULT).let(GsHackDefaults::coerceTvShader),
        shadeBoostEnabled = optBoolean("shadeBoostEnabled", false) || isShadeBoostActive(
            brightness = optInt("shadeBoostBrightness", 50).coerceIn(1, 100),
            contrast = optInt("shadeBoostContrast", 50).coerceIn(1, 100),
            saturation = optInt("shadeBoostSaturation", 50).coerceIn(1, 100),
            gamma = optInt("shadeBoostGamma", 50).coerceIn(1, 100)
        ),
        shadeBoostBrightness = optInt("shadeBoostBrightness", 50).coerceIn(1, 100),
        shadeBoostContrast = optInt("shadeBoostContrast", 50).coerceIn(1, 100),
        shadeBoostSaturation = optInt("shadeBoostSaturation", 50).coerceIn(1, 100),
        shadeBoostGamma = optInt("shadeBoostGamma", 50).coerceIn(1, 100),
        anisotropicFiltering = GsHackDefaults.coerceAnisotropicFiltering(
            optInt("anisotropicFiltering", GsHackDefaults.ANISOTROPIC_FILTERING_DEFAULT)
        ),
        enableHwMipmapping = optBoolean("enableHwMipmapping", GsHackDefaults.HW_MIPMAPPING_DEFAULT),
        antiBlur = optBoolean("antiBlur", GsHackDefaults.ANTI_BLUR_DEFAULT),
        deinterlaceMode = GsHackDefaults.coerceDeinterlaceMode(
            optInt("deinterlaceMode", GsHackDefaults.DEINTERLACE_MODE_DEFAULT)
        ),
        dithering = GsHackDefaults.coerceDithering(
            optInt("dithering", GsHackDefaults.DITHERING_DEFAULT)
        ),
        enableWidescreenPatches = optBoolean("enableWidescreenPatches", false),
        enableNoInterlacingPatches = optBoolean("enableNoInterlacingPatches", false),
        cpuSpriteRenderSize = optInt("cpuSpriteRenderSize", GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT),
        cpuSpriteRenderLevel = optInt("cpuSpriteRenderLevel", GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT),
        softwareClutRender = optInt("softwareClutRender", GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT),
        gpuTargetClutMode = optInt("gpuTargetClutMode", GsHackDefaults.GPU_TARGET_CLUT_DEFAULT),
        skipDrawStart = optInt("skipDrawStart", 0),
        skipDrawEnd = optInt("skipDrawEnd", 0),
        autoFlushHardware = optInt("autoFlushHardware", GsHackDefaults.AUTO_FLUSH_DEFAULT),
        cpuFramebufferConversion = optBoolean("cpuFramebufferConversion", false),
        disableDepthConversion = optBoolean("disableDepthConversion", false),
        disableSafeFeatures = optBoolean("disableSafeFeatures", false),
        disableRenderFixes = optBoolean("disableRenderFixes", false),
        preloadFrameData = optBoolean("preloadFrameData", false),
        disablePartialInvalidation = optBoolean("disablePartialInvalidation", false),
        textureInsideRt = optInt("textureInsideRt", GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT),
        readTargetsOnClose = optBoolean("readTargetsOnClose", false),
        estimateTextureRegion = optBoolean("estimateTextureRegion", false),
        gpuPaletteConversion = optBoolean("gpuPaletteConversion", false),
        halfPixelOffset = optInt("halfPixelOffset", GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT),
        nativeScaling = GsHackDefaults.coerceNativeScaling(
            optInt("nativeScaling", GsHackDefaults.NATIVE_SCALING_DEFAULT)
        ),
        roundSprite = optInt("roundSprite", GsHackDefaults.ROUND_SPRITE_DEFAULT),
        bilinearUpscale = optInt("bilinearUpscale", GsHackDefaults.BILINEAR_UPSCALE_DEFAULT),
        textureOffsetX = optInt("textureOffsetX", 0),
        textureOffsetY = optInt("textureOffsetY", 0),
        alignSprite = optBoolean("alignSprite", false),
        mergeSprite = optBoolean("mergeSprite", false),
        forceEvenSpritePosition = optBoolean("forceEvenSpritePosition", false),
        nativePaletteDraw = optBoolean("nativePaletteDraw", false),
        touchControlVisualStyle = if (has("touchControlVisualStyle")) {
            TouchControlVisualStyle.fromPreference(optInt("touchControlVisualStyle"))
        } else {
            null
        },
        touchControlPressEffect = if (has("touchControlPressEffect")) {
            TouchControlPressEffect.fromPreference(optInt("touchControlPressEffect"))
        } else {
            null
        },
        touchControlsLayout = optJSONObject("touchControlsLayout")?.toTouchControlsLayoutProfile(),
        providedKeys = providedKeys,
        updatedAt = optLong("updatedAt", System.currentTimeMillis())
    )
}

private fun PerGameSettings.toJson(): JSONObject {
    return JSONObject().apply {
        put("gameKey", gameKey)
        put("gameTitle", gameTitle)
        put("gameSerial", gameSerial)
        val keys = providedKeys
        fun shouldWrite(key: String): Boolean = keys == null || key in keys
        if (shouldWrite("renderer")) put("renderer", sanitizeRendererValue(renderer))
        if (shouldWrite("upscaleMultiplier")) put("upscaleMultiplier", upscaleMultiplier.toDouble())
        if (shouldWrite("aspectRatio")) put("aspectRatio", sanitizeAspectRatioValue(aspectRatio))
        if (shouldWrite("showFps")) put("showFps", showFps)
        if (shouldWrite("fpsOverlayMode")) put("fpsOverlayMode", fpsOverlayMode)
        if (shouldWrite("racingMode")) put("racingMode", racingMode)
        if (shouldWrite("touchHaptics")) put("touchHaptics", touchHaptics)
        if (shouldWrite("touchHapticsPreset")) put("touchHapticsPreset", touchHapticsPreset)
        if (shouldWrite("gyroMode")) put("gyroMode", gyroMode)
        if (shouldWrite("gyroSensitivity")) put("gyroSensitivity", gyroSensitivity)
        if (shouldWrite("gyroSmoothing")) put("gyroSmoothing", gyroSmoothing)
        if (shouldWrite("gyroInvertX")) put("gyroInvertX", gyroInvertX)
        if (shouldWrite("gyroInvertY")) put("gyroInvertY", gyroInvertY)
        if (shouldWrite("gamepadRightStickUpToR2")) put("gamepadRightStickUpToR2", gamepadRightStickUpToR2)
        if (shouldWrite("gamepadRightStickDownToL2")) put("gamepadRightStickDownToL2", gamepadRightStickDownToL2)
        if (shouldWrite("gamepadButtonHaptics")) put("gamepadButtonHaptics", gamepadButtonHaptics)
        if (shouldWrite("pressureModifierAmount")) put("pressureModifierAmount", pressureModifierAmount.coerceIn(1, 100))
        if (shouldWrite("autoSaveOnExit")) put("autoSaveOnExit", autoSaveOnExit)
        if (shouldWrite("autoLoadOnStart")) put("autoLoadOnStart", autoLoadOnStart)
        if (shouldWrite("enableFastBoot")) put("enableFastBoot", enableFastBoot)
        if (shouldWrite("enableInstantVu1")) put("enableInstantVu1", enableInstantVu1)
        if (shouldWrite("enableMtvu")) put("enableMtvu", enableMtvu)
        if (shouldWrite("enableThreadPinning")) put("enableThreadPinning", enableThreadPinning)
        if (shouldWrite("enableFastCdvd")) put("enableFastCdvd", enableFastCdvd)
        if (shouldWrite("enableCheats")) put("enableCheats", enableCheats)
        if (shouldWrite("enableGameFixes")) put("enableGameFixes", enableGameFixes)
        if (shouldWrite("enableEeTimingHack")) put("enableEeTimingHack", enableEeTimingHack)
        if (shouldWrite("eeFpuRoundMode")) put("eeFpuRoundMode", sanitizeFloatRoundMode(eeFpuRoundMode, AppPreferences.DEFAULT_EE_FPU_ROUND_MODE))
        if (shouldWrite("vu0RoundMode")) put("vu0RoundMode", sanitizeFloatRoundMode(vu0RoundMode, AppPreferences.DEFAULT_VU_ROUND_MODE))
        if (shouldWrite("vu1RoundMode")) put("vu1RoundMode", sanitizeFloatRoundMode(vu1RoundMode, AppPreferences.DEFAULT_VU_ROUND_MODE))
        if (shouldWrite("eeFpuClampingMode")) put("eeFpuClampingMode", sanitizeClampingMode(eeFpuClampingMode, AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE))
        if (shouldWrite("vu0ClampingMode")) put("vu0ClampingMode", sanitizeClampingMode(vu0ClampingMode, AppPreferences.DEFAULT_VU0_CLAMPING_MODE))
        if (shouldWrite("vu1ClampingMode")) put("vu1ClampingMode", sanitizeClampingMode(vu1ClampingMode, AppPreferences.DEFAULT_VU1_CLAMPING_MODE))
        if (shouldWrite("hwDownloadMode")) put("hwDownloadMode", GsHackDefaults.coerceHardwareDownloadMode(hwDownloadMode))
        if (shouldWrite("eeCycleRate")) put("eeCycleRate", eeCycleRate)
        if (shouldWrite("eeCycleSkip")) put("eeCycleSkip", eeCycleSkip)
        if (shouldWrite("frameSkip")) put("frameSkip", GsHackDefaults.coerceFrameSkip(frameSkip))
        if (shouldWrite("skipDuplicateFrames")) put("skipDuplicateFrames", skipDuplicateFrames)
        if (shouldWrite("frameLimitEnabled")) put("frameLimitEnabled", frameLimitEnabled)
        if (shouldWrite("targetFps")) put("targetFps", targetFps)
        if (shouldWrite("ntscFramerate")) put("ntscFramerate", ntscFramerate.toDouble())
        if (shouldWrite("palFramerate")) put("palFramerate", palFramerate.toDouble())
        if (shouldWrite("textureFiltering")) put("textureFiltering", GsHackDefaults.coerceBilinearFiltering(textureFiltering))
        if (shouldWrite("trilinearFiltering")) put("trilinearFiltering", GsHackDefaults.coerceTrilinearFiltering(trilinearFiltering))
        if (shouldWrite("blendingAccuracy")) put("blendingAccuracy", GsHackDefaults.coerceBlendingAccuracy(blendingAccuracy))
        if (shouldWrite("texturePreloading")) put("texturePreloading", GsHackDefaults.coerceTexturePreloading(texturePreloading))
        if (shouldWrite("enableFxaa")) put("enableFxaa", enableFxaa)
        if (shouldWrite("casMode")) put("casMode", casMode)
        if (shouldWrite("sgsrMode")) put("sgsrMode", sgsrMode.coerceIn(0, 3))
        if (shouldWrite("casSharpness")) put("casSharpness", casSharpness)
        if (shouldWrite("tvShader")) put("tvShader", GsHackDefaults.coerceTvShader(tvShader))
        if (shouldWrite("shadeBoostEnabled")) put("shadeBoostEnabled", shadeBoostEnabled)
        if (shouldWrite("shadeBoostBrightness")) put("shadeBoostBrightness", shadeBoostBrightness)
        if (shouldWrite("shadeBoostContrast")) put("shadeBoostContrast", shadeBoostContrast)
        if (shouldWrite("shadeBoostSaturation")) put("shadeBoostSaturation", shadeBoostSaturation)
        if (shouldWrite("shadeBoostGamma")) put("shadeBoostGamma", shadeBoostGamma)
        if (shouldWrite("anisotropicFiltering")) put("anisotropicFiltering", GsHackDefaults.coerceAnisotropicFiltering(anisotropicFiltering))
        if (shouldWrite("enableHwMipmapping")) put("enableHwMipmapping", enableHwMipmapping)
        if (shouldWrite("antiBlur")) put("antiBlur", antiBlur)
        if (shouldWrite("deinterlaceMode")) put("deinterlaceMode", GsHackDefaults.coerceDeinterlaceMode(deinterlaceMode))
        if (shouldWrite("dithering")) put("dithering", GsHackDefaults.coerceDithering(dithering))
        if (shouldWrite("enableWidescreenPatches")) put("enableWidescreenPatches", enableWidescreenPatches)
        if (shouldWrite("enableNoInterlacingPatches")) put("enableNoInterlacingPatches", enableNoInterlacingPatches)
        if (shouldWrite("cpuSpriteRenderSize")) put("cpuSpriteRenderSize", cpuSpriteRenderSize)
        if (shouldWrite("cpuSpriteRenderLevel")) put("cpuSpriteRenderLevel", cpuSpriteRenderLevel)
        if (shouldWrite("softwareClutRender")) put("softwareClutRender", softwareClutRender)
        if (shouldWrite("gpuTargetClutMode")) put("gpuTargetClutMode", gpuTargetClutMode)
        if (shouldWrite("skipDrawStart")) put("skipDrawStart", skipDrawStart)
        if (shouldWrite("skipDrawEnd")) put("skipDrawEnd", skipDrawEnd)
        if (shouldWrite("autoFlushHardware")) put("autoFlushHardware", autoFlushHardware)
        if (shouldWrite("cpuFramebufferConversion")) put("cpuFramebufferConversion", cpuFramebufferConversion)
        if (shouldWrite("disableDepthConversion")) put("disableDepthConversion", disableDepthConversion)
        if (shouldWrite("disableSafeFeatures")) put("disableSafeFeatures", disableSafeFeatures)
        if (shouldWrite("disableRenderFixes")) put("disableRenderFixes", disableRenderFixes)
        if (shouldWrite("preloadFrameData")) put("preloadFrameData", preloadFrameData)
        if (shouldWrite("disablePartialInvalidation")) put("disablePartialInvalidation", disablePartialInvalidation)
        if (shouldWrite("textureInsideRt")) put("textureInsideRt", textureInsideRt)
        if (shouldWrite("readTargetsOnClose")) put("readTargetsOnClose", readTargetsOnClose)
        if (shouldWrite("estimateTextureRegion")) put("estimateTextureRegion", estimateTextureRegion)
        if (shouldWrite("gpuPaletteConversion")) put("gpuPaletteConversion", gpuPaletteConversion)
        if (shouldWrite("halfPixelOffset")) put("halfPixelOffset", halfPixelOffset)
        if (shouldWrite("nativeScaling")) put("nativeScaling", GsHackDefaults.coerceNativeScaling(nativeScaling))
        if (shouldWrite("roundSprite")) put("roundSprite", roundSprite)
        if (shouldWrite("bilinearUpscale")) put("bilinearUpscale", bilinearUpscale)
        if (shouldWrite("textureOffsetX")) put("textureOffsetX", textureOffsetX)
        if (shouldWrite("textureOffsetY")) put("textureOffsetY", textureOffsetY)
        if (shouldWrite("alignSprite")) put("alignSprite", alignSprite)
        if (shouldWrite("mergeSprite")) put("mergeSprite", mergeSprite)
        if (shouldWrite("forceEvenSpritePosition")) put("forceEvenSpritePosition", forceEvenSpritePosition)
        if (shouldWrite("nativePaletteDraw")) put("nativePaletteDraw", nativePaletteDraw)
        if (shouldWrite("touchControlVisualStyle")) {
            touchControlVisualStyle?.let { put("touchControlVisualStyle", it.preferenceValue) }
        }
        if (shouldWrite("touchControlPressEffect")) {
            touchControlPressEffect?.let { put("touchControlPressEffect", it.preferenceValue) }
        }
        if (shouldWrite("touchControlsLayout")) touchControlsLayout?.let { put("touchControlsLayout", it.toJson()) }
        put("updatedAt", updatedAt)
    }
}

private fun TouchControlsLayoutProfile.toJson(): JSONObject {
    return JSONObject().apply {
        put("dpadOffset", dpadOffset.toJson())
        put("lstickOffset", lstickOffset.toJson())
        put("rstickOffset", rstickOffset.toJson())
        put("actionOffset", actionOffset.toJson())
        put("lbtnOffset", lbtnOffset.toJson())
        put("rbtnOffset", rbtnOffset.toJson())
        put("centerOffset", centerOffset.toJson())
        put(
            "stickScale",
            stickScale.coerceIn(AppPreferences.OVERLAY_CONTROL_SCALE_MIN, AppPreferences.OVERLAY_CONTROL_SCALE_MAX)
        )
        put("controlLayouts", controlLayouts.toJson())
    }
}

private fun JSONObject.toTouchControlsLayoutProfile(): TouchControlsLayoutProfile {
    val stickScale = optInt("stickScale", AppPreferences.OVERLAY_CONTROL_SCALE_DEFAULT)
        .coerceIn(AppPreferences.OVERLAY_CONTROL_SCALE_MIN, AppPreferences.OVERLAY_CONTROL_SCALE_MAX)
    val layouts = optJSONObject("controlLayouts")
        ?.toOverlayControlLayouts()
        ?.takeIf { it.isNotEmpty() }
        ?: AppPreferences.defaultOverlayControlLayouts(stickScale)
    return TouchControlsLayoutProfile(
        dpadOffset = readOffset("dpadOffset", AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y),
        lstickOffset = readOffset("lstickOffset", AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y),
        rstickOffset = readOffset("rstickOffset", AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y),
        actionOffset = readOffset("actionOffset", AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y),
        lbtnOffset = readOffset("lbtnOffset", AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y),
        rbtnOffset = readOffset("rbtnOffset", AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y),
        centerOffset = readOffset("centerOffset", AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y),
        stickScale = stickScale,
        controlLayouts = layouts
    )
}

private fun Pair<Float, Float>.toJson(): JSONObject {
    return JSONObject()
        .put("x", first.toDouble())
        .put("y", second.toDouble())
}

private fun JSONObject.readOffset(key: String, fallback: Pair<Float, Float>): Pair<Float, Float> {
    val json = optJSONObject(key) ?: return fallback
    return json.optDouble("x", fallback.first.toDouble()).toFloat() to
        json.optDouble("y", fallback.second.toDouble()).toFloat()
}

private fun Map<String, OverlayControlLayout>.toJson(): JSONObject {
    return JSONObject().apply {
        forEach { (id, layout) ->
            put(id, layout.toJson())
        }
    }
}

private fun JSONObject.toOverlayControlLayouts(): Map<String, OverlayControlLayout> {
    return keys().asSequence().associateWith { id ->
        optJSONObject(id)?.toOverlayControlLayout() ?: OverlayControlLayout()
    }
}

private fun OverlayControlLayout.toJson(): JSONObject {
    return JSONObject()
        .put("offset", offset.toJson())
        .put(
            "scale",
            scale.coerceIn(AppPreferences.OVERLAY_CONTROL_SCALE_MIN, AppPreferences.OVERLAY_CONTROL_SCALE_MAX)
        )
        .put("widthScale", widthScale.coerceIn(100, 240))
        .put(
            "opacity",
            opacity.coerceIn(
                AppPreferences.OVERLAY_CONTROL_OPACITY_MIN,
                AppPreferences.OVERLAY_CONTROL_OPACITY_MAX
            )
        )
        .put("visible", visible)
        .put("surfaceOnly", surfaceOnly)
}

private fun JSONObject.toOverlayControlLayout(): OverlayControlLayout {
    return OverlayControlLayout(
        offset = readOffset("offset", 0f to 0f),
        scale = optInt("scale", AppPreferences.OVERLAY_CONTROL_SCALE_DEFAULT)
            .coerceIn(AppPreferences.OVERLAY_CONTROL_SCALE_MIN, AppPreferences.OVERLAY_CONTROL_SCALE_MAX),
        widthScale = optInt("widthScale", 100).coerceIn(100, 240),
        opacity = optInt("opacity", AppPreferences.OVERLAY_CONTROL_OPACITY_DEFAULT)
            .coerceIn(
                AppPreferences.OVERLAY_CONTROL_OPACITY_MIN,
                AppPreferences.OVERLAY_CONTROL_OPACITY_MAX
            ),
        visible = optBoolean("visible", true),
        surfaceOnly = optBoolean("surfaceOnly", false)
    )
}

private fun sanitizeRendererValue(value: Int): Int {
    return RendererDefaults.normalizeAndroidRenderer(value)
}

private fun sanitizeAspectRatioValue(value: Int): Int {
    return if (value in 0..4) value else 1
}

private fun sanitizeFloatRoundMode(value: Int, fallback: Int): Int {
    return if (value in AppPreferences.FLOAT_ROUND_NEAREST..AppPreferences.FLOAT_ROUND_CHOP) {
        value
    } else {
        fallback
    }
}

private fun sanitizeClampingMode(value: Int, fallback: Int): Int {
    return if (value in AppPreferences.CLAMPING_NONE..AppPreferences.CLAMPING_FULL) {
        value
    } else {
        fallback
    }
}

private fun sanitizeRegionFramerate(value: Float, fallback: Float): Float {
    return if (value.isFinite()) value.coerceIn(20f, 120f) else fallback
}

private fun JSONObject.readUpscaleMultiplier(): Float {
    val doubleValue = optDouble("upscaleMultiplier", Double.NaN)
    return when {
        !doubleValue.isNaN() -> doubleValue.toFloat()
        has("upscaleMultiplier") -> optInt("upscaleMultiplier", 1).toFloat()
        else -> 1f
    }.let(::normalizeUpscale)
}

private fun JSONObject.readTrilinearFiltering(): Int {
    return GsHackDefaults.coerceTrilinearFiltering(
        optInt("trilinearFiltering", GsHackDefaults.TRILINEAR_FILTERING_DEFAULT)
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

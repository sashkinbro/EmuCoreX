package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.GsHackDefaults
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
    val enableMtvu: Boolean = true,
    val enableFastCdvd: Boolean = false,
    val enableCheats: Boolean = true,
    val hwDownloadMode: Int = 0,
    val eeCycleRate: Int = 0,
    val eeCycleSkip: Int = 0,
    val frameSkip: Int = 0,
    val frameLimitEnabled: Boolean = false,
    val targetFps: Int = 0,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val casMode: Int = 0,
    val casSharpness: Int = 50,
    val anisotropicFiltering: Int = 0,
    val enableHwMipmapping: Boolean = GsHackDefaults.HW_MIPMAPPING_DEFAULT,
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
    val providedKeys: Set<String>? = null,
    val updatedAt: Long = System.currentTimeMillis()
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

    fun clearManualHardwareFixesForAllProfiles(): Boolean {
        val items = loadAll()
        if (items.isEmpty()) return false

        val normalized = items.map { it.withManualHardwareFixesCleared() }
        if (normalized == items) return false

        writeAll(normalized.sortedBy { it.gameTitle.lowercase() })
        return true
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
            buildList {
                for (index in 0 until profiles.length()) {
                    val item = profiles.optJSONObject(index) ?: continue
                    add(item.toPerGameSettings())
                }
            }
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

private fun JSONObject.toPerGameSettings(): PerGameSettings {
    val providedKeys = keys().asSequence().toSet()
    return PerGameSettings(
        gameKey = optString("gameKey"),
        gameTitle = optString("gameTitle"),
        gameSerial = optString("gameSerial").takeIf { it.isNotBlank() },
        renderer = optInt("renderer", EmulatorBridge.VULKAN_RENDERER).let(::sanitizeRendererValue),
        upscaleMultiplier = readUpscaleMultiplier(),
        aspectRatio = optInt("aspectRatio", 1),
        showFps = optBoolean("showFps", false),
        fpsOverlayMode = optInt("fpsOverlayMode", AppPreferences.FPS_OVERLAY_MODE_DETAILED),
        enableMtvu = optBoolean("enableMtvu", true),
        enableFastCdvd = optBoolean("enableFastCdvd", false),
        enableCheats = optBoolean("enableCheats", true),
        hwDownloadMode = optInt("hwDownloadMode", 0),
        eeCycleRate = optInt("eeCycleRate", 0),
        eeCycleSkip = optInt("eeCycleSkip", 0),
        frameSkip = optInt("frameSkip", 0),
        frameLimitEnabled = optBoolean("frameLimitEnabled", false),
        targetFps = optInt("targetFps", 0).let { if (it <= 0) 0 else it.coerceIn(20, 120) },
        textureFiltering = optInt("textureFiltering", GsHackDefaults.BILINEAR_FILTERING_DEFAULT),
        trilinearFiltering = optInt("trilinearFiltering", GsHackDefaults.TRILINEAR_FILTERING_DEFAULT),
        blendingAccuracy = optInt("blendingAccuracy", GsHackDefaults.BLENDING_ACCURACY_DEFAULT),
        texturePreloading = optInt("texturePreloading", GsHackDefaults.TEXTURE_PRELOADING_DEFAULT),
        enableFxaa = optBoolean("enableFxaa", false),
        casMode = optInt("casMode", 0),
        casSharpness = optInt("casSharpness", 50),
        anisotropicFiltering = optInt("anisotropicFiltering", 0),
        enableHwMipmapping = optBoolean("enableHwMipmapping", GsHackDefaults.HW_MIPMAPPING_DEFAULT),
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
        nativeScaling = optInt("nativeScaling", GsHackDefaults.NATIVE_SCALING_DEFAULT),
        roundSprite = optInt("roundSprite", GsHackDefaults.ROUND_SPRITE_DEFAULT),
        bilinearUpscale = optInt("bilinearUpscale", GsHackDefaults.BILINEAR_UPSCALE_DEFAULT),
        textureOffsetX = optInt("textureOffsetX", 0),
        textureOffsetY = optInt("textureOffsetY", 0),
        alignSprite = optBoolean("alignSprite", false),
        mergeSprite = optBoolean("mergeSprite", false),
        forceEvenSpritePosition = optBoolean("forceEvenSpritePosition", false),
        nativePaletteDraw = optBoolean("nativePaletteDraw", false),
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
        if (shouldWrite("aspectRatio")) put("aspectRatio", aspectRatio)
        if (shouldWrite("showFps")) put("showFps", showFps)
        if (shouldWrite("fpsOverlayMode")) put("fpsOverlayMode", fpsOverlayMode)
        if (shouldWrite("enableMtvu")) put("enableMtvu", enableMtvu)
        if (shouldWrite("enableFastCdvd")) put("enableFastCdvd", enableFastCdvd)
        if (shouldWrite("enableCheats")) put("enableCheats", enableCheats)
        if (shouldWrite("hwDownloadMode")) put("hwDownloadMode", hwDownloadMode)
        if (shouldWrite("eeCycleRate")) put("eeCycleRate", eeCycleRate)
        if (shouldWrite("eeCycleSkip")) put("eeCycleSkip", eeCycleSkip)
        if (shouldWrite("frameSkip")) put("frameSkip", frameSkip)
        if (shouldWrite("frameLimitEnabled")) put("frameLimitEnabled", frameLimitEnabled)
        if (shouldWrite("targetFps")) put("targetFps", targetFps)
        if (shouldWrite("textureFiltering")) put("textureFiltering", textureFiltering)
        if (shouldWrite("trilinearFiltering")) put("trilinearFiltering", trilinearFiltering)
        if (shouldWrite("blendingAccuracy")) put("blendingAccuracy", blendingAccuracy)
        if (shouldWrite("texturePreloading")) put("texturePreloading", texturePreloading)
        if (shouldWrite("enableFxaa")) put("enableFxaa", enableFxaa)
        if (shouldWrite("casMode")) put("casMode", casMode)
        if (shouldWrite("casSharpness")) put("casSharpness", casSharpness)
        if (shouldWrite("anisotropicFiltering")) put("anisotropicFiltering", anisotropicFiltering)
        if (shouldWrite("enableHwMipmapping")) put("enableHwMipmapping", enableHwMipmapping)
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
        if (shouldWrite("nativeScaling")) put("nativeScaling", nativeScaling)
        if (shouldWrite("roundSprite")) put("roundSprite", roundSprite)
        if (shouldWrite("bilinearUpscale")) put("bilinearUpscale", bilinearUpscale)
        if (shouldWrite("textureOffsetX")) put("textureOffsetX", textureOffsetX)
        if (shouldWrite("textureOffsetY")) put("textureOffsetY", textureOffsetY)
        if (shouldWrite("alignSprite")) put("alignSprite", alignSprite)
        if (shouldWrite("mergeSprite")) put("mergeSprite", mergeSprite)
        if (shouldWrite("forceEvenSpritePosition")) put("forceEvenSpritePosition", forceEvenSpritePosition)
        if (shouldWrite("nativePaletteDraw")) put("nativePaletteDraw", nativePaletteDraw)
        put("updatedAt", updatedAt)
    }
}

private fun sanitizeRendererValue(value: Int): Int {
    return if (value <= 0) EmulatorBridge.VULKAN_RENDERER else value
}

private fun JSONObject.readUpscaleMultiplier(): Float {
    val doubleValue = optDouble("upscaleMultiplier", Double.NaN)
    return when {
        !doubleValue.isNaN() -> doubleValue.toFloat()
        has("upscaleMultiplier") -> optInt("upscaleMultiplier", 1).toFloat()
        else -> 1f
    }.let(::normalizeUpscale)
}

private fun PerGameSettings.withManualHardwareFixesCleared(): PerGameSettings {
    return copy(
        cpuSpriteRenderSize = GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT,
        cpuSpriteRenderLevel = GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT,
        softwareClutRender = GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT,
        gpuTargetClutMode = GsHackDefaults.GPU_TARGET_CLUT_DEFAULT,
        skipDrawStart = 0,
        skipDrawEnd = 0,
        autoFlushHardware = GsHackDefaults.AUTO_FLUSH_DEFAULT,
        cpuFramebufferConversion = false,
        disableDepthConversion = false,
        disableSafeFeatures = false,
        disableRenderFixes = false,
        preloadFrameData = false,
        disablePartialInvalidation = false,
        textureInsideRt = GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT,
        readTargetsOnClose = false,
        estimateTextureRegion = false,
        gpuPaletteConversion = false,
        halfPixelOffset = GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT,
        nativeScaling = GsHackDefaults.NATIVE_SCALING_DEFAULT,
        roundSprite = GsHackDefaults.ROUND_SPRITE_DEFAULT,
        bilinearUpscale = GsHackDefaults.BILINEAR_UPSCALE_DEFAULT,
        textureOffsetX = 0,
        textureOffsetY = 0,
        alignSprite = false,
        mergeSprite = false,
        forceEvenSpritePosition = false,
        nativePaletteDraw = false
    )
}

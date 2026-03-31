package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.core.normalizeUpscale
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class PerGameSettings(
    val gameKey: String,
    val gameTitle: String,
    val gameSerial: String? = null,
    val renderer: Int = 14,
    val upscaleMultiplier: Float = 1f,
    val aspectRatio: Int = 1,
    val performancePreset: Int = PerformancePresets.CUSTOM,
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
    val targetFps: Int = 60,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val casMode: Int = 0,
    val casSharpness: Int = 50,
    val anisotropicFiltering: Int = 0,
    val enableHwMipmapping: Boolean = true,
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
    return PerGameSettings(
        gameKey = optString("gameKey"),
        gameTitle = optString("gameTitle"),
        gameSerial = optString("gameSerial").takeIf { it.isNotBlank() },
        renderer = optInt("renderer", 14),
        upscaleMultiplier = readUpscaleMultiplier(),
        aspectRatio = optInt("aspectRatio", 1),
        performancePreset = optInt("performancePreset", PerformancePresets.CUSTOM),
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
        targetFps = optInt("targetFps", 60),
        textureFiltering = optInt("textureFiltering", GsHackDefaults.BILINEAR_FILTERING_DEFAULT),
        trilinearFiltering = optInt("trilinearFiltering", GsHackDefaults.TRILINEAR_FILTERING_DEFAULT),
        blendingAccuracy = optInt("blendingAccuracy", GsHackDefaults.BLENDING_ACCURACY_DEFAULT),
        texturePreloading = optInt("texturePreloading", GsHackDefaults.TEXTURE_PRELOADING_DEFAULT),
        enableFxaa = optBoolean("enableFxaa", false),
        casMode = optInt("casMode", 0),
        casSharpness = optInt("casSharpness", 50),
        anisotropicFiltering = optInt("anisotropicFiltering", 0),
        enableHwMipmapping = optBoolean("enableHwMipmapping", true),
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
        updatedAt = optLong("updatedAt", System.currentTimeMillis())
    )
}

private fun PerGameSettings.toJson(): JSONObject {
    return JSONObject().apply {
        put("gameKey", gameKey)
        put("gameTitle", gameTitle)
        put("gameSerial", gameSerial)
        put("renderer", renderer)
        put("upscaleMultiplier", upscaleMultiplier.toDouble())
        put("aspectRatio", aspectRatio)
        put("performancePreset", performancePreset)
        put("showFps", showFps)
        put("fpsOverlayMode", fpsOverlayMode)
        put("enableMtvu", enableMtvu)
        put("enableFastCdvd", enableFastCdvd)
        put("enableCheats", enableCheats)
        put("hwDownloadMode", hwDownloadMode)
        put("eeCycleRate", eeCycleRate)
        put("eeCycleSkip", eeCycleSkip)
        put("frameSkip", frameSkip)
        put("frameLimitEnabled", frameLimitEnabled)
        put("targetFps", targetFps)
        put("textureFiltering", textureFiltering)
        put("trilinearFiltering", trilinearFiltering)
        put("blendingAccuracy", blendingAccuracy)
        put("texturePreloading", texturePreloading)
        put("enableFxaa", enableFxaa)
        put("casMode", casMode)
        put("casSharpness", casSharpness)
        put("anisotropicFiltering", anisotropicFiltering)
        put("enableHwMipmapping", enableHwMipmapping)
        put("enableWidescreenPatches", enableWidescreenPatches)
        put("enableNoInterlacingPatches", enableNoInterlacingPatches)
        put("cpuSpriteRenderSize", cpuSpriteRenderSize)
        put("cpuSpriteRenderLevel", cpuSpriteRenderLevel)
        put("softwareClutRender", softwareClutRender)
        put("gpuTargetClutMode", gpuTargetClutMode)
        put("skipDrawStart", skipDrawStart)
        put("skipDrawEnd", skipDrawEnd)
        put("autoFlushHardware", autoFlushHardware)
        put("cpuFramebufferConversion", cpuFramebufferConversion)
        put("disableDepthConversion", disableDepthConversion)
        put("disableSafeFeatures", disableSafeFeatures)
        put("disableRenderFixes", disableRenderFixes)
        put("preloadFrameData", preloadFrameData)
        put("disablePartialInvalidation", disablePartialInvalidation)
        put("textureInsideRt", textureInsideRt)
        put("readTargetsOnClose", readTargetsOnClose)
        put("estimateTextureRegion", estimateTextureRegion)
        put("gpuPaletteConversion", gpuPaletteConversion)
        put("halfPixelOffset", halfPixelOffset)
        put("nativeScaling", nativeScaling)
        put("roundSprite", roundSprite)
        put("bilinearUpscale", bilinearUpscale)
        put("textureOffsetX", textureOffsetX)
        put("textureOffsetY", textureOffsetY)
        put("alignSprite", alignSprite)
        put("mergeSprite", mergeSprite)
        put("forceEvenSpritePosition", forceEvenSpritePosition)
        put("nativePaletteDraw", nativePaletteDraw)
        put("updatedAt", updatedAt)
    }
}

private fun JSONObject.readUpscaleMultiplier(): Float {
    val doubleValue = optDouble("upscaleMultiplier", Double.NaN)
    return when {
        !doubleValue.isNaN() -> doubleValue.toFloat()
        has("upscaleMultiplier") -> optInt("upscaleMultiplier", 1).toFloat()
        else -> 1f
    }.let(::normalizeUpscale)
}

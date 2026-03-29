package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorStorage
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class PerGameSettings(
    val gameKey: String,
    val gameTitle: String,
    val overrideRenderer: Boolean = false,
    val renderer: Int = 14,
    val overrideUpscale: Boolean = false,
    val upscaleMultiplier: Int = 1,
    val overrideAspectRatio: Boolean = false,
    val aspectRatio: Int = 1,
    val overrideMtvu: Boolean = false,
    val enableMtvu: Boolean = true,
    val overrideFastCdvd: Boolean = false,
    val enableFastCdvd: Boolean = false,
    val overrideFrameSkip: Boolean = false,
    val frameSkip: Int = 0,
    val overrideTextureFiltering: Boolean = false,
    val textureFiltering: Int = 1,
    val overrideWidescreenPatches: Boolean = false,
    val enableWidescreenPatches: Boolean = false,
    val overrideFrameLimit: Boolean = false,
    val frameLimitEnabled: Boolean = false,
    val overrideTargetFps: Boolean = false,
    val targetFps: Int = 60,
    val updatedAt: Long = System.currentTimeMillis()
)

class PerGameSettingsRepository(context: Context) {
    private val file = File(EmulatorStorage.appStateDir(context), "per-game-settings.json")

    fun get(gameKey: String): PerGameSettings? {
        return loadAll().firstOrNull { it.gameKey == gameKey }
    }

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
        writeAll(items)
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
            ).toString()
        )
    }
}

private fun JSONObject.toPerGameSettings(): PerGameSettings {
    return PerGameSettings(
        gameKey = optString("gameKey"),
        gameTitle = optString("gameTitle"),
        overrideRenderer = optBoolean("overrideRenderer"),
        renderer = optInt("renderer", 14),
        overrideUpscale = optBoolean("overrideUpscale"),
        upscaleMultiplier = optInt("upscaleMultiplier", 1),
        overrideAspectRatio = optBoolean("overrideAspectRatio"),
        aspectRatio = optInt("aspectRatio", 1),
        overrideMtvu = optBoolean("overrideMtvu"),
        enableMtvu = optBoolean("enableMtvu", true),
        overrideFastCdvd = optBoolean("overrideFastCdvd"),
        enableFastCdvd = optBoolean("enableFastCdvd"),
        overrideFrameSkip = optBoolean("overrideFrameSkip"),
        frameSkip = optInt("frameSkip", 0),
        overrideTextureFiltering = optBoolean("overrideTextureFiltering"),
        textureFiltering = optInt("textureFiltering", 1),
        overrideWidescreenPatches = optBoolean("overrideWidescreenPatches"),
        enableWidescreenPatches = optBoolean("enableWidescreenPatches"),
        overrideFrameLimit = optBoolean("overrideFrameLimit"),
        frameLimitEnabled = optBoolean("frameLimitEnabled"),
        overrideTargetFps = optBoolean("overrideTargetFps"),
        targetFps = optInt("targetFps", 60),
        updatedAt = optLong("updatedAt", System.currentTimeMillis())
    )
}

private fun PerGameSettings.toJson(): JSONObject {
    return JSONObject().apply {
        put("gameKey", gameKey)
        put("gameTitle", gameTitle)
        put("overrideRenderer", overrideRenderer)
        put("renderer", renderer)
        put("overrideUpscale", overrideUpscale)
        put("upscaleMultiplier", upscaleMultiplier)
        put("overrideAspectRatio", overrideAspectRatio)
        put("aspectRatio", aspectRatio)
        put("overrideMtvu", overrideMtvu)
        put("enableMtvu", enableMtvu)
        put("overrideFastCdvd", overrideFastCdvd)
        put("enableFastCdvd", enableFastCdvd)
        put("overrideFrameSkip", overrideFrameSkip)
        put("frameSkip", frameSkip)
        put("overrideTextureFiltering", overrideTextureFiltering)
        put("textureFiltering", textureFiltering)
        put("overrideWidescreenPatches", overrideWidescreenPatches)
        put("enableWidescreenPatches", enableWidescreenPatches)
        put("overrideFrameLimit", overrideFrameLimit)
        put("frameLimitEnabled", frameLimitEnabled)
        put("overrideTargetFps", overrideTargetFps)
        put("targetFps", targetFps)
        put("updatedAt", updatedAt)
    }
}

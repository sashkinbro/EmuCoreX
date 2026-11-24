package com.sbro.emucorex.data.store

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * `catalog/v1/store.json` — the EmuCore Store catalog, referenced by the optional `manifest.store`
 * file reference of the signed Hub manifest. Format: EmuCoreX-News-Media-Hub/store/README.md.
 * Clients that predate the Store ignore the unknown manifest field.
 */
@Serializable
data class StoreCatalog(
    val formatVersion: Int,
    val releaseId: String,
    val categories: List<StoreCategory> = emptyList(),
    val items: List<StoreItem> = emptyList()
)

@Serializable
enum class StoreKind {
    @SerialName("theme") THEME,
    @SerialName("wallpaper") WALLPAPER,
    @SerialName("sounds") SOUNDS,
    @SerialName("controls") CONTROLS
}

@Serializable
data class StoreCategory(
    val id: String,
    val kind: StoreKind,
    val labels: Map<String, String> = emptyMap()
) {
    fun label(locale: String): String = labels.localizedValue(locale) ?: id
}

@Serializable
data class StoreFile(
    val path: String,
    val sha256: String,
    val bytes: Long,
    val width: Int = 0,
    val height: Int = 0
)

@Serializable
data class StoreItem(
    val id: String,
    val kind: StoreKind,
    val version: Int,
    val requiresPro: Boolean = false,
    val minAppVersionCode: Int = 0,
    val featured: Boolean = false,
    val publishedAt: String = "",
    val categories: List<String> = emptyList(),
    val tags: List<String> = emptyList(),
    val name: Map<String, String> = emptyMap(),
    val description: Map<String, String> = emptyMap(),
    val swatch: List<String> = emptyList(),
    val cover: StoreFile,
    val gallery: List<StoreFile> = emptyList(),
    val packageFile: StoreFile,
    val sizeBytes: Long = 0
) {
    fun displayName(locale: String): String = name.localizedValue(locale) ?: id
    fun displayDescription(locale: String): String = description.localizedValue(locale).orEmpty()
}

/** Catalog resolved against the pinned commit base URL of the sync that produced it. */
data class StoreSnapshot(
    val baseUrl: String,
    val catalogRevision: Long,
    val catalog: StoreCatalog
) {
    fun url(file: StoreFile): String = baseUrl + file.path
}

internal fun Map<String, String>.localizedValue(locale: String): String? {
    val language = locale.substringBefore('-').substringBefore('_').lowercase()
    return this[locale] ?: this[language] ?: this["en"] ?: values.firstOrNull()
}

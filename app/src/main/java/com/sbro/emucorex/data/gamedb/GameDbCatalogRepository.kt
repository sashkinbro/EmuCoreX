package com.sbro.emucorex.data.gamedb

import android.content.Context
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.CoverArtRepository
import com.sbro.emucorex.data.pcsx2.GameIndexOverlay
import com.sbro.emucorex.data.pcsx2.GameIndexOverlayEntry
import java.util.Locale

enum class GameDbSettingCategory {
    CORE_FIX,
    ROUND_MODE,
    CLAMP_MODE,
    SPEED_HACK,
    GRAPHICS_FIX
}

/**
 * Sections of the bundled GameIndex file. The names are the YAML keys the emulator core reads,
 * and the same names identify a fix inside [GameDbUserOverrides].
 */
enum class GameDbFixSection(val yamlName: String) {
    GAME_FIXES("gameFixes"),
    ROUND_MODES("roundModes"),
    CLAMP_MODES("clampModes"),
    SPEED_HACKS("speedHacks"),
    GS_HW_FIXES("gsHWFixes");

    companion object {
        fun fromYamlName(name: String): GameDbFixSection? = entries.firstOrNull { it.yamlName == name }
    }
}

val GameDbSettingCategory.section: GameDbFixSection
    get() = when (this) {
        GameDbSettingCategory.CORE_FIX -> GameDbFixSection.GAME_FIXES
        GameDbSettingCategory.ROUND_MODE -> GameDbFixSection.ROUND_MODES
        GameDbSettingCategory.CLAMP_MODE -> GameDbFixSection.CLAMP_MODES
        GameDbSettingCategory.SPEED_HACK -> GameDbFixSection.SPEED_HACKS
        GameDbSettingCategory.GRAPHICS_FIX -> GameDbFixSection.GS_HW_FIXES
    }

val GameDbFixSection.category: GameDbSettingCategory
    get() = when (this) {
        GameDbFixSection.GAME_FIXES -> GameDbSettingCategory.CORE_FIX
        GameDbFixSection.ROUND_MODES -> GameDbSettingCategory.ROUND_MODE
        GameDbFixSection.CLAMP_MODES -> GameDbSettingCategory.CLAMP_MODE
        GameDbFixSection.SPEED_HACKS -> GameDbSettingCategory.SPEED_HACK
        GameDbFixSection.GS_HW_FIXES -> GameDbSettingCategory.GRAPHICS_FIX
    }

/** Stable, serial-independent identity of a single GameDB line: `<section>|<key>`. */
object GameDbFixId {
    private const val SEPARATOR = '|'

    fun of(section: GameDbFixSection, key: String): String = "${section.yamlName}$SEPARATOR$key"

    fun of(setting: GameDbSettingItem): String = of(setting.category.section, setting.key)

    fun sectionOf(id: String): GameDbFixSection? =
        GameDbFixSection.fromYamlName(id.substringBefore(SEPARATOR, ""))

    fun keyOf(id: String): String = id.substringAfter(SEPARATOR, "")
}

data class GameDbSettingItem(
    val category: GameDbSettingCategory,
    val key: String,
    val value: String,
    val note: String?,
    val controlledByGameFixesToggle: Boolean
) {
    val id: String = GameDbFixId.of(category.section, key)

    val searchableText: String = listOfNotNull(key, value, note)
        .joinToString(" ")
        .lowercase(Locale.ROOT)
}

data class GameDbCatalogEntry(
    val serial: String,
    val title: String,
    val region: String?,
    val coverUrl: String?,
    val settings: List<GameDbSettingItem>
) {
    val coreSettingCount: Int = settings.count { it.controlledByGameFixesToggle }
    val graphicsSettingCount: Int = settings.size - coreSettingCount
    val searchableText: String = buildString {
        append(serial.lowercase(Locale.ROOT))
        append(' ')
        append(title.lowercase(Locale.ROOT))
        region?.let {
            append(' ')
            append(it.lowercase(Locale.ROOT))
        }
        settings.forEach {
            append(' ')
            append(it.searchableText)
        }
    }
}

class GameDbCatalogRepository(context: Context) {
    private val appContext = context.applicationContext
    private val preferences = AppPreferences(appContext)
    private val coverStyle = preferences.getCoverArtStyleSync()
    private val coverBaseUrl = when (coverStyle) {
        AppPreferences.COVER_ART_STYLE_3D -> CoverArtRepository.DEFAULT_COVER_3D_BASE_URL
        else -> preferences.getCoverDownloadBaseUrlSync()
            ?.split(Regex("\\s+"))
            ?.firstOrNull { it.startsWith("http://") || it.startsWith("https://") }
            ?: CoverArtRepository.DEFAULT_COVER_BASE_URL
    }.trimEnd('/')
    private val coverExtension = if (coverStyle == AppPreferences.COVER_ART_STYLE_3D) "png" else "jpg"

    fun loadEntries(): List<GameDbCatalogEntry> {
        cachedEntries?.let { return it }
        synchronized(cacheLock) {
            cachedEntries?.let { return it }
            val parsed = parseGameIndex()
            cachedEntries = parsed
            return parsed
        }
    }

    private fun parseGameIndex(): List<GameDbCatalogEntry> {
        val entries = ArrayList<GameDbCatalogEntry>(4_000)
        var serial: String? = null
        var name: String? = null
        var englishName: String? = null
        var region: String? = null
        var activeSection: String? = null
        val settings = ArrayList<GameDbSettingItem>(8)
        val overlays = GameIndexOverlay.load(appContext)
        val visited = hashSetOf<String>()

        fun flush() {
            val currentSerial = serial ?: return
            visited += currentSerial
            val overlay = overlays[currentSerial]
            val effectiveSettings = applyOverlaySettings(settings, overlay)
            if (effectiveSettings.isNotEmpty()) {
                entries += GameDbCatalogEntry(
                    serial = currentSerial,
                    title = overlay?.scalars?.get("name-en") ?: englishName
                        ?: overlay?.scalars?.get("name") ?: name ?: currentSerial,
                    region = overlay?.scalars?.get("region") ?: region,
                    coverUrl = "$coverBaseUrl/$currentSerial.$coverExtension",
                    settings = effectiveSettings
                )
            }
        }

        appContext.assets.open(GAME_INDEX_ASSET_PATH).bufferedReader().useLines { lines ->
            lines.forEach { rawLine ->
                if (rawLine.isBlank() || rawLine.trimStart().startsWith('#')) return@forEach
                val indent = rawLine.indexOfFirst { !it.isWhitespace() }.coerceAtLeast(0)
                val content = rawLine.trim()

                if (indent == 0 && content.endsWith(':')) {
                    flush()
                    serial = content.removeSuffix(":").trim().uppercase(Locale.ROOT)
                    name = null
                    englishName = null
                    region = null
                    activeSection = null
                    settings.clear()
                    return@forEach
                }

                if (serial == null) return@forEach
                if (indent == 2) {
                    val (withoutComment, _) = splitComment(content)
                    val key = withoutComment.substringBefore(':').trim()
                    val value = withoutComment.substringAfter(':', "").trim()
                    when (key) {
                        "name" -> name = parseScalar(value)
                        "name-en" -> englishName = parseScalar(value)
                        "region" -> region = parseScalar(value)
                    }
                    activeSection = key.takeIf { it in supportedSections }
                    return@forEach
                }

                if (indent < 4) return@forEach
                val section = activeSection ?: return@forEach
                val (withoutComment, note) = splitComment(content)
                when (section) {
                    "gameFixes" -> {
                        if (withoutComment.startsWith('-')) {
                            val fix = parseScalar(withoutComment.removePrefix("-").trim())
                            if (fix.isNotBlank()) {
                                settings += GameDbSettingItem(
                                    category = GameDbSettingCategory.CORE_FIX,
                                    key = fix,
                                    value = "Enabled",
                                    note = note,
                                    controlledByGameFixesToggle = true
                                )
                            }
                        }
                    }
                    else -> {
                        if (!withoutComment.contains(':')) return@forEach
                        val key = withoutComment.substringBefore(':').trim()
                        val value = parseScalar(withoutComment.substringAfter(':').trim())
                        if (key.isBlank() || value.isBlank()) return@forEach
                        settings += GameDbSettingItem(
                            category = section.toCategory(),
                            key = key,
                            value = value,
                            note = note,
                            controlledByGameFixesToggle = section != "gsHWFixes"
                        )
                    }
                }
            }
        }
        flush()

        overlays.values.asSequence().filter { it.serial !in visited }.forEach { overlay ->
            val overlaySettings = applyOverlaySettings(emptyList(), overlay)
            if (overlaySettings.isNotEmpty()) {
                entries += GameDbCatalogEntry(
                    serial = overlay.serial,
                    title = overlay.scalars["name-en"] ?: overlay.scalars["name"] ?: overlay.serial,
                    region = overlay.scalars["region"],
                    coverUrl = "$coverBaseUrl/${overlay.serial}.$coverExtension",
                    settings = overlaySettings
                )
            }
        }

        return entries.sortedWith(
            compareBy<GameDbCatalogEntry> { it.title.lowercase(Locale.ROOT) }
                .thenBy { it.serial }
        )
    }

    private fun applyOverlaySettings(
        base: List<GameDbSettingItem>,
        overlay: GameIndexOverlayEntry?
    ): List<GameDbSettingItem> {
        if (overlay == null) return base.toList()
        val result = base.toMutableList()
        overlay.sections.forEach { (section, values) ->
            when (section) {
                "gsHWFixes" -> {
                    result.removeAll { it.category == GameDbSettingCategory.GRAPHICS_FIX }
                    result += values.values.map { (key, value) ->
                        setting(GameDbSettingCategory.GRAPHICS_FIX, key, value, controlled = false)
                    }
                }
                "roundModes", "clampModes" -> {
                    val category = section.toCategory()
                    values.values.forEach { (key, value) ->
                        result.removeAll { it.category == category && it.key == key }
                        result += setting(category, key, value, controlled = true)
                    }
                }
                "gameFixes" -> if (values.items.isNotEmpty()) {
                    result.removeAll { it.category == GameDbSettingCategory.CORE_FIX }
                    result += values.items.map { fix ->
                        setting(GameDbSettingCategory.CORE_FIX, fix, "Enabled", controlled = true)
                    }
                }
                "speedHacks" -> if (values.values.isNotEmpty() || values.items.isNotEmpty()) {
                    result.removeAll { it.category == GameDbSettingCategory.SPEED_HACK }
                    result += values.values.map { (key, value) ->
                        setting(GameDbSettingCategory.SPEED_HACK, key, value, controlled = true)
                    }
                    result += values.items.map { item ->
                        setting(GameDbSettingCategory.SPEED_HACK, item, "Enabled", controlled = true)
                    }
                }
            }
        }
        return result
    }

    private fun setting(
        category: GameDbSettingCategory,
        key: String,
        value: String,
        controlled: Boolean
    ) = GameDbSettingItem(category, key, value, note = null, controlledByGameFixesToggle = controlled)

    private fun String.toCategory(): GameDbSettingCategory = when (this) {
        "roundModes" -> GameDbSettingCategory.ROUND_MODE
        "clampModes" -> GameDbSettingCategory.CLAMP_MODE
        "speedHacks" -> GameDbSettingCategory.SPEED_HACK
        "gsHWFixes" -> GameDbSettingCategory.GRAPHICS_FIX
        else -> GameDbSettingCategory.CORE_FIX
    }

    private fun parseScalar(raw: String): String {
        return raw.trim()
            .removeSurrounding("\"")
            .removeSurrounding("'")
            .replace("\\\"", "\"")
    }

    private fun splitComment(line: String): Pair<String, String?> {
        var singleQuoted = false
        var doubleQuoted = false
        line.forEachIndexed { index, char ->
            when (char) {
                '\'' -> if (!doubleQuoted) singleQuoted = !singleQuoted
                '"' -> if (!singleQuoted && (index == 0 || line[index - 1] != '\\')) doubleQuoted = !doubleQuoted
                '#' -> if (!singleQuoted && !doubleQuoted) {
                    return line.substring(0, index).trimEnd() to
                        line.substring(index + 1).trim().takeIf { it.isNotBlank() }
                }
            }
        }
        return line.trimEnd() to null
    }

    private companion object {
        private const val GAME_INDEX_ASSET_PATH = "resources/GameIndex.yaml"
        private val supportedSections = setOf("gameFixes", "roundModes", "clampModes", "speedHacks", "gsHWFixes")
        private val cacheLock = Any()

        @Volatile
        private var cachedEntries: List<GameDbCatalogEntry>? = null
    }
}

package com.sbro.emucorex.data.pcsx2

import android.content.Context
import org.json.JSONObject
import java.util.Locale

enum class Pcsx2CompatibilityStatus {
    UNKNOWN,
    NOTHING,
    INTRO,
    IN_GAME,
    PLAYABLE,
    PERFECT
}

data class Pcsx2CompatibilityEntry(
    val serial: String,
    val title: String? = null,
    val region: String? = null,
    val status: Pcsx2CompatibilityStatus = Pcsx2CompatibilityStatus.UNKNOWN,
    val matchedBy: String? = null
)

class Pcsx2CompatibilityRepository(private val context: Context) {

    fun findByIgdbId(igdbId: Long?): Pcsx2CompatibilityEntry? {
        val key = igdbId?.toString() ?: return null
        return loadIgdbIndex()[key]
    }

    fun findBest(serial: String?, title: String?): Pcsx2CompatibilityEntry? {
        return findBySerial(serial) ?: findByTitle(title)
    }

    fun findBySerial(serial: String?): Pcsx2CompatibilityEntry? {
        val normalizedSerial = serial
            ?.trim()
            ?.uppercase(Locale.ROOT)
            ?.takeIf { it.isNotBlank() }
            ?: return null
        return loadIndex()[normalizedSerial]
    }

    fun findByTitle(title: String?): Pcsx2CompatibilityEntry? {
        val normalizedTitle = normalizeTitle(title) ?: return null
        return loadTitleIndex()[normalizedTitle]
    }

    private fun loadIndex(): Map<String, Pcsx2CompatibilityEntry> {
        cachedIndex?.let { return it }
        synchronized(cacheLock) {
            cachedIndex?.let { return it }
            val parsed = parseIndex()
            cachedIndex = parsed
            return parsed
        }
    }

    private fun loadIgdbIndex(): Map<String, Pcsx2CompatibilityEntry> {
        cachedIgdbIndex?.let { return it }
        synchronized(cacheLock) {
            cachedIgdbIndex?.let { return it }
            val parsed = parseIgdbIndex()
            cachedIgdbIndex = parsed
            return parsed
        }
    }

    private fun loadTitleIndex(): Map<String, Pcsx2CompatibilityEntry> {
        cachedTitleIndex?.let { return it }
        synchronized(cacheLock) {
            cachedTitleIndex?.let { return it }
            val parsed = parseTitleIndex() ?: buildTitleIndex(loadIndex().values)
            cachedTitleIndex = parsed
            return parsed
        }
    }

    private fun parseIgdbIndex(): Map<String, Pcsx2CompatibilityEntry> {
        val json = loadPrecomputedJson() ?: return emptyMap()
        val igdbObject = json.optJSONObject("igdb") ?: return emptyMap()
        return buildMap {
            val keys = igdbObject.keys()
            while (keys.hasNext()) {
                val key = keys.next()
                parseJsonEntry(igdbObject.optJSONObject(key))?.let { put(key, it) }
            }
        }
    }

    private fun parseTitleIndex(): Map<String, Pcsx2CompatibilityEntry>? {
        val json = loadPrecomputedJson() ?: return null
        val titleObject = json.optJSONObject("title") ?: return null
        return buildMap {
            val keys = titleObject.keys()
            while (keys.hasNext()) {
                val key = keys.next()
                parseJsonEntry(titleObject.optJSONObject(key))?.let { put(key, it) }
            }
        }
    }

    private fun parseIndex(): Map<String, Pcsx2CompatibilityEntry> {
        val precomputed = parseSerialIndex()
        if (precomputed.isNotEmpty()) {
            return precomputed
        }
        val entries = linkedMapOf<String, Pcsx2CompatibilityEntry>()
        context.assets.open(GAME_INDEX_ASSET_PATH).bufferedReader().useLines { lines ->
            var currentSerial: String? = null
            var currentTitle: String? = null
            var currentRegion: String? = null
            var currentStatus = Pcsx2CompatibilityStatus.UNKNOWN

            fun flushCurrent() {
                val serial = currentSerial ?: return
                entries[serial] = Pcsx2CompatibilityEntry(
                    serial = serial,
                    title = currentTitle,
                    region = currentRegion,
                    status = currentStatus
                )
            }

            lines.forEach { rawLine ->
                val line = rawLine.trimEnd()
                if (line.isBlank() || line.trimStart().startsWith("#")) {
                    return@forEach
                }

                val isTopLevel = rawLine.isNotEmpty() && !rawLine.first().isWhitespace()
                if (isTopLevel && line.endsWith(":")) {
                    flushCurrent()
                    currentSerial = line.removeSuffix(":").trim().uppercase(Locale.ROOT)
                    currentTitle = null
                    currentRegion = null
                    currentStatus = Pcsx2CompatibilityStatus.UNKNOWN
                    return@forEach
                }

                if (currentSerial == null) {
                    return@forEach
                }

                val trimmed = line.trim()
                when {
                    trimmed.startsWith("name-en:") -> {
                        currentTitle = parseYamlValue(trimmed.substringAfter(':'))
                    }
                    trimmed.startsWith("name:") && currentTitle == null -> {
                        currentTitle = parseYamlValue(trimmed.substringAfter(':'))
                    }
                    trimmed.startsWith("region:") -> {
                        currentRegion = parseYamlValue(trimmed.substringAfter(':'))
                    }
                    trimmed.startsWith("compat:") -> {
                        currentStatus = trimmed.substringAfter(':')
                            .trim()
                            .toIntOrNull()
                            .toStatus()
                    }
                }
            }

            flushCurrent()
        }
        return entries
    }

    private fun parseSerialIndex(): Map<String, Pcsx2CompatibilityEntry> {
        val json = loadPrecomputedJson() ?: return emptyMap()
        val serialObject = json.optJSONObject("serial") ?: return emptyMap()
        return buildMap {
            val keys = serialObject.keys()
            while (keys.hasNext()) {
                val key = keys.next()
                parseJsonEntry(serialObject.optJSONObject(key))?.let { put(key.uppercase(Locale.ROOT), it) }
            }
        }
    }

    private fun loadPrecomputedJson(): JSONObject? {
        cachedJson?.let { return it }
        synchronized(cacheLock) {
            cachedJson?.let { return it }
            val loaded = runCatching {
                val text = context.assets.open(PRECOMPUTED_INDEX_ASSET_PATH)
                    .bufferedReader()
                    .use { it.readText() }
                JSONObject(text)
            }.getOrNull()
            cachedJson = loaded
            return loaded
        }
    }

    private fun parseJsonEntry(json: JSONObject?): Pcsx2CompatibilityEntry? {
        json ?: return null
        val serial = json.optString("serial").takeIf { it.isNotBlank() } ?: return null
        return Pcsx2CompatibilityEntry(
            serial = serial,
            title = json.optString("title").takeIf { it.isNotBlank() },
            region = json.optString("region").takeIf { it.isNotBlank() },
            status = json.optString("status").toStatus(),
            matchedBy = json.optString("matched_by").takeIf { it.isNotBlank() }
        )
    }

    private fun buildTitleIndex(entries: Collection<Pcsx2CompatibilityEntry>): Map<String, Pcsx2CompatibilityEntry> {
        val out = linkedMapOf<String, Pcsx2CompatibilityEntry>()
        entries.forEach { entry ->
            val normalizedTitle = normalizeTitle(entry.title) ?: return@forEach
            val existing = out[normalizedTitle]
            if (existing == null || entry.status.ordinal > existing.status.ordinal) {
                out[normalizedTitle] = entry
            }
        }
        return out
    }

    private fun parseYamlValue(rawValue: String): String {
        val value = rawValue.trim()
        return value.removeSurrounding("\"").removeSurrounding("'")
    }

    private fun normalizeTitle(value: String?): String? {
        return value
            ?.trim()
            ?.lowercase(Locale.ROOT)
            ?.replace(Regex("""[^\p{L}\p{N}]+"""), " ")
            ?.replace(Regex("""\s+"""), " ")
            ?.trim()
            ?.takeIf { it.isNotBlank() }
    }

    private fun Int?.toStatus(): Pcsx2CompatibilityStatus {
        return when (this) {
            1 -> Pcsx2CompatibilityStatus.NOTHING
            2 -> Pcsx2CompatibilityStatus.INTRO
            3 -> Pcsx2CompatibilityStatus.IN_GAME
            4 -> Pcsx2CompatibilityStatus.PLAYABLE
            5 -> Pcsx2CompatibilityStatus.PERFECT
            else -> Pcsx2CompatibilityStatus.UNKNOWN
        }
    }

    private fun String?.toStatus(): Pcsx2CompatibilityStatus {
        return when (this?.uppercase(Locale.ROOT)) {
            "NOTHING" -> Pcsx2CompatibilityStatus.NOTHING
            "INTRO" -> Pcsx2CompatibilityStatus.INTRO
            "IN_GAME" -> Pcsx2CompatibilityStatus.IN_GAME
            "PLAYABLE" -> Pcsx2CompatibilityStatus.PLAYABLE
            "PERFECT" -> Pcsx2CompatibilityStatus.PERFECT
            else -> Pcsx2CompatibilityStatus.UNKNOWN
        }
    }

    private companion object {
        private const val GAME_INDEX_ASSET_PATH = "resources/GameIndex.yaml"
        private const val PRECOMPUTED_INDEX_ASSET_PATH = "catalog/pcsx2_compat_index.json"
        private val cacheLock = Any()

        @Volatile
        private var cachedIndex: Map<String, Pcsx2CompatibilityEntry>? = null

        @Volatile
        private var cachedTitleIndex: Map<String, Pcsx2CompatibilityEntry>? = null

        @Volatile
        private var cachedIgdbIndex: Map<String, Pcsx2CompatibilityEntry>? = null

        @Volatile
        private var cachedJson: JSONObject? = null
    }
}

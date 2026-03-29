package com.sbro.emucorex.data.ps2

import android.content.Context
import org.json.JSONObject
import java.util.Locale

data class Ps2IdentityMatch(
    val igdbId: Long,
    val confidence: Int,
    val matchedBy: String
)

class Ps2IdentityIndexRepository(private val context: Context) {

    fun findBestMatch(
        serial: String?,
        titleCandidates: List<String>
    ): Ps2IdentityMatch? {
        findBySerial(serial)?.let { return it }

        val normalizedCandidates = titleCandidates
            .mapNotNull(::normalizeTitle)
            .distinct()

        val titleMap = loadTitleMap()
        for (candidate in normalizedCandidates) {
            val entry = titleMap[candidate] ?: continue
            return Ps2IdentityMatch(
                igdbId = entry.igdbId,
                confidence = entry.confidence,
                matchedBy = entry.matchedBy ?: "title_index"
            )
        }

        return null
    }

    private fun findBySerial(serial: String?): Ps2IdentityMatch? {
        val normalizedSerial = serial
            ?.trim()
            ?.uppercase(Locale.ROOT)
            ?.takeIf { it.isNotBlank() }
            ?: return null
        val entry = loadSerialMap()[normalizedSerial] ?: return null
        return Ps2IdentityMatch(
            igdbId = entry.igdbId,
            confidence = entry.confidence,
            matchedBy = entry.matchedBy ?: "serial_index"
        )
    }

    private fun loadSerialMap(): Map<String, IdentityEntry> {
        cachedSerialMap?.let { return it }
        synchronized(cacheLock) {
            cachedSerialMap?.let { return it }
            val parsed = parseMap("serial_to_igdb")
            cachedSerialMap = parsed
            return parsed
        }
    }

    private fun loadTitleMap(): Map<String, IdentityEntry> {
        cachedTitleMap?.let { return it }
        synchronized(cacheLock) {
            cachedTitleMap?.let { return it }
            val parsed = parseMap("title_to_igdb")
            cachedTitleMap = parsed
            return parsed
        }
    }

    private fun parseMap(key: String): Map<String, IdentityEntry> {
        val json = loadJson() ?: return emptyMap()
        val root = json.optJSONObject(key) ?: return emptyMap()
        return buildMap {
            val keys = root.keys()
            while (keys.hasNext()) {
                val mapKey = keys.next()
                val item = root.optJSONObject(mapKey) ?: continue
                val igdbId = item.optLong("igdb_id", -1L)
                if (igdbId <= 0L) continue
                put(
                    mapKey,
                    IdentityEntry(
                        igdbId = igdbId,
                        confidence = item.optInt("confidence", 0),
                        matchedBy = item.optString("matched_by").takeIf { it.isNotBlank() }
                    )
                )
            }
        }
    }

    private fun loadJson(): JSONObject? {
        cachedJson?.let { return it }
        synchronized(cacheLock) {
            cachedJson?.let { return it }
            val loaded = runCatching {
                val text = context.assets.open(IDENTITY_INDEX_ASSET_PATH)
                    .bufferedReader()
                    .use { it.readText() }
                JSONObject(text)
            }.getOrNull()
            cachedJson = loaded
            return loaded
        }
    }

    private fun normalizeTitle(value: String?): String? {
        return value
            ?.trim()
            ?.lowercase(Locale.ROOT)
            ?.replace("&", " and ")
            ?.replace("@", " at ")
            ?.replace(Regex("""[^a-z0-9]+"""), " ")
            ?.replace(Regex("""\s+"""), " ")
            ?.trim()
            ?.takeIf { it.isNotBlank() }
    }

    private data class IdentityEntry(
        val igdbId: Long,
        val confidence: Int,
        val matchedBy: String?
    )

    private companion object {
        private const val IDENTITY_INDEX_ASSET_PATH = "catalog/rom_identity_index.json"
        private val cacheLock = Any()

        @Volatile
        private var cachedJson: JSONObject? = null

        @Volatile
        private var cachedSerialMap: Map<String, IdentityEntry>? = null

        @Volatile
        private var cachedTitleMap: Map<String, IdentityEntry>? = null
    }
}

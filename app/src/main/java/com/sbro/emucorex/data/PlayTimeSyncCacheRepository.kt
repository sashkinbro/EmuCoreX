package com.sbro.emucorex.data

import android.content.Context
import androidx.core.content.edit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** A play-time batch persisted as in flight until the cloud commit succeeds; [id] is stable across retries. */
data class InFlightPlayTime(val id: String, val entries: List<PlayerPlayTimeDelta>)

/** Cloud flush retry delay: 1, 2, 4 ... capped at 60 minutes after the n-th consecutive failure. */
object PlayTimeFlushBackoff {
    const val BASE_MS = 60_000L
    const val MAX_MS = 60L * 60_000L

    fun delayMs(consecutiveFailures: Int): Long {
        if (consecutiveFailures <= 0) return 0L
        val shift = (consecutiveFailures - 1).coerceAtMost(16)
        return (BASE_MS shl shift).coerceAtMost(MAX_MS)
    }
}

class PlayTimeSyncCacheRepository(context: Context) {

    private val preferences = context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
    private val mutex = Mutex()

    suspend fun add(entry: PlayerPlayTimeDelta) {
        if (entry.gamePath.isNullOrBlank() || entry.durationMs <= 0L || isAutotestPlayTimeEntry(entry)) return
        mutex.withLock {
            withContext(Dispatchers.IO) {
                val entries = readEntriesLocked().toMutableMap()
                val key = buildCacheKey(entry)
                val existing = entries[key]
                entries[key] = existing?.copy(
                    title = entry.title.ifBlank { existing.title },
                    serial = entry.serial ?: existing.serial,
                    coverArtPath = entry.coverArtPath ?: existing.coverArtPath,
                    durationMs = existing.durationMs + entry.durationMs,
                    sessionCount = existing.sessionCount + entry.sessionCount,
                    lastPlayedAtMs = maxOf(existing.lastPlayedAtMs, entry.lastPlayedAtMs)
                ) ?: entry
                writeEntriesLocked(entries.values.toList())
            }
        }
    }

    /**
     * Returns the batch to upload. Entries are NOT deleted from storage: pending entries are moved to
     * a persisted in-flight slot together with a stable [InFlightPlayTime.id], and only
     * [completeFlush] removes them. A failed attempt or a process kill therefore never loses time,
     * and a retry reuses the same id so the server can skip a batch whose acknowledgement was lost.
     * An existing in-flight batch is always returned first.
     */
    suspend fun beginFlush(): InFlightPlayTime? {
        return mutex.withLock {
            withContext(Dispatchers.IO) {
                readInFlightLocked()?.let { return@withContext it }
                val entries = readEntriesLocked().values.toList()
                if (entries.isEmpty()) return@withContext null
                val flight = InFlightPlayTime(UUID.randomUUID().toString(), entries)
                preferences.edit {
                    putString(KEY_INFLIGHT_ID, flight.id)
                    putString(KEY_INFLIGHT_ENTRIES, encodeEntries(entries))
                    remove(KEY_PENDING_ENTRIES)
                }
                flight
            }
        }
    }

    suspend fun completeFlush(id: String) {
        mutex.withLock {
            withContext(Dispatchers.IO) {
                if (preferences.getString(KEY_INFLIGHT_ID, null) == id) {
                    preferences.edit {
                        remove(KEY_INFLIGHT_ID)
                        remove(KEY_INFLIGHT_ENTRIES)
                    }
                }
            }
        }
    }

    suspend fun getNextCloudAttemptAtMs(): Long {
        return mutex.withLock {
            withContext(Dispatchers.IO) { preferences.getLong(KEY_NEXT_ATTEMPT_AT_MS, 0L) }
        }
    }

    suspend fun recordFlushFailure(nowMs: Long): Long {
        return mutex.withLock {
            withContext(Dispatchers.IO) {
                val failures = preferences.getInt(KEY_FAILURE_COUNT, 0) + 1
                val next = nowMs + PlayTimeFlushBackoff.delayMs(failures)
                preferences.edit {
                    putInt(KEY_FAILURE_COUNT, failures)
                    putLong(KEY_NEXT_ATTEMPT_AT_MS, next)
                }
                next
            }
        }
    }

    suspend fun clearFlushBackoff() {
        mutex.withLock {
            withContext(Dispatchers.IO) {
                preferences.edit {
                    remove(KEY_FAILURE_COUNT)
                    remove(KEY_NEXT_ATTEMPT_AT_MS)
                }
            }
        }
    }

    suspend fun getLastCloudSyncAtMs(): Long {
        return mutex.withLock {
            withContext(Dispatchers.IO) {
                preferences.getLong(KEY_LAST_CLOUD_SYNC_AT_MS, 0L)
            }
        }
    }

    suspend fun setLastCloudSyncAtMs(timestampMs: Long) {
        mutex.withLock {
            withContext(Dispatchers.IO) {
                preferences.edit { putLong(KEY_LAST_CLOUD_SYNC_AT_MS, timestampMs) }
            }
        }
    }

    private fun decodeEntries(raw: String): Map<String, PlayerPlayTimeDelta> {
        if (raw.isBlank()) return emptyMap()
        return runCatching {
            val array = JSONArray(raw)
            buildMap {
                repeat(array.length()) { index ->
                    val json = array.optJSONObject(index) ?: return@repeat
                    val entry = PlayerPlayTimeDelta(
                        gamePath = json.optString("path").takeIf { it.isNotBlank() },
                        title = json.optString("title"),
                        serial = json.optString("serial").takeIf { it.isNotBlank() },
                        coverArtPath = json.optString("cover").takeIf { it.isNotBlank() },
                        durationMs = json.optLong("durationMs"),
                        sessionCount = json.optLong("sessions"),
                        lastPlayedAtMs = json.optLong("lastPlayedAtMs")
                    )
                    if (!entry.gamePath.isNullOrBlank() && entry.durationMs > 0L && !isAutotestPlayTimeEntry(entry)) {
                        put(buildCacheKey(entry), entry)
                    }
                }
            }
        }.getOrDefault(emptyMap())
    }

    private fun readInFlightLocked(): InFlightPlayTime? {
        val id = preferences.getString(KEY_INFLIGHT_ID, null).orEmpty()
        if (id.isBlank()) return null
        val entries = decodeEntries(preferences.getString(KEY_INFLIGHT_ENTRIES, null).orEmpty())
        if (entries.isEmpty()) {
            preferences.edit { remove(KEY_INFLIGHT_ID); remove(KEY_INFLIGHT_ENTRIES) }
            return null
        }
        return InFlightPlayTime(id, entries.values.toList())
    }

    private fun readEntriesLocked(): Map<String, PlayerPlayTimeDelta> =
        decodeEntries(preferences.getString(KEY_PENDING_ENTRIES, null).orEmpty())

    private fun writeEntriesLocked(entries: List<PlayerPlayTimeDelta>) {
        preferences.edit { putString(KEY_PENDING_ENTRIES, encodeEntries(entries)) }
    }

    private fun encodeEntries(entries: List<PlayerPlayTimeDelta>): String {
        val array = JSONArray()
        entries.filterNot(::isAutotestPlayTimeEntry).forEach { entry ->
            array.put(
                JSONObject()
                    .put("path", entry.gamePath.orEmpty())
                    .put("title", entry.title)
                    .put("serial", entry.serial.orEmpty())
                    .put("cover", entry.coverArtPath.orEmpty())
                    .put("durationMs", entry.durationMs)
                    .put("sessions", entry.sessionCount)
                    .put("lastPlayedAtMs", entry.lastPlayedAtMs)
            )
        }
        return array.toString()
    }

    private fun buildCacheKey(entry: PlayerPlayTimeDelta): String {
        return entry.serial?.takeIf { it.isNotBlank() }?.uppercase()
            ?: entry.gamePath.orEmpty()
    }

    private companion object {
        const val PREFERENCES_NAME = "play_time_sync_cache"
        const val KEY_PENDING_ENTRIES = "pending_entries"
        const val KEY_LAST_CLOUD_SYNC_AT_MS = "last_cloud_sync_at_ms"
        const val KEY_INFLIGHT_ID = "inflight_id"
        const val KEY_INFLIGHT_ENTRIES = "inflight_entries"
        const val KEY_FAILURE_COUNT = "cloud_failure_count"
        const val KEY_NEXT_ATTEMPT_AT_MS = "cloud_next_attempt_at_ms"
    }
}

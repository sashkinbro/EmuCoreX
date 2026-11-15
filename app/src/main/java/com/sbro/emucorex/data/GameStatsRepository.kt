package com.sbro.emucorex.data

import android.content.Context
import android.util.Log
import com.google.android.gms.tasks.Task
import com.google.firebase.Timestamp
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.Query
import com.google.firebase.firestore.SetOptions
import com.sbro.emucorex.data.ps2.Ps2CatalogRepository
import androidx.core.content.edit
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import org.json.JSONArray
import org.json.JSONObject
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.util.Locale
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

data class GameStatsDelta(
    val key: String,
    val serial: String?,
    val title: String,
    val durationMs: Long,
    val sessions: Long,
    val lastPlayedAtMs: Long,
    val firstPlay: Boolean
)

data class TopGameStat(
    val key: String,
    val serial: String?,
    val title: String,
    val totalPlayTimeMs: Long,
    val sessions: Int,
    val players: Int,
    val lastPlayedAtMs: Long?,
    val igdbId: Long? = null,
    val coverUrl: String? = null,
    val fallbackCoverUrl: String? = null,
    val groupKeys: List<String> = emptyList()
)

data class GameDeviceStat(
    val gameKey: String,
    val deviceKey: String,
    val soc: String,
    val gpu: String?,
    val ramMb: Int,
    val totalPlayTimeMs: Long,
    val sessions: Int
)

data class TopGamesSnapshot(
    val period: GameTopPeriod,
    val entries: List<TopGameStat>,
    val updatedAtMs: Long,
    val totalGames: Int,
    val totalPlayTimeMs: Long,
    val totalPlayers: Int,
    val fetchedAtMs: Long = 0L
)

enum class GameTopPeriod(val docId: String) {
    AllTime("allTime"),
    Week("week"),
    Month("month")
}

/**
 * Global per-game play-time aggregation.
 *
 * Writes are incremental deltas to per-game counter documents and never block the
 * play-time flush: failures are logged and swallowed by the caller. Reads use the
 * scheduled summary document when available and fall back to a direct top query
 * while the Cloud Function has not published yet.
 */
class GameStatsRepository(context: Context) {

    private val appContext = context.applicationContext
    private val syncPrefs = appContext.getSharedPreferences("game_stats_sync", Context.MODE_PRIVATE)
    private val pendingMutex = Mutex()
    private val firestore = FirebaseFirestore.getInstance()
    private val catalogRepository = Ps2CatalogRepository(appContext)
    private val coverArtRepository = CoverArtRepository(appContext)
    private val cache = LinkedHashMap<GameTopPeriod, TopGamesSnapshot>()
    private val resolvedCatalogIds = HashMap<String, Long?>()
    private val catalogCovers = HashMap<Long, String?>()

    /**
     * Records deltas, retrying anything left over from earlier failed calls first. Deltas stay in a
     * small persisted queue until their batch commits, so a failed/denied/offline flush is retried
     * on the next call instead of being lost. Throws the last failure so the caller can log it.
     *
     * Only counter fields are sent for documents this device already created; title, serial and
     * device descriptors are sent only on (re)creation so updates never touch immutable-ish fields.
     */
    suspend fun recordDeltas(deltas: List<GameStatsDelta>) {
        val incoming = deltas.filter { it.durationMs > 0L && isValidGameStatKey(it.key) }
        pendingMutex.withLock {
            var queue = mergePendingGameStats(readPendingLocked(), incoming)
            if (queue.isEmpty()) return
            writePendingLocked(queue)

            val device = ProfileDeviceInfoProvider.current(appContext)
            val deviceKey = normalizeGameDeviceKey(device.soc)
            val deviceSoc = device.soc.take(MAX_SOC_LENGTH).ifBlank { deviceKey }
            val deviceGpu = device.gpuFamily.take(MAX_GPU_LENGTH).takeIf { it.isNotBlank() }
            val deviceRamMb = device.ramMb.coerceIn(0L, 262_144L).toInt()

            var failure: Throwable? = null
            while (queue.isNotEmpty()) {
                val chunk = queue.take(MAX_DELTAS_PER_BATCH)
                val created = readCreatedLocked()
                val batch = firestore.batch()
                val touched = ArrayList<String>(chunk.size * 2)
                chunk.forEach { delta ->
                    val allTimePath = "$ALL_TIME_COLLECTION/${delta.key}"
                    val devicePath = "$DEVICE_COLLECTION/${delta.key}_$deviceKey"
                    touched += allTimePath
                    touched += devicePath
                    addDeltaToBatch(
                        batch, delta, deviceKey, deviceSoc, deviceGpu, deviceRamMb,
                        includeAllTimeStatic = allTimePath !in created,
                        includeDeviceStatic = devicePath !in created
                    )
                }
                val outcome = try {
                    withTimeout(COMMIT_TIMEOUT_MS) { batch.commit().await() }
                    CommitOutcome.Committed
                } catch (_: TimeoutCancellationException) {
                    // The SDK keeps the write queued and sends it when connectivity returns; retrying
                    // here would double count it, so treat it as accepted rather than re-enqueue.
                    CommitOutcome.Queued
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    failure = e
                    CommitOutcome.Failed
                }
                if (outcome == CommitOutcome.Failed) {
                    // The doc may have been removed or rejected: resend full create fields next time.
                    writeCreatedLocked(created - touched.toSet())
                    break
                }
                writeCreatedLocked(created + touched)
                queue = queue.drop(chunk.size)
                writePendingLocked(queue)
                Log.d(TAG, "Recorded game stats for ${chunk.size} game(s) ($outcome)")
            }
            failure?.let { throw it }
        }
    }

    private fun addDeltaToBatch(
        batch: com.google.firebase.firestore.WriteBatch,
        delta: GameStatsDelta,
        deviceKey: String,
        deviceSoc: String,
        deviceGpu: String?,
        deviceRamMb: Int,
        includeAllTimeStatic: Boolean,
        includeDeviceStatic: Boolean
    ) {
        val key = delta.key
        val title = delta.title.take(MAX_TITLE_LENGTH).ifBlank { key }
        val allTime = mutableMapOf<String, Any>(
            FIELD_KEY to key,
            FIELD_TOTAL_MS to com.google.firebase.firestore.FieldValue.increment(delta.durationMs),
            FIELD_SESSIONS to com.google.firebase.firestore.FieldValue.increment(delta.sessions),
            FIELD_PLAYERS to com.google.firebase.firestore.FieldValue.increment(if (delta.firstPlay) 1L else 0L),
            FIELD_LAST_PLAYED_AT_MS to delta.lastPlayedAtMs,
            FIELD_STAT_UPDATED_AT to com.google.firebase.firestore.FieldValue.serverTimestamp()
        )
        if (includeAllTimeStatic) {
            allTime[FIELD_TITLE] = title
            delta.serial?.takeIf { it.isNotBlank() }?.let {
                allTime[FIELD_SERIAL] = it.take(MAX_SERIAL_LENGTH)
            }
        }
        batch.set(
            firestore.collection(ALL_TIME_COLLECTION).document(key),
            allTime,
            SetOptions.merge()
        )

        val weekKey = GameStatsPeriods.weekKey(delta.lastPlayedAtMs)
        val monthKey = GameStatsPeriods.monthKey(delta.lastPlayedAtMs)
        batch.set(
            firestore.collection(PERIOD_COLLECTION)
                .document(GameStatsPeriods.periodStatDocId(key, weekKey)),
            periodPatch(key, delta, weekKey, GameStatsPeriods.weekExpireAtMs(delta.lastPlayedAtMs)),
            SetOptions.merge()
        )
        batch.set(
            firestore.collection(PERIOD_COLLECTION)
                .document(GameStatsPeriods.periodStatDocId(key, monthKey)),
            periodPatch(key, delta, monthKey, GameStatsPeriods.monthExpireAtMs(delta.lastPlayedAtMs)),
            SetOptions.merge()
        )

        val devicePatch = mutableMapOf<String, Any>(
            FIELD_KEY to key,
            FIELD_DEVICE_KEY to deviceKey,
            FIELD_TOTAL_MS to com.google.firebase.firestore.FieldValue.increment(delta.durationMs),
            FIELD_SESSIONS to com.google.firebase.firestore.FieldValue.increment(delta.sessions),
            FIELD_STAT_UPDATED_AT to com.google.firebase.firestore.FieldValue.serverTimestamp()
        )
        if (includeDeviceStatic) {
            devicePatch[FIELD_TITLE] = title
            devicePatch[FIELD_SOC] = deviceSoc
            devicePatch[FIELD_RAM] = deviceRamMb
            deviceGpu?.let { devicePatch[FIELD_GPU] = it }
            delta.serial?.takeIf { it.isNotBlank() }?.let {
                devicePatch[FIELD_SERIAL] = it.take(MAX_SERIAL_LENGTH)
            }
        }
        batch.set(
            firestore.collection(DEVICE_COLLECTION).document("${key}_$deviceKey"),
            devicePatch,
            SetOptions.merge()
        )
    }

    private enum class CommitOutcome { Committed, Queued, Failed }

    private fun readPendingLocked(): List<GameStatsDelta> {
        val raw = syncPrefs.getString(PREF_PENDING, null).orEmpty()
        if (raw.isBlank()) return emptyList()
        return runCatching {
            val array = JSONArray(raw)
            (0 until array.length()).mapNotNull { index ->
                val json = array.optJSONObject(index) ?: return@mapNotNull null
                GameStatsDelta(
                    key = json.optString("k"),
                    serial = json.optString("s").takeIf { it.isNotBlank() },
                    title = json.optString("t"),
                    durationMs = json.optLong("ms"),
                    sessions = json.optLong("n"),
                    lastPlayedAtMs = json.optLong("lp"),
                    firstPlay = json.optBoolean("f")
                ).takeIf { it.durationMs > 0L && isValidGameStatKey(it.key) }
            }
        }.getOrDefault(emptyList())
    }

    private fun writePendingLocked(queue: List<GameStatsDelta>) {
        val array = JSONArray()
        queue.forEach { d ->
            array.put(
                JSONObject().put("k", d.key).put("s", d.serial.orEmpty()).put("t", d.title)
                    .put("ms", d.durationMs).put("n", d.sessions).put("lp", d.lastPlayedAtMs)
                    .put("f", d.firstPlay)
            )
        }
        syncPrefs.edit {
            if (queue.isEmpty()) remove(PREF_PENDING) else putString(PREF_PENDING, array.toString())
        }
    }

    private fun readCreatedLocked(): Set<String> =
        syncPrefs.getStringSet(PREF_CREATED, emptySet()).orEmpty().toSet()

    private fun writeCreatedLocked(paths: Set<String>) {
        val bounded = if (paths.size > MAX_CREATED_TRACKED) paths.toList().takeLast(MAX_CREATED_TRACKED).toSet() else paths
        syncPrefs.edit { putStringSet(PREF_CREATED, bounded) }
    }

    suspend fun loadTopGames(period: GameTopPeriod, forceRefresh: Boolean = false): TopGamesSnapshot {
        val cached = cache[period]
        if (!forceRefresh && cached != null &&
            System.currentTimeMillis() - cached.fetchedAtMs < CACHE_TTL_MS
        ) {
            return cached
        }
        val snapshot = loadSummary(period) ?: emptySnapshot(period)
        if (snapshot.entries.isNotEmpty() || snapshot.updatedAtMs > 0L) {
            cache[period] = snapshot
        }
        return snapshot
    }

    private fun emptySnapshot(period: GameTopPeriod): TopGamesSnapshot = TopGamesSnapshot(
        period = period,
        entries = emptyList(),
        updatedAtMs = 0L,
        totalGames = 0,
        totalPlayTimeMs = 0L,
        totalPlayers = 0,
        fetchedAtMs = 0L
    )

    suspend fun loadDeviceStats(gameKeys: List<String>): List<GameDeviceStat> {
        val keys = gameKeys
            .filter(::isValidGameStatKey)
            .distinct()
            .take(MAX_DEVICE_QUERY_KEYS)
        if (keys.isEmpty()) return emptyList()
        val documents = firestore.collection(DEVICE_COLLECTION)
            .whereIn(FIELD_KEY, keys)
            .orderBy(FIELD_TOTAL_MS, Query.Direction.DESCENDING)
            .limit(DEVICE_STATS_LIMIT)
            .get()
            .await()
            .documents
        return documents.mapNotNull { document ->
            val key = document.getString(FIELD_KEY) ?: return@mapNotNull null
            GameDeviceStat(
                gameKey = key,
                deviceKey = document.getString(FIELD_DEVICE_KEY).orEmpty(),
                soc = document.getString(FIELD_SOC).orEmpty(),
                gpu = document.getString(FIELD_GPU)?.takeIf { it.isNotBlank() },
                ramMb = (document.getLong(FIELD_RAM) ?: 0L).coerceIn(0L, 262_144L).toInt(),
                totalPlayTimeMs = document.getLong(FIELD_TOTAL_MS) ?: 0L,
                sessions = (document.getLong(FIELD_SESSIONS) ?: 0L)
                    .coerceIn(0L, Int.MAX_VALUE.toLong())
                    .toInt()
            )
        }
    }

    fun clearCache() {
        cache.clear()
    }

    private suspend fun loadSummary(period: GameTopPeriod): TopGamesSnapshot? {
        val document = firestore.collection(SUMMARY_COLLECTION).document(period.docId).get().await()
        if (!document.exists()) return null
        val rawEntries = document.get(FIELD_ENTRIES) as? List<*> ?: emptyList<Any>()
        val entries = rawEntries.mapNotNull(::toTopGameStat)
        return buildSnapshot(
            period = period,
            entries = entries,
            updatedAtMs = document.getTimestamp(FIELD_UPDATED_AT)?.toDate()?.time ?: 0L,
            totalGames = (document.getLong(FIELD_TOTAL_GAMES) ?: 0L).toInt(),
            totalPlayTimeMs = document.getLong(FIELD_TOTAL_PLAY_TIME_MS) ?: 0L,
            totalPlayers = (document.getLong(FIELD_TOTAL_PLAYERS) ?: 0L).toInt()
        )
    }

    private suspend fun buildSnapshot(
        period: GameTopPeriod,
        entries: List<TopGameStat>,
        updatedAtMs: Long,
        totalGames: Int,
        totalPlayTimeMs: Long,
        totalPlayers: Int
    ): TopGamesSnapshot = withContext(Dispatchers.IO) {
        val enriched = entries.map { entry ->
            val catalogId = resolveCatalogId(entry.serial, entry.title, entry.key)
            entry.copy(
                igdbId = catalogId,
                coverUrl = coverArtRepository.buildPublicCoverUrl(entry.serial ?: entry.key),
                fallbackCoverUrl = catalogId?.let(::catalogCover),
                groupKeys = listOf(entry.key)
            )
        }
        TopGamesSnapshot(
            period = period,
            entries = groupTopGames(enriched).take(MAX_DISPLAY_ENTRIES),
            updatedAtMs = updatedAtMs,
            totalGames = totalGames,
            totalPlayTimeMs = totalPlayTimeMs,
            totalPlayers = totalPlayers,
            fetchedAtMs = System.currentTimeMillis()
        )
    }

    private fun resolveCatalogId(serial: String?, title: String, key: String): Long? {
        val cacheKey = "${serial.orEmpty()}|$title"
        synchronized(resolvedCatalogIds) {
            if (resolvedCatalogIds.containsKey(cacheKey)) return resolvedCatalogIds[cacheKey]
        }
        val resolved = catalogRepository.findCatalogMatchId(serial ?: key, title)
        synchronized(resolvedCatalogIds) {
            resolvedCatalogIds[cacheKey] = resolved
        }
        return resolved
    }

    private fun catalogCover(igdbId: Long): String? {
        synchronized(catalogCovers) {
            if (catalogCovers.containsKey(igdbId)) return catalogCovers[igdbId]
        }
        val cover = catalogRepository.getCoverUrl(igdbId)
        synchronized(catalogCovers) {
            catalogCovers[igdbId] = cover
        }
        return cover
    }

    private fun periodPatch(
        key: String,
        delta: GameStatsDelta,
        periodKey: String,
        expireAtMs: Long
    ): Map<String, Any> {
        val patch = mutableMapOf<String, Any>(
            FIELD_KEY to key,
            FIELD_TITLE to delta.title.take(MAX_TITLE_LENGTH).ifBlank { key },
            FIELD_PERIOD_KEY to periodKey,
            FIELD_TOTAL_MS to com.google.firebase.firestore.FieldValue.increment(delta.durationMs),
            FIELD_SESSIONS to com.google.firebase.firestore.FieldValue.increment(delta.sessions),
            FIELD_EXPIRE_AT to Timestamp(
                expireAtMs / 1000L,
                ((expireAtMs % 1000L) * 1_000_000L).toInt()
            ),
            FIELD_STAT_UPDATED_AT to com.google.firebase.firestore.FieldValue.serverTimestamp()
        )
        delta.serial?.takeIf { it.isNotBlank() }?.let {
            patch[FIELD_SERIAL] = it.take(MAX_SERIAL_LENGTH)
        }
        return patch
    }

    private fun toTopGameStat(raw: Any?): TopGameStat? {
        val map = raw as? Map<*, *> ?: return null
        val key = map[FIELD_KEY] as? String ?: return null
        if (!isValidGameStatKey(key)) return null
        return TopGameStat(
            key = key,
            serial = (map[FIELD_SERIAL] as? String)?.takeIf { it.isNotBlank() },
            title = (map[FIELD_TITLE] as? String)?.takeIf { it.isNotBlank() } ?: key,
            totalPlayTimeMs = (map[FIELD_TOTAL_MS] as? Number)?.toLong() ?: 0L,
            sessions = ((map[FIELD_SESSIONS] as? Number)?.toLong() ?: 0L)
                .coerceIn(0L, Int.MAX_VALUE.toLong()).toInt(),
            players = ((map[FIELD_PLAYERS] as? Number)?.toLong() ?: 0L)
                .coerceIn(0L, Int.MAX_VALUE.toLong()).toInt(),
            lastPlayedAtMs = (map[FIELD_LAST_PLAYED_AT_MS] as? Number)?.toLong()?.takeIf { it > 0L }
        )
    }

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { result -> continuation.resume(result) }
        addOnFailureListener { error -> continuation.resumeWithException(error) }
        addOnCanceledListener { continuation.cancel() }
    }

    companion object {
        private const val TAG = "GameStatsRepository"
        private const val ALL_TIME_COLLECTION = "gamePlayTimeStats"
        private const val PERIOD_COLLECTION = "gamePeriodStats"
        private const val SUMMARY_COLLECTION = "gameTopSummary"
        private const val DEVICE_COLLECTION = "gameDeviceStats"
        private const val MAX_DISPLAY_ENTRIES = 300
        private const val CACHE_TTL_MS = 6L * 60L * 60L * 1000L
        private const val MAX_TITLE_LENGTH = 120
        private const val MAX_SERIAL_LENGTH = 32
        private const val MAX_SOC_LENGTH = 80
        private const val MAX_GPU_LENGTH = 40
        private const val MAX_DELTAS_PER_BATCH = 100
        private const val MAX_CREATED_TRACKED = 1000
        private const val COMMIT_TIMEOUT_MS = 20_000L
        private const val PREF_PENDING = "pending_deltas"
        private const val PREF_CREATED = "created_docs"
        private const val MAX_DEVICE_QUERY_KEYS = 10
        private const val DEVICE_STATS_LIMIT = 5L

        private const val FIELD_KEY = "k"
        private const val FIELD_TITLE = "t"
        private const val FIELD_SERIAL = "s"
        private const val FIELD_TOTAL_MS = "ms"
        private const val FIELD_SESSIONS = "n"
        private const val FIELD_PLAYERS = "p"
        private const val FIELD_LAST_PLAYED_AT_MS = "lp"
        private const val FIELD_PERIOD_KEY = "pk"
        private const val FIELD_EXPIRE_AT = "expireAt"
        private const val FIELD_DEVICE_KEY = "dk"
        private const val FIELD_SOC = "soc"
        private const val FIELD_GPU = "gpu"
        private const val FIELD_RAM = "ram"
        private const val FIELD_UPDATED_AT = "updatedAt"
        private const val FIELD_STAT_UPDATED_AT = "u"
        private const val FIELD_ENTRIES = "entries"
        private const val FIELD_TOTAL_GAMES = "totalGames"
        private const val FIELD_TOTAL_PLAY_TIME_MS = "totalPlayTimeMs"
        private const val FIELD_TOTAL_PLAYERS = "totalPlayers"
    }
}

internal fun groupTopGames(raw: List<TopGameStat>): List<TopGameStat> {
    val grouped = LinkedHashMap<String, TopGameStat>()
    raw.forEach { entry ->
        val groupKey = entry.igdbId?.let { "igdb:$it" } ?: "key:${entry.key}"
        val existing = grouped[groupKey]
        grouped[groupKey] = if (existing == null) entry else mergeTopGameStats(existing, entry)
    }
    return grouped.values.sortedByDescending { it.totalPlayTimeMs }
}

/** Merges retry-queue deltas with new ones; same game and same UTC week+month collapse into one. */
internal fun mergePendingGameStats(
    existing: List<GameStatsDelta>,
    incoming: List<GameStatsDelta>,
    maxEntries: Int = 50
): List<GameStatsDelta> {
    val merged = ArrayList<GameStatsDelta>(existing.size + incoming.size)
    (existing + incoming).forEach { delta ->
        val index = merged.indexOfFirst {
            it.key == delta.key &&
                GameStatsPeriods.weekKey(it.lastPlayedAtMs) == GameStatsPeriods.weekKey(delta.lastPlayedAtMs) &&
                GameStatsPeriods.monthKey(it.lastPlayedAtMs) == GameStatsPeriods.monthKey(delta.lastPlayedAtMs)
        }
        if (index < 0) {
            merged += delta
        } else {
            val a = merged[index]
            merged[index] = a.copy(
                serial = delta.serial ?: a.serial,
                title = delta.title.ifBlank { a.title },
                durationMs = a.durationMs + delta.durationMs,
                sessions = a.sessions + delta.sessions,
                lastPlayedAtMs = maxOf(a.lastPlayedAtMs, delta.lastPlayedAtMs),
                firstPlay = a.firstPlay || delta.firstPlay
            )
        }
    }
    return if (merged.size > maxEntries) merged.takeLast(maxEntries) else merged
}

internal fun normalizeGameDeviceKey(soc: String?): String {
    val normalized = soc
        .orEmpty()
        .trim()
        .uppercase(Locale.ROOT)
        .replace(Regex("[^A-Z0-9_-]"), "_")
        .trim('_')
        .take(32)
    return normalized.ifBlank { "UNKNOWN" }
}

internal fun mergeTopGameStats(first: TopGameStat, second: TopGameStat): TopGameStat {
    val dominant = if (second.totalPlayTimeMs > first.totalPlayTimeMs) second else first
    return dominant.copy(
        totalPlayTimeMs = first.totalPlayTimeMs + second.totalPlayTimeMs,
        sessions = first.sessions + second.sessions,
        players = first.players + second.players,
        lastPlayedAtMs = maxOf(first.lastPlayedAtMs ?: 0L, second.lastPlayedAtMs ?: 0L).takeIf { it > 0L },
        groupKeys = (first.groupKeys + second.groupKeys).distinct()
    )
}

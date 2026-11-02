package com.sbro.emucorex.data.gamedb

import android.content.Context
import android.util.Log
import com.sbro.emucorex.core.EmulatorStorage
import java.io.File
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject

/** One edited GameDB line for one game. Everything here is optional; defaults mean "as bundled". */
data class GameDbFixOverride(
    val enabled: Boolean = true,
    val value: String? = null,
    val added: Boolean = false
) {
    val customized: Boolean get() = !enabled || added || value != null
}

/** Every edit the user made for a single disc serial. */
data class GameDbGameOverride(
    val fixes: Map<String, GameDbFixOverride> = emptyMap()
) {
    val isEmpty: Boolean get() = fixes.isEmpty()

    val disabledCount: Int get() = fixes.count { !it.value.enabled }

    val addedCount: Int get() = fixes.count { it.value.added }

    val customizedCount: Int get() = fixes.count { it.value.customized }

    fun isEnabled(id: String): Boolean = fixes[id]?.enabled ?: true

    fun valueOf(id: String): String? = fixes[id]?.value

    fun isAdded(id: String): Boolean = fixes[id]?.added == true

    fun isCustomized(id: String): Boolean = fixes[id]?.customized == true

    fun addedKeys(): Set<String> = fixes.filterValues { it.added }.keys

    fun edited(section: GameDbFixSection): Map<String, GameDbFixOverride> =
        fixes.filterKeys { GameDbFixId.sectionOf(it) == section }

    internal fun withFix(id: String, override: GameDbFixOverride): GameDbGameOverride {
        val updated = LinkedHashMap(fixes)
        if (!override.customized) updated.remove(id) else updated[id] = override
        return copy(fixes = updated)
    }

    internal fun withoutFix(id: String): GameDbGameOverride =
        if (fixes.containsKey(id)) copy(fixes = fixes - id) else this
}

/**
 * Persists the per-game GameDB edits made from the GameDB browser.
 *
 * The JSON file is the source of truth; the YAML overlay the emulator core reads is generated from
 * it by [GameDbOverrideRenderer]. Keeping the user decision in JSON is what makes the edits survive
 * app restarts, app updates and settings backups.
 */
class GameDbUserOverrides(context: Context) {
    private val appContext = context.applicationContext
    private val file = File(EmulatorStorage.appStateDir(appContext), FILE_NAME)
    private val lock = Any()

    private val gamesState = MutableStateFlow(readGames(file))

    /** Per-serial edits, keyed by upper-case disc serial. */
    val games: StateFlow<Map<String, GameDbGameOverride>> = gamesState.asStateFlow()

    fun get(serial: String): GameDbGameOverride = gamesState.value[serial.uppercase()] ?: EMPTY

    fun isEnabled(serial: String, id: String): Boolean = get(serial).isEnabled(id)

    fun setEnabled(serial: String, id: String, enabled: Boolean) {
        update(serial) { current ->
            val existing = current.fixes[id]
            when {
                existing == null -> if (enabled) current else current.withFix(id, GameDbFixOverride(enabled = false))
                else -> current.withFix(id, existing.copy(enabled = enabled))
            }
        }
    }

    /** Picking a value also turns the line on: choosing one only makes sense to apply it. */
    fun setValue(serial: String, id: String, value: String?) {
        update(serial) { current ->
            val existing = current.fixes[id] ?: GameDbFixOverride()
            current.withFix(id, existing.copy(enabled = true, value = value?.takeIf { it.isNotBlank() }))
        }
    }

    fun addFix(serial: String, id: String, value: String?) {
        update(serial) { current ->
            val existing = current.fixes[id] ?: GameDbFixOverride()
            current.withFix(id, existing.copy(enabled = true, value = value, added = true))
        }
    }

    fun removeFix(serial: String, id: String) {
        update(serial) { it.withoutFix(id) }
    }

    fun resetGame(serial: String) {
        update(serial) { EMPTY }
    }

    fun resetAll() {
        val changed = synchronized(lock) {
            if (gamesState.value.isEmpty()) return@synchronized false
            gamesState.value = emptyMap()
            true
        }
        if (changed) persist()
    }

    private fun update(serial: String, block: (GameDbGameOverride) -> GameDbGameOverride) {
        val key = serial.uppercase()
        val changed = synchronized(lock) {
            val current = gamesState.value
            val updatedGame = block(current[key] ?: EMPTY)
            val normalized = if (updatedGame.isEmpty) current - key else current + (key to updatedGame)
            if (normalized == current) return@synchronized false
            gamesState.value = normalized
            true
        }
        if (changed) persist()
    }

    // region rendering stamp

    /** App-version stamp of the last generated overlay, so it is only rebuilt when it can differ. */
    fun renderStamp(): String? = readRoot(file)?.optString(RENDER_STAMP_KEY)?.takeIf { it.isNotBlank() }

    fun markRendered(stamp: String) {
        synchronized(lock) {
            val root = readRoot(file) ?: JSONObject()
            root.put("version", VERSION)
            root.put(RENDER_STAMP_KEY, stamp)
            root.put(GAMES_KEY, gamesToJson(gamesState.value))
            writeRoot(root)
        }
    }

    // endregion

    // region backup

    fun exportJson(): JSONObject = synchronized(lock) {
        JSONObject().apply {
            put("version", VERSION)
            put(GAMES_KEY, gamesToJson(gamesState.value))
        }
    }

    fun importJson(json: JSONObject) {
        val imported = gamesFromJson(json.optJSONObject(GAMES_KEY))
        synchronized(lock) {
            gamesState.value = imported
        }
        persist()
    }

    // endregion

    private fun persist() {
        synchronized(lock) {
            // The render stamp is deliberately dropped: any edit invalidates the generated overlay,
            // so the next start has to rebuild it even if the app dies before that happens.
            val root = JSONObject().apply {
                put("version", VERSION)
                put(GAMES_KEY, gamesToJson(gamesState.value))
            }
            writeRoot(root)
        }
    }

    private fun writeRoot(root: JSONObject) {
        runCatching {
            file.parentFile?.mkdirs()
            val temp = File(file.parentFile, "${file.name}.tmp")
            temp.writeText(root.toString())
            if (!temp.renameTo(file)) {
                file.writeText(root.toString())
                temp.delete()
            }
        }.onFailure { error ->
            Log.e(TAG, "Unable to persist GameDB overrides", error)
        }
    }

    companion object {
        private const val TAG = "GameDbUserOverrides"
        private const val FILE_NAME = "gamedb-overrides.json"
        private const val VERSION = 1
        private const val GAMES_KEY = "games"
        private const val RENDER_STAMP_KEY = "renderStamp"
        private const val FIXES_KEY = "fixes"
        private const val ENABLED_KEY = "enabled"
        private const val VALUE_KEY = "value"
        private const val ADDED_KEY = "added"

        internal val EMPTY = GameDbGameOverride()

        internal fun gamesToJson(games: Map<String, GameDbGameOverride>): JSONObject =
            JSONObject().apply {
                games.forEach { (serial, game) ->
                    if (game.isEmpty) return@forEach
                    put(serial, JSONObject().apply {
                        put(FIXES_KEY, JSONObject().apply {
                            game.fixes.forEach { (id, override) ->
                                put(id, JSONObject().apply {
                                    if (!override.enabled) put(ENABLED_KEY, false)
                                    override.value?.let { put(VALUE_KEY, it) }
                                    if (override.added) put(ADDED_KEY, true)
                                })
                            }
                        })
                    })
                }
            }

        internal fun gamesFromJson(json: JSONObject?): Map<String, GameDbGameOverride> {
            if (json == null) return emptyMap()
            val result = LinkedHashMap<String, GameDbGameOverride>()
            json.keys().forEach { serial ->
                val fixesJson = json.optJSONObject(serial)?.optJSONObject(FIXES_KEY) ?: return@forEach
                val fixes = LinkedHashMap<String, GameDbFixOverride>()
                fixesJson.keys().forEach { id ->
                    val entry = fixesJson.optJSONObject(id) ?: return@forEach
                    val override = GameDbFixOverride(
                        enabled = entry.optBoolean(ENABLED_KEY, true),
                        value = entry.optString(VALUE_KEY).takeIf { it.isNotBlank() },
                        added = entry.optBoolean(ADDED_KEY, false)
                    )
                    if (override.customized) fixes[id] = override
                }
                if (fixes.isNotEmpty()) result[serial.uppercase()] = GameDbGameOverride(fixes)
            }
            return result
        }

        internal fun readGames(file: File): Map<String, GameDbGameOverride> =
            runCatching { gamesFromJson(readRoot(file)?.optJSONObject(GAMES_KEY)) }.getOrDefault(emptyMap())

        internal fun readRoot(file: File): JSONObject? = runCatching {
            if (file.isFile) JSONObject(file.readText()) else null
        }.getOrNull()
    }
}

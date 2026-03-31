package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class GameLibraryCacheSnapshot(
    val games: List<GameItem>,
    val savedAt: Long
)

class GameLibraryCacheRepository(context: Context) {

    private val appContext = context.applicationContext
    private val compatibilityRepository = Pcsx2CompatibilityRepository(appContext)
    private val cacheFile = File(appContext.filesDir, "library/game_library_cache.json")
    private val lock = Any()

    fun load(rootPath: String): List<GameItem> {
        return loadSnapshot(rootPath).games
    }

    fun loadSnapshot(rootPath: String): GameLibraryCacheSnapshot {
        synchronized(lock) {
            if (!cacheFile.exists()) return GameLibraryCacheSnapshot(emptyList(), 0L)

            return runCatching {
                val rootObject = JSONObject(cacheFile.readText())
                val libraries = rootObject.optJSONArray("libraries")
                    ?: return GameLibraryCacheSnapshot(emptyList(), 0L)
                val libraryObject = (0 until libraries.length())
                    .mapNotNull { index -> libraries.optJSONObject(index) }
                    .firstOrNull { it.optString("root_path") == rootPath }
                    ?: return GameLibraryCacheSnapshot(emptyList(), 0L)

                val games = libraryObject.optJSONArray("games") ?: JSONArray()
                val parsedGames = buildList {
                    for (index in 0 until games.length()) {
                        val game = games.optJSONObject(index) ?: continue
                        val serial = game.optString("serial").takeIf { it.isNotBlank() }
                        add(
                            GameItem(
                                title = game.optString("title", game.optString("file_name")),
                                path = game.optString("path"),
                                fileName = game.optString("file_name"),
                                fileSize = game.optLong("file_size"),
                                lastModified = game.optLong("last_modified"),
                                coverArtPath = game.optString("cover_art_path").takeIf { it.isNotBlank() },
                                serial = serial,
                                pcsx2Compatibility = compatibilityRepository.findBySerial(serial)
                            )
                        )
                    }
                }
                GameLibraryCacheSnapshot(
                    games = parsedGames,
                    savedAt = libraryObject.optLong("saved_at", 0L)
                )
            }.getOrDefault(GameLibraryCacheSnapshot(emptyList(), 0L))
        }
    }

    fun save(rootPath: String, games: List<GameItem>) {
        synchronized(lock) {
            runCatching {
                cacheFile.parentFile?.mkdirs()
                val rootObject = if (cacheFile.exists()) {
                    JSONObject(cacheFile.readText())
                } else {
                    JSONObject()
                }

                val existingLibraries = rootObject.optJSONArray("libraries") ?: JSONArray()
                val updatedLibraries = JSONArray()
                for (index in 0 until existingLibraries.length()) {
                    val libraryObject = existingLibraries.optJSONObject(index) ?: continue
                    if (libraryObject.optString("root_path") != rootPath) {
                        updatedLibraries.put(libraryObject)
                    }
                }

                updatedLibraries.put(
                    JSONObject().apply {
                        put("root_path", rootPath)
                        put("saved_at", System.currentTimeMillis())
                        put(
                            "games",
                            JSONArray().apply {
                                games.forEach { game ->
                                    put(
                                        JSONObject().apply {
                                            put("title", game.title)
                                            put("path", game.path)
                                            put("file_name", game.fileName)
                                            put("file_size", game.fileSize)
                                            put("last_modified", game.lastModified)
                                            put("cover_art_path", game.coverArtPath ?: "")
                                            put("serial", game.serial ?: "")
                                        }
                                    )
                                }
                            }
                        )
                    }
                )

                rootObject.put("libraries", updatedLibraries)
                cacheFile.writeText(rootObject.toString())
            }
        }
    }
}

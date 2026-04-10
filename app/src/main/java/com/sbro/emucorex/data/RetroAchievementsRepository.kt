package com.sbro.emucorex.data

import android.content.Context
import androidx.core.net.toUri
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.NativeApp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

data class RetroAchievementEntry(
    val id: Long,
    val title: String,
    val description: String,
    val points: Int,
    val category: Int,
    val type: Int,
    val rarity: Float,
    val rarityHardcore: Float,
    val earnedSoftcore: Boolean,
    val earnedHardcore: Boolean,
    val badgeUrl: String?,
    val badgeLockedUrl: String?
) {
    val isEarned: Boolean
        get() = earnedSoftcore || earnedHardcore
}

data class RetroAchievementGameData(
    val gameId: Long,
    val title: String,
    val gameImageUrl: String?,
    val achievements: List<RetroAchievementEntry>,
    val resolvedOnly: Boolean
) {
    val earnedCount: Int
        get() = achievements.count { it.isEarned }

    val totalCount: Int
        get() = achievements.size

    val earnedPoints: Int
        get() = achievements.filter { it.isEarned }.sumOf { it.points }

    val totalPoints: Int
        get() = achievements.sumOf { it.points }
}

data class LibraryUnlockedAchievement(
    val gameTitle: String,
    val gamePath: String,
    val achievement: RetroAchievementEntry
)

class RetroAchievementsRepository(private val context: Context) {

    private val preferences = AppPreferences(context)
    private val gameRepository = GameRepository()

    suspend fun loadGameData(gamePath: String): RetroAchievementGameData? {
        if (gamePath.isBlank()) return null

        return withContext(Dispatchers.IO) {
            buildLookupCandidates(gamePath).firstNotNullOfOrNull { candidate ->
                safeLoadGameDataCandidate(candidate)
            }
        }
    }

    suspend fun loadUnlockedAchievementsFromLibrary(
        onProgress: (processed: Int, total: Int) -> Unit = { _, _ -> }
    ): List<LibraryUnlockedAchievement> {
        val libraryPath = preferences.gamePath.first().orEmpty()
        if (libraryPath.isBlank()) return emptyList()
        val cacheKey = buildUnlockedCacheKey(libraryPath = libraryPath)
        unlockedAchievementsCache[cacheKey]?.let { cached ->
            onProgress(cached.size, cached.size)
            return cached
        }

        val games = if (libraryPath.startsWith("content://")) {
            gameRepository.scanDirectoryFromUri(libraryPath.toUri(), context)
        } else {
            gameRepository.scanDirectory(libraryPath, context)
        }

        val result = mutableListOf<LibraryUnlockedAchievement>()
        games.forEachIndexed { index, game ->
            onProgress(index + 1, games.size)
            val gameData = runCatching { loadGameData(game.path) }.getOrNull() ?: return@forEachIndexed
            gameData.achievements
                .asSequence()
                .filter { it.isEarned }
                .mapTo(result) { achievement ->
                    LibraryUnlockedAchievement(
                        gameTitle = game.title,
                        gamePath = game.path,
                        achievement = achievement
                    )
                }
        }

        val sorted = result.sortedWith(
            compareByDescending<LibraryUnlockedAchievement> { it.achievement.earnedHardcore }
                .thenBy { it.gameTitle.lowercase() }
                .thenBy { it.achievement.title.lowercase() }
        )
        unlockedAchievementsCache[cacheKey] = sorted
        return sorted
    }

    suspend fun peekCachedUnlockedAchievementsFromLibrary(): List<LibraryUnlockedAchievement>? {
        val libraryPath = preferences.gamePath.first().orEmpty()
        if (libraryPath.isBlank()) return null
        return unlockedAchievementsCache[buildUnlockedCacheKey(libraryPath)]
    }

    private fun buildLookupCandidates(gamePath: String): List<String> {
        if (!gamePath.startsWith("content://")) {
            return listOf(DocumentPathResolver.resolveFilePath(context, gamePath) ?: gamePath)
        }

        val displayName = runCatching { DocumentPathResolver.getDisplayName(context, gamePath) }.getOrNull()
            ?.takeIf { it.isNotBlank() }
        val contentCandidate = displayName?.let { "$gamePath|$it" } ?: gamePath
        val resolvedPath = DocumentPathResolver.resolveFilePath(context, gamePath)
        return listOfNotNull(contentCandidate, resolvedPath).distinct()
    }

    private suspend fun safeLoadGameDataCandidate(candidate: String): RetroAchievementGameData? {
        if (candidate.isBlank()) return null

        return nativeLoadMutex.withLock {
            runCatching {
                NativeApp.getRetroAchievementGameData(candidate)?.toRetroAchievementGameData()
            }.getOrNull()
        }
    }

    companion object {
        private val unlockedAchievementsCache = mutableMapOf<String, List<LibraryUnlockedAchievement>>()
        private val nativeLoadMutex = Mutex()

        fun invalidateUnlockedAchievementsCache() {
            unlockedAchievementsCache.clear()
        }

        private fun buildUnlockedCacheKey(libraryPath: String): String {
            return libraryPath.trim()
        }
    }
}

private fun String.toRetroAchievementGameData(): RetroAchievementGameData? {
    return runCatching {
        val root = JSONObject(this)
        val achievements = root.optJSONArray("achievements").toRetroAchievementEntries()
        RetroAchievementGameData(
            gameId = root.optLong("gameId"),
            title = root.optString("title"),
            gameImageUrl = root.optString("gameImageUrl").takeIf { it.isNotBlank() },
            achievements = achievements,
            resolvedOnly = root.optBoolean("resolvedOnly")
        )
    }.getOrNull()
}

private fun JSONArray?.toRetroAchievementEntries(): List<RetroAchievementEntry> {
    if (this == null) return emptyList()
    return buildList {
        for (index in 0 until length()) {
            val item = optJSONObject(index) ?: continue
            add(
                RetroAchievementEntry(
                    id = item.optLong("id"),
                    title = item.optString("title"),
                    description = item.optString("description"),
                    points = item.optInt("points"),
                    category = item.optInt("category"),
                    type = item.optInt("type"),
                    rarity = item.optDouble("rarity").toFloat(),
                    rarityHardcore = item.optDouble("rarityHardcore").toFloat(),
                    earnedSoftcore = item.optBoolean("earnedSoftcore"),
                    earnedHardcore = item.optBoolean("earnedHardcore"),
                    badgeUrl = item.optString("badgeUrl").takeIf { it.isNotBlank() },
                    badgeLockedUrl = item.optString("badgeLockedUrl").takeIf { it.isNotBlank() }
                )
            )
        }
    }
}

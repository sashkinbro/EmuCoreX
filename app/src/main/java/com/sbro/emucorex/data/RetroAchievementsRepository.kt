package com.sbro.emucorex.data

import android.content.Context
import androidx.core.net.toUri
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.NativeApp
import org.json.JSONArray
import org.json.JSONObject
import kotlinx.coroutines.flow.first

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
    val achievements: List<RetroAchievementEntry>
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

    fun loadGameData(gamePath: String): RetroAchievementGameData? {
        val resolvedPath = DocumentPathResolver.resolveFilePath(context, gamePath) ?: gamePath
        val raw = NativeApp.getRetroAchievementGameData(resolvedPath) ?: return null
        return raw.toRetroAchievementGameData()
    }

    suspend fun loadUnlockedAchievementsFromLibrary(
        onProgress: (processed: Int, total: Int) -> Unit = { _, _ -> }
    ): List<LibraryUnlockedAchievement> {
        val libraryPath = preferences.gamePath.first().orEmpty()
        if (libraryPath.isBlank()) return emptyList()

        val games = if (libraryPath.startsWith("content://")) {
            gameRepository.scanDirectoryFromUri(libraryPath.toUri(), context)
        } else {
            gameRepository.scanDirectory(libraryPath, context)
        }

        val result = mutableListOf<LibraryUnlockedAchievement>()
        games.forEachIndexed { index, game ->
            onProgress(index + 1, games.size)
            val gameData = loadGameData(game.path) ?: return@forEachIndexed
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

        return result.sortedWith(
            compareByDescending<LibraryUnlockedAchievement> { it.achievement.earnedHardcore }
                .thenBy { it.gameTitle.lowercase() }
                .thenBy { it.achievement.title.lowercase() }
        )
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
            achievements = achievements
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

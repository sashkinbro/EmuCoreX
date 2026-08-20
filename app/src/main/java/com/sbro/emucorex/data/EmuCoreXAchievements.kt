package com.sbro.emucorex.data

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.google.android.gms.tasks.Task
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.sbro.emucorex.R
import kotlinx.coroutines.suspendCancellableCoroutine
import java.util.Locale
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

enum class EmuAchievementMetric {
    GamesPlayed,
    TotalPlayTimeMinutes,
    TotalSessions,
    ArcadeGames,
    LongestGameMinutes,
    GodOfWarMinutes,
    GtaSanAndreasMinutes,
    TekkenFiveMinutes
}

data class EmuAchievementDefinition(
    val id: String,
    val titleRes: Int,
    val descriptionRes: Int,
    val metric: EmuAchievementMetric,
    val target: Long,
    val points: Int,
    val hidden: Boolean = false
)

data class EmuAchievementState(
    val definition: EmuAchievementDefinition,
    val progress: Long,
    val unlockedAtMs: Long? = null
) {
    val unlocked: Boolean get() = unlockedAtMs != null
    val progressFraction: Float get() = (progress.toFloat() / definition.target.coerceAtLeast(1L)).coerceIn(0f, 1f)
}

object EmuCoreXAchievementCatalog {
    const val VERSION = 1

    val definitions = listOf(
        EmuAchievementDefinition("first_game", R.string.achievement_first_game_title, R.string.achievement_first_game_description, EmuAchievementMetric.GamesPlayed, 1, 10),
        EmuAchievementDefinition("five_games", R.string.achievement_five_games_title, R.string.achievement_five_games_description, EmuAchievementMetric.GamesPlayed, 5, 20),
        EmuAchievementDefinition("twenty_games", R.string.achievement_twenty_games_title, R.string.achievement_twenty_games_description, EmuAchievementMetric.GamesPlayed, 20, 50),
        EmuAchievementDefinition("one_hour", R.string.achievement_one_hour_title, R.string.achievement_one_hour_description, EmuAchievementMetric.TotalPlayTimeMinutes, 60, 15),
        EmuAchievementDefinition("ten_hours", R.string.achievement_ten_hours_title, R.string.achievement_ten_hours_description, EmuAchievementMetric.TotalPlayTimeMinutes, 600, 40),
        EmuAchievementDefinition("hundred_hours", R.string.achievement_hundred_hours_title, R.string.achievement_hundred_hours_description, EmuAchievementMetric.TotalPlayTimeMinutes, 6_000, 100),
        EmuAchievementDefinition("ten_sessions", R.string.achievement_ten_sessions_title, R.string.achievement_ten_sessions_description, EmuAchievementMetric.TotalSessions, 10, 20),
        EmuAchievementDefinition("arcade_debut", R.string.achievement_arcade_debut_title, R.string.achievement_arcade_debut_description, EmuAchievementMetric.ArcadeGames, 1, 25),
        EmuAchievementDefinition("dedicated", R.string.achievement_dedicated_title, R.string.achievement_dedicated_description, EmuAchievementMetric.LongestGameMinutes, 600, 60, hidden = true),
        EmuAchievementDefinition("god_of_war_secret", R.string.achievement_god_of_war_title, R.string.achievement_god_of_war_description, EmuAchievementMetric.GodOfWarMinutes, 120, 50, hidden = true),
        EmuAchievementDefinition("san_andreas_secret", R.string.achievement_san_andreas_title, R.string.achievement_san_andreas_description, EmuAchievementMetric.GtaSanAndreasMinutes, 120, 50, hidden = true),
        EmuAchievementDefinition("tekken_five_secret", R.string.achievement_tekken_five_title, R.string.achievement_tekken_five_description, EmuAchievementMetric.TekkenFiveMinutes, 90, 50, hidden = true)
    )
}

class EmuAchievementRepository(context: Context) {
    private val appContext = context.applicationContext
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()

    suspend fun evaluate(profile: PlayerProfile): List<EmuAchievementState> {
        if (auth.currentUser?.uid != profile.uid) return loadPublic(profile.uid, profile)
        return evaluateAndSync(profile.toAchievementSnapshot())
    }

    suspend fun evaluateCurrentProfile(): List<EmuAchievementState> {
        val uid = auth.currentUser?.uid ?: return emptyList()
        val profile = firestore.collection(USERS).document(uid).get().await().toAchievementSnapshot()
            ?: return emptyList()
        return evaluateAndSync(profile)
    }

    private suspend fun evaluateAndSync(snapshot: AchievementSnapshot): List<EmuAchievementState> {
        val uid = auth.currentUser?.uid ?: return emptyList()
        val unlockDocuments = firestore.collection(UNLOCKS).whereEqualTo("uid", uid).get().await().documents
        val unlocks = unlockDocuments.associate { it.getString("achievementId").orEmpty() to it.unlockedAtMs() }
        val candidates = EmuCoreXAchievementCatalog.definitions.filter { definition ->
            unlocks[definition.id] == null && snapshot.progressFor(definition.metric) >= definition.target
        }

        val newlyUnlocked = if (candidates.isEmpty()) emptyList() else firestore.runTransaction { transaction ->
            candidates.filter { definition ->
                val unlockRef = firestore.collection(UNLOCKS).document("${uid}_${definition.id}")
                if (transaction.get(unlockRef).exists()) return@filter false
                val progress = snapshot.progressFor(definition.metric)
                transaction.set(unlockRef, mapOf(
                    "uid" to uid,
                    "achievementId" to definition.id,
                    "unlockedAt" to FieldValue.serverTimestamp(),
                    "progress" to progress.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
                    "target" to definition.target.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
                    "catalogVersion" to EmuCoreXAchievementCatalog.VERSION,
                    "sourceGameSerial" to snapshot.lastSerial.orEmpty().take(32),
                    "visibility" to "public"
                ))
                val eventId = "achievement_${definition.id}"
                transaction.set(
                    firestore.collection(PUBLIC_PROFILES).document(uid).collection(FEED).document(eventId),
                    mapOf(
                        "uid" to uid,
                        "eventId" to eventId,
                        "type" to "achievement_unlocked",
                        "achievementId" to definition.id,
                        "visibility" to "public",
                        "createdAt" to FieldValue.serverTimestamp()
                    )
                )
                true
            }
        }.await()

        updateProgress(uid, snapshot)
        newlyUnlocked.forEach(::notifyUnlocked)
        val now = System.currentTimeMillis()
        val unlockedIds = unlocks.keys + newlyUnlocked.map { it.id }
        return EmuCoreXAchievementCatalog.definitions.map { definition ->
            EmuAchievementState(
                definition = definition,
                progress = snapshot.progressFor(definition.metric).coerceAtMost(definition.target),
                unlockedAtMs = unlocks[definition.id] ?: now.takeIf { definition.id in unlockedIds }
            )
        }
    }

    private suspend fun loadPublic(uid: String, profile: PlayerProfile): List<EmuAchievementState> {
        val unlocks = firestore.collection(UNLOCKS)
            .whereEqualTo("uid", uid)
            .whereEqualTo("visibility", "public")
            .get().await().documents
            .associate { it.getString("achievementId").orEmpty() to it.unlockedAtMs() }
        val snapshot = profile.toAchievementSnapshot()
        return EmuCoreXAchievementCatalog.definitions.map { definition ->
            EmuAchievementState(
                definition = definition,
                progress = if (definition.id in unlocks) definition.target else 0,
                unlockedAtMs = unlocks[definition.id]
            )
        }
    }

    private suspend fun updateProgress(uid: String, snapshot: AchievementSnapshot) {
        val batch = firestore.batch()
        EmuCoreXAchievementCatalog.definitions.forEach { definition ->
            val progress = snapshot.progressFor(definition.metric).coerceAtMost(definition.target)
            batch.set(
                firestore.collection(USERS).document(uid).collection(PROGRESS).document(definition.id),
                mapOf(
                    "achievementId" to definition.id,
                    "progress" to progress.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
                    "target" to definition.target.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
                    "updatedAt" to FieldValue.serverTimestamp()
                )
            )
        }
        batch.commit().await()
    }

    private fun notifyUnlocked(definition: EmuAchievementDefinition) {
        val manager = appContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(NotificationChannel(
                CHANNEL_ID,
                appContext.getString(R.string.achievement_notification_channel),
                NotificationManager.IMPORTANCE_DEFAULT
            ))
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(appContext, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return
        val notification = NotificationCompat.Builder(appContext, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(appContext.getString(R.string.achievement_unlocked_notification))
            .setContentText(appContext.getString(definition.titleRes))
            .setAutoCancel(true)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)
            .build()
        NotificationManagerCompat.from(appContext).notify(definition.id.hashCode(), notification)
    }

    private data class AchievementSnapshot(
        val gamesPlayed: Long,
        val totalPlayTimeMs: Long,
        val games: List<AchievementGame>,
        val lastSerial: String?
    ) {
        fun progressFor(metric: EmuAchievementMetric): Long = when (metric) {
            EmuAchievementMetric.GamesPlayed -> gamesPlayed
            EmuAchievementMetric.TotalPlayTimeMinutes -> totalPlayTimeMs / 60_000L
            EmuAchievementMetric.TotalSessions -> games.sumOf { it.sessions }
            EmuAchievementMetric.ArcadeGames -> games.count { it.serial.orEmpty().uppercase(Locale.ROOT).startsWith("NM") }.toLong()
            EmuAchievementMetric.LongestGameMinutes -> (games.maxOfOrNull { it.totalPlayTimeMs } ?: 0L) / 60_000L
            EmuAchievementMetric.GodOfWarMinutes -> minutesForTitle("god of war")
            EmuAchievementMetric.GtaSanAndreasMinutes -> minutesForTitle("san andreas")
            EmuAchievementMetric.TekkenFiveMinutes -> minutesForTitle("tekken 5")
        }

        private fun minutesForTitle(needle: String): Long = games
            .filter { it.title.lowercase(Locale.ROOT).contains(needle) }
            .sumOf { it.totalPlayTimeMs } / 60_000L
    }

    private data class AchievementGame(
        val title: String,
        val serial: String?,
        val totalPlayTimeMs: Long,
        val sessions: Long
    )

    private fun PlayerProfile.toAchievementSnapshot() = AchievementSnapshot(
        gamesPlayed = gamesPlayed.toLong(),
        totalPlayTimeMs = totalPlayTimeMs,
        games = games.map { AchievementGame(it.title, it.serial, it.totalPlayTimeMs, it.sessions.toLong()) },
        lastSerial = games.maxByOrNull { it.lastPlayedAtMs ?: 0L }?.serial
    )

    private fun DocumentSnapshot.toAchievementSnapshot(): AchievementSnapshot? {
        if (!exists()) return null
        val rawGames = get("games") as? Map<*, *> ?: emptyMap<String, Any>()
        val games = rawGames.values.mapNotNull { raw ->
            val map = raw as? Map<*, *> ?: return@mapNotNull null
            AchievementGame(
                title = map["title"] as? String ?: return@mapNotNull null,
                serial = map["serial"] as? String,
                totalPlayTimeMs = (map["totalPlayTimeMs"] as? Number)?.toLong() ?: 0L,
                sessions = (map["sessions"] as? Number)?.toLong() ?: 0L
            )
        }
        return AchievementSnapshot(
            gamesPlayed = getLong("gamesPlayed") ?: games.size.toLong(),
            totalPlayTimeMs = getLong("totalPlayTimeMs") ?: games.sumOf { it.totalPlayTimeMs },
            games = games,
            lastSerial = games.maxByOrNull { it.totalPlayTimeMs }?.serial
        )
    }

    private fun DocumentSnapshot.unlockedAtMs(): Long = getTimestamp("unlockedAt")?.toDate()?.time ?: 1L

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { continuation.resume(it) }
        addOnFailureListener { continuation.resumeWithException(it) }
        addOnCanceledListener { continuation.cancel() }
    }

    private companion object {
        const val USERS = "users"
        const val PUBLIC_PROFILES = "publicProfiles"
        const val UNLOCKS = "achievementUnlocks"
        const val PROGRESS = "achievementProgress"
        const val FEED = "feed"
        const val CHANNEL_ID = "emucorex_achievements"
    }
}

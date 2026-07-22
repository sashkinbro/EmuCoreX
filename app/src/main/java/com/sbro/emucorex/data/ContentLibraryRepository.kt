package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorBridge
import kotlinx.coroutines.flow.first
import java.util.Locale

data class SelectedGameIdentity(
    val serial: String?,
    val crc: String?
)

class ContentLibraryRepository(context: Context) {
    private val appContext = context.applicationContext
    private val preferences = AppPreferences(appContext)
    private val cache = GameLibraryCacheRepository(appContext)

    suspend fun loadGames(): List<GameItem> {
        val paths = preferences.gamePaths.first()
        if (paths.isEmpty()) return emptyList()
        val preferEnglish = preferences.preferEnglishGameTitles.first()
        return cache.loadSnapshot(GameLibraryCacheRepository.libraryKey(paths), preferEnglish)
            .games
            .filter { !it.serial.isNullOrBlank() }
            .sortedBy { it.title.lowercase(Locale.getDefault()) }
    }

    fun resolveIdentity(game: GameItem): SelectedGameIdentity {
        val metadata = runCatching { EmulatorBridge.getGameMetadata(game.path) }.getOrNull()
        val serial = metadata?.serial?.normalizeGameSerial() ?: game.serial?.normalizeGameSerial()
        val crc = metadata?.serialWithCrc
            ?.let(CRC_PATTERN::find)
            ?.value
            ?.uppercase(Locale.US)
        return SelectedGameIdentity(serial = serial, crc = crc)
    }

    private companion object {
        val CRC_PATTERN = Regex("(?i)(?<![0-9A-F])[0-9A-F]{8}(?![0-9A-F])")
    }
}

private fun String.normalizeGameSerial(): String? {
    val compact = trim().uppercase(Locale.US).replace(Regex("[-_ ]"), "")
    if (!compact.matches(Regex("[A-Z]{4}[0-9]{5}"))) return null
    return "${compact.take(4)}-${compact.drop(4)}"
}

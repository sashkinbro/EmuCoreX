package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import androidx.core.net.toUri
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import kotlinx.coroutines.flow.first

data class SaveStateSlotInfo(
    val slot: Int,
    val exists: Boolean,
    val fileName: String?,
    val sizeBytes: Long,
    val lastModified: Long
)

data class SaveStateEntryInfo(
    val absolutePath: String,
    val fileName: String,
    val serial: String,
    val crc: String?,
    val slot: Int,
    val sizeBytes: Long,
    val lastModified: Long,
    val gamePath: String?,
    val gameTitle: String,
    val coverArtPath: String?
) {
    val canLoad: Boolean get() = !gamePath.isNullOrBlank()
}

class SaveStateRepository(private val context: Context) {

    private val gameRepository = GameRepository()
    private val coverArtRepository = CoverArtRepository(context)
    private val preferences = AppPreferences(context)
    private val compatibilityRepository = Pcsx2CompatibilityRepository(context)

    fun listSlots(gamePath: String): List<SaveStateSlotInfo> {
        return (0 until 5).map { slot ->
            val path = runCatching { NativeApp.getSaveStatePathForFile(gamePath, slot) }.getOrNull()
            val file = path?.let(::File)
            SaveStateSlotInfo(
                slot = slot,
                exists = file?.exists() == true,
                fileName = file?.name,
                sizeBytes = file?.takeIf(File::exists)?.length() ?: 0L,
                lastModified = file?.takeIf(File::exists)?.lastModified() ?: 0L
            )
        }
    }

    suspend fun listEntries(filterGamePath: String? = null, filterGameTitle: String? = null): List<SaveStateEntryInfo> {
        return if (filterGamePath.isNullOrBlank()) {
            listAllEntriesFast()
        } else {
            listEntriesForGame(filterGamePath, filterGameTitle)
        }
    }

    suspend fun enrichGlobalEntries(entries: List<SaveStateEntryInfo>): List<SaveStateEntryInfo> {
        if (entries.isEmpty()) return entries
        val libraryIndex = loadLibraryIndex()
        return entries.map { entry ->
            if (entry.gamePath != null && !entry.coverArtPath.isNullOrBlank()) {
                entry
            } else {
                val match = libraryIndex.bySaveFileName[entry.fileName]
                    ?: libraryIndex.findBestMatch(serial = entry.serial, titleHint = entry.gameTitle)
                if (match == null) {
                    entry
                } else {
                    entry.copy(
                        gamePath = match.path,
                        gameTitle = match.title,
                        coverArtPath = match.coverArtPath ?: entry.coverArtPath
                    )
                }
            }
        }
    }

    fun deleteEntry(entry: SaveStateEntryInfo): Boolean {
        return runCatching {
            File(entry.absolutePath).takeIf(File::exists)?.delete() == true
        }.getOrDefault(false)
    }

    fun backupEntries(entries: List<SaveStateEntryInfo>, destination: Uri): Boolean {
        val contentResolver = context.contentResolver
        val existingEntries = entries.map { File(it.absolutePath) }.filter(File::exists)
        if (existingEntries.isEmpty()) return false
        return runCatching {
            contentResolver.openOutputStream(destination)?.use { output ->
                ZipOutputStream(output).use { zip ->
                    val manifest = JSONObject().put(
                        "entries",
                        JSONArray().apply {
                            existingEntries.forEach { file ->
                                put(JSONObject().put("fileName", file.name))
                            }
                        }
                    )
                    zip.putNextEntry(ZipEntry("manifest.json"))
                    zip.write(manifest.toString().toByteArray())
                    zip.closeEntry()
                    existingEntries.forEach { file ->
                        zip.putNextEntry(ZipEntry("slots/${file.name}"))
                        file.inputStream().use { it.copyTo(zip) }
                        zip.closeEntry()
                    }
                }
            } != null
        }.getOrDefault(false)
    }

    fun restoreStates(source: Uri): Boolean {
        val contentResolver = context.contentResolver
        return runCatching {
            contentResolver.openInputStream(source)?.use { input ->
                ZipInputStream(input).use { zip ->
                    generateSequence { zip.nextEntry }.forEach { entry ->
                        if (!entry.name.startsWith("slots/")) return@forEach
                        val targetName = entry.name.substringAfterLast('/')
                        val targetFile = File(EmulatorStorage.saveStatesDir(context), targetName)
                        targetFile.parentFile?.mkdirs()
                        targetFile.outputStream().use { output -> zip.copyTo(output) }
                        zip.closeEntry()
                    }
                }
            } != null
        }.getOrDefault(false)
    }

    fun getPreviewImagePath(entry: SaveStateEntryInfo): String? {
        val saveFile = File(entry.absolutePath)
        if (!saveFile.exists()) return null

        val previewDir = File(context.cacheDir, "save-previews").apply { mkdirs() }
        val previewFile = File(previewDir, "${entry.absolutePath.sha1()}.png")
        if (previewFile.exists() && previewFile.lastModified() >= saveFile.lastModified()) {
            return previewFile.absolutePath
        }

        return runCatching {
            val bytes = NativeApp.getSaveStateScreenshot(saveFile.absolutePath) ?: return@runCatching null
            FileOutputStream(previewFile).use { output ->
                output.write(bytes)
            }
            previewFile.setLastModified(saveFile.lastModified())
            previewFile.absolutePath
        }.getOrNull()
    }

    private fun listAllEntriesFast(): List<SaveStateEntryInfo> {
        val files = EmulatorStorage.saveStatesDir(context)
            .listFiles()
            .orEmpty()
            .filter { it.isFile && isPrimarySaveState(it.name) }

        return files.mapNotNull { file ->
            val parsed = parseSaveStateName(file.name) ?: return@mapNotNull null
            parsed.toFastEntry(file)
        }.sortedWith(
            compareByDescending<SaveStateEntryInfo> { it.lastModified }
                .thenBy { it.gameTitle.lowercase(Locale.ROOT) }
                .thenBy { it.slot }
        )
    }

    private fun listEntriesForGame(gamePath: String, gameTitle: String?): List<SaveStateEntryInfo> {
        val fallbackInfo = resolveFallbackGameInfo(gamePath, gameTitle)
        val targetSerial = fallbackInfo.serial

        return listSlots(gamePath)
            .filter { it.exists }
            .mapNotNull { slot ->
                val absolutePath = NativeApp.getSaveStatePathForFile(gamePath, slot.slot) ?: return@mapNotNull null
                val file = File(absolutePath)
                if (!file.exists()) return@mapNotNull null

                val parsed = parseSaveStateName(file.name)
                val serialKey = parsed?.serial ?: targetSerial ?: return@mapNotNull null
                SaveStateEntryInfo(
                    absolutePath = absolutePath,
                    fileName = file.name,
                    serial = serialKey,
                    crc = parsed?.crc,
                    slot = slot.slot,
                    sizeBytes = slot.sizeBytes,
                    lastModified = slot.lastModified,
                    gamePath = gamePath,
                    gameTitle = fallbackInfo.title,
                    coverArtPath = fallbackInfo.coverArtPath
                        ?: coverArtRepository.findCachedCoverPath(serialKey)
                )
            }
            .sortedByDescending { it.lastModified }
    }

    private suspend fun loadLibraryIndex(): LibraryIndex {
        val libraryPath = preferences.gamePath.first()
        val libraryGames = when {
            libraryPath.isNullOrBlank() -> emptyList()
            libraryPath.startsWith("content://") -> gameRepository.scanDirectoryFromUri(libraryPath.toUri(), context)
            else -> gameRepository.scanDirectory(libraryPath, context)
        }

        val bySerial = linkedMapOf<String, GameItem>()
        val byTitle = linkedMapOf<String, MutableList<GameItem>>()
        val byPath = linkedMapOf<String, GameItem>()
        val bySaveFileName = linkedMapOf<String, GameItem>()

        libraryGames.forEach { game ->
            byPath[game.path] = game
            game.serial.normalizeSerialKey()?.let { serial ->
                bySerial.putIfAbsent(serial, game)
            }
            normalizeTitleKey(game.title)?.let { key ->
                byTitle.getOrPut(key) { mutableListOf() }.add(game)
            }
            buildSaveFileNamesForGame(game.path).forEach { fileName ->
                bySaveFileName.putIfAbsent(fileName, game)
            }
        }

        return LibraryIndex(
            bySerial = bySerial,
            byTitle = byTitle,
            byPath = byPath,
            bySaveFileName = bySaveFileName
        )
    }

    private fun resolveFallbackGameInfo(gamePath: String, gameTitle: String?): FallbackGameInfo {
        val resolvedPath = DocumentPathResolver.resolveFilePath(context, gamePath) ?: gamePath
        val metadata = runCatching { EmulatorBridge.getGameMetadata(resolvedPath) }.getOrNull()
        val normalizedSerial = metadata?.serial.normalizeSerialKey()
        val cleanPassedTitle = gameTitle.takeIf(::isUsableTitle)
        val cleanMetadataTitle = metadata?.title?.takeIf(::isUsableTitle)
        val title = when {
            cleanPassedTitle != null -> cleanPassedTitle
            cleanMetadataTitle != null -> cleanMetadataTitle
            else -> DocumentPathResolver.getDisplayName(context, gamePath).substringBeforeLast('.')
        }.ifBlank {
            compatibilityRepository.findBySerial(normalizedSerial)?.title
                ?: DocumentPathResolver.getDisplayName(context, gamePath).substringBeforeLast('.')
        }
        val coverArtPath = gameRepository.findCoverForGame(
            path = gamePath,
            context = context,
            serial = normalizedSerial,
            title = title
        ) ?: coverArtRepository.findCachedCoverPath(normalizedSerial)
        return FallbackGameInfo(
            serial = normalizedSerial,
            title = title,
            coverArtPath = coverArtPath
        )
    }

    private fun ParsedSaveStateName.toFastEntry(file: File): SaveStateEntryInfo {
        val titleHint = compatibilityRepository.findBySerial(serial)?.title
        val resolvedTitle = titleHint ?: serial
        return SaveStateEntryInfo(
            absolutePath = file.absolutePath,
            fileName = file.name,
            serial = serial,
            crc = crc,
            slot = slot,
            sizeBytes = file.length(),
            lastModified = file.lastModified(),
            gamePath = null,
            gameTitle = resolvedTitle,
            coverArtPath = coverArtRepository.findCachedCoverPath(serial)
        )
    }

    private fun parseSaveStateName(fileName: String): ParsedSaveStateName? {
        val match = SAVE_STATE_FILE_REGEX.matchEntire(fileName) ?: return null
        return ParsedSaveStateName(
            serial = match.groupValues[1].normalizeSerialKey() ?: return null,
            crc = match.groupValues[2].uppercase(Locale.ROOT),
            slot = match.groupValues[3].toIntOrNull() ?: return null
        )
    }

    private fun isPrimarySaveState(fileName: String): Boolean {
        return fileName.endsWith(".p2s", ignoreCase = true) &&
            !fileName.endsWith(".backup", ignoreCase = true) &&
            !fileName.contains(".resume.", ignoreCase = true)
    }

    private fun buildSaveFileNamesForGame(gamePath: String): List<String> {
        return (0 until 5).mapNotNull { slot ->
            runCatching { NativeApp.getSaveStatePathForFile(gamePath, slot) }
                .getOrNull()
                ?.let(::File)
                ?.name
        }
    }

    private fun String?.normalizeSerialKey(): String? {
        if (this.isNullOrBlank()) return null
        val cleanSerial = this.trim().uppercase(Locale.ROOT)
        val regex = Regex("([A-Z]{4})[^A-Z0-9]*([0-9]{3})[^A-Z0-9]*([0-9]{2})")
        val altRegex = Regex("([A-Z]{4})[^A-Z0-9]*([0-9]{5})")
        regex.find(cleanSerial)?.let { match ->
            return "${match.groupValues[1]}-${match.groupValues[2]}${match.groupValues[3]}"
        }
        altRegex.find(cleanSerial)?.let { match ->
            return "${match.groupValues[1]}-${match.groupValues[2]}"
        }
        return cleanSerial.replace(Regex("[^A-Z0-9_-]"), "").takeIf { it.isNotBlank() }
    }

    private fun normalizeTitleKey(value: String?): String? {
        return value
            ?.trim()
            ?.lowercase(Locale.ROOT)
            ?.replace(Regex("[^\\p{L}\\p{N}]+"), " ")
            ?.replace(Regex("\\s+"), " ")
            ?.trim()
            ?.takeIf { it.isNotBlank() }
    }

    private fun isUsableTitle(value: String?): Boolean {
        if (value.isNullOrBlank()) return false
        val trimmed = value.trim()
        return !trimmed.startsWith("content://") &&
            !trimmed.startsWith("primary%3A", ignoreCase = true) &&
            !trimmed.contains("%2F", ignoreCase = true) &&
            !trimmed.contains("%3A", ignoreCase = true)
    }

    private data class ParsedSaveStateName(
        val serial: String,
        val crc: String,
        val slot: Int
    )

    private data class FallbackGameInfo(
        val serial: String?,
        val title: String,
        val coverArtPath: String?
    )

    private data class LibraryIndex(
        val bySerial: Map<String, GameItem>,
        val byTitle: Map<String, List<GameItem>>,
        val byPath: Map<String, GameItem>,
        val bySaveFileName: Map<String, GameItem>
    ) {
        fun findBestMatch(serial: String?, titleHint: String?): GameItem? {
            serial?.let { bySerial[it] }?.let { return it }
            val titleKey = titleHint
                ?.trim()
                ?.lowercase(Locale.ROOT)
                ?.replace(Regex("[^\\p{L}\\p{N}]+"), " ")
                ?.replace(Regex("\\s+"), " ")
                ?.trim()
            return titleKey?.let { byTitle[it]?.firstOrNull() }
        }
    }

    private companion object {
        private val SAVE_STATE_FILE_REGEX = Regex("""^(.+?) \(([0-9A-Fa-f]{8})\)\.(\d{2})\.p2s$""")
    }
}

private fun String.sha1(): String {
    val digest = MessageDigest.getInstance("SHA-1")
    return digest.digest(toByteArray()).joinToString("") { "%02x".format(it) }
}

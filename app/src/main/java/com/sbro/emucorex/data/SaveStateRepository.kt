package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import kotlinx.coroutines.flow.first
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

private const val AUTO_SAVE_SLOT = 0
private val SAVE_STATE_SLOTS = 0..10
private val SAVE_STATE_FILE_REGEX = Regex("""^(.+?) \(([0-9A-Fa-f]{8})\)\.(\d{2})\.p2s$""")
private const val SAVE_STATE_VERSION_ENTRY = "PCSX2 Savestate Version.id"
private const val CURRENT_SAVE_STATE_MAJOR = 0x9A59
private const val AETHERSX2_SAVE_STATE_MAJOR = 0x9A2C
private const val NETHERSX2_SAVE_STATE_MAJOR = 0x9A34
private const val MAX_IMPORT_FILE_BYTES = 1_073_741_824L
private const val MAX_IMPORT_ARCHIVE_BYTES = 4_294_967_296L
private const val MAX_IMPORT_CANDIDATES = 500

enum class SaveStateImportSource {
    NETHERSX2,
    ARMSX2,
    EMUCOREX,
    AUTO
}

enum class SaveStateImportFormat {
    CURRENT,
    AETHERSX2,
    NETHERSX2,
    UNKNOWN
}

data class SaveStateImportCandidate(
    val sourceEntryName: String?,
    val originalFileName: String,
    val targetFileName: String,
    val targetSlot: Int,
    val serial: String?,
    val format: SaveStateImportFormat,
    val sizeBytes: Long
)

data class SaveStateImportPreview(
    val stagedFilePath: String,
    val displayName: String,
    val requestedSource: SaveStateImportSource,
    val detectedSource: SaveStateImportSource,
    val candidates: List<SaveStateImportCandidate>,
    val skippedCount: Int,
    val incompatibleCount: Int
) {
    val importableCount: Int get() = candidates.size
}

data class SaveStateImportResult(
    val importedCount: Int,
    val failedCount: Int
) {
    val isSuccess: Boolean get() = importedCount > 0 && failedCount == 0
}

internal data class ParsedExternalSaveStateName(
    val serial: String,
    val crc: String,
    val slot: Int
)

internal fun parseExternalSaveStateName(fileName: String): ParsedExternalSaveStateName? {
    val cleanName = fileName.substringAfterLast('/').substringAfterLast('\\')
    val match = SAVE_STATE_FILE_REGEX.matchEntire(cleanName) ?: return null
    return ParsedExternalSaveStateName(
        serial = match.groupValues[1].normalizeSaveStateSerialKey() ?: return null,
        crc = match.groupValues[2].uppercase(Locale.ROOT),
        slot = match.groupValues[3].toIntOrNull()?.takeIf { it in SAVE_STATE_SLOTS } ?: return null
    )
}

internal fun saveStateFormatForVersion(version: Int?): SaveStateImportFormat {
    if (version == null) return SaveStateImportFormat.UNKNOWN
    return when (version ushr 16) {
        CURRENT_SAVE_STATE_MAJOR -> if (version.toUInt() <= 0x9A590000u) {
            SaveStateImportFormat.CURRENT
        } else {
            SaveStateImportFormat.UNKNOWN
        }
        AETHERSX2_SAVE_STATE_MAJOR -> SaveStateImportFormat.AETHERSX2
        NETHERSX2_SAVE_STATE_MAJOR -> SaveStateImportFormat.NETHERSX2
        else -> SaveStateImportFormat.UNKNOWN
    }
}

internal fun allocateSaveStateSlot(preferred: Int?, occupied: Set<Int>): Int? {
    if (preferred != null && preferred in SAVE_STATE_SLOTS && preferred !in occupied) return preferred
    return (1..10).firstOrNull { it !in occupied }
}

internal fun findFallbackSaveStateFile(
    files: Array<out File>,
    gameSerial: String?,
    slot: Int
): File? {
    val normalizedSerial = gameSerial.normalizeSaveStateSerialKey() ?: return null
    return files.asSequence()
        .filter(File::isFile)
        .mapNotNull { file ->
            val match = SAVE_STATE_FILE_REGEX.matchEntire(file.name) ?: return@mapNotNull null
            val serial = match.groupValues[1].normalizeSaveStateSerialKey() ?: return@mapNotNull null
            val fileSlot = match.groupValues[3].toIntOrNull() ?: return@mapNotNull null
            file.takeIf { serial == normalizedSerial && fileSlot == slot }
        }
        .maxByOrNull(File::lastModified)
}

private fun String?.normalizeSaveStateSerialKey(): String? {
    if (this.isNullOrBlank()) return null
    val cleanSerial = trim().uppercase(Locale.ROOT)
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

data class SaveStateSlotInfo(
    val slot: Int,
    val exists: Boolean,
    val absolutePath: String?,
    val fileName: String?,
    val sizeBytes: Long,
    val lastModified: Long,
    val isAutoSave: Boolean = slot == AUTO_SAVE_SLOT
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
    val coverArtPath: String?,
    val isAutoSave: Boolean = slot == AUTO_SAVE_SLOT
) {
    val canLoad: Boolean get() = !gamePath.isNullOrBlank()
}

class SaveStateRepository(private val context: Context) {

    private val gameRepository = GameRepository()
    private val gameLibraryCacheRepository = GameLibraryCacheRepository(context)
    private val coverArtRepository = CoverArtRepository(context)
    private val preferences = AppPreferences(context)
    private val compatibilityRepository = Pcsx2CompatibilityRepository(context)

    fun listSlots(gamePath: String, gameSerial: String? = null): List<SaveStateSlotInfo> {
        val saveStateFiles = saveStatesDir().listFiles().orEmpty()
        return SAVE_STATE_SLOTS.map { slot ->
            val path = runCatching { NativeApp.getSaveStatePathForFile(gamePath, slot) }.getOrNull()
            val file = resolveSaveStateFile(
                nativePath = path,
                saveStateFiles = saveStateFiles,
                gameSerial = gameSerial,
                slot = slot
            )
            SaveStateSlotInfo(
                slot = slot,
                exists = file?.exists() == true,
                absolutePath = file?.absolutePath,
                fileName = file?.name,
                sizeBytes = file?.takeIf(File::exists)?.length() ?: 0L,
                lastModified = file?.takeIf(File::exists)?.lastModified() ?: 0L
            )
        }
    }

    private fun resolveSaveStateFile(
        nativePath: String?,
        saveStateFiles: Array<out File>,
        gameSerial: String?,
        slot: Int
    ): File? {
        nativePath?.let(::File)?.takeIf(File::exists)?.let { return it }
        // GameList can still be warming up after a cold launch. The library already knows the
        // serial, so use it to locate the slot without waiting for native metadata discovery.
        return findFallbackSaveStateFile(saveStateFiles, gameSerial, slot)
    }

    fun findLatestSlot(gamePath: String, gameSerial: String? = null): Int? {
        return listSlots(gamePath, gameSerial)
            .filter { it.exists }
            .maxByOrNull { it.lastModified }
            ?.slot
    }

    fun listEntries(
        filterGamePath: String? = null,
        filterGameTitle: String? = null,
        filterGameSerial: String? = null
    ): List<SaveStateEntryInfo> {
        return if (filterGamePath.isNullOrBlank()) {
            listAllEntriesFast()
        } else {
            listEntriesForGame(filterGamePath, filterGameTitle, filterGameSerial)
        }
    }

    suspend fun enrichGlobalEntries(entries: List<SaveStateEntryInfo>): List<SaveStateEntryInfo> {
        if (entries.isEmpty()) return entries
        val libraryIndex = loadLibraryIndex()
        return entries.map { entry ->
            if (entry.gamePath != null && !entry.coverArtPath.isNullOrBlank()) {
                entry
            } else {
                val match = libraryIndex.findBestMatch(serial = entry.serial, titleHint = entry.gameTitle)
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

    fun analyzeImport(
        source: Uri,
        requestedSource: SaveStateImportSource,
        gamePath: String? = null,
        gameSerial: String? = null
    ): SaveStateImportPreview {
        val displayName = readDisplayName(source)
        val importDir = importCacheDir()
        val stagedFile = File(importDir, "save-import-${UUID.randomUUID()}.bin")
        try {
            context.contentResolver.openInputStream(source)?.use { input ->
                stagedFile.outputStream().use { output ->
                    input.copyBoundedTo(output, MAX_IMPORT_ARCHIVE_BYTES)
                }
            } ?: error("Unable to open the selected file")

            return ZipFile(stagedFile).use { zip ->
                val directState = zip.getEntry(SAVE_STATE_VERSION_ENTRY)
                val detectedSource = detectImportSource(zip, directState != null)
                val rawCandidates = if (directState != null) {
                    listOf(
                        RawImportCandidate(
                            entryName = null,
                            fileName = displayName,
                            sizeBytes = stagedFile.length(),
                            version = zip.getInputStream(directState).use(::readSaveStateVersion)
                        )
                    )
                } else {
                    collectArchiveCandidates(zip, requestedSource)
                }

                buildImportPreview(
                    stagedFile = stagedFile,
                    displayName = displayName,
                    requestedSource = requestedSource,
                    detectedSource = detectedSource,
                    rawCandidates = rawCandidates,
                    gamePath = gamePath,
                    gameSerial = gameSerial
                )
            }
        } catch (error: Throwable) {
            stagedFile.delete()
            throw error
        }
    }

    fun importStates(preview: SaveStateImportPreview): SaveStateImportResult {
        val stagedFile = File(preview.stagedFilePath)
        val importDir = importCacheDir().canonicalFile
        require(stagedFile.canonicalFile.parentFile == importDir) { "Invalid staged import path" }
        if (!stagedFile.isFile) return SaveStateImportResult(importedCount = 0, failedCount = preview.candidates.size)

        var imported = 0
        var failed = 0
        try {
            ZipFile(stagedFile).use { zip ->
                preview.candidates.forEach { candidate ->
                    val target = File(saveStatesDir(), candidate.targetFileName)
                    if (target.exists()) {
                        failed++
                        return@forEach
                    }
                    val temporary = File(target.parentFile, ".${target.name}.${UUID.randomUUID()}.importing")
                    val success = runCatching {
                        target.parentFile?.mkdirs()
                        val input = candidate.sourceEntryName?.let { entryName ->
                            zip.getEntry(entryName)?.let(zip::getInputStream)
                        } ?: stagedFile.inputStream()
                        requireNotNull(input) { "Save-state entry disappeared" }.use { source ->
                            temporary.outputStream().use { output ->
                                source.copyBoundedTo(output, MAX_IMPORT_FILE_BYTES)
                            }
                        }
                        check(!target.exists()) { "Target slot is no longer empty" }
                        check(temporary.renameTo(target)) { "Unable to finalize imported save state" }
                    }.isSuccess
                    if (success) imported++ else {
                        temporary.delete()
                        failed++
                    }
                }
            }
        } finally {
            stagedFile.delete()
        }
        return SaveStateImportResult(importedCount = imported, failedCount = failed)
    }

    fun discardImport(preview: SaveStateImportPreview?) {
        val path = preview?.stagedFilePath ?: return
        runCatching {
            val file = File(path).canonicalFile
            if (file.parentFile == importCacheDir().canonicalFile) file.delete()
        }
    }

    private fun buildImportPreview(
        stagedFile: File,
        displayName: String,
        requestedSource: SaveStateImportSource,
        detectedSource: SaveStateImportSource,
        rawCandidates: List<RawImportCandidate>,
        gamePath: String?,
        gameSerial: String?
    ): SaveStateImportPreview {
        val occupiedNames = saveStatesDir().listFiles().orEmpty().filter(File::isFile).mapTo(mutableSetOf()) { it.name }
        val occupiedSlots = if (gamePath.isNullOrBlank()) {
            mutableMapOf<String, MutableSet<Int>>()
        } else {
            mutableMapOf("selected" to listSlots(gamePath, gameSerial).filter { it.exists }.mapTo(mutableSetOf()) { it.slot })
        }
        val normalizedGameSerial = gameSerial.normalizeSaveStateSerialKey()
        var skipped = 0
        var incompatible = 0
        val candidates = mutableListOf<SaveStateImportCandidate>()

        rawCandidates.take(MAX_IMPORT_CANDIDATES).forEach { raw ->
            val format = saveStateFormatForVersion(raw.version)
            if (format == SaveStateImportFormat.UNKNOWN || raw.sizeBytes !in 1..MAX_IMPORT_FILE_BYTES) {
                incompatible++
                return@forEach
            }
            val parsed = parseExternalSaveStateName(raw.fileName)
            if (normalizedGameSerial != null && parsed?.serial != null && parsed.serial != normalizedGameSerial) {
                skipped++
                return@forEach
            }

            val slotKey = if (gamePath.isNullOrBlank()) {
                parsed?.let { "${it.serial}|${it.crc}" }
            } else {
                "selected"
            }
            if (slotKey == null) {
                skipped++
                return@forEach
            }
            val slots = occupiedSlots.getOrPut(slotKey) {
                occupiedNames.mapNotNullTo(mutableSetOf()) { existing ->
                    parseExternalSaveStateName(existing)?.takeIf { "${it.serial}|${it.crc}" == slotKey }?.slot
                }
            }
            val targetSlot = allocateSaveStateSlot(parsed?.slot, slots)
            if (targetSlot == null) {
                skipped++
                return@forEach
            }
            val targetName = if (!gamePath.isNullOrBlank()) {
                runCatching { NativeApp.getSaveStatePathForFile(gamePath, targetSlot) }
                    .getOrNull()
                    ?.let(::File)
                    ?.name
                    ?.takeIf { it.endsWith(".p2s", ignoreCase = true) }
                    ?: parsed?.let { "${it.serial} (${it.crc}).${targetSlot.toString().padStart(2, '0')}.p2s" }
            } else {
                parsed?.let { "${it.serial} (${it.crc}).${targetSlot.toString().padStart(2, '0')}.p2s" }
            }
            if (targetName.isNullOrBlank() || targetName in occupiedNames) {
                skipped++
                return@forEach
            }
            occupiedNames += targetName
            slots += targetSlot
            candidates += SaveStateImportCandidate(
                sourceEntryName = raw.entryName,
                originalFileName = raw.fileName,
                targetFileName = targetName,
                targetSlot = targetSlot,
                serial = parsed?.serial ?: normalizedGameSerial,
                format = format,
                sizeBytes = raw.sizeBytes
            )
        }
        skipped += (rawCandidates.size - MAX_IMPORT_CANDIDATES).coerceAtLeast(0)

        return SaveStateImportPreview(
            stagedFilePath = stagedFile.absolutePath,
            displayName = displayName,
            requestedSource = requestedSource,
            detectedSource = detectedSource,
            candidates = candidates,
            skippedCount = skipped,
            incompatibleCount = incompatible
        )
    }

    private fun collectArchiveCandidates(zip: ZipFile, requestedSource: SaveStateImportSource): List<RawImportCandidate> {
        return zip.entries().asSequence()
            .filterNot(ZipEntry::isDirectory)
            .filter { entry ->
                val normalized = entry.name.replace('\\', '/').trimStart('/')
                val isState = normalized.endsWith(".p2s", ignoreCase = true)
                isState && when (requestedSource) {
                    SaveStateImportSource.EMUCOREX -> normalized.startsWith("slots/", ignoreCase = true)
                    SaveStateImportSource.ARMSX2 -> normalized.startsWith("files/sstates/", ignoreCase = true) ||
                        normalized.startsWith("sstates/", ignoreCase = true)
                    SaveStateImportSource.NETHERSX2 -> normalized.startsWith("sstates/", ignoreCase = true) ||
                        normalized.startsWith("savestates/", ignoreCase = true) || '/' !in normalized
                    SaveStateImportSource.AUTO -> true
                }
            }
            .take(MAX_IMPORT_CANDIDATES + 1)
            .map { entry ->
                val version = zip.getInputStream(entry).use { nestedInput ->
                    readNestedSaveStateVersion(nestedInput)
                }
                RawImportCandidate(
                    entryName = entry.name,
                    fileName = entry.name.substringAfterLast('/').substringAfterLast('\\'),
                    sizeBytes = entry.size,
                    version = version
                )
            }
            .toList()
    }

    private fun detectImportSource(zip: ZipFile, directState: Boolean): SaveStateImportSource {
        if (directState) return SaveStateImportSource.AUTO
        val names = zip.entries().asSequence().map { it.name.replace('\\', '/') }.toList()
        return when {
            names.any { it.equals("armsx2-backup.json", ignoreCase = true) || it.startsWith("files/sstates/", ignoreCase = true) } ->
                SaveStateImportSource.ARMSX2
            names.any { it.equals("manifest.json", ignoreCase = true) && names.any { name -> name.startsWith("slots/", ignoreCase = true) } } ->
                SaveStateImportSource.EMUCOREX
            names.any { it.startsWith("sstates/", ignoreCase = true) || it.startsWith("savestates/", ignoreCase = true) } ->
                SaveStateImportSource.NETHERSX2
            else -> SaveStateImportSource.AUTO
        }
    }

    private fun readDisplayName(uri: Uri): String {
        return runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            }
        }.getOrNull()?.takeIf { it.isNotBlank() } ?: uri.lastPathSegment?.substringAfterLast('/') ?: "save-state.p2s"
    }

    private fun importCacheDir(): File = File(context.cacheDir, "save-state-imports").apply { mkdirs() }

    private data class RawImportCandidate(
        val entryName: String?,
        val fileName: String,
        val sizeBytes: Long,
        val version: Int?
    )

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
        val files = saveStatesDir()
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

    private fun saveStatesDir(): File {
        return EmulatorStorage.saveStatesDir(context, preferences.getEmulatorDataPathSync())
    }

    private fun listEntriesForGame(
        gamePath: String,
        gameTitle: String?,
        gameSerial: String?
    ): List<SaveStateEntryInfo> {
        val fallbackInfo = resolveFallbackGameInfo(gamePath, gameTitle, gameSerial)
        val targetSerial = fallbackInfo.serial

        return listSlots(gamePath, targetSerial)
            .filter { it.exists }
            .mapNotNull { slot ->
                val file = slot.absolutePath?.let(::File)?.takeIf(File::exists) ?: return@mapNotNull null
                val absolutePath = file.absolutePath

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
                        ?: coverArtRepository.findCachedCoverPath(serialKey),
                    isAutoSave = slot.isAutoSave
                )
            }
            .sortedByDescending { it.lastModified }
    }

    private suspend fun loadLibraryIndex(): LibraryIndex {
        val libraryPaths = preferences.gamePaths.first()
        val cachedLibraryGames = when {
            libraryPaths.isEmpty() -> emptyList()
            else -> gameLibraryCacheRepository.loadSnapshot(GameLibraryCacheRepository.libraryKey(libraryPaths)).games
        }
        val recentGames = preferences.recentGames.first().map { recent ->
            GameItem(
                title = recent.title.ifBlank { recent.path.substringAfterLast('/').substringBeforeLast('.') },
                path = recent.path,
                fileName = recent.path.substringAfterLast('/'),
                fileSize = 0L,
                lastModified = recent.lastPlayedAt,
                coverArtPath = null,
                serial = recent.serial,
                pcsx2Compatibility = null
            )
        }
        val libraryGames = (recentGames + cachedLibraryGames)
            .distinctBy { it.path }

        val bySerial = linkedMapOf<String, GameItem>()
        val byTitle = linkedMapOf<String, MutableList<GameItem>>()
        val byPath = linkedMapOf<String, GameItem>()

        libraryGames.forEach { game ->
            byPath[game.path] = game
            game.serial.normalizeSerialKey()?.let { serial ->
                bySerial.putIfAbsent(serial, game)
            }
            normalizeTitleKey(game.title)?.let { key ->
                byTitle.getOrPut(key) { mutableListOf() }.add(game)
            }
        }

        return LibraryIndex(
            bySerial = bySerial,
            byTitle = byTitle,
            byPath = byPath
        )
    }

    private fun resolveFallbackGameInfo(
        gamePath: String,
        gameTitle: String?,
        gameSerial: String? = null
    ): FallbackGameInfo {
        val resolvedPath = DocumentPathResolver.resolveFilePath(context, gamePath) ?: gamePath
        val metadata = runCatching { EmulatorBridge.getGameMetadata(resolvedPath) }.getOrNull()
        val normalizedSerial = gameSerial.normalizeSerialKey() ?: metadata?.serial.normalizeSerialKey()
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
            coverArtPath = coverArtRepository.findCachedCoverPath(serial),
            isAutoSave = slot == AUTO_SAVE_SLOT
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

    private fun String?.normalizeSerialKey(): String? {
        return normalizeSaveStateSerialKey()
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
        val byPath: Map<String, GameItem>
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

}

private fun String.sha1(): String {
    val digest = MessageDigest.getInstance("SHA-1")
    return digest.digest(toByteArray()).joinToString("") { "%02x".format(it) }
}

private fun readNestedSaveStateVersion(input: InputStream): Int? {
    return runCatching {
        ZipInputStream(input).use { zip ->
            var version: Int? = null
            while (true) {
                val entry = zip.nextEntry ?: break
                if (!entry.isDirectory && entry.name == SAVE_STATE_VERSION_ENTRY) {
                    version = readSaveStateVersion(zip)
                    break
                }
                zip.closeEntry()
            }
            version
        }
    }.getOrNull()
}

private fun readSaveStateVersion(input: InputStream): Int? {
    val bytes = ByteArray(4)
    var offset = 0
    while (offset < bytes.size) {
        val read = input.read(bytes, offset, bytes.size - offset)
        if (read <= 0) return null
        offset += read
    }
    return (bytes[0].toInt() and 0xff) or
        ((bytes[1].toInt() and 0xff) shl 8) or
        ((bytes[2].toInt() and 0xff) shl 16) or
        ((bytes[3].toInt() and 0xff) shl 24)
}

private fun InputStream.copyBoundedTo(output: java.io.OutputStream, maxBytes: Long): Long {
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var total = 0L
    while (true) {
        val read = read(buffer)
        if (read < 0) break
        total += read
        require(total <= maxBytes) { "Selected save-state file is too large" }
        output.write(buffer, 0, read)
    }
    return total
}

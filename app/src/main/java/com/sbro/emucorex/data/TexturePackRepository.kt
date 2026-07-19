package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import java.io.File
import java.io.InputStream
import java.util.Locale
import java.util.UUID
import java.util.zip.ZipInputStream
import kotlinx.coroutines.flow.first

data class TexturePackInfo(
    val serial: String,
    val gameTitle: String?,
    val replacementCount: Int,
    val dumpCount: Int,
    val sizeBytes: Long,
    val lastModifiedAt: Long
)

data class TexturePackSummary(
    val rootPath: String,
    val packs: List<TexturePackInfo>,
    val totalReplacementCount: Int,
    val totalDumpCount: Int,
    val totalSizeBytes: Long
)

data class TextureImportResult(
    val success: Boolean,
    val importedFiles: Int = 0,
    val importedSerials: Set<String> = emptySet()
)

class TexturePackRepository(
    private val context: Context,
    private val preferences: AppPreferences
) {
    private val textureExtensions = setOf("png", "dds")
    private val serialPattern = Regex("[A-Z]{4}[-_ ]?\\d{5}", RegexOption.IGNORE_CASE)
    private val compatibilityRepository = Pcsx2CompatibilityRepository(context.applicationContext)
    private val libraryCacheRepository = GameLibraryCacheRepository(context.applicationContext)

    suspend fun listPacks(): TexturePackSummary {
        val root = texturesRoot()
        val libraryTitles = loadLibraryTitlesBySerial()
        val packs = root.listFiles()
            ?.asSequence()
            ?.filter { it.isDirectory }
            ?.mapNotNull { folder -> buildPackInfo(folder, libraryTitles) }
            ?.sortedWith(
                compareBy<TexturePackInfo> { (it.gameTitle ?: it.serial).lowercase(Locale.US) }
                    .thenBy { it.serial }
            )
            ?.toList()
            .orEmpty()

        return TexturePackSummary(
            rootPath = root.absolutePath,
            packs = packs,
            totalReplacementCount = packs.sumOf { it.replacementCount },
            totalDumpCount = packs.sumOf { it.dumpCount },
            totalSizeBytes = packs.sumOf { it.sizeBytes }
        )
    }

    fun importPackZip(uri: Uri): TextureImportResult {
        val displayName = displayName(uri)
        val input = runCatching { context.contentResolver.openInputStream(uri) }.getOrNull()
            ?: return TextureImportResult(success = false)
        return input.use { importPackZip(it, displayName) }
    }

    internal fun importPackZip(input: InputStream, displayName: String?): TextureImportResult {
        val stagingRoot = File(context.cacheDir, "texture-import-${UUID.randomUUID()}")
        val fallbackSerial = findSerial(displayName)
        val importedSerials = linkedSetOf<String>()
        val stagedFiles = linkedSetOf<String>()
        var entryCount = 0
        var totalBytes = 0L

        return try {
            stagingRoot.mkdirs()
            val canonicalStagingRoot = stagingRoot.canonicalFile
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    try {
                        entryCount++
                        require(entryCount <= MAX_ARCHIVE_ENTRIES) { "Texture archive contains too many entries" }
                        if (entry.isDirectory) continue
                        val cleanParts = cleanZipPath(entry.name) ?: error("Invalid texture archive path")
                        if (cleanParts.isEmpty() || !isTextureFile(cleanParts.last())) continue

                        val serial = serialFromParts(cleanParts) ?: fallbackSerial ?: continue
                        val relativeParts = replacementRelativePath(cleanParts, serial)
                        if (relativeParts.isEmpty()) continue
                        val stagedRoot = File(canonicalStagingRoot, serial)
                        val stagedTarget = safeChild(stagedRoot, relativeParts)
                            ?: error("Invalid texture archive path")
                        stagedTarget.parentFile?.mkdirs()
                        stagedTarget.outputStream().use { output ->
                            val copied = zip.copyToWithLimit(output, MAX_TEXTURE_FILE_BYTES)
                            totalBytes += copied
                            require(totalBytes <= MAX_ARCHIVE_BYTES) { "Texture archive is too large" }
                        }
                        stagedFiles += stagedTarget.canonicalPath
                        importedSerials += serial
                    } finally {
                        zip.closeEntry()
                    }
                }
            }

            if (stagedFiles.isEmpty()) {
                TextureImportResult(success = false)
            } else {
                stagedFiles.forEach { stagedPath ->
                    val staged = File(stagedPath)
                    val serial = staged.relativeTo(canonicalStagingRoot).invariantSeparatorsPath.substringBefore('/')
                    val relative = staged.relativeTo(File(canonicalStagingRoot, serial)).invariantSeparatorsPath
                    val target = safeChild(replacementsDir(serial), relative.split('/'))
                        ?: error("Invalid staged texture path")
                    target.parentFile?.mkdirs()
                    staged.copyTo(target, overwrite = true)
                }
                TextureImportResult(
                    success = true,
                    importedFiles = stagedFiles.size,
                    importedSerials = importedSerials
                )
            }
        } catch (_: Exception) {
            TextureImportResult(success = false)
        } finally {
            stagingRoot.deleteRecursively()
        }
    }

    fun deletePack(serial: String): Boolean {
        val normalized = normalizeSerial(serial) ?: return false
        val directory = existingGameDir(normalized) ?: return false
        return !directory.exists() || directory.deleteRecursively()
    }

    fun clearDumps(serial: String): Boolean {
        val normalized = normalizeSerial(serial) ?: return false
        val game = existingGameDir(normalized) ?: return false
        if (!game.exists()) return true
        val dumps = File(game, "dumps")
        if (!dumps.exists()) return true
        return dumps.deleteRecursively() && dumps.mkdirs()
    }

    private fun buildPackInfo(folder: File, libraryTitles: Map<String, String>): TexturePackInfo? {
        val serial = normalizeSerial(folder.name) ?: return null
        val replacementDir = File(folder, "replacements")
        val dumpDir = File(folder, "dumps")
        val replacementFiles = textureFiles(replacementDir)
        val dumpFiles = textureFiles(dumpDir)
        val allFiles = replacementFiles + dumpFiles
        return TexturePackInfo(
            serial = serial,
            gameTitle = libraryTitles[serial]
                ?: compatibilityRepository.findBySerial(serial)?.title
                    ?.trim()
                    ?.takeIf { it.isNotBlank() && !it.equals(serial, ignoreCase = true) },
            replacementCount = replacementFiles.size,
            dumpCount = dumpFiles.size,
            sizeBytes = allFiles.sumOf { it.length() },
            lastModifiedAt = allFiles.maxOfOrNull { it.lastModified() } ?: folder.lastModified()
        )
    }

    private suspend fun loadLibraryTitlesBySerial(): Map<String, String> {
        val paths = preferences.gamePaths.first()
        if (paths.isEmpty()) return emptyMap()
        return libraryCacheRepository
            .loadSnapshot(GameLibraryCacheRepository.libraryKey(paths))
            .games
            .mapNotNull { game ->
                val serial = game.serial?.let(::normalizeSerial) ?: return@mapNotNull null
                val title = game.title.trim().takeIf { it.isNotBlank() && !it.equals(serial, ignoreCase = true) }
                    ?: return@mapNotNull null
                serial to title
            }
            .toMap()
    }

    private fun texturesRoot(): File {
        return EmulatorStorage.texturesDir(context, preferences.getEmulatorDataPathSync())
    }

    private fun gameDir(serial: String): File {
        return File(texturesRoot(), serial).apply { mkdirs() }
    }

    private fun existingGameDir(serial: String): File? {
        val root = texturesRoot().canonicalFile
        val target = File(root, serial).canonicalFile
        return target.takeIf { it.parentFile == root }
    }

    private fun replacementsDir(serial: String): File {
        return File(gameDir(serial), "replacements").apply { mkdirs() }
    }

    private fun dumpsDir(serial: String): File {
        return File(gameDir(serial), "dumps").apply { mkdirs() }
    }

    private fun textureFiles(root: File): List<File> {
        if (!root.exists()) return emptyList()
        return root.walkTopDown()
            .filter { it.isFile && isTextureFile(it.name) }
            .toList()
    }

    private fun isTextureFile(name: String): Boolean {
        return name.substringAfterLast('.', "").lowercase(Locale.US) in textureExtensions
    }

    private fun cleanZipPath(path: String): List<String>? {
        if (path.startsWith('/') || path.startsWith('\\')) return null
        val rawParts = path.replace('\\', '/').split('/').map { it.trim() }
        if (rawParts.any { it == ".." || it.contains(':') || it.indexOf('\u0000') >= 0 }) return null
        return rawParts
            .filter { it.isNotEmpty() && it != "." }
    }

    private fun serialFromParts(parts: List<String>): String? {
        return parts.firstNotNullOfOrNull(::findSerial)
    }

    private fun findSerial(text: String?): String? {
        if (text.isNullOrBlank()) return null
        val match = serialPattern.find(text) ?: return null
        return normalizeSerial(match.value)
    }

    private fun normalizeSerial(raw: String): String? {
        val compact = raw.trim().uppercase(Locale.US).replace('_', '-').replace(' ', '-')
        val match = serialPattern.find(compact) ?: return null
        val value = match.value.replace('_', '-').replace(' ', '-')
        return if ('-' in value) value else "${value.take(4)}-${value.drop(4)}"
    }

    private fun replacementRelativePath(parts: List<String>, serial: String): List<String> {
        val normalizedParts = parts.map { part -> normalizeSerial(part) ?: part }
        val serialIndex = normalizedParts.indexOfFirst { it.equals(serial, ignoreCase = true) }
        val replacementIndex = normalizedParts.indexOfFirst { it.equals("replacements", ignoreCase = true) }
        val startIndex = maxOf(serialIndex, replacementIndex).let { if (it >= 0) it + 1 else 0 }
        return parts.drop(startIndex)
    }

    private fun safeChild(root: File, relativeParts: List<String>): File? {
        val rootCanonical = root.canonicalFile
        val target = relativeParts.fold(rootCanonical) { current, part -> File(current, part) }.canonicalFile
        return if (target.path.startsWith(rootCanonical.path + File.separator)) target else null
    }

    private fun displayName(uri: Uri): String? {
        return runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        cursor.getString(cursor.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
                    } else {
                        null
                    }
                }
        }.getOrNull() ?: uri.lastPathSegment
    }

    private companion object {
        const val MAX_ARCHIVE_ENTRIES = 20_000
        const val MAX_TEXTURE_FILE_BYTES = 512L * 1024L * 1024L
        const val MAX_ARCHIVE_BYTES = 4L * 1024L * 1024L * 1024L
    }
}

private fun InputStream.copyToWithLimit(output: java.io.OutputStream, limit: Long): Long {
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var copied = 0L
    while (true) {
        val read = read(buffer)
        if (read < 0) return copied
        copied += read
        require(copied <= limit) { "Texture file is too large" }
        output.write(buffer, 0, read)
    }
}

package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import androidx.core.net.toUri
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityEntry
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import java.io.File

data class GameItem(
    val title: String,
    val path: String,
    val fileName: String,
    val fileSize: Long,
    val lastModified: Long,
    val coverArtPath: String? = null,
    val serial: String? = null,
    val pcsx2Compatibility: Pcsx2CompatibilityEntry? = null
)

class GameRepository {

    companion object {
        private val SUPPORTED_EXTENSIONS = setOf("iso", "bin", "chd", "cso", "gz", "elf")
        private val COVER_EXTENSIONS = setOf("jpg", "jpeg", "png", "webp")
        private val COVER_DIRECTORY_NAMES = setOf("covers", "cover", "art", "artwork", "boxart", "box art")
    }

    fun scanDirectory(path: String, context: Context): List<GameItem> {
        return scanDirectory(path, context, emptyMap())
    }

    fun scanDirectory(
        path: String,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        shouldAbort: () -> Boolean = { false }
    ): List<GameItem> {
        val dir = File(path)
        if (!dir.exists() || !dir.isDirectory) return emptyList()
        return scanLocalDirectory(dir, context, cachedGamesByPath, shouldAbort).sortedBy { it.title.lowercase() }
    }

    fun scanDirectoryFromUri(uri: Uri, context: Context): List<GameItem> {
        return scanDirectoryFromUri(uri, context, emptyMap())
    }

    fun scanDirectoryFromUri(
        uri: Uri,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        shouldAbort: () -> Boolean = { false }
    ): List<GameItem> {
        val docFile = androidx.documentfile.provider.DocumentFile.fromTreeUri(context, uri)
            ?: return emptyList()
        return scanDocumentFile(docFile, context, cachedGamesByPath, shouldAbort).sortedBy { it.title.lowercase() }
    }

    fun formatFileSize(bytes: Long): String {
        return when {
            bytes >= 1_073_741_824 -> "%.2f GB".format(bytes / 1_073_741_824.0)
            bytes >= 1_048_576 -> "%.1f MB".format(bytes / 1_048_576.0)
            bytes >= 1024 -> "%.0f KB".format(bytes / 1024.0)
            else -> "$bytes B"
        }
    }

    fun findCoverForGame(
        path: String,
        context: Context,
        serial: String? = null,
        title: String? = null
    ): String? {
        return if (path.startsWith("content://")) {
            findDocumentCover(path, context, serial, title)
        } else {
            findLocalCover(path, context, serial, title)
        }
    }

    fun downloadCoverForGame(game: GameItem, context: Context): String? {
        if (!game.coverArtPath.isNullOrBlank()) {
            val coverFile = File(game.coverArtPath)
            if (coverFile.exists()) {
                return game.coverArtPath
            }
        }

        return game.serial?.let { serial ->
            CoverArtRepository(context).downloadCover(serial)
        }
    }

    private fun scanLocalDirectory(
        dir: File,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        shouldAbort: () -> Boolean
    ): List<GameItem> {
        val items = mutableListOf<GameItem>()
        val children = dir.listFiles().orEmpty()
        val coverCandidates = buildLocalCoverCandidates(children)
        val coverRepository = CoverArtRepository(context)
        val compatibilityRepository = Pcsx2CompatibilityRepository(context)

        children.forEach { file ->
            if (shouldAbort()) return items
            when {
                file.isDirectory && normalizeBaseName(file.name) !in COVER_DIRECTORY_NAMES -> {
                    items.addAll(scanLocalDirectory(file, context, cachedGamesByPath, shouldAbort))
                }

                file.isFile && file.extension.lowercase() in SUPPORTED_EXTENSIONS -> {
                    val cachedGame = cachedGamesByPath[file.absolutePath]
                    val canReuseCachedMetadata = cachedGame != null &&
                        cachedGame.fileSize == file.length() &&
                        cachedGame.lastModified == file.lastModified() &&
                        cachedGame.fileName == file.name &&
                        !cachedGame.serial.isNullOrBlank()
                    val metadata = if (canReuseCachedMetadata) {
                        com.sbro.emucorex.core.GameMetadata(cachedGame.title, cachedGame.serial)
                    } else {
                        EmulatorBridge.getGameMetadata(file.absolutePath)
                    }
                    if (BiosValidator.isLikelyBiosLibraryEntry(file.name, metadata.title, metadata.serial, file.length())) {
                        return@forEach
                    }

                    val title = metadata.title
                    items += GameItem(
                        title = title,
                        path = file.absolutePath,
                        fileName = file.name,
                        fileSize = file.length(),
                        lastModified = file.lastModified(),
                        coverArtPath = coverRepository.findCachedCoverPath(metadata.serial)
                            ?: cachedGame?.coverArtPath?.takeIf { File(it).exists() }
                            ?: coverCandidates[normalizeBaseName(file.nameWithoutExtension)]?.absolutePath
                            ?: coverCandidates[normalizeBaseName(cleanGameName(title))]?.absolutePath,
                        serial = metadata.serial,
                        pcsx2Compatibility = compatibilityRepository.findBySerial(metadata.serial)
                    )
                }
            }
        }

        return items
    }

    private fun scanDocumentFile(
        docFile: androidx.documentfile.provider.DocumentFile,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        shouldAbort: () -> Boolean
    ): List<GameItem> {
        val items = mutableListOf<GameItem>()
        val children = docFile.listFiles()
        val coverCandidates = buildDocumentCoverCandidates(children)
        val coverRepository = CoverArtRepository(context)
        val compatibilityRepository = Pcsx2CompatibilityRepository(context)

        for (file in children) {
            if (shouldAbort()) return items
            if (file.isDirectory) {
                if (normalizeBaseName(file.name.orEmpty()) !in COVER_DIRECTORY_NAMES) {
                    items.addAll(scanDocumentFile(file, context, cachedGamesByPath, shouldAbort))
                }
            } else if (file.isFile) {
                val name = file.name ?: continue
                val ext = name.substringAfterLast('.', "").lowercase()
                if (ext !in SUPPORTED_EXTENSIONS) continue

                val uriPath = file.uri.toString()
                val cachedGame = cachedGamesByPath[uriPath]
                val canReuseCachedMetadata = cachedGame != null &&
                    cachedGame.fileSize == file.length() &&
                    cachedGame.lastModified == file.lastModified() &&
                    cachedGame.fileName == name &&
                    !cachedGame.serial.isNullOrBlank()
                val metadata = if (canReuseCachedMetadata) {
                    com.sbro.emucorex.core.GameMetadata(cachedGame.title, cachedGame.serial)
                } else {
                    EmulatorBridge.getGameMetadata(uriPath)
                }

                if (BiosValidator.isLikelyBiosLibraryEntry(name, metadata.title, metadata.serial, file.length())) {
                    continue
                }

                val title = metadata.title
                items += GameItem(
                    title = title,
                    path = uriPath,
                    fileName = name,
                    fileSize = file.length(),
                    lastModified = file.lastModified(),
                    coverArtPath = coverRepository.findCachedCoverUri(metadata.serial)
                        ?: cachedGame?.coverArtPath
                        ?: coverCandidates[normalizeBaseName(name.substringBeforeLast('.'))]?.uri?.toString()
                        ?: coverCandidates[normalizeBaseName(cleanGameName(title))]?.uri?.toString(),
                    serial = metadata.serial,
                    pcsx2Compatibility = compatibilityRepository.findBySerial(metadata.serial)
                )
            }
        }

        return items
    }

    private fun findLocalCover(path: String, context: Context, serial: String?, title: String?): String? {
        val file = File(path)
        val parent = file.parentFile ?: return null
        val baseName = normalizeBaseName(file.nameWithoutExtension)
        val titleKey = normalizeBaseName(cleanGameName(title ?: EmulatorBridge.getGameTitle(path)))
        val coverCandidates = buildLocalCoverCandidates(parent.listFiles().orEmpty())
        return CoverArtRepository(context).findCachedCoverPath(serial)
            ?: coverCandidates[baseName]?.absolutePath
            ?: coverCandidates[titleKey]?.absolutePath
    }

    private fun findDocumentCover(path: String, context: Context, serial: String?, title: String?): String? {
        val uri = path.toUri()
        val document = androidx.documentfile.provider.DocumentFile.fromSingleUri(context, uri) ?: return null
        val parent = androidx.documentfile.provider.DocumentFile.fromTreeUri(
            context,
            uri.buildUpon().path(uri.path?.substringBeforeLast('/')).build()
        ) ?: return null
        val baseName = normalizeBaseName(document.name.orEmpty().substringBeforeLast('.'))
        val titleKey = normalizeBaseName(cleanGameName(title ?: document.name.orEmpty().substringBeforeLast('.')))
        val coverCandidates = buildDocumentCoverCandidates(parent.listFiles())
        return CoverArtRepository(context).findCachedCoverUri(serial)
            ?: coverCandidates[baseName]?.uri?.toString()
            ?: coverCandidates[titleKey]?.uri?.toString()
    }

    private fun normalizeBaseName(value: String): String {
        return value.lowercase()
            .replace('_', ' ')
            .replace('-', ' ')
            .replace(Regex("""\[[^]]*]|\([^)]*\)"""), " ")
            .replace(Regex("""\b(disc|disk|cd|dvd)\s*[0-9]+\b"""), " ")
            .replace(Regex("""\b\(?(usa|eur|europe|japan|jpn|beta|demo|proto|prototype|rev\s*[a-z0-9]+)\)?\b"""), " ")
            .replace(Regex("\\s+"), " ")
            .trim()
    }

    private fun cleanGameName(value: String): String {
        return value.substringBeforeLast('.')
            .replace(Regex("""\s+"""), " ")
            .trim()
    }

    private fun buildLocalCoverCandidates(children: Array<out File>): Map<String, File> {
        val directFiles = children.filter { it.isFile && it.extension.lowercase() in COVER_EXTENSIONS }
        val nestedCoverFiles = children
            .filter { it.isDirectory && normalizeBaseName(it.name) in COVER_DIRECTORY_NAMES }
            .flatMap { it.listFiles().orEmpty().asIterable() }
            .filter { it.isFile && it.extension.lowercase() in COVER_EXTENSIONS }

        return (directFiles + nestedCoverFiles).associateBy { normalizeBaseName(cleanGameName(it.nameWithoutExtension)) }
    }

    private fun buildDocumentCoverCandidates(
        children: Array<androidx.documentfile.provider.DocumentFile>
    ): Map<String, androidx.documentfile.provider.DocumentFile> {
        val directFiles = children
            .filter { it.isFile && it.name?.substringAfterLast('.', "")?.lowercase() in COVER_EXTENSIONS }
        val nestedCoverFiles = children
            .filter { it.isDirectory && normalizeBaseName(it.name.orEmpty()) in COVER_DIRECTORY_NAMES }
            .flatMap { it.listFiles().asIterable() }
            .filter { it.isFile && it.name?.substringAfterLast('.', "")?.lowercase() in COVER_EXTENSIONS }

        return (directFiles + nestedCoverFiles).associateBy {
            normalizeBaseName(cleanGameName(it.name.orEmpty().substringBeforeLast('.')))
        }
    }
}

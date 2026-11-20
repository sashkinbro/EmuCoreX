package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.SetupValidator
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
        private const val TAG = "GameRepository"
        private val SUPPORTED_EXTENSIONS = setOf("iso", "bin", "img", "mdf", "gz", "cso", "zso", "chd", "elf", "acgame")
        private val COVER_EXTENSIONS = setOf("jpg", "jpeg", "png", "webp")
        private val COVER_DIRECTORY_NAMES = setOf("covers", "cover", "art", "artwork", "boxart", "box art")
        private const val MAX_DOCUMENT_SCAN_DEPTH = 32
        private const val MAX_DOCUMENT_SCAN_ENTRIES = 20_000

        internal fun parentDocumentId(documentId: String): String? {
            val separatorIndex = documentId.lastIndexOf('/')
            if (separatorIndex >= 0) return documentId.substring(0, separatorIndex)

            val volumeSeparatorIndex = documentId.indexOf(':')
            return if (volumeSeparatorIndex >= 0 && volumeSeparatorIndex < documentId.lastIndex) {
                documentId.substring(0, volumeSeparatorIndex + 1)
            } else {
                null
            }
        }

        internal fun relativeDocumentSegments(rootDocumentId: String, targetDocumentId: String): List<String>? {
            if (targetDocumentId == rootDocumentId) return emptyList()
            val prefix = if (rootDocumentId.endsWith(':')) rootDocumentId else "$rootDocumentId/"
            if (!targetDocumentId.startsWith(prefix)) return null
            return targetDocumentId.removePrefix(prefix).split('/').filter(String::isNotBlank)
        }
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
        return scanRoot(path, context, cachedGamesByPath, shouldAbort = shouldAbort).games
    }

    /**
     * Scans one library root (plain folder or SAF tree URI) and reports whether the enumeration was
     * complete. [reuseBlankSerial] lets unchanged cached entries with no serial skip the native
     * metadata read (used by quick refreshes; full scans retry them).
     */
    fun scanRoot(
        root: String,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        reuseBlankSerial: Boolean = false,
        shouldAbort: () -> Boolean = { false }
    ): LibraryRootScan {
        val state = ScanState()
        val games = try {
            if (root.startsWith("content://")) {
                scanTreeRoot(root.toUri(), context, cachedGamesByPath, reuseBlankSerial, shouldAbort, state)
            } else {
                val dir = File(root)
                if (!dir.exists() || !dir.isDirectory || dir.listFiles() == null) {
                    state.complete = false
                    emptyList()
                } else {
                    val serialOverrides = AppPreferences(context).getGameSerialOverridesSync()
                    scanLocalDirectory(dir, context, cachedGamesByPath, serialOverrides, reuseBlankSerial, shouldAbort, state)
                }
            }
        } catch (error: Exception) {
            Log.w(TAG, "Library root scan failed: $root", error)
            state.complete = false
            emptyList()
        }
        return LibraryRootScan(root, games.sortedBy { it.title.lowercase() }, state.complete)
    }

    private class ScanState {
        @Volatile var complete = true
    }

    private fun canReuseCached(
        cached: GameItem?,
        size: Long,
        modified: Long,
        name: String,
        serialOverride: String?,
        reuseBlankSerial: Boolean
    ): Boolean {
        if (cached == null || cached.fileSize != size || cached.lastModified != modified || cached.fileName != name) {
            return false
        }
        if (cached.serial.isNullOrBlank()) {
            // Already attempted once; an override still forces a re-read.
            return reuseBlankSerial && serialOverride == null
        }
        return serialOverride == null || cached.serial.equals(serialOverride, ignoreCase = true)
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
        return scanRoot(uri.toString(), context, cachedGamesByPath, shouldAbort = shouldAbort).games
    }

    private fun scanTreeRoot(
        uri: Uri,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        reuseBlankSerial: Boolean,
        shouldAbort: () -> Boolean,
        state: ScanState
    ): List<GameItem> {
        if (!DocumentsContract.isTreeUri(uri)) {
            state.complete = false
            return emptyList()
        }
        val rootId = runCatching {
            if (DocumentsContract.isDocumentUri(context, uri)) DocumentsContract.getDocumentId(uri)
            else DocumentsContract.getTreeDocumentId(uri)
        }.getOrNull()
        if (rootId == null) {
            state.complete = false
            return emptyList()
        }
        val serialOverrides = AppPreferences(context).getGameSerialOverridesSync()
        return scanDocumentDirectory(
            uri, rootId, context, cachedGamesByPath, serialOverrides, reuseBlankSerial,
            shouldAbort, 0, DocumentScanBudget(), state
        )
    }

    fun findCoverForGame(
        path: String,
        context: Context,
        serial: String? = null,
        title: String? = null
    ): String? {
        return if (path.startsWith("content://")) {
            runCatching { findDocumentCover(path, context, serial, title) }
                .onFailure { error -> Log.w(TAG, "Unable to find cover for document URI: $path", error) }
                .getOrNull()
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
        serialOverrides: Map<String, String>,
        reuseBlankSerial: Boolean,
        shouldAbort: () -> Boolean,
        state: ScanState
    ): List<GameItem> {
        val items = mutableListOf<GameItem>()
        val children = dir.listFiles() ?: run {
            state.complete = false
            return items
        }
        val coverCandidates = buildLocalCoverCandidates(children)
        val coverRepository = CoverArtRepository(context)
        val customCoverRepository = CustomGameCoverRepository(context)
        val compatibilityRepository = Pcsx2CompatibilityRepository(context)
        val containsArcadeManifest = children.any {
            it.isFile && it.extension.equals("acgame", ignoreCase = true)
        }

        children.forEach { file ->
            if (shouldAbort()) return items
            when {
                file.isDirectory && normalizeBaseName(file.name) !in COVER_DIRECTORY_NAMES -> {
                    items.addAll(
                        scanLocalDirectory(file, context, cachedGamesByPath, serialOverrides, reuseBlankSerial, shouldAbort, state)
                    )
                }

                file.isFile && file.extension.lowercase() in SUPPORTED_EXTENSIONS -> {
                    // A .acgame manifest is the launchable item. Its ELF/disc/CHD files are
                    // private assets and must not appear as duplicate, unbootable games.
                    if (containsArcadeManifest && !file.extension.equals("acgame", ignoreCase = true)) {
                        return@forEach
                    }
                    val cachedGame = cachedGamesByPath[file.absolutePath]
                    val serialOverride = serialOverrides[file.absolutePath]
                    val canReuseCachedMetadata = canReuseCached(
                        cachedGame, file.length(), file.lastModified(), file.name, serialOverride, reuseBlankSerial
                    )
                    val metadata = if (canReuseCachedMetadata && cachedGame != null) {
                        com.sbro.emucorex.core.GameMetadata(cachedGame.title, cachedGame.serial)
                    } else {
                        EmulatorBridge.getGameMetadata(file.absolutePath)
                    }
                    if (BiosValidator.isLikelyBiosLibraryEntry(file.name, metadata.title, metadata.serial, file.length())) {
                        return@forEach
                    }

                    val cleanTitle = EmulatorBridge.cleanGameDisplayTitle(metadata.title, file.name)
                    val effectiveSerial = serialOverride ?: metadata.serial
                    val compatibility = compatibilityRepository.findBest(effectiveSerial, cleanTitle)
                    val serial = effectiveSerial ?: compatibility?.serial
                    val title = compatibility?.title ?: cleanTitle
                    items += GameItem(
                        title = title,
                        path = file.absolutePath,
                        fileName = file.name,
                        fileSize = file.length(),
                        lastModified = file.lastModified(),
                        coverArtPath = customCoverRepository.findCustomCoverPath(file.absolutePath)
                            ?: coverRepository.findCachedCoverPath(serial)
                            ?: cachedGame?.coverArtPath?.takeIf { File(it).exists() }
                            ?: coverCandidates[normalizeBaseName(file.nameWithoutExtension)]?.absolutePath
                            ?: coverCandidates[normalizeBaseName(cleanGameName(title))]?.absolutePath,
                        serial = serial,
                        pcsx2Compatibility = compatibility
                    )
                }
            }
        }

        return items
    }

    /** One child row from a single ContentResolver query on a SAF directory. */
    private class DocChild(
        val documentId: String,
        val uri: Uri,
        val name: String,
        val mimeType: String?,
        val size: Long,
        val lastModified: Long
    )

    /** Lists a SAF directory with one query; null when the directory is unreadable. */
    private fun queryDocumentChildren(context: Context, treeUri: Uri, parentDocumentId: String): List<DocChild>? {
        return try {
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocumentId)
            val projection = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED
            )
            context.contentResolver.query(childrenUri, projection, null, null, null)?.use { cursor ->
                val result = ArrayList<DocChild>(cursor.count.coerceAtLeast(0))
                while (cursor.moveToNext()) {
                    val id = cursor.getString(0) ?: continue
                    val rawName = cursor.getString(1).orEmpty()
                    result += DocChild(
                        documentId = id,
                        uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, id),
                        name = if (rawName.isBlank()) "" else DocumentPathResolver.normalizeDisplayName(rawName),
                        mimeType = cursor.getString(2),
                        size = if (cursor.isNull(3)) 0L else cursor.getLong(3),
                        lastModified = if (cursor.isNull(4)) 0L else cursor.getLong(4)
                    )
                }
                result
            }
        } catch (error: Exception) {
            Log.w(TAG, "Unable to list document directory: $parentDocumentId", error)
            null
        }
    }

    private fun scanDocumentDirectory(
        treeUri: Uri,
        directoryId: String,
        context: Context,
        cachedGamesByPath: Map<String, GameItem>,
        serialOverrides: Map<String, String>,
        reuseBlankSerial: Boolean,
        shouldAbort: () -> Boolean,
        depth: Int,
        budget: DocumentScanBudget,
        state: ScanState,
        isUnknownKind: Boolean = false
    ): List<GameItem> {
        if (depth > MAX_DOCUMENT_SCAN_DEPTH) {
            state.complete = false
            return emptyList()
        }
        if (!budget.tryEnterDirectory(directoryId)) return emptyList()
        val items = mutableListOf<GameItem>()
        val children = queryDocumentChildren(context, treeUri, directoryId) ?: run {
            // An entry of unknown type may just be a file; only real directories count as failure.
            if (!isUnknownKind) state.complete = false
            return items
        }
        val coverCandidates = buildDocumentCoverCandidates(context, treeUri, children)
        val coverRepository = CoverArtRepository(context)
        val customCoverRepository = CustomGameCoverRepository(context)
        val compatibilityRepository = Pcsx2CompatibilityRepository(context)
        val containsArcadeManifest = children.any { child ->
            child.name.substringAfterLast('.', "").equals("acgame", ignoreCase = true)
        }

        for (file in children) {
            if (shouldAbort()) return items
            if (!budget.tryVisitEntry()) {
                state.complete = false
                return items
            }
            val mimeType = file.mimeType
            val name = file.name
            when (val kind = SetupValidator.classifyDocumentEntry(mimeType, name)) {
                SetupValidator.DocumentEntryKind.DIRECTORY,
                SetupValidator.DocumentEntryKind.UNKNOWN -> {
                    if (normalizeBaseName(name) !in COVER_DIRECTORY_NAMES) {
                        items.addAll(
                            scanDocumentDirectory(
                                treeUri,
                                file.documentId,
                                context,
                                cachedGamesByPath,
                                serialOverrides,
                                reuseBlankSerial,
                                shouldAbort,
                                depth + 1,
                                budget,
                                state,
                                isUnknownKind = kind == SetupValidator.DocumentEntryKind.UNKNOWN
                            )
                        )
                    }
                }
                SetupValidator.DocumentEntryKind.GAME_FILE -> {
                    if (containsArcadeManifest && !name.substringAfterLast('.', "").equals("acgame", ignoreCase = true)) {
                        continue
                    }
                    val uriPath = file.uri.toString()
                    val fileSize = file.size
                    val lastModified = file.lastModified

                    val cachedGame = cachedGamesByPath[uriPath]
                    val serialOverride = serialOverrides[uriPath]
                    val canReuseCachedMetadata = canReuseCached(
                        cachedGame, fileSize, lastModified, name, serialOverride, reuseBlankSerial
                    )
                    val metadata = if (canReuseCachedMetadata && cachedGame != null) {
                        com.sbro.emucorex.core.GameMetadata(cachedGame.title, cachedGame.serial)
                    } else {
                        EmulatorBridge.getGameMetadata(uriPath)
                    }

                    if (BiosValidator.isLikelyBiosLibraryEntry(name, metadata.title, metadata.serial, fileSize)) {
                        continue
                    }

                    val cleanTitle = cleanScannedTitle(metadata.title, name)
                    val effectiveSerial = serialOverride ?: metadata.serial
                    val compatibility = compatibilityRepository.findBest(effectiveSerial, cleanTitle)
                    val serial = effectiveSerial ?: compatibility?.serial
                    val title = compatibility?.title ?: cleanTitle
                    items += GameItem(
                        title = title,
                        path = uriPath,
                        fileName = name,
                        fileSize = fileSize,
                        lastModified = lastModified,
                        coverArtPath = customCoverRepository.findCustomCoverPath(uriPath)
                            ?: coverRepository.findCachedCoverUri(serial)
                            ?: cachedGame?.coverArtPath
                            ?: coverCandidates[normalizeBaseName(name.substringBeforeLast('.'))]?.uri?.toString()
                            ?: coverCandidates[normalizeBaseName(cleanGameName(title))]?.uri?.toString(),
                        serial = serial,
                        pcsx2Compatibility = compatibility
                    )
                }
                SetupValidator.DocumentEntryKind.OTHER -> Unit
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
        return CustomGameCoverRepository(context).findCustomCoverPath(path)
            ?: CoverArtRepository(context).findCachedCoverPath(serial)
            ?: coverCandidates[baseName]?.absolutePath
            ?: coverCandidates[titleKey]?.absolutePath
    }

    private fun findDocumentCover(path: String, context: Context, serial: String?, title: String?): String? {
        CustomGameCoverRepository(context).findCustomCoverPath(path)?.let { return it }
        CoverArtRepository(context).findCachedCoverUri(serial)?.let { return it }

        val uri = path.toUri()
        val document = DocumentFile.fromSingleUri(context, uri) ?: return null
        val parent = findDocumentParent(context, uri) ?: return null
        val baseName = normalizeBaseName(document.name.orEmpty().substringBeforeLast('.'))
        val titleKey = normalizeBaseName(cleanGameName(title ?: document.name.orEmpty().substringBeforeLast('.')))
        val coverCandidates = buildDocumentCoverCandidates(parent.listFiles())
        return coverCandidates[baseName]?.uri?.toString()
            ?: coverCandidates[titleKey]?.uri?.toString()
    }

    private fun findDocumentParent(context: Context, documentUri: Uri): DocumentFile? {
        val documentId = DocumentsContract.getDocumentId(documentUri)
        val parentId = parentDocumentId(documentId) ?: return null
        val persistedTrees = context.contentResolver.persistedUriPermissions
            .asSequence()
            .filter { permission -> permission.isReadPermission && permission.uri.authority == documentUri.authority }
            .mapNotNull { permission ->
                val rootId = runCatching { DocumentsContract.getTreeDocumentId(permission.uri) }.getOrNull()
                    ?: return@mapNotNull null
                Triple(permission.uri, rootId, relativeDocumentSegments(rootId, parentId))
            }
            .filter { (_, _, segments) -> segments != null }
            .sortedByDescending { (_, rootId, _) -> rootId.length }
            .toList()

        for ((treeUri, _, nullableSegments) in persistedTrees) {
            var current = DocumentFile.fromTreeUri(context, treeUri) ?: continue
            var found = true
            for (segment in nullableSegments.orEmpty()) {
                current = current.findFile(segment) ?: run {
                    found = false
                    break
                }
            }
            if (found) return current
        }

        return null
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

    private fun cleanScannedTitle(rawTitle: String, displayName: String): String {
        return EmulatorBridge.cleanGameDisplayTitle(rawTitle, displayName)
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
        context: Context,
        treeUri: Uri,
        children: List<DocChild>
    ): Map<String, DocChild> {
        fun isCoverImage(child: DocChild) =
            child.mimeType != DocumentsContract.Document.MIME_TYPE_DIR &&
                child.name.substringAfterLast('.', "").lowercase() in COVER_EXTENSIONS
        val directFiles = children.filter(::isCoverImage)
        val nestedCoverFiles = children
            .filter {
                (it.mimeType == DocumentsContract.Document.MIME_TYPE_DIR || it.mimeType == null) &&
                    normalizeBaseName(it.name) in COVER_DIRECTORY_NAMES
            }
            .flatMap { queryDocumentChildren(context, treeUri, it.documentId).orEmpty() }
            .filter(::isCoverImage)
        return (directFiles + nestedCoverFiles).associateBy {
            normalizeBaseName(cleanGameName(it.name.substringBeforeLast('.')))
        }
    }

    private fun buildDocumentCoverCandidates(
        children: Array<DocumentFile>
    ): Map<String, DocumentFile> {
        val directFiles = children
            .filter {
                runCatching { it.type }.getOrNull() != DocumentsContract.Document.MIME_TYPE_DIR &&
                    documentDisplayName(null, it)
                        .substringAfterLast('.', "")
                        .lowercase() in COVER_EXTENSIONS
            }
        val nestedCoverFiles = children
            .filter {
                val type = runCatching { it.type }.getOrNull()
                (type == DocumentsContract.Document.MIME_TYPE_DIR || type == null) &&
                    normalizeBaseName(documentDisplayName(null, it)) in COVER_DIRECTORY_NAMES
            }
            .flatMap { runCatching { it.listFiles().asIterable() }.getOrDefault(emptyList()) }
            .filter {
                runCatching { it.type }.getOrNull() != DocumentsContract.Document.MIME_TYPE_DIR &&
                    documentDisplayName(null, it)
                        .substringAfterLast('.', "")
                        .lowercase() in COVER_EXTENSIONS
            }

        return (directFiles + nestedCoverFiles).associateBy {
            normalizeBaseName(cleanGameName(documentDisplayName(null, it).substringBeforeLast('.')))
        }
    }

    private fun documentDisplayName(context: Context?, document: DocumentFile): String {
        val rawName = runCatching { document.name }.getOrNull().orEmpty()
        if (rawName.isNotBlank()) return DocumentPathResolver.normalizeDisplayName(rawName)
        if (context == null) return ""
        return runCatching { DocumentPathResolver.getDisplayName(context, document.uri.toString()) }
            .getOrDefault("")
    }

    private class DocumentScanBudget {
        private var visitedEntries = 0
        private val visitedDirectories = HashSet<String>()

        fun tryEnterDirectory(uri: String): Boolean = visitedDirectories.add(uri)

        fun tryVisitEntry(): Boolean {
            if (visitedEntries >= MAX_DOCUMENT_SCAN_ENTRIES) return false
            visitedEntries++
            return true
        }
    }
}

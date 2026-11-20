package com.sbro.emucorex.data

import android.content.Context
import android.provider.OpenableColumns
import androidx.core.net.toUri
import kotlinx.coroutines.flow.first
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import java.io.File
import java.net.URLDecoder
import java.util.Locale

/** A per-game profile reference used for key matching. */
data class PerGameKeyProfileRef(val gameKey: String, val gameSerial: String?)

/** A library entry used for key matching. */
data class PerGameKeyLibraryRef(val path: String, val fileName: String, val fileSize: Long)

/** What is known about the game being launched. */
data class PerGameKeyLaunchInfo(
    val rawPath: String,
    val resolvedFilePath: String?,
    val fileName: String?,
    val fileSize: Long?,
    val serial: String?
)

/**
 * Pure matching logic that maps a launch path (as sent by external launchers) onto the key
 * under which the per-game profile is stored. Returns null when nothing matches.
 */
object PerGameKeyMatcher {
    fun normalizeFileUri(raw: String): String {
        if (!raw.startsWith("file://", ignoreCase = true)) return raw
        val encoded = raw.substring("file://".length)
        return runCatching { URLDecoder.decode(encoded.replace("+", "%2B"), "UTF-8") }.getOrDefault(encoded)
    }

    private const val EXTERNAL_STORAGE_PREFIX = "content://com.android.externalstorage.documents/"

    private fun decodeUriPart(part: String): String =
        runCatching { URLDecoder.decode(part.replace("+", "%2B"), "UTF-8") }.getOrDefault(part)

    /** Same safe set as android.net.Uri.encode, so results equal the strings the platform builds. */
    internal fun encodeUriPart(value: String): String {
        val safe = "_-!.~'()*"
        val sb = StringBuilder()
        for (b in value.toByteArray(Charsets.UTF_8)) {
            val c = (b.toInt() and 0xFF).toChar()
            if ((c in 'A'..'Z') || (c in 'a'..'z') || (c in '0'..'9') || c in safe) sb.append(c)
            else sb.append('%').append("%02X".format(b.toInt() and 0xFF))
        }
        return sb.toString()
    }

    /** External-storage document id ("primary:PS2/Games/X.iso") for a document/tree-document URI or a plain/file path. */
    internal fun externalStorageDocumentId(raw: String): String? {
        if (raw.startsWith("content://", ignoreCase = true)) {
            if (!raw.startsWith(EXTERNAL_STORAGE_PREFIX)) return null
            val rest = raw.removePrefix(EXTERNAL_STORAGE_PREFIX)
            val doc = when {
                rest.startsWith("document/") -> rest.removePrefix("document/")
                else -> rest.substringAfter("/document/", "")
            }.substringBefore('?').takeIf { it.isNotEmpty() } ?: return null
            return decodeUriPart(doc)
        }
        val path = normalizeFileUri(raw)
        for (prefix in listOf("/storage/emulated/0/", "/sdcard/", "/storage/self/primary/")) {
            if (path.startsWith(prefix) && path.length > prefix.length) return "primary:" + path.removePrefix(prefix)
        }
        val volume = Regex("^/storage/([A-Za-z0-9]{4}-[A-Za-z0-9]{4})/(.+)$").matchEntire(path)
        if (volume != null) return volume.groupValues[1] + ":" + volume.groupValues[2]
        return null
    }

    /** Tree document id of an external-storage tree URI ("content://.../tree/primary%3AGames[/document/...]"). */
    internal fun treeDocumentId(treeUri: String): String? {
        if (!treeUri.startsWith(EXTERNAL_STORAGE_PREFIX + "tree/")) return null
        return decodeUriPart(treeUri.removePrefix(EXTERNAL_STORAGE_PREFIX + "tree/").substringBefore('/').substringBefore('?'))
            .takeIf { it.isNotEmpty() }
    }

    internal fun buildTreeDocumentUri(treeDocId: String, docId: String): String =
        EXTERNAL_STORAGE_PREFIX + "tree/" + encodeUriPart(treeDocId) + "/document/" + encodeUriPart(docId)

    internal fun isUnderTree(treeDocId: String, docId: String): Boolean =
        docId == treeDocId || docId.startsWith(if (treeDocId.endsWith("/") || treeDocId.endsWith(":")) treeDocId else "$treeDocId/")

    /**
     * Permission-free mapping of a launcher-supplied path (file://, /storage/..., or an external-storage
     * document URI) onto the SAF tree document URI of a held library root. Prefers the exact library entry.
     */
    fun mapToTreeDocumentUri(candidates: Collection<String>, treeUris: List<String>, libraryPaths: List<String>): String? {
        val docIds = candidates.mapNotNull(::externalStorageDocumentId).distinct()
        if (docIds.isEmpty()) return null
        val libraryByDocId = libraryPaths.mapNotNull { path -> externalStorageDocumentId(path)?.let { it to path } }
        for (docId in docIds) {
            libraryByDocId.firstOrNull { it.first == docId }?.let { return it.second }
        }
        for (tree in treeUris) {
            val treeId = treeDocumentId(tree) ?: continue
            for (docId in docIds) {
                if (docId != treeId && isUnderTree(treeId, docId)) return buildTreeDocumentUri(treeId, docId)
            }
        }
        return null
    }

    fun normalizeSerial(serial: String?): String? {
        val compact = serial?.trim()?.uppercase(Locale.US)?.replace(Regex("[-_ ]"), "").orEmpty()
        if (!compact.matches(Regex("[A-Z]{4}[0-9]{5}"))) return null
        return "${compact.take(4)}-${compact.drop(4)}"
    }

    fun match(
        launch: PerGameKeyLaunchInfo,
        profiles: List<PerGameKeyProfileRef>,
        library: List<PerGameKeyLibraryRef>,
        realPathOf: (String) -> String?
    ): String? = matchWithTrees(launch, profiles, library, emptyList(), realPathOf)

    fun matchWithTrees(
        launch: PerGameKeyLaunchInfo,
        profiles: List<PerGameKeyProfileRef>,
        library: List<PerGameKeyLibraryRef>,
        treeUris: List<String>,
        realPathOf: (String) -> String?
    ): String? {
        val profileKeys = profiles.mapTo(HashSet()) { it.gameKey }
        // 1. Exact key.
        if (launch.rawPath in profileKeys) return launch.rawPath

        // 2. Normalized candidates.
        val candidates = linkedSetOf<String>().apply {
            add(launch.rawPath)
            add(normalizeFileUri(launch.rawPath))
            launch.resolvedFilePath?.takeIf { it.isNotBlank() }?.let(::add)
        }
        candidates.firstOrNull { it in profileKeys }?.let { return it }

        // Library entries whose path (or real path) is one of the candidates.
        val libraryByPath = library.filter { entry ->
            entry.path in candidates || realPathOf(entry.path)?.let { it in candidates } == true
        }
        libraryByPath.firstOrNull { it.path in profileKeys }?.let { return it.path }
        libraryByPath.firstOrNull()?.let { entry ->
            // Same file in the library: prefer an existing profile, else its canonical library key.
            profiles.firstOrNull { ref -> realPathOf(ref.gameKey)?.let { it in candidates } == true }
                ?.let { return it.gameKey }
            return entry.path
        }

        // Profile keys that resolve to the same real file.
        profiles.firstOrNull { ref ->
            ref.gameKey.startsWith("content://") && realPathOf(ref.gameKey)?.let { it in candidates } == true
        }?.let { return it.gameKey }

        // Permission-free: same external-storage document id as a profile key / library entry / held tree.
        val docIds = candidates.mapNotNull(::externalStorageDocumentId).toSet()
        if (docIds.isNotEmpty()) {
            profiles.firstOrNull { externalStorageDocumentId(it.gameKey) in docIds }?.let { return it.gameKey }
            mapToTreeDocumentUri(candidates, treeUris, library.map { it.path })?.let { return it }
        }

        // Same name + size (library, then profiles never have size so library only).
        val name = launch.fileName?.takeIf { it.isNotBlank() }
        val size = launch.fileSize?.takeIf { it > 0L }
        if (name != null && size != null) {
            val sameFile = library.filter { it.fileSize == size && it.fileName.equals(name, ignoreCase = true) }
            if (sameFile.size == 1) {
                val entry = sameFile.first()
                return entry.path
            }
        }

        // 3. Serial fallback, only when unambiguous.
        val serial = normalizeSerial(launch.serial) ?: return null
        val bySerial = profiles.filter { normalizeSerial(it.gameSerial) == serial }
        return bySerial.singleOrNull()?.gameKey
    }
}

class PerGameKeyResolver(context: Context) {
    private val appContext = context.applicationContext
    private val settingsRepository = PerGameSettingsRepository(appContext)
    private val contentLibraryRepository = ContentLibraryRepository(appContext)

    /** Blocking/IO heavy: call off the main thread, before any per-game lookup at launch. */
    suspend fun resolve(rawPath: String, vmActive: Boolean): String {
        if (rawPath.isBlank()) return rawPath
        return runCatching {
            val profiles = settingsRepository.getAll()
            if (profiles.any { it.gameKey == rawPath }) return rawPath
            val refs = profiles.map { PerGameKeyProfileRef(it.gameKey, it.gameSerial) }
            val library = runCatching { contentLibraryRepository.loadGames() }.getOrDefault(emptyList())
                .map { PerGameKeyLibraryRef(it.path, it.fileName, it.fileSize) }
            val normalized = PerGameKeyMatcher.normalizeFileUri(rawPath)
            val resolvedFile = when {
                rawPath.startsWith("content://") ->
                    runCatching { DocumentPathResolver.resolveFilePath(appContext, rawPath) }.getOrNull()
                else -> normalized
            }
            val fileName = runCatching { DocumentPathResolver.getDisplayName(appContext, rawPath) }.getOrNull()
            val fileSize = querySize(rawPath, resolvedFile)
            val cache = HashMap<String, String?>()
            val realPathOf: (String) -> String? = { path ->
                cache.getOrPut(path) {
                    if (path.startsWith("content://")) {
                        runCatching { DocumentPathResolver.resolveFilePath(appContext, path) }.getOrNull()
                    } else {
                        PerGameKeyMatcher.normalizeFileUri(path)
                    }
                }
            }
            val launch = PerGameKeyLaunchInfo(rawPath, resolvedFile, fileName, fileSize, serial = null)
            val treeUris = runCatching { AppPreferences(appContext).gamePaths.first() }.getOrDefault(emptyList())
                .filter { it.startsWith("content://") && "/tree/" in it }
            val direct = PerGameKeyMatcher.matchWithTrees(launch, refs, library, treeUris, realPathOf)
            if (direct != null) return@runCatching direct
            val bySerial = if (!vmActive) {
                val serial = runCatching { EmulatorBridge.getGameMetadata(rawPath).serial }.getOrNull()
                PerGameKeyMatcher.match(launch.copy(serial = serial), refs, emptyList(), realPathOf)
            } else {
                null
            }
            bySerial ?: rawPath
        }.getOrDefault(rawPath)
    }

    private fun querySize(rawPath: String, resolvedFile: String?): Long? {
        resolvedFile?.takeIf { !it.startsWith("content://") }?.let { path ->
            File(path).takeIf { it.isFile }?.length()?.let { return it }
        }
        if (!rawPath.startsWith("content://")) return null
        return runCatching {
            appContext.contentResolver.query(rawPath.toUri(), arrayOf(OpenableColumns.SIZE), null, null, null)
                ?.use { c -> if (c.moveToFirst() && !c.isNull(0)) c.getLong(0) else null }
        }.getOrNull()
    }
}

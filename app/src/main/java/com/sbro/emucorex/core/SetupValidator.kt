package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import java.io.File

object SetupValidator {
    private val supportedGameExtensions = setOf("iso", "bin", "img", "mdf", "gz", "cso", "zso", "chd", "elf")
    private const val MAX_GAME_READ_PROBE_FILES = 24
    private const val MAX_GAME_READ_PROBE_DIRECTORIES = 96

    internal enum class DocumentEntryKind {
        DIRECTORY,
        GAME_FILE,
        UNKNOWN,
        OTHER
    }

    fun isGameFolderPresentForStartup(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            val uri = rawPath.toUri()
            treeDocument(context, uri) != null && canReadTree(context, uri)
        } else {
            val dir = File(rawPath)
            if (DocumentPathResolver.findAccessibleTreeUriForRawPath(context, rawPath) != null) {
                return true
            }
            dir.exists() && dir.isDirectory
        }
    }

    fun isAnyGameFolderPresentForStartup(context: Context, rawPaths: List<String>): Boolean =
        rawPaths.any { isGameFolderPresentForStartup(context, it) }

    fun hasCoreReadableGameFile(context: Context, rawPaths: List<String>): Boolean =
        rawPaths.any { hasCoreReadableGameFile(context, it) }

    fun isGameFolderAccessible(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            val uri = rawPath.toUri()
            treeDocument(context, uri) != null && canReadTree(context, uri)
        } else {
            val dir = File(rawPath)
            if (DocumentPathResolver.isScopedStorageExternalPath(rawPath)) {
                val migratedUri = DocumentPathResolver.findAccessibleTreeUriForRawPath(context, rawPath)
                if (migratedUri != null) {
                    return treeDocument(context, migratedUri) != null && canReadTree(context, migratedUri)
                }
                return false
            }
            dir.exists() && dir.isDirectory
        }
    }

    fun hasCoreReadableGameFile(context: Context, rawPath: String?): Boolean {
        if (!isGameFolderAccessible(context, rawPath)) return false
        rawPath ?: return false

        return if (rawPath.startsWith("content://")) {
            val root = treeDocument(context, rawPath.toUri()) ?: return false
            findReadableDocumentGame(context, root, ProbeBudget()) != null
        } else {
            val dir = File(rawPath)
            if (dir.isDirectory) {
                findReadableLocalGame(context, dir, ProbeBudget()) != null
            } else {
                false
            }
        }
    }

    private fun findReadableLocalGame(context: Context, dir: File, budget: ProbeBudget): String? {
        if (!budget.tryEnterDirectory()) return null
        val children = dir.listFiles().orEmpty()
        for (child in children) {
            if (child.isDirectory) {
                findReadableLocalGame(context, child, budget)?.let { return it }
            } else if (child.isFile && child.extension.lowercase() in supportedGameExtensions) {
                if (!budget.tryCheckFile()) return null
                if (isLaunchPathReadable(context, child.absolutePath)) return child.absolutePath
            }
        }
        return null
    }

    private fun findReadableDocumentGame(context: Context, root: DocumentFile, budget: ProbeBudget): String? {
        if (!budget.tryEnterDirectory()) return null
        val children = runCatching { root.listFiles() }.getOrDefault(emptyArray())
        for (child in children) {
            val mimeType = runCatching { child.type }.getOrNull()
            val displayName = runCatching { child.name }.getOrNull().orEmpty().ifBlank {
                DocumentPathResolver.getDisplayName(context, child.uri.toString())
            }
            when (classifyDocumentEntry(mimeType, displayName)) {
                DocumentEntryKind.DIRECTORY,
                DocumentEntryKind.UNKNOWN -> {
                    // A number of cloud/USB DocumentsProviders return a null MIME type for
                    // directories. Treat unknown entries as possible directories; listFiles()
                    // safely returns an empty array when the entry is actually a file.
                    findReadableDocumentGame(context, child, budget)?.let { return it }
                }
                DocumentEntryKind.GAME_FILE -> {
                    if (!budget.tryCheckFile()) return null
                    val uriPath = child.uri.toString()
                    if (isLaunchPathReadable(context, uriPath)) return uriPath
                }
                DocumentEntryKind.OTHER -> Unit
            }
        }
        return null
    }

    internal fun classifyDocumentEntry(mimeType: String?, displayName: String?): DocumentEntryKind {
        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) {
            return DocumentEntryKind.DIRECTORY
        }
        val extension = displayName.orEmpty().substringAfterLast('.', "").lowercase()
        if (extension in supportedGameExtensions) {
            return DocumentEntryKind.GAME_FILE
        }
        return if (mimeType == null) DocumentEntryKind.UNKNOWN else DocumentEntryKind.OTHER
    }

    private fun isLaunchPathReadable(context: Context, rawGamePath: String): Boolean {
        if (rawGamePath.startsWith("content://")) {
            return runCatching {
                context.contentResolver.openFileDescriptor(rawGamePath.toUri(), "r")?.use { descriptor ->
                    descriptor.statSize != 0L
                } ?: false
            }.getOrDefault(false)
        }

        val preparedPath = DocumentPathResolver.prepareGameLaunchPath(context, rawGamePath) ?: return false
        val file = File(preparedPath)
        return file.isFile && file.canRead() && file.length() > 0L
    }

    private fun treeDocument(context: Context, uri: Uri): DocumentFile? {
        return runCatching {
            if (DocumentsContract.isTreeUri(uri)) DocumentFile.fromTreeUri(context, uri) else null
        }.getOrNull()
    }

    private fun canReadTree(context: Context, uri: Uri): Boolean = runCatching {
        val root = treeDocument(context, uri) ?: return@runCatching false
        context.contentResolver.query(
            root.uri,
            arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID),
            null,
            null,
            null
        )?.use { cursor -> cursor.moveToFirst() } ?: false
    }.getOrDefault(false)

    private class ProbeBudget {
        private var checkedFiles = 0
        private var checkedDirectories = 0

        fun tryCheckFile(): Boolean {
            if (checkedFiles >= MAX_GAME_READ_PROBE_FILES) return false
            checkedFiles++
            return true
        }

        fun tryEnterDirectory(): Boolean {
            if (checkedDirectories >= MAX_GAME_READ_PROBE_DIRECTORIES) return false
            checkedDirectories++
            return true
        }
    }
}

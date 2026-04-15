package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.OpenableColumns
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.io.FileOutputStream
import androidx.core.net.toUri

object DocumentPathResolver {

    data class PreparedBiosSelection(
        val directoryPath: String,
        val fileName: String?
    )

    private val biosImageExtensions = setOf("bin", "rom")
    private val biosArtifactExtensions = setOf("mec", "nvm", "elf")
    private val biosImportExtensions = biosImageExtensions + biosArtifactExtensions
    private val biosNameHints = listOf("scph", "ps2", "bios", "rom")

    fun resolveFilePath(context: Context, rawPath: String): String? {
        if (!rawPath.startsWith("content://")) return rawPath

        val uri = rawPath.toUri()
        val directPath = resolveExternalStoragePath(uri)
        if (directPath != null) return directPath

        val fileName = DocumentFile.fromSingleUri(context, uri)?.name ?: return null
        return findFileInPersistedTree(context, uri, fileName)
    }

    fun resolveDirectoryPath(rawPath: String): String? {
        if (!rawPath.startsWith("content://")) return rawPath
        return resolveExternalStoragePath(rawPath.toUri())
    }

    fun findAccessibleTreeUriForRawPath(context: Context, rawPath: String): Uri? {
        if (rawPath.startsWith("content://")) return rawPath.toUri()

        val normalizedRawPath = File(rawPath).absolutePath.removeSuffix("/")
        val persistedTrees = context.contentResolver.persistedUriPermissions
            .mapNotNull { permission ->
                val treeUri = permission.uri
                val treePath = resolveExternalStoragePath(treeUri)?.removeSuffix("/") ?: return@mapNotNull null
                treeUri to treePath
            }
            .sortedByDescending { (_, treePath) -> treePath.length }

        for ((treeUri, treePath) in persistedTrees) {
            if (normalizedRawPath != treePath && !normalizedRawPath.startsWith("$treePath/")) {
                continue
            }

            if (normalizedRawPath == treePath) {
                return treeUri
            }

            val root = DocumentFile.fromTreeUri(context, treeUri) ?: continue
            val relativeSegments = normalizedRawPath
                .removePrefix(treePath)
                .trim('/')
                .split('/')
                .filter { it.isNotBlank() }

            var current = root
            var failed = false
            for (segment in relativeSegments) {
                current = current.findFile(segment) ?: run {
                    failed = true
                    break
                }
            }
            if (!failed) {
                return current.uri
            }
        }

        return null
    }

    fun isScopedStorageExternalPath(rawPath: String): Boolean {
        if (rawPath.startsWith("content://")) return false
        val normalized = File(rawPath).absolutePath
        val primaryExternal = Environment.getExternalStorageDirectory().absolutePath
        return normalized.startsWith(primaryExternal)
    }

    fun prepareBiosDirectory(context: Context, rawPath: String?): String? {
        return prepareBiosSelection(context, rawPath)?.directoryPath
    }

    fun prepareBiosSelection(context: Context, rawPath: String?): PreparedBiosSelection? {
        if (rawPath.isNullOrBlank()) return null
        if (!rawPath.startsWith("content://")) {
            val file = File(rawPath)
            return when {
                file.isFile && isLikelyMainBiosName(file.name) -> PreparedBiosSelection(
                    directoryPath = file.parentFile?.absolutePath ?: file.absoluteFile.parent.orEmpty(),
                    fileName = file.name
                )
                file.isDirectory -> PreparedBiosSelection(
                    directoryPath = file.absolutePath,
                    fileName = findPreferredBiosFileName(file.absolutePath)
                )
                else -> null
            }
        }

        val uri = rawPath.toUri()
        val targetDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "imported-bios")
        if (!targetDir.exists()) {
            targetDir.mkdirs()
        }

        val single = DocumentFile.fromSingleUri(context, uri)
        if (single?.isFile == true) {
            val copiedPath = copySingleBiosFile(context, single, targetDir) ?: return null
            return PreparedBiosSelection(
                directoryPath = targetDir.absolutePath,
                fileName = File(copiedPath).name
            )
        }

        val root = DocumentFile.fromTreeUri(context, uri) ?: return null
        copyBiosFilesRecursive(context, root, targetDir)
        return PreparedBiosSelection(
            directoryPath = targetDir.absolutePath,
            fileName = findPreferredBiosFileName(targetDir.absolutePath)
        )
    }

    fun findPreferredBiosFileName(directoryPath: String?): String? {
        if (directoryPath.isNullOrBlank()) return null
        val dir = File(directoryPath)
        if (!dir.isDirectory) return null

        return dir.walkTopDown()
            .maxDepth(2)
            .filter { it.isFile && isLikelyMainBiosName(it.name) }.minByOrNull { it.name.lowercase() }
            ?.name
    }

    fun getDisplayName(context: Context, rawPath: String): String {
        if (!rawPath.startsWith("content://")) return File(rawPath).name

        val uri = rawPath.toUri()
        val fromResolver = runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        cursor.getString(0)
                    } else {
                        null
                    }
                }
        }.getOrNull()
        if (!fromResolver.isNullOrBlank()) return fromResolver

        val fromSingle = DocumentFile.fromSingleUri(context, uri)?.name
        if (!fromSingle.isNullOrBlank()) return fromSingle

        val fromTree = DocumentFile.fromTreeUri(context, uri)?.name
        if (!fromTree.isNullOrBlank()) return fromTree

        return uri.lastPathSegment ?: rawPath
    }

    fun getFileSize(context: Context, rawPath: String): Long {
        if (!rawPath.startsWith("content://")) {
            val file = File(rawPath)
            return if (file.exists()) file.length() else 0L
        }

        val uri = rawPath.toUri()
        return DocumentFile.fromSingleUri(context, uri)?.length() ?: 0L
    }

    private fun resolveExternalStoragePath(uri: Uri): String? {
        val documentId = runCatching { DocumentsContract.getDocumentId(uri) }.getOrNull()
            ?: runCatching { DocumentsContract.getTreeDocumentId(uri) }.getOrNull()
            ?: return null

        val parts = documentId.split(':', limit = 2)
        if (parts.isEmpty()) return null

        val volume = parts[0]
        val relativePath = parts.getOrNull(1).orEmpty()

        return when {
            volume.equals("primary", ignoreCase = true) -> {
                val base = Environment.getExternalStorageDirectory()
                if (relativePath.isBlank()) base.absolutePath
                else File(base, relativePath).absolutePath
            }
            volume.equals("home", ignoreCase = true) -> {
                val base = File(Environment.getExternalStorageDirectory(), "Documents")
                if (relativePath.isBlank()) base.absolutePath
                else File(base, relativePath).absolutePath
            }
            volume.startsWith("/") -> volume
            else -> null
        }
    }

    private fun findFileInPersistedTree(context: Context, targetUri: Uri, fileName: String): String? {
        val persistedTrees = context.contentResolver.persistedUriPermissions
            .mapNotNull { permission -> DocumentFile.fromTreeUri(context, permission.uri) }

        for (tree in persistedTrees) {
            val resolved = findFileRecursive(tree, targetUri, fileName)
            if (resolved != null) return resolved
        }

        return null
    }

    private fun findFileRecursive(root: DocumentFile, targetUri: Uri, fileName: String): String? {
        for (child in root.listFiles()) {
            if (child.uri == targetUri) {
                return resolveExternalStoragePath(child.uri)
            }

            if (child.isDirectory) {
                val nested = findFileRecursive(child, targetUri, fileName)
                if (nested != null) return nested
            } else if (child.name == fileName) {
                val direct = resolveExternalStoragePath(child.uri)
                if (direct != null) return direct
            }
        }

        return null
    }

    private fun copyBiosFilesRecursive(context: Context, root: DocumentFile, targetDir: File) {
        for (child in root.listFiles()) {
            if (child.isDirectory) {
                copyBiosFilesRecursive(context, child, targetDir)
            } else if (child.isFile && isLikelyImportedBiosName(child.name)) {
                val targetFile = File(targetDir, sanitizeFileName(child.name ?: "bios.bin"))
                copyUriToFile(context, child.uri, targetFile)
            }
        }
    }

    private fun copySingleBiosFile(context: Context, file: DocumentFile, targetDir: File): String? {
        if (!isLikelyMainBiosName(file.name)) return null

        clearImportedBiosImages(targetDir)
        val targetFile = File(targetDir, sanitizeFileName(file.name ?: "bios.bin"))
        return copyUriToFile(context, file.uri, targetFile)
    }

    private fun copyUriToFile(context: Context, uri: Uri, targetFile: File): String? {
        return runCatching {
            targetFile.parentFile?.mkdirs()
            context.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(targetFile).use { output ->
                    input.copyTo(output)
                }
            } ?: return null
            targetFile.absolutePath
        }.getOrNull()
    }

    private fun isLikelyImportedBiosName(name: String?): Boolean {
        val fileName = name?.lowercase() ?: return false
        val ext = fileName.substringAfterLast('.', "")
        return ext in biosImportExtensions && biosNameHints.any(fileName::contains)
    }

    private fun isLikelyMainBiosName(name: String?): Boolean {
        val fileName = name?.lowercase() ?: return false
        val ext = fileName.substringAfterLast('.', "")
        return ext in biosImageExtensions && biosNameHints.any(fileName::contains)
    }

    private fun clearImportedBiosImages(targetDir: File) {
        targetDir.listFiles()
            ?.filter { it.isFile && isLikelyMainBiosName(it.name) }
            ?.forEach { it.delete() }
    }

    private fun sanitizeFileName(name: String): String {
        return buildString(name.length) {
            name.forEach { ch ->
                append(
                    when {
                        ch.isLetterOrDigit() || ch == '.' || ch == '-' || ch == '_' -> ch
                        else -> '_'
                    }
                )
            }
        }
    }
}

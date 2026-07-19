package com.sbro.emucorex.core

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import java.io.File

object BiosValidator {

    private val biosImageExtensions = setOf("bin", "rom")
    private val biosLibraryExtensions = biosImageExtensions + setOf("mec", "nvm", "elf")
    private val fileNameHints = listOf("scph", "ps2", "bios", "rom")
    private val extraArtifactHints = listOf("rom0", "rom1", "rom2", "erom", "dvdpl", "cdvd")
    private val biosImageSizeRange = (512L * 1024L)..(8L * 1024L * 1024L)
    private const val MAX_BIOS_PROBE_FILES = 24
    private const val MAX_BIOS_PROBE_DIRECTORIES = 96

    internal enum class DocumentEntryKind {
        DIRECTORY,
        BIOS_FILE,
        UNKNOWN,
        OTHER
    }

    fun hasUsableBiosFiles(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            hasUsableContentBios(context, rawPath.toUri()) ||
                DocumentPathResolver.hasPreparedBiosForSource(context, rawPath)
        } else {
            val file = File(rawPath)
            when {
                file.isFile -> isValidLocalBios(file)
                file.isDirectory -> file.walkTopDown().maxDepth(2).any(::isValidLocalBios)
                else -> false
            }
        }
    }

    private fun hasUsableContentBios(context: Context, uri: Uri): Boolean = documentCheckOrFalse {
        if (DocumentsContract.isTreeUri(uri)) {
            val root = DocumentFile.fromTreeUri(context, uri) ?: return@documentCheckOrFalse false
            containsBiosFile(context, root, ProbeBudget())
        } else {
            val single = DocumentFile.fromSingleUri(context, uri) ?: return@documentCheckOrFalse false
            val displayName = documentDisplayName(context, single)
            val fileSize = runCatching { single.length() }.getOrDefault(0L)
            isBiosCandidate(displayName, fileSize) && isValidContentBios(context, single.uri, displayName, fileSize)
        }
    }

    internal fun documentCheckOrFalse(check: () -> Boolean): Boolean {
        return try {
            check()
        } catch (_: RuntimeException) {
            false
        }
    }

    private fun containsBiosFile(context: Context, root: DocumentFile, budget: ProbeBudget): Boolean {
        if (!budget.tryEnterDirectory()) return false
        val children = runCatching { root.listFiles() }.getOrDefault(emptyArray())
        for (child in children) {
            val mimeType = runCatching { child.type }.getOrNull()
            val displayName = documentDisplayName(context, child)
            when (classifyDocumentEntry(mimeType, displayName)) {
                DocumentEntryKind.DIRECTORY,
                DocumentEntryKind.UNKNOWN -> {
                    // Some cloud and USB providers expose directories with a null MIME type.
                    // listFiles() is safe for an actual file and simply produces no children.
                    if (containsBiosFile(context, child, budget)) return true
                }
                DocumentEntryKind.BIOS_FILE -> {
                    if (!budget.tryCheckFile()) return false
                    val fileSize = runCatching { child.length() }.getOrDefault(0L)
                    if (isBiosCandidate(displayName, fileSize) &&
                        isValidContentBios(context, child.uri, displayName, fileSize)
                    ) {
                        return true
                    }
                }
                DocumentEntryKind.OTHER -> Unit
            }
        }
        return false
    }

    private fun documentDisplayName(context: Context, document: DocumentFile): String =
        runCatching { document.name }.getOrNull().orEmpty().ifBlank {
            runCatching { DocumentPathResolver.getDisplayName(context, document.uri.toString()) }
                .getOrDefault("")
        }

    private fun isReadable(context: Context, uri: Uri): Boolean = runCatching {
        context.contentResolver.openFileDescriptor(uri, "r")?.use { descriptor ->
            descriptor.statSize != 0L
        } ?: false
    }.getOrDefault(false)

    internal fun classifyDocumentEntry(mimeType: String?, displayName: String?): DocumentEntryKind {
        if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) return DocumentEntryKind.DIRECTORY
        if (isBiosCandidateName(displayName)) return DocumentEntryKind.BIOS_FILE
        return if (mimeType == null) DocumentEntryKind.UNKNOWN else DocumentEntryKind.OTHER
    }

    private fun isValidLocalBios(file: File): Boolean {
        if (!file.isFile || !isBiosCandidate(file.name, file.length())) return false
        val knownValidShape = isUsableMainBiosImage(file.name, file.length()) && file.canRead()
        if (!NativeApp.hasNativeCore) return knownValidShape
        return runCatching { NativeApp.isBiosPath(file.absolutePath) }.getOrDefault(false) || knownValidShape
    }

    private fun isValidContentBios(context: Context, uri: Uri, displayName: String, fileSize: Long): Boolean {
        if (!isBiosCandidate(displayName, fileSize)) return false
        val knownValidShape = isUsableMainBiosImage(displayName, fileSize) && isReadable(context, uri)
        if (!NativeApp.hasNativeCore) return knownValidShape

        val descriptor = runCatching { context.contentResolver.openFileDescriptor(uri, "r") }.getOrNull()
            ?: return knownValidShape
        var detachedFd = -1
        return try {
            detachedFd = descriptor.detachFd()
            NativeApp.isBiosFd(detachedFd) || knownValidShape
        } catch (_: RuntimeException) {
            if (detachedFd >= 0) {
                runCatching { ParcelFileDescriptor.adoptFd(detachedFd).close() }
            }
            knownValidShape
        } finally {
            runCatching { descriptor.close() }
        }
    }

    private fun isBiosCandidate(name: String?, fileSize: Long): Boolean =
        isBiosCandidateName(name) && (fileSize <= 0L || fileSize in biosImageSizeRange)

    private fun isBiosCandidateName(name: String?): Boolean {
        val extension = name.orEmpty().substringAfterLast('.', "").lowercase()
        return extension in biosImageExtensions
    }

    fun isLikelyBiosLibraryEntry(
        fileName: String,
        title: String?,
        serial: String?,
        fileSize: Long
    ): Boolean {
        val lowerFileName = fileName.lowercase()
        val lowerTitle = title.orEmpty().lowercase()
        val combined = "$lowerFileName $lowerTitle"
        val ext = lowerFileName.substringAfterLast('.', "")
        val titleLooksLikeBios = lowerTitle.contains("playstation 2 bios") ||
            lowerTitle == "ps2 bios" ||
            lowerTitle.contains("sony playstation 2 bios")
        val serialLooksLikeBios = serial.orEmpty().lowercase().contains("bios")
        val biosHint = isLikelyBiosName(fileName) || extraArtifactHints.any(combined::contains)
        val likelyBiosSizedBlob = ext in biosImageExtensions && fileSize in biosImageSizeRange

        return titleLooksLikeBios ||
            (serialLooksLikeBios && (biosHint || lowerTitle.contains("playstation 2") || lowerTitle.contains("ps2"))) ||
            biosHint ||
            (likelyBiosSizedBlob && (combined.contains("playstation 2") || combined.contains("ps2") || combined.contains("scph")))
    }

    fun isLikelyBiosName(name: String?): Boolean {
        val fileName = name?.lowercase() ?: return false
        val ext = fileName.substringAfterLast('.', "")
        return ext in biosLibraryExtensions && (fileNameHints.any(fileName::contains) || extraArtifactHints.any(fileName::contains))
    }

    private fun isLikelyMainBiosName(name: String?): Boolean {
        val fileName = name?.lowercase() ?: return false
        val ext = fileName.substringAfterLast('.', "")
        return ext in biosImageExtensions && fileNameHints.any(fileName::contains)
    }

    internal fun isUsableMainBiosImage(name: String?, fileSize: Long): Boolean {
        return isLikelyMainBiosName(name) && (fileSize <= 0L || fileSize in biosImageSizeRange)
    }

    private class ProbeBudget {
        private var checkedFiles = 0
        private var checkedDirectories = 0

        fun tryCheckFile(): Boolean {
            if (checkedFiles >= MAX_BIOS_PROBE_FILES) return false
            checkedFiles++
            return true
        }

        fun tryEnterDirectory(): Boolean {
            if (checkedDirectories >= MAX_BIOS_PROBE_DIRECTORIES) return false
            checkedDirectories++
            return true
        }
    }
}

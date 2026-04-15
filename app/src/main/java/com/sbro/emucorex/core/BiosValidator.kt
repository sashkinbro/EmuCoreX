package com.sbro.emucorex.core

import android.content.Context
import androidx.documentfile.provider.DocumentFile
import java.io.File
import androidx.core.net.toUri

object BiosValidator {

    private val biosImageExtensions = setOf("bin", "rom")
    private val biosLibraryExtensions = biosImageExtensions + setOf("mec", "nvm", "elf")
    private val fileNameHints = listOf("scph", "ps2", "bios", "rom")
    private val extraArtifactHints = listOf("rom0", "rom1", "rom2", "erom", "dvdpl", "cdvd")
    private val biosImageSizeRange = (512L * 1024L)..(8L * 1024L * 1024L)

    fun hasUsableBiosFiles(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            val uri = rawPath.toUri()
            val single = DocumentFile.fromSingleUri(context, uri)
            if (single?.isFile == true) {
                return isUsableMainBiosImage(single.name, single.length())
            }

            val root = DocumentFile.fromTreeUri(context, uri) ?: return false
            containsBiosFile(root)
        } else {
            val file = File(rawPath)
            when {
                file.isFile -> isUsableMainBiosImage(file.name, file.length())
                file.isDirectory -> file.walkTopDown().maxDepth(2).any(::isLikelyBiosFile)
                else -> false
            }
        }
    }

    private fun containsBiosFile(root: DocumentFile): Boolean {
        for (child in root.listFiles()) {
            if (child.isDirectory && containsBiosFile(child)) return true
            if (child.isFile && isUsableMainBiosImage(child.name, child.length())) return true
        }
        return false
    }

    private fun isLikelyBiosFile(file: File): Boolean {
        return file.isFile && isUsableMainBiosImage(file.name, file.length())
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

    private fun isUsableMainBiosImage(name: String?, fileSize: Long): Boolean {
        return isLikelyMainBiosName(name) && (fileSize <= 0L || fileSize in biosImageSizeRange)
    }
}

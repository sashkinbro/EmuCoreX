package com.sbro.emucorex.core

import android.content.Context
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import java.io.File

object SetupValidator {

    fun isGameFolderPresentForStartup(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            val root = DocumentFile.fromTreeUri(context, rawPath.toUri()) ?: return false
            runCatching { root.exists() && root.isDirectory }.getOrDefault(false)
        } else {
            if (DocumentPathResolver.findAccessibleTreeUriForRawPath(context, rawPath) != null) {
                return true
            }
            val dir = File(rawPath)
            dir.exists() && dir.isDirectory
        }
    }

    fun isGameFolderAccessible(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            val root = DocumentFile.fromTreeUri(context, rawPath.toUri()) ?: return false
            runCatching { root.isDirectory && root.exists() && root.listFiles().isNotEmpty() || root.isDirectory && root.exists() }
                .getOrDefault(false)
        } else {
            if (DocumentPathResolver.isScopedStorageExternalPath(rawPath)) {
                val migratedUri = DocumentPathResolver.findAccessibleTreeUriForRawPath(context, rawPath)
                if (migratedUri != null) {
                    val root = DocumentFile.fromTreeUri(context, migratedUri) ?: return false
                    return runCatching { root.isDirectory && root.exists() }.getOrDefault(false)
                }
                return false
            }
            val dir = File(rawPath)
            dir.exists() && dir.isDirectory
        }
    }
}

package com.sbro.emucorex.core

import android.content.Context
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import java.io.File

object SetupValidator {

    fun isGameFolderAccessible(context: Context, rawPath: String?): Boolean {
        if (rawPath.isNullOrBlank()) return false

        return if (rawPath.startsWith("content://")) {
            val root = DocumentFile.fromTreeUri(context, rawPath.toUri()) ?: return false
            runCatching { root.isDirectory && root.exists() && root.listFiles().isNotEmpty() || root.isDirectory && root.exists() }
                .getOrDefault(false)
        } else {
            val dir = File(rawPath)
            dir.exists() && dir.isDirectory
        }
    }
}

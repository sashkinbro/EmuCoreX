package com.sbro.emucorex.data

import java.io.File

internal object CoverCachePolicy {
    fun isPathInside(path: String?, directory: File): Boolean {
        if (path.isNullOrBlank() || path.startsWith("content://") || path.startsWith("http://") ||
            path.startsWith("https://")) {
            return false
        }
        val candidate = runCatching { File(path).canonicalFile }.getOrNull() ?: return false
        val root = runCatching { directory.canonicalFile }.getOrNull() ?: return false
        return isFileInside(candidate, root)
    }

    fun isFileInside(candidate: File, root: File): Boolean {
        val rootPath = root.path.trimEnd(File.separatorChar) + File.separator
        return candidate.path.startsWith(rootPath, ignoreCase = System.getProperty("os.name")
            ?.startsWith("Windows", ignoreCase = true) == true)
    }
}

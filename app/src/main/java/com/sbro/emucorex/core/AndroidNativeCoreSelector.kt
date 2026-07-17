package com.sbro.emucorex.core

import android.system.Os
import android.system.OsConstants

private const val STANDARD_PAGE_SIZE_BYTES = 4_096L
private const val LARGE_PAGE_SIZE_BYTES = 16_384L

internal object AndroidNativeCoreSelector {
    internal fun runtimePageSizeBytes(): Long = runCatching {
        Os.sysconf(OsConstants._SC_PAGESIZE).takeIf { it > 0L } ?: STANDARD_PAGE_SIZE_BYTES
    }.getOrDefault(STANDARD_PAGE_SIZE_BYTES)

    internal fun selectedLibraryName(
        pageSizeBytes: Long = runtimePageSizeBytes()
    ): String = selectAndroidNativeCoreLibrary(pageSizeBytes)
}

internal fun selectAndroidNativeCoreLibrary(pageSizeBytes: Long): String =
    when (pageSizeBytes) {
        STANDARD_PAGE_SIZE_BYTES -> "emucore_4k"
        LARGE_PAGE_SIZE_BYTES -> "emucore_16k"
        else -> error("Unsupported Android page size: $pageSizeBytes bytes")
    }

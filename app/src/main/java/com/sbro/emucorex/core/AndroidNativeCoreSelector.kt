package com.sbro.emucorex.core

import android.system.Os
import android.system.OsConstants

private const val STANDARD_PAGE_SIZE_BYTES = 4_096L
private const val LARGE_PAGE_SIZE_BYTES = 16_384L
private val ADRENO_RENDERER = Regex("\\badreno\\b", RegexOption.IGNORE_CASE)

internal object AndroidNativeCoreSelector {
    internal fun runtimePageSizeBytes(): Long = runCatching {
        Os.sysconf(OsConstants._SC_PAGESIZE).takeIf { it > 0L } ?: STANDARD_PAGE_SIZE_BYTES
    }.getOrDefault(STANDARD_PAGE_SIZE_BYTES)

    internal fun selectedLibraryName(
        pageSizeBytes: Long = runtimePageSizeBytes(),
        renderer: String? = GpuRendererQuery.query()
    ): String = selectAndroidNativeCoreLibrary(pageSizeBytes, renderer)
}

internal fun selectAndroidNativeCoreLibrary(pageSizeBytes: Long): String =
    selectAndroidNativeCoreLibrary(pageSizeBytes, renderer = null)

internal fun selectAndroidNativeCoreLibrary(pageSizeBytes: Long, renderer: String?): String {
    val family = if (ADRENO_RENDERER.containsMatchIn(renderer.orEmpty())) "adreno" else "mali"
    val pageVariant = when (pageSizeBytes) {
        STANDARD_PAGE_SIZE_BYTES -> "4k"
        LARGE_PAGE_SIZE_BYTES -> "16k"
        else -> error("Unsupported Android page size: $pageSizeBytes bytes")
    }
    return "emucore_${family}_$pageVariant"
}

package com.sbro.emucorex.core

/**
 * Bridge for native conversions. Native code looks the [onProgress] name and signature up
 * with JNI reflection, so keep both stable in release builds (see proguard-rules.pro).
 *
 * Returning `true` from the update lambda cancels the native conversion.
 */
class ConversionProgressCallback(
    private val onUpdate: (processedBytes: Long, totalBytes: Long) -> Boolean
) {
    fun onProgress(processedBytes: Long, totalBytes: Long): Boolean =
        onUpdate(processedBytes, totalBytes)
}

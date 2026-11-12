package com.sbro.emucorex.core

/**
 * Analytics is intentionally disabled in the offline-first build.
 * Keep the call surface so gameplay code does not depend on a telemetry SDK.
 */
object AppAnalytics {
    fun initialize(context: android.content.Context) = Unit
    fun logOnboardingCompleted(performanceProfile: Int) = Unit
    fun logEmulationStarted(
        launchType: String,
        renderer: Int,
        audioBackend: Int,
        upscaleMultiplier: Float,
        performanceProfile: Int,
        saveStateLoad: Boolean
    ) = Unit
    fun logEmulationStartFailed(launchType: String, reason: String) = Unit
    fun logEmulationEnded(activePlayTimeMs: Long, renderer: Int, audioBackend: Int) = Unit
    fun logSaveStateAction(action: String, automatic: Boolean, success: Boolean) = Unit
}

internal object AnalyticsDimensions {
    private val launchTypes = setOf("game", "bios", "autotest", "smoke_test")
    private val failureReasons = setOf("bios_missing", "path_unavailable", "native_start")
    private val saveStateActions = setOf("save", "load")

    fun launchType(bootToBios: Boolean, bootSmokeProbe: Boolean, autotestMode: Boolean): String = when {
        bootSmokeProbe -> "smoke_test"
        autotestMode -> "autotest"
        bootToBios -> "bios"
        else -> "game"
    }

    fun sanitizeLaunchType(value: String): String = value.takeIf(launchTypes::contains) ?: "unknown"

    fun sanitizeFailureReason(value: String): String = value.takeIf(failureReasons::contains) ?: "unknown"

    fun sanitizeSaveStateAction(value: String): String = value.takeIf(saveStateActions::contains) ?: "unknown"

    fun renderer(value: Int): String = when (value) {
        RendererDefaults.OPENGL -> "opengl"
        RendererDefaults.SOFTWARE -> "software"
        RendererDefaults.VULKAN -> "vulkan"
        else -> "unknown"
    }

    fun audioBackend(value: Int): String = when (AudioDefaults.coerceBackend(value)) {
        AudioDefaults.BACKEND_OPENSLES -> "opensles"
        else -> "aaudio"
    }

    fun performanceProfile(value: Int): String = when (PerformanceProfiles.normalize(value)) {
        PerformanceProfiles.FAST -> "fast"
        else -> "safe"
    }

    fun upscale(value: Float): String = when {
        !value.isFinite() || value <= 1f -> "native"
        value < 2.5f -> "2x"
        value < 3.5f -> "3x"
        else -> "4x_plus"
    }

    fun durationBucket(activePlayTimeMs: Long): String = when {
        activePlayTimeMs < 60_000L -> "under_1m"
        activePlayTimeMs < 5 * 60_000L -> "1_5m"
        activePlayTimeMs < 15 * 60_000L -> "5_15m"
        activePlayTimeMs < 30 * 60_000L -> "15_30m"
        activePlayTimeMs < 60 * 60_000L -> "30_60m"
        else -> "over_60m"
    }
}

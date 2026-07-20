package com.sbro.emucorex.core

import android.content.Context
import android.os.Bundle
import android.util.Log
import com.google.firebase.analytics.FirebaseAnalytics
import com.sbro.emucorex.BuildConfig
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Centralized, best-effort product analytics.
 *
 * Event methods intentionally accept only bounded technical values. Never add game titles,
 * serials, file paths, account identifiers, or other user-provided strings here.
 */
object AppAnalytics {
    private const val TAG = "AppAnalytics"

    private val ready = AtomicBoolean(false)

    @Volatile
    private var firebaseAnalytics: FirebaseAnalytics? = null

    fun initialize(context: Context) {
        if (ready.get()) return

        runCatching {
            val analytics = FirebaseAnalytics.getInstance(context.applicationContext)
            analytics.setConsent(
                mapOf(
                    FirebaseAnalytics.ConsentType.ANALYTICS_STORAGE to
                        FirebaseAnalytics.ConsentStatus.GRANTED,
                    FirebaseAnalytics.ConsentType.AD_STORAGE to
                        FirebaseAnalytics.ConsentStatus.DENIED,
                    FirebaseAnalytics.ConsentType.AD_USER_DATA to
                        FirebaseAnalytics.ConsentStatus.DENIED,
                    FirebaseAnalytics.ConsentType.AD_PERSONALIZATION to
                        FirebaseAnalytics.ConsentStatus.DENIED
                )
            )
            analytics.setAnalyticsCollectionEnabled(true)
            analytics.setUserProperty(
                "build_type",
                if (BuildConfig.DEBUG) "debug" else "release"
            )
            firebaseAnalytics = analytics
            ready.set(true)
        }.onFailure { error ->
            Log.w(TAG, "Firebase Analytics initialization failed", error)
        }
    }

    fun logOnboardingCompleted(performanceProfile: Int) {
        logEvent("onboarding_complete") {
            putString("performance_profile", AnalyticsDimensions.performanceProfile(performanceProfile))
        }
    }

    fun logEmulationStarted(
        launchType: String,
        renderer: Int,
        audioBackend: Int,
        upscaleMultiplier: Float,
        performanceProfile: Int,
        saveStateLoad: Boolean
    ) {
        logEvent("emulation_start") {
            putString("launch_type", AnalyticsDimensions.sanitizeLaunchType(launchType))
            putString("renderer", AnalyticsDimensions.renderer(renderer))
            putString("audio_backend", AnalyticsDimensions.audioBackend(audioBackend))
            putString("upscale", AnalyticsDimensions.upscale(upscaleMultiplier))
            putString("performance_profile", AnalyticsDimensions.performanceProfile(performanceProfile))
            putLong("save_state_load", saveStateLoad.toAnalyticsLong())
        }
    }

    fun logEmulationStartFailed(launchType: String, reason: String) {
        logEvent("emulation_start_failed") {
            putString("launch_type", AnalyticsDimensions.sanitizeLaunchType(launchType))
            putString("reason", AnalyticsDimensions.sanitizeFailureReason(reason))
        }
    }

    fun logEmulationEnded(activePlayTimeMs: Long, renderer: Int, audioBackend: Int) {
        val safeDurationMs = activePlayTimeMs.coerceAtLeast(0L)
        logEvent("emulation_end") {
            putLong("duration_seconds", safeDurationMs / 1_000L)
            putString("duration_bucket", AnalyticsDimensions.durationBucket(safeDurationMs))
            putString("renderer", AnalyticsDimensions.renderer(renderer))
            putString("audio_backend", AnalyticsDimensions.audioBackend(audioBackend))
        }
    }

    fun logSaveStateAction(action: String, automatic: Boolean, success: Boolean) {
        logEvent("save_state_action") {
            putString("action", AnalyticsDimensions.sanitizeSaveStateAction(action))
            putLong("automatic", automatic.toAnalyticsLong())
            putLong("success", success.toAnalyticsLong())
        }
    }

    private inline fun logEvent(name: String, buildParams: Bundle.() -> Unit) {
        if (!ready.get()) return
        val analytics = firebaseAnalytics ?: return
        runCatching {
            analytics.logEvent(name, Bundle().apply(buildParams))
        }.onFailure { error ->
            Log.w(TAG, "Failed to log analytics event: $name", error)
        }
    }

    private fun Boolean.toAnalyticsLong(): Long = if (this) 1L else 0L
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

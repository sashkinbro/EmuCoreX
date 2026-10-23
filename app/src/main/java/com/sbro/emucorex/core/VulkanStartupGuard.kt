package com.sbro.emucorex.core

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Crash-safe Vulkan startup guard.
 *
 * A native crash (SIGABRT from the vendor driver) kills the process before an in-process
 * renderer fallback can run. This guard records that a Vulkan VM start is in progress and
 * detects on the next app start whether that process actually died from a native crash.
 * If so, the next game launch uses OpenGL once. The saved renderer setting is never
 * modified, and an explicit renderer choice from the user clears the pending fallback.
 *
 * The marker stays armed for a short grace period after the VM reports a successful
 * start, because TFX pipelines are compiled on demand and a broken pipeline can still
 * abort the driver during the first rendered frames.
 */
internal object VulkanStartupGuard {
    private const val PREFS = "vulkan_startup_guard"
    private const val KEY_STARTED_AT = "launch_started_at"
    private const val KEY_PENDING_FALLBACK = "pending_fallback"
    internal const val MAX_CRASH_AGE_MS = 10 * 60 * 1000L
    internal const val STARTUP_GRACE_MS = 60_000L

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    fun beginLaunch(
        context: Context?,
        renderer: Int,
        nowMillis: Long = System.currentTimeMillis()
    ): Long {
        val appContext = context?.applicationContext ?: return 0L
        if (renderer != RendererDefaults.VULKAN) {
            // Spend recovery only when the bridge reaches an actual native launch.
            // BIOS/path validation and cancellation can stop the UI preflight earlier.
            clearPendingFallback(appContext)
            return 0L
        }

        appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putLong(KEY_STARTED_AT, nowMillis)
            .commit()
        return nowMillis
    }

    fun finishLaunch(context: Context?, renderer: Int, token: Long = 0L) {
        val appContext = context?.applicationContext ?: return
        if (renderer != RendererDefaults.VULKAN)
            return

        val preferences = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (token != 0L && preferences.getLong(KEY_STARTED_AT, 0L) != token)
            return

        preferences.edit().remove(KEY_STARTED_AT).commit()
    }

    fun scheduleFinish(
        context: Context?,
        renderer: Int,
        token: Long,
        delayMillis: Long = STARTUP_GRACE_MS
    ) {
        val appContext = context?.applicationContext ?: return
        if (renderer != RendererDefaults.VULKAN || token == 0L)
            return

        scope.launch {
            delay(delayMillis)
            finishLaunch(appContext, renderer, token)
        }
    }

    /** Called once during application startup, after the process has a Context. */
    fun consumeStartupCrash(context: Context?, nowMillis: Long = System.currentTimeMillis()) {
        val appContext = context?.applicationContext ?: return
        val preferences = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val startedAt = preferences.getLong(KEY_STARTED_AT, 0L)
        if (startedAt <= 0L)
            return

        preferences.edit().remove(KEY_STARTED_AT).commit()
        if (!isWithinCrashWindow(startedAt, nowMillis) ||
            !previousLaunchCrashedNatively(appContext, startedAt)) {
            return
        }

        preferences.edit().putBoolean(KEY_PENDING_FALLBACK, true).commit()
    }

    /**
     * Resolves crash recovery during launch preflight. The one-shot fallback remains
     * pending until [beginLaunch] reaches the native OpenGL launch, so failed BIOS/path
     * validation cannot discard recovery. Any other renderer choice clears it.
     */
    fun resolveFallbackRenderer(context: Context?, requestedRenderer: Int): Int? {
        val appContext = context?.applicationContext ?: return null
        val preferences = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (!preferences.getBoolean(KEY_PENDING_FALLBACK, false))
            return null

        if (requestedRenderer == RendererDefaults.VULKAN)
            return RendererDefaults.OPENGL

        clearPendingFallback(appContext)
        return null
    }

    fun clearPendingFallback(context: Context?) {
        val appContext = context?.applicationContext ?: return
        appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .remove(KEY_PENDING_FALLBACK)
            .commit()
    }

    @Suppress("NewApi")
    private fun previousLaunchCrashedNatively(context: Context, startedAt: Long): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
            return false

        val manager = context.getSystemService(ActivityManager::class.java) ?: return false
        val exits = runCatching {
            manager.getHistoricalProcessExitReasons(context.packageName, 0, 8)
        }.getOrNull() ?: return false

        return exits.any { info ->
            info.processName == context.packageName &&
                info.reason == ApplicationExitInfo.REASON_CRASH_NATIVE &&
                info.timestamp >= startedAt
        }
    }

    internal fun isWithinCrashWindow(startedAt: Long, nowMillis: Long): Boolean {
        if (startedAt <= 0L)
            return false

        val age = nowMillis - startedAt
        return age in 0..MAX_CRASH_AGE_MS
    }
}

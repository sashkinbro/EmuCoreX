package com.sbro.emucorex.core

import android.app.UiModeManager
import android.content.Context
import android.content.pm.PackageManager
import android.content.res.Configuration
import androidx.compose.runtime.staticCompositionLocalOf
import kotlin.math.roundToInt

enum class TvInterfaceMode(val preferenceValue: Int) {
    AUTO(0),
    STANDARD(1),
    TV(2);

    companion object {
        fun fromPreference(value: Int?): TvInterfaceMode {
            return entries.firstOrNull { it.preferenceValue == value } ?: AUTO
        }
    }
}

data class TvUiEnvironment(
    val mode: TvInterfaceMode = TvInterfaceMode.AUTO,
    val systemTelevision: Boolean = false,
    val launchedFromTv: Boolean = false,
    val enabled: Boolean = false
)

object TvUiPolicy {
    fun resolve(
        context: Context,
        mode: TvInterfaceMode,
        launchedFromTv: Boolean = false
    ): TvUiEnvironment {
        val systemTelevision = isSystemTelevision(context)
        return TvUiEnvironment(
            mode = mode,
            systemTelevision = systemTelevision,
            launchedFromTv = launchedFromTv,
            enabled = shouldUseTvInterface(mode, systemTelevision, launchedFromTv)
        )
    }

    fun isSystemTelevision(context: Context): Boolean {
        val hasLeanback = context.packageManager.hasSystemFeature(PackageManager.FEATURE_LEANBACK)
        val uiModeManager = context.getSystemService(UiModeManager::class.java)
        val televisionUiMode = uiModeManager?.currentModeType == Configuration.UI_MODE_TYPE_TELEVISION
        return hasLeanback || televisionUiMode
    }

    internal fun shouldUseTvInterface(
        mode: TvInterfaceMode,
        systemTelevision: Boolean,
        launchedFromTv: Boolean
    ): Boolean {
        return when (mode) {
            TvInterfaceMode.AUTO -> systemTelevision || launchedFromTv
            TvInterfaceMode.STANDARD -> false
            TvInterfaceMode.TV -> true
        }
    }
}

/** Layout dimensions which keep interactive TV content inside the common 5% safe area. */
object TvUiMetrics {
    fun safeHorizontalDp(screenWidthDp: Int): Int =
        (screenWidthDp.coerceAtLeast(1) * 0.05f).roundToInt().coerceIn(32, 96)

    fun safeVerticalDp(screenHeightDp: Int): Int =
        (screenHeightDp.coerceAtLeast(1) * 0.05f).roundToInt().coerceIn(20, 54)

    fun navigationWidthDp(screenWidthDp: Int): Int =
        (screenWidthDp.coerceAtLeast(1) * 0.30f).roundToInt().coerceIn(248, 360)

    fun contentReservedWidthDp(screenWidthDp: Int): Int =
        safeHorizontalDp(screenWidthDp) * 2 + navigationWidthDp(screenWidthDp)
}

val LocalTvUiEnvironment = staticCompositionLocalOf { TvUiEnvironment() }

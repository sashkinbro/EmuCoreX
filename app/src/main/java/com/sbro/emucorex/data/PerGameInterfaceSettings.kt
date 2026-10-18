package com.sbro.emucorex.data

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** Sparse interface overrides; older game profiles inherit all of these defaults. */
@Serializable
data class PerGameInterfaceSettings(
    val overlayShow: Boolean = true,
    val overlayScale: Int = 100,
    val overlayOpacity: Int = AppPreferences.DEFAULT_OVERLAY_OPACITY,
    val hideOverlayOnGamepad: Boolean = true,
    val compactControls: Boolean = true,
    val keepScreenOn: Boolean = true,
    val leftStickSensitivity: Int = 100,
    val rightStickSensitivity: Int = 100,
    val invertLeftStick: Boolean = false,
    val invertRightStick: Boolean = false,
    val invertLeftStickHorizontal: Boolean = false,
    val invertRightStickHorizontal: Boolean = false,
    val fpsOverlayCorner: Int = AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT,
    val fpsOverlayScale: Int = AppPreferences.DEFAULT_FPS_OVERLAY_SCALE,
    val fpsOverlayMetrics: Int = PerformanceOverlayMetrics.DEFAULT,
    val autoSaveEnabled: Boolean = false,
    val autoSaveIntervalMinutes: Int = 1,
    val providedKeys: Set<String> = emptySet()
) {
    fun sanitized(): PerGameInterfaceSettings = copy(
        overlayScale = overlayScale.coerceIn(50, 150),
        overlayOpacity = overlayOpacity.coerceIn(AppPreferences.OVERLAY_OPACITY_MIN, AppPreferences.OVERLAY_OPACITY_MAX),
        leftStickSensitivity = leftStickSensitivity.coerceIn(50, 200),
        rightStickSensitivity = rightStickSensitivity.coerceIn(50, 200),
        fpsOverlayCorner = fpsOverlayCorner.takeIf { it in 0..3 } ?: AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT,
        fpsOverlayScale = fpsOverlayScale.coerceIn(AppPreferences.MIN_FPS_OVERLAY_SCALE, AppPreferences.MAX_FPS_OVERLAY_SCALE),
        fpsOverlayMetrics = PerformanceOverlayMetrics.sanitize(fpsOverlayMetrics),
        autoSaveIntervalMinutes = autoSaveIntervalMinutes.coerceIn(1, 999),
        providedKeys = providedKeys.intersect(KEYS)
    )

    fun resolveAgainst(defaults: PerGameInterfaceSettings): PerGameInterfaceSettings {
        val own = sanitized()
        return defaults.copy(
            overlayShow = if ("overlayShow" in own.providedKeys) own.overlayShow else defaults.overlayShow,
            overlayScale = if ("overlayScale" in own.providedKeys) own.overlayScale else defaults.overlayScale,
            overlayOpacity = if ("overlayOpacity" in own.providedKeys) own.overlayOpacity else defaults.overlayOpacity,
            hideOverlayOnGamepad = if ("hideOverlayOnGamepad" in own.providedKeys) own.hideOverlayOnGamepad else defaults.hideOverlayOnGamepad,
            compactControls = if ("compactControls" in own.providedKeys) own.compactControls else defaults.compactControls,
            keepScreenOn = if ("keepScreenOn" in own.providedKeys) own.keepScreenOn else defaults.keepScreenOn,
            leftStickSensitivity = if ("leftStickSensitivity" in own.providedKeys) own.leftStickSensitivity else defaults.leftStickSensitivity,
            rightStickSensitivity = if ("rightStickSensitivity" in own.providedKeys) own.rightStickSensitivity else defaults.rightStickSensitivity,
            invertLeftStick = if ("invertLeftStick" in own.providedKeys) own.invertLeftStick else defaults.invertLeftStick,
            invertRightStick = if ("invertRightStick" in own.providedKeys) own.invertRightStick else defaults.invertRightStick,
            invertLeftStickHorizontal = if ("invertLeftStickHorizontal" in own.providedKeys) own.invertLeftStickHorizontal else defaults.invertLeftStickHorizontal,
            invertRightStickHorizontal = if ("invertRightStickHorizontal" in own.providedKeys) own.invertRightStickHorizontal else defaults.invertRightStickHorizontal,
            fpsOverlayCorner = if ("fpsOverlayCorner" in own.providedKeys) own.fpsOverlayCorner else defaults.fpsOverlayCorner,
            fpsOverlayScale = if ("fpsOverlayScale" in own.providedKeys) own.fpsOverlayScale else defaults.fpsOverlayScale,
            fpsOverlayMetrics = if ("fpsOverlayMetrics" in own.providedKeys) own.fpsOverlayMetrics else defaults.fpsOverlayMetrics,
            autoSaveEnabled = if ("autoSaveEnabled" in own.providedKeys) own.autoSaveEnabled else defaults.autoSaveEnabled,
            autoSaveIntervalMinutes = if ("autoSaveIntervalMinutes" in own.providedKeys) own.autoSaveIntervalMinutes else defaults.autoSaveIntervalMinutes,
            providedKeys = own.providedKeys
        ).sanitized()
    }

    fun overridesComparedWith(defaults: PerGameInterfaceSettings): PerGameInterfaceSettings? {
        val own = sanitized()
        val base = defaults.sanitized()
        val keys = buildSet {
            if (own.overlayShow != base.overlayShow) add("overlayShow")
            if (own.overlayScale != base.overlayScale) add("overlayScale")
            if (own.overlayOpacity != base.overlayOpacity) add("overlayOpacity")
            if (own.hideOverlayOnGamepad != base.hideOverlayOnGamepad) add("hideOverlayOnGamepad")
            if (own.compactControls != base.compactControls) add("compactControls")
            if (own.keepScreenOn != base.keepScreenOn) add("keepScreenOn")
            if (own.leftStickSensitivity != base.leftStickSensitivity) add("leftStickSensitivity")
            if (own.rightStickSensitivity != base.rightStickSensitivity) add("rightStickSensitivity")
            if (own.invertLeftStick != base.invertLeftStick) add("invertLeftStick")
            if (own.invertRightStick != base.invertRightStick) add("invertRightStick")
            if (own.invertLeftStickHorizontal != base.invertLeftStickHorizontal) add("invertLeftStickHorizontal")
            if (own.invertRightStickHorizontal != base.invertRightStickHorizontal) add("invertRightStickHorizontal")
            if (own.fpsOverlayCorner != base.fpsOverlayCorner) add("fpsOverlayCorner")
            if (own.fpsOverlayScale != base.fpsOverlayScale) add("fpsOverlayScale")
            if (own.fpsOverlayMetrics != base.fpsOverlayMetrics) add("fpsOverlayMetrics")
            if (own.autoSaveEnabled != base.autoSaveEnabled) add("autoSaveEnabled")
            if (own.autoSaveIntervalMinutes != base.autoSaveIntervalMinutes) add("autoSaveIntervalMinutes")
        }
        return own.copy(providedKeys = keys).takeIf { keys.isNotEmpty() }
    }

    fun encode(): String = codec.encodeToString(sanitized())

    companion object {
        private val codec = Json { ignoreUnknownKeys = true }
        fun decode(value: String): PerGameInterfaceSettings = codec.decodeFromString<PerGameInterfaceSettings>(value).sanitized()

        val KEYS: Set<String> = setOf(
            "overlayShow",
            "overlayScale",
            "overlayOpacity",
            "hideOverlayOnGamepad",
            "compactControls",
            "keepScreenOn",
            "leftStickSensitivity",
            "rightStickSensitivity",
            "invertLeftStick",
            "invertRightStick",
            "invertLeftStickHorizontal",
            "invertRightStickHorizontal",
            "fpsOverlayCorner",
            "fpsOverlayScale",
            "fpsOverlayMetrics",
            "autoSaveEnabled",
            "autoSaveIntervalMinutes",
        )
    }
}

internal fun SettingsSnapshot.toInterfaceSettings(autoSaveEnabled: Boolean = false, autoSaveIntervalMinutes: Int = 1): PerGameInterfaceSettings = PerGameInterfaceSettings(
    overlayShow = overlayShow,
    overlayScale = overlayScale,
    overlayOpacity = overlayOpacity,
    hideOverlayOnGamepad = hideOverlayOnGamepad,
    compactControls = compactControls,
    keepScreenOn = keepScreenOn,
    leftStickSensitivity = leftStickSensitivity,
    rightStickSensitivity = rightStickSensitivity,
    invertLeftStick = invertLeftStick,
    invertRightStick = invertRightStick,
    invertLeftStickHorizontal = invertLeftStickHorizontal,
    invertRightStickHorizontal = invertRightStickHorizontal,
    fpsOverlayCorner = fpsOverlayCorner,
    fpsOverlayScale = fpsOverlayScale,
    fpsOverlayMetrics = fpsOverlayMetrics,
    autoSaveEnabled = autoSaveEnabled,
    autoSaveIntervalMinutes = autoSaveIntervalMinutes,
)

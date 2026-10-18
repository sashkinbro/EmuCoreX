package com.sbro.emucorex.data

import kotlinx.serialization.json.Json
import org.junit.Assert.*
import org.junit.Test

class PerGameInterfaceSettingsTest {
    private val defaults = PerGameInterfaceSettings()

    @Test fun unchangedSettingsRemainInherited() {
        assertNull(defaults.overridesComparedWith(defaults))
    }

    @Test fun everyInterfaceSettingRoundTripsAsAnIndependentOverride() {
        val changes: Map<String, PerGameInterfaceSettings> = linkedMapOf(
            "overlayShow" to defaults.copy(overlayShow = false),
            "overlayScale" to defaults.copy(overlayScale = 125),
            "overlayOpacity" to defaults.copy(overlayOpacity = 25),
            "hideOverlayOnGamepad" to defaults.copy(hideOverlayOnGamepad = false),
            "compactControls" to defaults.copy(compactControls = false),
            "keepScreenOn" to defaults.copy(keepScreenOn = false),
            "leftStickSensitivity" to defaults.copy(leftStickSensitivity = 150),
            "rightStickSensitivity" to defaults.copy(rightStickSensitivity = 125),
            "invertLeftStick" to defaults.copy(invertLeftStick = true),
            "invertRightStick" to defaults.copy(invertRightStick = true),
            "invertLeftStickHorizontal" to defaults.copy(invertLeftStickHorizontal = true),
            "invertRightStickHorizontal" to defaults.copy(invertRightStickHorizontal = true),
            "fpsOverlayCorner" to defaults.copy(fpsOverlayCorner = 2),
            "fpsOverlayScale" to defaults.copy(fpsOverlayScale = 175),
            "fpsOverlayMetrics" to defaults.copy(fpsOverlayMetrics = 0),
            "autoSaveEnabled" to defaults.copy(autoSaveEnabled = true),
            "autoSaveIntervalMinutes" to defaults.copy(autoSaveIntervalMinutes = 5)
        )
        for ((key, changed) in changes) {
            val override = requireNotNull(changed.overridesComparedWith(defaults))
            assertEquals(key, setOf(key), override.providedKeys)
            val reloaded = Json.decodeFromString<PerGameInterfaceSettings>(Json.encodeToString(override))
            assertEquals(key, changed, reloaded.resolveAgainst(defaults).copy(providedKeys = emptySet()))
            assertNull(key, reloaded.resolveAgainst(changed).overridesComparedWith(changed))
        }
    }

    @Test fun unownedSettingsTrackNewDefaultsWithoutAffectingAnotherGame() {
        val gameA = requireNotNull(defaults.copy(overlayScale = 125).overridesComparedWith(defaults))
        val newDefaults = defaults.copy(overlayOpacity = 30, keepScreenOn = false)
        val expectedA = newDefaults.copy(overlayScale = 125, providedKeys = setOf("overlayScale"))
        assertEquals(expectedA, gameA.resolveAgainst(newDefaults))
        assertEquals(newDefaults, PerGameInterfaceSettings().resolveAgainst(newDefaults))
        assertEquals(100, defaults.overlayScale)
    }

    @Test fun encodedOverridesRoundTripIncludingOwnership() {
        val override = requireNotNull(
            defaults.copy(overlayScale = 125, autoSaveEnabled = true, autoSaveIntervalMinutes = 5)
                .overridesComparedWith(defaults)
        )
        assertEquals(override, PerGameInterfaceSettings.decode(override.encode()))
        assertEquals(
            setOf("overlayScale", "autoSaveEnabled", "autoSaveIntervalMinutes"),
            PerGameInterfaceSettings.decode(override.encode()).providedKeys
        )
    }

    @Test fun importedValuesAreClampedBeforeUse() {
        val invalid = defaults.copy(overlayScale = -1, overlayOpacity = 999,
            leftStickSensitivity = 0, fpsOverlayCorner = 99, fpsOverlayScale = 999,
            autoSaveIntervalMinutes = -10).sanitized()
        assertEquals(50, invalid.overlayScale)
        assertEquals(AppPreferences.OVERLAY_OPACITY_MAX, invalid.overlayOpacity)
        assertEquals(50, invalid.leftStickSensitivity)
        assertEquals(AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT, invalid.fpsOverlayCorner)
        assertEquals(AppPreferences.MAX_FPS_OVERLAY_SCALE, invalid.fpsOverlayScale)
        assertEquals(1, invalid.autoSaveIntervalMinutes)
    }
}

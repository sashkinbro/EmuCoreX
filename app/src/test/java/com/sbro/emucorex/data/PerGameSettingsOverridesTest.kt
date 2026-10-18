package com.sbro.emucorex.data

import org.junit.Assert.*
import org.junit.Test

class PerGameSettingsOverridesTest {
    private val defaults = PerGameSettings(gameKey = "A.iso", gameTitle = "A")

    @Test fun savingUnchangedManagerDraftDoesNotFreezeDefaults() {
        assertEquals(emptySet<String>(), defaults.withOnlyOverridesComparedWith(defaults).providedKeys)
    }

    @Test fun managerKeepsOnlyChangedRuntimeAndInterfaceSettings() {
        val draft = defaults.copy(renderer = 2,
            interfaceSettings = PerGameInterfaceSettings(overlayScale = 125))
        val saved = draft.withOnlyOverridesComparedWith(defaults)
        assertEquals(setOf("renderer", "interfaceSettings"), saved.providedKeys)
        assertEquals(setOf("overlayScale"), saved.interfaceSettings?.providedKeys)
        val reverted = draft.copy(renderer = defaults.renderer,
            interfaceSettings = PerGameInterfaceSettings()).withOnlyOverridesComparedWith(defaults)
        assertEquals(emptySet<String>(), reverted.providedKeys)
        assertNull(reverted.interfaceSettings)
    }

    @Test fun existingExplicitGsChoicesCanBeRetainedWithoutPinningOtherDefaults() {
        val saved = defaults.withOnlyOverridesComparedWith(defaults, retainedKeys = setOf("hwDownloadMode"))
        assertEquals(setOf("hwDownloadMode"), saved.providedKeys)
    }

    @Test fun touchControlsLayoutAndCustomLibrarySurviveSparseSaving() {
        val saved = defaults.copy(
            touchControlsLayout = TouchControlsLayoutProfile(stickScale = 130),
            customTouchControls = CustomTouchControlLibrary.Empty
        ).withOnlyOverridesComparedWith(defaults)
        assertEquals(
            setOf("touchControlsLayout", "customTouchControls"),
            saved.providedKeys
        )
        assertEquals(130, saved.touchControlsLayout?.stickScale)
        assertEquals(CustomTouchControlLibrary.Empty, saved.customTouchControls)
    }

    @Test fun metadataDoesNotCreateOverridesOrChangeAnotherGame() {
        val other = defaults.copy(gameKey = "B.iso", gameTitle = "B", updatedAt = 42)
        assertEquals(emptySet<String>(), other.withOnlyOverridesComparedWith(defaults).providedKeys)
        assertEquals("A.iso", defaults.gameKey)
    }
}

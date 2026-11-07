package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.PerGameSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ShaderChainSelectionTest {
    @Test
    fun missingProfileInheritsGlobalShader() {
        val selection = resolveShaderChainSelection(
            globalEnabled = true,
            globalPreset = " /shaders/crt.slangp ",
            profile = null
        )

        assertNull(selection.overrideEnabled)
        assertFalse(selection.usesGameProfile)
        assertTrue(selection.effectiveEnabled)
        assertEquals("/shaders/crt.slangp", selection.effectivePreset)
        assertEquals("", selection.gamePreset)
    }

    @Test
    fun gameOverrideUsesItsOwnPresetAndLeavesGlobalVisible() {
        val selection = resolveShaderChainSelection(
            globalEnabled = true,
            globalPreset = "/shaders/global.slangp",
            profile = profile(overrideEnabled = true, preset = " /shaders/game.slangp ")
        )

        assertTrue(selection.usesGameProfile)
        assertTrue(selection.effectiveEnabled)
        assertEquals("/shaders/game.slangp", selection.effectivePreset)
        assertEquals("/shaders/game.slangp", selection.gamePreset)
        assertEquals("/shaders/global.slangp", selection.globalPreset)
    }

    @Test
    fun gameOffDisablesInheritedGlobalShader() {
        val selection = resolveShaderChainSelection(
            globalEnabled = true,
            globalPreset = "/shaders/global.slangp",
            profile = profile(overrideEnabled = false, preset = "")
        )

        assertTrue(selection.usesGameProfile)
        assertFalse(selection.effectiveEnabled)
        assertEquals("", selection.effectivePreset)
    }

    @Test
    fun emptyGamePresetCannotEnableShaders() {
        val selection = resolveShaderChainSelection(
            globalEnabled = true,
            globalPreset = "/shaders/global.slangp",
            profile = profile(overrideEnabled = true, preset = "   ")
        )

        assertTrue(selection.usesGameProfile)
        assertFalse(selection.effectiveEnabled)
        assertEquals("", selection.effectivePreset)
    }

    @Test
    fun disabledGlobalPresetStaysVisibleButInactive() {
        val selection = resolveShaderChainSelection(
            globalEnabled = false,
            globalPreset = "/shaders/global.slangp",
            profile = null
        )

        assertFalse(selection.usesGameProfile)
        assertFalse(selection.effectiveEnabled)
        assertEquals("/shaders/global.slangp", selection.effectivePreset)
    }

    @Test
    fun updatePresetsAreAppliedToTheMatchingScope() {
        val gameEnabled = ShaderChainUpdate.gameEnabled(" /shaders/game.slangp ")
        assertEquals(true, gameEnabled.overrideEnabled)
        assertEquals(" /shaders/game.slangp ", gameEnabled.preset)

        assertEquals(false, ShaderChainUpdate.GameDisabled.overrideEnabled)
        assertEquals("", ShaderChainUpdate.GameDisabled.preset)

        assertNull(ShaderChainUpdate.InheritGlobal.overrideEnabled)
        assertEquals("", ShaderChainUpdate.InheritGlobal.preset)
    }

    private fun profile(
        overrideEnabled: Boolean? = null,
        preset: String = ""
    ) = PerGameSettings(
        gameKey = "game.iso",
        gameTitle = "Game",
        shaderChainOverrideEnabled = overrideEnabled,
        shaderChainPreset = preset
    )
}

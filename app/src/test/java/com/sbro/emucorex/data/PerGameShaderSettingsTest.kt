package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PerGameShaderSettingsTest {
    @Test
    fun inheritedShaderTracksGlobalSelection() {
        val resolved = profile().resolveShaderChain(
            globalEnabled = true,
            globalPreset = " /shaders/global.slangp "
        )

        assertTrue(resolved.enabled)
        assertEquals("/shaders/global.slangp", resolved.preset)
    }

    @Test
    fun disabledOverrideClearsGlobalShader() {
        val resolved = profile(
            overrideEnabled = false,
            preset = "/shaders/game.slangp"
        ).resolveShaderChain(
            globalEnabled = true,
            globalPreset = "/shaders/global.slangp"
        )

        assertFalse(resolved.enabled)
        assertEquals("", resolved.preset)
    }

    @Test
    fun enabledOverrideUsesGamePreset() {
        val resolved = profile(
            overrideEnabled = true,
            preset = " /shaders/game.slangp "
        ).resolveShaderChain(
            globalEnabled = false,
            globalPreset = ""
        )

        assertTrue(resolved.enabled)
        assertEquals("/shaders/game.slangp", resolved.preset)
    }

    @Test
    fun emptyGamePresetCannotEnableShaderChain() {
        val resolved = profile(
            overrideEnabled = true,
            preset = "  "
        ).resolveShaderChain(
            globalEnabled = true,
            globalPreset = "/shaders/global.slangp"
        )

        assertFalse(resolved.enabled)
        assertEquals("", resolved.preset)
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

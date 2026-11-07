package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.RetroArchShaderPreset
import com.sbro.emucorex.data.ShaderPackInstallProgress

/**
 * Effective shader-chain selection for the running game.
 *
 * [overrideEnabled] follows the persisted per-game semantics:
 * null inherits the app-wide shader, false turns shaders off for this game and
 * true uses [profilePreset] for this game. The app-wide selection is kept
 * untouched in all three cases, so changing a shader from the in-game menu
 * never edits the global preference.
 */
internal data class ShaderChainSelection(
    val globalEnabled: Boolean = false,
    val globalPreset: String = "",
    val overrideEnabled: Boolean? = null,
    val profilePreset: String = ""
) {
    val usesGameProfile: Boolean
        get() = overrideEnabled != null

    val effectivePreset: String
        get() = when (overrideEnabled) {
            null -> globalPreset
            false -> ""
            true -> profilePreset
        }.trim()

    val effectiveEnabled: Boolean
        get() = (overrideEnabled ?: globalEnabled) && effectivePreset.isNotEmpty()

    /** The preset the game profile stores, and which an enable toggle restores. */
    val gamePreset: String
        get() = profilePreset.trim()
}

internal fun resolveShaderChainSelection(
    globalEnabled: Boolean,
    globalPreset: String,
    profile: PerGameSettings?
): ShaderChainSelection = ShaderChainSelection(
    globalEnabled = globalEnabled,
    globalPreset = globalPreset.trim(),
    overrideEnabled = profile?.shaderChainOverrideEnabled,
    profilePreset = profile?.shaderChainPreset.orEmpty().trim()
)

/** A profile write requested by an in-game shader change. */
internal data class ShaderChainUpdate(
    val overrideEnabled: Boolean?,
    val preset: String
) {
    companion object {
        val InheritGlobal = ShaderChainUpdate(overrideEnabled = null, preset = "")
        fun gameEnabled(preset: String) = ShaderChainUpdate(overrideEnabled = true, preset = preset)
        val GameDisabled = ShaderChainUpdate(overrideEnabled = false, preset = "")
    }
}

internal enum class ShaderPackMessage {
    Installed,
    Failed
}

/**
 * State of the in-game Shaders tab. It lives in the view model (like the cheat
 * pack browser) so a shader-pack download survives the menu being closed, and
 * the preset catalog is not rescanned on every tab switch.
 */
internal data class ShaderBrowserState(
    val loading: Boolean = false,
    val presets: List<RetroArchShaderPreset> = emptyList(),
    val installed: Boolean = false,
    val busy: Boolean = false,
    val progress: ShaderPackInstallProgress? = null,
    val message: ShaderPackMessage? = null,
    val selection: ShaderChainSelection = ShaderChainSelection()
) {
    val presetCount: Int
        get() = presets.size
}

internal fun shaderPresetShortLabel(preset: RetroArchShaderPreset): String =
    preset.label.substringAfterLast('/')

internal fun findShaderPreset(
    presets: List<RetroArchShaderPreset>,
    path: String
): RetroArchShaderPreset? = path.takeIf(String::isNotBlank)?.let { selected ->
    presets.firstOrNull { it.absolutePath == selected }
}

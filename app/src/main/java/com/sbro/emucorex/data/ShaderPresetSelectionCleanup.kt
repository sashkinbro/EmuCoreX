// SPDX-FileCopyrightText: 2026 SBRO
// SPDX-License-Identifier: LicenseRef-EmuCoreX-Proprietary
package com.sbro.emucorex.data

import java.io.File
import kotlinx.coroutines.flow.first

data class ShaderPresetSelectionCleanupResult(
    val globalCleared: Boolean,
    val clearedGameKeys: Set<String>,
    val activeSelectionCleared: Boolean
)

/** Compare canonical identities without requiring tombstoned files to be deleted. */
internal class RemovedShaderPresetPaths(paths: Collection<String>) {
    private val canonicalPaths = paths.mapNotNull(::canonicalPresetPath).toSet()

    fun contains(path: String?): Boolean = canonicalPresetPath(path)?.let(canonicalPaths::contains) == true
}

private fun canonicalPresetPath(path: String?): String? =
    path?.takeIf(String::isNotBlank)?.let { runCatching { File(it).canonicalPath }.getOrNull() }

internal fun PerGameSettings.withoutRemovedShaderSelection(
    removed: RemovedShaderPresetPaths
): PerGameSettings? {
    if (!removed.contains(shaderChainPreset)) return null
    return copy(
        // An inherited profile must keep inheriting an unrelated global preset.
        shaderChainOverrideEnabled = shaderChainOverrideEnabled?.let { false },
        shaderChainPreset = ""
    )
}

internal suspend fun clearRemovedShaderSelections(
    removedPaths: Collection<String>,
    globalPresetPath: String,
    profiles: List<PerGameSettings>,
    activePresetPath: String?,
    clearGlobal: suspend () -> Unit,
    saveProfile: (PerGameSettings) -> Unit
): ShaderPresetSelectionCleanupResult {
    val removed = RemovedShaderPresetPaths(removedPaths)
    val globalCleared = removed.contains(globalPresetPath)
    if (globalCleared) clearGlobal()
    val changedKeys = linkedSetOf<String>()
    profiles.forEach { profile ->
        profile.withoutRemovedShaderSelection(removed)?.let { updated ->
            saveProfile(updated)
            changedKeys += updated.gameKey
        }
    }
    return ShaderPresetSelectionCleanupResult(
        globalCleared = globalCleared,
        clearedGameKeys = changedKeys,
        activeSelectionCleared = removed.contains(activePresetPath)
    )
}

/** Persists selection cleanup after successful logical removal; performs no file deletion.
 * The caller supplies the running effective preset to also handle inherited selections.
 * Persistence failures propagate so the UI can report them and still clear active rendering.
 */
object ShaderPresetSelectionCleanup {
    suspend fun clearRemovedSelections(
        preferences: AppPreferences,
        profiles: PerGameSettingsRepository,
        removedPaths: Collection<String>,
        activePresetPath: String? = null
    ): ShaderPresetSelectionCleanupResult {
        if (removedPaths.isEmpty()) return ShaderPresetSelectionCleanupResult(false, emptySet(), false)
        val global = preferences.settingsSnapshot.first()
        return clearRemovedShaderSelections(
            removedPaths = removedPaths,
            globalPresetPath = global.shaderChainPreset,
            profiles = profiles.getAll(),
            activePresetPath = activePresetPath,
            clearGlobal = { preferences.setShaderChain(false, "") },
            saveProfile = profiles::save
        )
    }
}

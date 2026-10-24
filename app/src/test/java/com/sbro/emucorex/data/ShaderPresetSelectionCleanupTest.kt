// SPDX-FileCopyrightText: 2026 SBRO
// SPDX-License-Identifier: LicenseRef-EmuCoreX-Proprietary
package com.sbro.emucorex.data

import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class ShaderPresetSelectionCleanupTest {
    @Test fun tombstonedPresetStillOnDiskMatchesItsCanonicalSelection() {
        val root = Files.createTempDirectory("shader-cleanup-").toFile()
        try {
            val preset = root.resolve("crt.slangp").apply { writeText("authored fixture") }
            val removed = RemovedShaderPresetPaths(listOf(preset.path))
            assertTrue(preset.exists())
            assertTrue(removed.contains(root.resolve("unused/../crt.slangp").path))
            assertFalse(removed.contains(root.resolve("crt-other.slangp").path))
            assertFalse(removed.contains(root.resolve("sibling/crt.slangp").path))
            assertFalse(removed.contains(""))
            assertFalse(removed.contains(null))
        } finally { root.deleteRecursively() }
    }

    @Test fun explicitRemovedOverrideBecomesDisabledWithoutChangingOtherProfileSettings() {
        val profile = PerGameSettings(
            gameKey = "game", gameTitle = "Game", renderer = 14, upscaleMultiplier = 2f,
            shaderChainOverrideEnabled = true, shaderChainPreset = "/authored/crt.slangp",
            providedKeys = setOf("renderer")
        )
        val cleared = profile.withoutRemovedShaderSelection(RemovedShaderPresetPaths(listOf(profile.shaderChainPreset)))!!
        assertEquals(profile.copy(shaderChainOverrideEnabled = false, shaderChainPreset = ""), cleared)
    }

    @Test fun disabledStaleSelectionIsClearedToo() {
        val profile = PerGameSettings("game", "Game", shaderChainOverrideEnabled = false, shaderChainPreset = "/authored/crt.slangp")
        val cleared = profile.withoutRemovedShaderSelection(RemovedShaderPresetPaths(listOf(profile.shaderChainPreset)))!!
        assertEquals(false, cleared.shaderChainOverrideEnabled)
        assertEquals("", cleared.shaderChainPreset)
    }

    @Test fun inheritedProfileRetainsInheritanceWhileDiscardingItsUnusedStalePath() {
        val profile = PerGameSettings("game", "Game", shaderChainOverrideEnabled = null, shaderChainPreset = "/authored/crt.slangp")
        val cleared = profile.withoutRemovedShaderSelection(RemovedShaderPresetPaths(listOf(profile.shaderChainPreset)))!!
        assertNull(cleared.shaderChainOverrideEnabled)
        assertEquals("", cleared.shaderChainPreset)
    }

    @Test fun unrelatedAndAlreadyEmptyProfilesNeedNoWrite() {
        val removed = RemovedShaderPresetPaths(listOf("/authored/removed.slangp"))
        assertNull(PerGameSettings("other", "Other", shaderChainOverrideEnabled = true, shaderChainPreset = "/authored/keep.slangp").withoutRemovedShaderSelection(removed))
        assertNull(PerGameSettings("inherit", "Inherited").withoutRemovedShaderSelection(removed))
    }

    @Test fun emptyRemovalDoesNotDisableAnySelection() {
        val removed = RemovedShaderPresetPaths(emptyList())
        assertFalse(removed.contains("/authored/keep.slangp"))
        assertNull(PerGameSettings("game", "Game", shaderChainOverrideEnabled = true, shaderChainPreset = "/authored/keep.slangp").withoutRemovedShaderSelection(removed))
    }

    @Test fun removalClearsGlobalAllMatchingOverridesAndInheritedActiveSelection() = runBlocking {
        val path = "/authored/remove.slangp"
        val saved = mutableListOf<PerGameSettings>()
        var globalWrites = 0
        val first = PerGameSettings("first", "First", shaderChainOverrideEnabled = true, shaderChainPreset = path)
        val second = PerGameSettings("second", "Second", shaderChainOverrideEnabled = false, shaderChainPreset = path)
        val inherit = PerGameSettings("inherit", "Inherited")
        val keep = PerGameSettings("keep", "Keep", shaderChainOverrideEnabled = true, shaderChainPreset = "/authored/keep.slangp")
        val result = clearRemovedShaderSelections(
            listOf(path), path, listOf(first, second, inherit, keep), path,
            clearGlobal = { globalWrites++ }, saveProfile = saved::add
        )
        assertEquals(1, globalWrites)
        assertTrue(result.globalCleared)
        assertTrue(result.activeSelectionCleared)
        assertEquals(setOf("first", "second"), result.clearedGameKeys)
        assertEquals(listOf(first, second).map { it.copy(shaderChainOverrideEnabled = false, shaderChainPreset = "") }, saved)
    }

    @Test fun deletingOnlyPerGameSelectionPreservesUnrelatedGlobalAndActiveGame() = runBlocking {
        val path = "/authored/remove.slangp"
        val saved = mutableListOf<PerGameSettings>()
        val profile = PerGameSettings("game", "Game", shaderChainOverrideEnabled = true, shaderChainPreset = path)
        val result = clearRemovedShaderSelections(
            listOf(path), "/authored/keep.slangp", listOf(profile), "/authored/keep.slangp",
            clearGlobal = { fail("Unrelated global selection must not be written") }, saveProfile = saved::add
        )
        assertFalse(result.globalCleared)
        assertFalse(result.activeSelectionCleared)
        assertEquals(setOf("game"), result.clearedGameKeys)
        assertEquals("", saved.single().shaderChainPreset)
    }

    @Test fun removingGlobalDoesNotDisableAnActiveUnrelatedOverride() = runBlocking {
        val result = clearRemovedShaderSelections(
            listOf("/authored/global.slangp"), "/authored/global.slangp", emptyList(), "/authored/override.slangp",
            clearGlobal = {}, saveProfile = { fail("No profile needs rewriting") }
        )
        assertTrue(result.globalCleared)
        assertFalse(result.activeSelectionCleared)
        assertTrue(result.clearedGameKeys.isEmpty())
    }

}

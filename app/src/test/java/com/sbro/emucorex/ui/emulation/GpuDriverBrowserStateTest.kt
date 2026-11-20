package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.PerGameSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GpuDriverBrowserStateTest {
    private val base = PerGameSettings(
        gameKey = "/g.iso",
        gameTitle = "G",
        providedKeys = setOf("renderer")
    )

    @Test
    fun overrideAddsDriverKeysAndPath() {
        val updated = base.withGpuDriverUpdate(GpuDriverProfileUpdate.Override("/d/libvulkan.so"))
        assertEquals(1, updated.gpuDriverType)
        assertEquals("/d/libvulkan.so", updated.customDriverPath)
        assertTrue(updated.providedKeys!!.containsAll(GPU_DRIVER_PROFILE_KEYS))
        assertTrue(updated.ownsGpuDriver())
    }

    @Test
    fun systemOverrideKeepsOwnershipWithoutPath() {
        val updated = base.withGpuDriverUpdate(GpuDriverProfileUpdate.Override(null))
        assertEquals(0, updated.gpuDriverType)
        assertNull(updated.customDriverPath)
        assertTrue(updated.ownsGpuDriver())
    }

    @Test
    fun useGlobalClearsDriverKeys() {
        val pinned = base.withGpuDriverUpdate(GpuDriverProfileUpdate.Override("/d/libvulkan.so"))
        val cleared = pinned.withGpuDriverUpdate(GpuDriverProfileUpdate.UseGlobal)
        assertEquals(0, cleared.gpuDriverType)
        assertNull(cleared.customDriverPath)
        assertFalse(cleared.ownsGpuDriver())
        assertEquals(setOf("renderer"), cleared.providedKeys)
    }

    @Test
    fun effectivePathFollowsScopeSemantics() {
        val state = GpuDriverBrowserState(
            canUseGameScope = true,
            scope = GpuDriverScope.Game,
            globalPath = "/g",
            gameOverride = true,
            gamePath = "/game"
        )
        assertEquals("/game", state.effectivePath)
        assertEquals("/g", state.copy(gameOverride = false).effectivePath)
        assertEquals("/g", state.copy(canUseGameScope = false).effectivePath)
    }

    @Test
    fun folderNamesBecomeFriendlyDriverNames() {
        val mali = friendlyGpuDriverIdentity("malisx2-0.5.1-308fe121c7")
        assertEquals("MaliSX2 0.5.1", mali.title)
        assertEquals("0.5.1", mali.version)
        assertEquals("Turnip 25.1.0", friendlyGpuDriverIdentity("turnip_v25.1.0_R3").title)
        assertEquals("Turnip axfl3-003", friendlyGpuDriverIdentity("turnip-axfl3-003-android").title)
        assertEquals("custom driver", friendlyGpuDriverIdentity("custom-driver").title.lowercase())
        assertEquals("1234", friendlyGpuDriverIdentity("1234").title)
    }
}

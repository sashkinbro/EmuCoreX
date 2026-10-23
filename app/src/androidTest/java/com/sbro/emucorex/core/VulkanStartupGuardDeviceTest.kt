package com.sbro.emucorex.core

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class VulkanStartupGuardDeviceTest {
    private val context = object : ContextWrapper(ApplicationProvider.getApplicationContext<Context>()) {
        override fun getApplicationContext(): Context = this
        override fun getSharedPreferences(name: String, mode: Int): SharedPreferences =
            super.getSharedPreferences("guard-test-$name", mode)
    }
    private val preferences = context.getSharedPreferences("vulkan_startup_guard", Context.MODE_PRIVATE)

    @Before
    fun reset() {
        preferences.edit().clear().commit()
    }

    @After
    fun cleanUp() {
        preferences.edit().clear().commit()
    }

    @Test
    fun failedPreflightRetainsFallbackUntilNativeLaunch() {
        preferences.edit().putBoolean("pending_fallback", true).commit()

        assertEquals(RendererDefaults.OPENGL,
            VulkanStartupGuard.resolveFallbackRenderer(context, RendererDefaults.VULKAN))
        // BIOS/path validation or cancellation can end a launch before beginLaunch.
        assertEquals("A preflight failure must not spend crash recovery", RendererDefaults.OPENGL,
            VulkanStartupGuard.resolveFallbackRenderer(context, RendererDefaults.VULKAN))

        assertEquals(0L, VulkanStartupGuard.beginLaunch(context, RendererDefaults.OPENGL))
        assertNull("The actual native launch spends recovery exactly once",
            VulkanStartupGuard.resolveFallbackRenderer(context, RendererDefaults.VULKAN))
    }

    @Test
    fun manualRendererChoiceClearsFallback() {
        preferences.edit().putBoolean("pending_fallback", true).commit()
        VulkanStartupGuard.clearPendingFallback(context)
        assertNull(VulkanStartupGuard.resolveFallbackRenderer(context, RendererDefaults.VULKAN))
    }

    @Test
    fun nonVulkanSelectionClearsFallback() {
        preferences.edit().putBoolean("pending_fallback", true).commit()
        assertNull(VulkanStartupGuard.resolveFallbackRenderer(context, RendererDefaults.OPENGL))
        assertNull(VulkanStartupGuard.resolveFallbackRenderer(context, RendererDefaults.VULKAN))
    }

    @Test
    fun oldLaunchCompletionCannotDisarmNewLaunch() {
        val first = VulkanStartupGuard.beginLaunch(context, RendererDefaults.VULKAN, 1_000L)
        val second = VulkanStartupGuard.beginLaunch(context, RendererDefaults.VULKAN, 2_000L)
        VulkanStartupGuard.finishLaunch(context, RendererDefaults.VULKAN, first)
        assertEquals(second, preferences.getLong("launch_started_at", 0L))
        VulkanStartupGuard.finishLaunch(context, RendererDefaults.VULKAN, second)
        assertEquals(0L, preferences.getLong("launch_started_at", 0L))
    }
}

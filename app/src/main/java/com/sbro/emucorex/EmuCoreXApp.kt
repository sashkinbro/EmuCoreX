package com.sbro.emucorex

import android.app.Application
import com.sbro.emucorex.core.AppAnalytics
import com.sbro.emucorex.core.AppIconManager
import com.sbro.emucorex.core.CrashLogger
import com.sbro.emucorex.core.NativeCrashArchive
import com.sbro.emucorex.core.DeviceGpuInfoProvider
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.VulkanStartupGuard
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.discord.DiscordIntegration
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.CancellationException

class EmuCoreXApp : Application() {
    internal val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onCreate() {
        super.onCreate()
        // The Discord SDK helper must not initialize the emulator-side application graph.
        if (getProcessName().endsWith(":discord")) return
        // CrashLogger must be the very first thing — it catches crashes in all subsequent init steps
        CrashLogger.init(this)
        applicationScope.launch { NativeCrashArchive.collect(this@EmuCoreXApp) }
        // If the previous process died with a native Vulkan crash, arm a one-shot OpenGL
        // launch. The saved renderer setting is not modified.
        runCatching { VulkanStartupGuard.consumeStartupCrash(this) }
        AppAnalytics.initialize(this)
        applicationScope.launch {
            try {
                DeviceGpuInfoProvider.preload(this@EmuCoreXApp)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                android.util.Log.w("DeviceGpu", "GPU profile preload failed", error)
            }
        }
        AppIconManager.applyProIcon(this, AppPreferences(this).getProUnlockedSync())
        EmulatorBridge.initializeOnce(this)
        RetroAchievementsStateManager.initialize()
        DiscordIntegration.initialize(this)
    }
}

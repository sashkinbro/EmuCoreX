package com.sbro.emucorex

import android.app.Application
import com.sbro.emucorex.core.AppAnalytics
import com.sbro.emucorex.core.AppIconManager
import com.sbro.emucorex.core.CrashLogger
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

class EmuCoreXApp : Application() {
    internal val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onCreate() {
        super.onCreate()
        // CrashLogger must be the very first thing — it catches crashes in all subsequent init steps
        CrashLogger.init(this)
        AppAnalytics.initialize(this)
        AppIconManager.applyProIcon(this, AppPreferences(this).getProUnlockedSync())
        EmulatorBridge.initializeOnce(this)
        RetroAchievementsStateManager.initialize()
    }
}

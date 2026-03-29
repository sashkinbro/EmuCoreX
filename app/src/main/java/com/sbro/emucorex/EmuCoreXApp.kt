package com.sbro.emucorex

import android.app.Application
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager

class EmuCoreXApp : Application() {

    override fun onCreate() {
        super.onCreate()
        EmulatorBridge.initializeOnce(this)
        RetroAchievementsStateManager.initialize()
    }
}

package com.sbro.emucorex

import android.app.Application
import com.sbro.emucorex.core.AppAnalytics
import com.sbro.emucorex.core.AppIconManager
import com.sbro.emucorex.core.CrashLogger
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.utils.RetroAchievementsStateManager
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.discord.DiscordIntegration
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.CancellationException
import com.sbro.emucorex.core.BackupSessionGate
import com.sbro.emucorex.data.drive.DriveBackupArchive
import com.sbro.emucorex.data.drive.DriveBackupException

class EmuCoreXApp : Application() {
    internal val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onCreate() {
        super.onCreate()
        // The Discord SDK helper must not initialize the emulator-side application graph.
        if (Application.getProcessName().endsWith(":discord")) return
        // CrashLogger must be the very first thing — it catches crashes in all subsequent init steps
        CrashLogger.init(this)
        if (DriveBackupArchive.hasPendingRecovery(this)) applicationScope.launch {
            try { BackupSessionGate.whileStopped { DriveBackupArchive(this@EmuCoreXApp).recoverPending() } }
            catch (cancelled: CancellationException) { throw cancelled }
            catch (error: Exception) {
                // VM startup performs the same recovery under the gate before it can open cards.
                if ((error as? DriveBackupException)?.reason != "busy") {
                    android.util.Log.w("DriveBackup", "Pending restore recovery will be retried before VM startup")
                }
            }
        }
        AppAnalytics.initialize(this)
        AppIconManager.applyProIcon(this, AppPreferences(this).getProUnlockedSync())
        EmulatorBridge.initializeOnce(this)
        RetroAchievementsStateManager.initialize()
        DiscordIntegration.initialize(this)
    }
}

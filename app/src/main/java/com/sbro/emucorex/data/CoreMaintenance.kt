package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.NativeApp
import java.io.File

/** Core-owned directories that are rebuilt on demand and never contain user data. */
internal val REGENERABLE_CORE_DIRECTORIES = listOf("inis", "cache")

/** Deletes regenerable core files, keeping the directories and every user file untouched. */
internal fun clearRegenerableCoreState(dataRoot: File): Int {
    var deletedFiles = 0
    for (name in REGENERABLE_CORE_DIRECTORIES) {
        val directory = File(dataRoot, name)
        if (!directory.isDirectory) continue
        directory.walkBottomUp().forEach { entry ->
            if (entry == directory) return@forEach
            if (entry.isFile) {
                if (entry.delete()) deletedFiles++
            } else if (entry.isDirectory) {
                entry.delete()
            }
        }
    }
    return deletedFiles
}

internal enum class CoreUpdateResetAction { NONE, STORE_SILENTLY, RESET_AUTOMATICALLY }

/**
 * Decides how to react to the core fingerprint of the installed build.
 *
 * Fresh installs only record the fingerprint, upgrades reset the regenerable core state
 * automatically, and an unchanged core does nothing.
 */
internal fun decideCoreUpdateResetAction(
    storedFingerprint: String?,
    currentFingerprint: String?,
    hadExistingInstall: Boolean
): CoreUpdateResetAction {
    if (currentFingerprint.isNullOrBlank()) return CoreUpdateResetAction.NONE
    if (storedFingerprint.isNullOrBlank()) {
        return if (hadExistingInstall) {
            CoreUpdateResetAction.RESET_AUTOMATICALLY
        } else {
            CoreUpdateResetAction.STORE_SILENTLY
        }
    }
    return if (storedFingerprint == currentFingerprint) {
        CoreUpdateResetAction.NONE
    } else {
        CoreUpdateResetAction.RESET_AUTOMATICALLY
    }
}

class CoreMaintenanceRepository(context: Context) {

    private val appContext = context.applicationContext

    fun resetGeneratedCoreState(): Int = clearRegenerableCoreState(NativeApp.dataRoot(appContext))
}

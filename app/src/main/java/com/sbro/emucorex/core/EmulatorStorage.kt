package com.sbro.emucorex.core

import android.content.Context
import java.io.File

object EmulatorStorage {

    private fun root(context: Context): File {
        return context.getExternalFilesDir(null) ?: context.filesDir
    }

    fun saveStatesDir(context: Context): File = File(root(context), "sstates").apply { mkdirs() }

    fun memoryCardsDir(context: Context): File = File(root(context), "memcards").apply { mkdirs() }

    fun cheatsDir(context: Context): File = File(root(context), "cheats").apply { mkdirs() }

    fun patchesDir(context: Context): File = File(root(context), "patches").apply { mkdirs() }

    fun logDir(context: Context): File = File(root(context), "log").apply { mkdirs() }

    fun backupsDir(context: Context): File = File(root(context), "backups").apply { mkdirs() }

    fun appStateDir(context: Context): File = File(root(context), "app-state").apply { mkdirs() }

    fun importedCheatsDir(context: Context): File = File(appStateDir(context), "imported-cheats").apply { mkdirs() }

    fun dataRoot(context: Context): File = root(context)
}

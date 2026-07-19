package com.sbro.emucorex.core

import android.content.Context
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.ConcurrentHashMap

enum class EmulatorDataLocation {
    INTERNAL,
    SD_CARD
}

object EmulatorStorage {
    private val verifiedCustomRoots = ConcurrentHashMap.newKeySet<String>()

    data class RuntimeDirectories(
        val saveStates: File,
        val memoryCards: File,
        val textures: File,
        val cheats: File,
        val patches: File,
        val logs: File
    )

    data class StandardDataRoot(
        val directory: File,
        val preferencePath: String?
    )

    private val runtimeSubdirectories = listOf(
        "sstates",
        "memcards",
        "textures",
        "cheats",
        "patches",
        "logs"
    )

    private fun defaultRoot(context: Context): File {
        return context.getExternalFilesDir(null) ?: context.filesDir
    }

    /**
     * Returns the app-specific directory on removable/secondary storage, if Android exposes one.
     * Index zero is the primary external storage used by [defaultRoot]; secondary entries are SD
     * cards or other removable volumes. No broad storage permission is required for these paths.
     */
    fun sdCardRoot(context: Context): File? = runCatching {
        findSecondaryExternalFilesDir(context.getExternalFilesDirs(null))
    }.getOrNull()

    fun prepareStandardDataRoot(
        context: Context,
        location: EmulatorDataLocation
    ): StandardDataRoot? {
        val directory = when (location) {
            EmulatorDataLocation.INTERNAL -> defaultRoot(context)
            EmulatorDataLocation.SD_CARD -> sdCardRoot(context) ?: return null
        }
        if (!prepareRoot(directory)) return null

        return StandardDataRoot(
            directory = directory,
            preferencePath = if (location == EmulatorDataLocation.INTERNAL) null else directory.absolutePath
        )
    }

    fun selectedStandardLocation(
        preferencePath: String?,
        sdCardPath: String?
    ): EmulatorDataLocation? = when {
        preferencePath.isNullOrBlank() -> EmulatorDataLocation.INTERNAL
        !sdCardPath.isNullOrBlank() && File(preferencePath).absolutePath == File(sdCardPath).absolutePath -> {
            EmulatorDataLocation.SD_CARD
        }
        else -> null
    }

    internal fun findSecondaryExternalFilesDir(directories: Array<File?>): File? =
        directories.asSequence().drop(1).filterNotNull().firstOrNull()

    private fun root(context: Context, customRootPath: String? = null): File {
        val customRoot = customRootPath
            ?.takeIf { it.isNotBlank() }
            ?.let(::File)
        // Selection performs the destructive write probe on Dispatchers.IO. Normal directory
        // access must not repeat create/delete probes because repositories can resolve paths from
        // the main thread while rendering UI.
        if (customRoot != null && isPreparedDirectoryTree(customRoot)) {
            return customRoot
        }

        return defaultRoot(context).apply { ensureRootDirectories(this) }
    }

    fun prepareCustomDataRoot(customRootPath: String?): Boolean {
        val customRoot = customRootPath
            ?.takeIf { it.isNotBlank() }
            ?.let(::File)
            ?: return false
        return prepareRoot(customRoot)
    }

    fun saveStatesDir(context: Context, customRootPath: String? = null): File =
        File(root(context, customRootPath), "sstates").apply { mkdirs() }

    fun memoryCardsDir(context: Context, customRootPath: String? = null): File =
        File(root(context, customRootPath), "memcards").apply { mkdirs() }

    fun texturesDir(context: Context, customRootPath: String? = null): File =
        File(root(context, customRootPath), "textures").apply { mkdirs() }

    fun cheatsDir(context: Context, customRootPath: String? = null): File =
        File(root(context, customRootPath), "cheats").apply { mkdirs() }

    fun patchesDir(context: Context, customRootPath: String? = null): File =
        File(root(context, customRootPath), "patches").apply { mkdirs() }

    fun logDir(context: Context, customRootPath: String? = null): File =
        File(root(context, customRootPath), "logs").apply { mkdirs() }

    fun runtimeDirectories(context: Context, customRootPath: String? = null): RuntimeDirectories {
        val root = root(context, customRootPath)
        fun directory(name: String): File = File(root, name).apply { mkdirs() }
        return RuntimeDirectories(
            saveStates = directory("sstates"),
            memoryCards = directory("memcards"),
            textures = directory("textures"),
            cheats = directory("cheats"),
            patches = directory("patches"),
            logs = directory("logs")
        )
    }

    fun appStateDir(context: Context): File = File(root(context), "app-state").apply { mkdirs() }

    fun importedCheatsDir(context: Context): File = File(appStateDir(context), "imported-cheats").apply { mkdirs() }

    private fun prepareRoot(root: File): Boolean {
        val rootPath = root.absolutePath
        if (!ensureRootDirectories(root)) {
            verifiedCustomRoots.remove(rootPath)
            return false
        }
        if (rootPath in verifiedCustomRoots) return true

        val writable = canWriteProbe(root) && runtimeSubdirectories.all { subdirectory ->
            canWriteProbe(File(root, subdirectory))
        }
        if (writable) verifiedCustomRoots.add(rootPath)
        return writable
    }

    private fun ensureRootDirectories(root: File): Boolean {
        return runCatching {
            if (!root.exists() && !root.mkdirs()) return false
            if (!root.isDirectory) return false

            for (subdirectory in runtimeSubdirectories) {
                val dir = File(root, subdirectory)
                if (!dir.exists() && !dir.mkdirs()) return false
                if (!dir.isDirectory) return false
            }
            true
        }.getOrDefault(false)
    }

    private fun isPreparedDirectoryTree(root: File): Boolean {
        if (!ensureRootDirectories(root) || !root.canWrite()) return false
        return runtimeSubdirectories.all { File(root, it).canWrite() }
    }

    private fun canWriteProbe(directory: File): Boolean {
        val probe = File(directory, ".emucorex-write-test")
        return runCatching {
            FileOutputStream(probe, false).use { stream ->
                stream.write(1)
            }
            if (probe.exists() && !probe.delete()) {
                probe.deleteOnExit()
            }
            true
        }.getOrDefault(false)
    }
}

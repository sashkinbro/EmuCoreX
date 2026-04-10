package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import com.sbro.emucorex.core.EmulatorStorage
import org.json.JSONObject
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

class SettingsBackupRepository(
    private val context: Context,
    private val preferences: AppPreferences,
    private val perGameSettingsRepository: PerGameSettingsRepository,
    private val cheatRepository: CheatRepository
) {

    suspend fun backup(destination: Uri): Boolean {
        return runCatching {
            context.contentResolver.openOutputStream(destination)?.use { output ->
                ZipOutputStream(output).use { zip ->
                    zip.writeJsonEntry("settings.json", preferences.exportJson())
                    zip.writeJsonEntry("per-game-settings.json", perGameSettingsRepository.exportJson())
                    zip.writeJsonEntry("cheats.json", cheatRepository.exportJson())
                    zip.writeDirectory("memory-cards", EmulatorStorage.memoryCardsDir(context))
                }
            } != null
        }.getOrDefault(false)
    }

    suspend fun restore(source: Uri): Boolean {
        return runCatching {
            context.contentResolver.openInputStream(source)?.use { input ->
                ZipInputStream(input).use { zip ->
                    generateSequence { zip.nextEntry }.forEach { entry ->
                        when (entry.name) {
                            "settings.json" -> preferences.importJson(JSONObject(zip.readBytes().decodeToString()))
                            "per-game-settings.json" -> perGameSettingsRepository.importJson(JSONObject(zip.readBytes().decodeToString()))
                            "cheats.json" -> cheatRepository.importJson(JSONObject(zip.readBytes().decodeToString()))
                            else -> {
                                if (entry.name.startsWith("memory-cards/") && !entry.isDirectory) {
                                    val relative = entry.name.removePrefix("memory-cards/")
                                    val target = File(EmulatorStorage.memoryCardsDir(context), relative)
                                    target.parentFile?.mkdirs()
                                    target.outputStream().use { output -> zip.copyTo(output) }
                                }
                            }
                        }
                        zip.closeEntry()
                    }
                }
            } != null
        }.getOrDefault(false)
    }
}

private fun ZipOutputStream.writeJsonEntry(name: String, json: JSONObject) {
    putNextEntry(ZipEntry(name))
    write(json.toString(2).toByteArray())
    closeEntry()
}

private fun ZipOutputStream.writeDirectory(prefix: String, directory: File) {
    directory.listFiles().orEmpty().forEach { file ->
        if (file.isDirectory) {
            writeDirectory("$prefix/${file.name}", file)
        } else {
            putNextEntry(ZipEntry("$prefix/${file.name}"))
            file.inputStream().use { it.copyTo(this) }
            closeEntry()
        }
    }
}

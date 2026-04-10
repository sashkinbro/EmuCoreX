package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorStorage
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class CheatBlock(
    val id: String,
    val title: String,
    val lines: List<String>,
    val enabled: Boolean
)

data class CheatGameConfig(
    val gameKey: String,
    val serial: String,
    val crc: String?,
    val sourceFileName: String?,
    val blocks: List<CheatBlock>
)

data class CheatFileEntry(
    val gameKey: String,
    val fileName: String,
    val displayName: String,
    val blockCount: Int
)

class CheatRepository(context: Context) {
    private val importedDir = EmulatorStorage.importedCheatsDir(context)
    private val stateFile = File(EmulatorStorage.appStateDir(context), "cheat-state.json")
    private val activeDir = EmulatorStorage.cheatsDir(context)

    fun getGameConfig(gameKey: String, serial: String, crc: String?): CheatGameConfig? {
        val sourceFile = importedFile(gameKey)
        if (!sourceFile.exists()) return null
        val raw = sourceFile.readText()
        val enabledIds = loadEnabledIds().optJSONArray(gameKey)?.toStringSet().orEmpty()
        val blocks = parseCheatBlocks(raw).map { it.copy(enabled = enabledIds.contains(it.id)) }
        return CheatGameConfig(
            gameKey = gameKey,
            serial = serial,
            crc = crc,
            sourceFileName = sourceFile.name,
            blocks = blocks
        )
    }

    fun getGameConfig(gameKeys: List<String>, serial: String, crc: String?): CheatGameConfig? {
        return gameKeys.firstNotNullOfOrNull { gameKey ->
            getGameConfig(gameKey, serial, crc)
        }
    }

    fun listImportedCheatFiles(): List<CheatFileEntry> {
        return importedDir.listFiles { file -> file.isFile && file.extension.equals("pnach", ignoreCase = true) }
            ?.sortedBy { it.name.lowercase() }
            ?.map { file ->
                val raw = runCatching { file.readText() }.getOrDefault("")
                CheatFileEntry(
                    gameKey = file.nameWithoutExtension,
                    fileName = file.name,
                    displayName = file.nameWithoutExtension.replace('_', ' '),
                    blockCount = parseCheatBlocks(raw).size
                )
            }
            .orEmpty()
    }

    fun getImportedCheatText(gameKey: String): String? {
        val file = importedFile(gameKey)
        if (!file.exists()) return null
        return file.readText()
    }

    fun updateImportedCheatText(gameKey: String, contents: String) {
        importCheatFile(gameKey = gameKey, fileName = "$gameKey.pnach", contents = contents)
    }

    fun importCheatFile(gameKey: String, fileName: String, contents: String) {
        val target = importedFile(gameKey)
        target.parentFile?.mkdirs()
        target.writeText(contents)
        val parsedIds = parseCheatBlocks(contents).map { it.id }.toSet()
        val state = loadEnabledIds()
        val old = state.optJSONArray(gameKey)?.toStringSet().orEmpty()
        state.put(gameKey, JSONArray(old.filter(parsedIds::contains)))
        writeEnabledIds(state)
    }

    fun setEnabledBlocks(gameKey: String, enabledIds: Set<String>) {
        val state = loadEnabledIds()
        state.put(gameKey, JSONArray(enabledIds.toList()))
        writeEnabledIds(state)
    }

    fun syncActiveCheats(gameKey: String, serial: String?, crc: String?) {
        if (serial.isNullOrBlank() || crc.isNullOrBlank()) return
        val source = importedFile(gameKey)
        if (!source.exists()) return
        val enabledIds = loadEnabledIds().optJSONArray(gameKey)?.toStringSet().orEmpty()
        val blocks = parseCheatBlocks(source.readText()).filter { enabledIds.contains(it.id) }
        val target = File(activeDir, "${serial}_${crc}.pnach")
        if (blocks.isEmpty()) {
            if (target.exists()) target.delete()
            return
        }
        target.writeText(
            buildString {
                blocks.forEach { block ->
                    append("// ${block.title}\n")
                    block.lines.forEach { append(it).append('\n') }
                    append('\n')
                }
            }.trim() + "\n"
        )
    }

    fun deleteImportedCheats(gameKey: String, serial: String?, crc: String?) {
        importedFile(gameKey).delete()
        setEnabledBlocks(gameKey, emptySet())
        if (!serial.isNullOrBlank() && !crc.isNullOrBlank()) {
            File(activeDir, "${serial}_${crc}.pnach").delete()
        }
    }

    fun exportJson(): JSONObject {
        return JSONObject().put("enabled", loadEnabledIds())
    }

    fun importJson(json: JSONObject) {
        writeEnabledIds(json.optJSONObject("enabled") ?: JSONObject())
    }

    private fun importedFile(gameKey: String): File = File(importedDir, "${sanitizeFileName(gameKey)}.pnach")

    private fun loadEnabledIds(): JSONObject {
        if (!stateFile.exists()) return JSONObject()
        return runCatching { JSONObject(stateFile.readText()) }.getOrDefault(JSONObject())
    }

    private fun writeEnabledIds(json: JSONObject) {
        stateFile.parentFile?.mkdirs()
        stateFile.writeText(json.toString())
    }

    private fun sanitizeFileName(value: String): String {
        return value.replace(Regex("[^a-zA-Z0-9._-]"), "_")
    }

    private fun parseCheatBlocks(raw: String): List<CheatBlock> {
        val lines = raw.lineSequence().map { it.trimEnd() }.toList()
        val blocks = mutableListOf<CheatBlock>()
        var currentTitle: String? = null
        var currentLines = mutableListOf<String>()
        var index = 1

        fun flush() {
            val usefulLines = currentLines.filter { it.isNotBlank() && !it.trimStart().startsWith("//") }
            if (usefulLines.isEmpty()) {
                currentLines = mutableListOf()
                return
            }
            val title = currentTitle?.takeIf { it.isNotBlank() } ?: "Cheat $index"
            blocks += CheatBlock(
                id = title.lowercase().replace(Regex("[^a-z0-9]+"), "_").ifBlank { "cheat_$index" },
                title = title,
                lines = usefulLines,
                enabled = false
            )
            index++
            currentTitle = null
            currentLines = mutableListOf()
        }

        lines.forEach { line ->
            val trimmed = line.trim()
            val label = when {
                trimmed.startsWith("//") -> trimmed.removePrefix("//").trim()
                trimmed.startsWith("comment=", ignoreCase = true) -> trimmed.substringAfter('=').trim()
                else -> null
            }
            if (!label.isNullOrBlank()) {
                if (currentLines.any { it.trim().startsWith("patch=", ignoreCase = true) }) {
                    flush()
                }
                currentTitle = label
            } else if (trimmed.startsWith("patch=", ignoreCase = true)) {
                currentLines += trimmed
            }
        }
        flush()
        return blocks.ifEmpty {
            lines
                .mapNotNull { it.trim().takeIf { value -> value.startsWith("patch=", ignoreCase = true) } }
                .mapIndexed { idx, line ->
                    CheatBlock(
                        id = "cheat_${idx + 1}",
                        title = "Cheat ${idx + 1}",
                        lines = listOf(line),
                        enabled = false
                    )
                }
        }
    }
}

private fun JSONArray.toStringSet(): Set<String> {
    return buildSet {
        for (index in 0 until length()) {
            val value = optString(index)
            if (value.isNotBlank()) add(value)
        }
    }
}

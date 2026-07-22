package com.sbro.emucorex.data

import android.content.Context
import com.sbro.emucorex.core.EmulatorStorage
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.Locale

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

class CheatRepository(private val context: Context) {
    private val preferences = AppPreferences(context)
    private val importedDir = EmulatorStorage.importedCheatsDir(context)
    private val stateFile = File(EmulatorStorage.appStateDir(context), "cheat-state.json")
    private val activeTargetsFile = File(EmulatorStorage.appStateDir(context), "cheat-active-targets.json")

    fun getGameConfig(gameKey: String, serial: String, crc: String?): CheatGameConfig? {
        val sourceFile = resolveImportedFile(gameKey)
        if (!sourceFile.exists()) return null
        val normalizedGameKey = sourceFile.nameWithoutExtension
        val raw = runCatching { sourceFile.readText() }.getOrNull() ?: return null
        val enabledIds = storedValues(loadEnabledIds(), normalizedGameKey, gameKey)
        val blocks = parseCheatBlocks(raw).map { it.copy(enabled = enabledIds.contains(it.id)) }
        return CheatGameConfig(
            gameKey = normalizedGameKey,
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
        val file = resolveImportedFile(gameKey)
        if (!file.exists()) return null
        return runCatching { file.readText() }.getOrNull()
    }

    fun updateImportedCheatText(gameKey: String, contents: String): Int {
        return importCheatFile(gameKey = gameKey, contents = contents)
    }

    fun importCheatFile(
        gameKey: String,
        contents: String,
        enableAllByDefault: Boolean = false,
        mergeWithExisting: Boolean = false
    ): Int {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val target = importedFile(normalizedGameKey)
        val state = loadEnabledIds()
        val old = storedValues(state, normalizedGameKey, gameKey)
        val existingBlocks = if (target.exists()) {
            runCatching { parseCheatBlocks(target.readText()) }.getOrDefault(emptyList())
        } else {
            emptyList()
        }
        val oldEnabledSignatures = existingBlocks
                .filter { old.contains(it.id) }
                .map(::cheatBlockSignature)
                .toSet()
        val importedBlocks = parseCheatBlocks(contents)
        if (importedBlocks.isEmpty()) return 0
        val blocks = if (mergeWithExisting) {
            mergeCheatBlocks(existingBlocks, importedBlocks)
        } else {
            importedBlocks
        }
        val storedContents = if (mergeWithExisting) serializeCheatBlocks(blocks) else contents
        target.parentFile?.mkdirs()
        target.writeText(storedContents)
        val enabledIds = if (enableAllByDefault) {
            blocks.map { it.id }
        } else {
            blocks.filter { old.contains(it.id) || cheatBlockSignature(it) in oldEnabledSignatures }
                .map { it.id }
        }
        state.put(normalizedGameKey, JSONArray(enabledIds))
        if (normalizedGameKey != gameKey) state.remove(gameKey)
        writeEnabledIds(state)
        return blocks.size
    }

    private fun mergeCheatBlocks(
        existing: List<CheatBlock>,
        imported: List<CheatBlock>
    ): List<CheatBlock> {
        val merged = linkedMapOf<String, CheatBlock>()
        (existing + imported).forEach { block ->
            val key = block.title.trim().lowercase(Locale.US)
            val previous = merged[key]
            merged[key] = if (previous == null) {
                block
            } else {
                previous.copy(lines = (previous.lines + block.lines).distinct())
            }
        }
        return merged.values.toList()
    }

    private fun serializeCheatBlocks(blocks: List<CheatBlock>): String = buildString {
        blocks.forEachIndexed { index, block ->
            if (index > 0) append('\n')
            append("// ").append(block.title).append('\n')
            block.lines.forEach { line -> append(line).append('\n') }
        }
    }

    fun setEnabledBlocks(gameKey: String, enabledIds: Set<String>) {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val state = loadEnabledIds()
        state.put(normalizedGameKey, JSONArray(enabledIds.toList()))
        if (normalizedGameKey != gameKey) state.remove(gameKey)
        writeEnabledIds(state)
    }

    fun syncActiveCheats(gameKey: String, serial: String?, crc: String?) {
        val source = resolveImportedFile(gameKey)
        val normalizedGameKey = source.nameWithoutExtension
        val normalizedCrc = crc?.trim()?.uppercase()?.takeIf { it.isNotBlank() } ?: return
        val normalizedSerial = serial?.trim()?.uppercase()?.takeIf { it.isNotBlank() }
        if (!source.exists()) {
            recordedActiveCheatFiles(normalizedGameKey).forEach { if (it.exists()) it.delete() }
            clearRecordedActiveCheatFiles(normalizedGameKey)
            return
        }
        val enabledIds = storedValues(loadEnabledIds(), normalizedGameKey, gameKey)
        val blocks = runCatching { parseCheatBlocks(source.readText()) }
            .getOrDefault(emptyList())
            .filter { enabledIds.contains(it.id) }
        val candidates = activeCheatFileCandidates(normalizedSerial, normalizedCrc)
        val target = canonicalActiveCheatFile(normalizedSerial, normalizedCrc)
        if (blocks.isEmpty()) {
            (candidates + recordedActiveCheatFiles(normalizedGameKey)).distinctBy(File::getAbsolutePath)
                .forEach { if (it.exists()) it.delete() }
            clearRecordedActiveCheatFiles(normalizedGameKey)
            return
        }
        activeDir().mkdirs()
        val contents = buildString {
            blocks.forEach { block ->
                append("// ${block.title}\n")
                block.lines.forEach { append(it).append('\n') }
                append('\n')
            }
        }.trim() + "\n"
        candidates.filterNot { it == target }.forEach { if (it.exists()) it.delete() }
        recordedActiveCheatFiles(normalizedGameKey).filterNot { it == target }.forEach { if (it.exists()) it.delete() }
        target.writeText(contents)
        recordActiveCheatFile(normalizedGameKey, target)
    }

    fun deleteImportedCheats(gameKey: String, serial: String?, crc: String?) {
        val source = resolveImportedFile(gameKey)
        val normalizedGameKey = source.nameWithoutExtension
        source.delete()
        setEnabledBlocks(normalizedGameKey, emptySet())
        recordedActiveCheatFiles(normalizedGameKey).forEach { if (it.exists()) it.delete() }
        clearRecordedActiveCheatFiles(normalizedGameKey)
        val inferred = inferSerialAndCrc(normalizedGameKey)
        val normalizedCrc = crc?.trim()?.uppercase()?.takeIf { it.isNotBlank() }
            ?: inferred?.second
            ?: return
        val normalizedSerial = serial?.trim()?.uppercase()?.takeIf { it.isNotBlank() }
            ?: inferred?.first
        activeCheatFileCandidates(normalizedSerial, normalizedCrc).forEach { if (it.exists()) it.delete() }
    }

    fun exportJson(): JSONObject {
        return JSONObject().put("enabled", loadEnabledIds())
    }

    fun importJson(json: JSONObject) {
        writeEnabledIds(json.optJSONObject("enabled") ?: JSONObject())
    }

    private fun importedFile(gameKey: String): File = File(importedDir, "${sanitizeFileName(gameKey)}.pnach")

    private fun resolveImportedFile(gameKey: String): File {
        val exact = importedFile(normalizeGameKey(gameKey))
        val candidates = importedDir.listFiles { file ->
            file.isFile && file.extension.equals("pnach", ignoreCase = true) &&
                file.nameWithoutExtension.equals(exact.nameWithoutExtension, ignoreCase = true)
        }.orEmpty()
        return candidates.firstOrNull { it.name == exact.name }
            ?: candidates.firstOrNull()
            ?: exact
    }

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

    private fun normalizeGameKey(gameKey: String): String = sanitizeFileName(gameKey).ifBlank { "cheat" }

    private fun inferSerialAndCrc(gameKey: String): Pair<String?, String>? {
        val serialAndCrc = Regex("^([A-Z]{4}[-_]?\\d{5})[_-]([0-9A-F]{8})$", RegexOption.IGNORE_CASE)
            .matchEntire(gameKey)
        if (serialAndCrc != null) {
            return serialAndCrc.groupValues[1].uppercase().replace('_', '-') to
                serialAndCrc.groupValues[2].uppercase()
        }
        val crcOnly = gameKey.uppercase().takeIf { it.matches(Regex("[0-9A-F]{8}")) }
        return crcOnly?.let { null to it }
    }

    private fun storedValues(state: JSONObject, normalizedKey: String, legacyKey: String): Set<String> {
        val caseInsensitiveKey = state.keys().asSequence().firstOrNull { key ->
            key.equals(normalizedKey, ignoreCase = true) || key.equals(legacyKey, ignoreCase = true)
        }
        return (state.optJSONArray(normalizedKey)
            ?: state.optJSONArray(legacyKey)
            ?: caseInsensitiveKey?.let(state::optJSONArray))
            ?.toStringSet()
            .orEmpty()
    }

    private fun activeCheatFileCandidates(serial: String?, crc: String): List<File> {
        val names = linkedSetOf<String>()
        if (!serial.isNullOrBlank()) names += "${sanitizeFileName(serial)}_$crc.pnach"
        names += "$crc.pnach"
        val root = activeDir().canonicalFile
        return names.map { File(root, it) }
    }

    private fun canonicalActiveCheatFile(serial: String?, crc: String): File {
        val name = if (serial.isNullOrBlank()) {
            "$crc.pnach"
        } else {
            "${sanitizeFileName(serial)}_$crc.pnach"
        }
        return File(activeDir().canonicalFile, name)
    }

    private fun recordedActiveCheatFiles(gameKey: String): List<File> {
        val names = loadActiveTargets().optJSONArray(gameKey)?.toStringSet().orEmpty()
        val root = activeDir().canonicalFile
        return names.mapNotNull { name ->
            if (name != File(name).name || !name.endsWith(".pnach", ignoreCase = true)) return@mapNotNull null
            File(root, name).canonicalFile.takeIf { it.parentFile == root }
        }
    }

    private fun recordActiveCheatFile(gameKey: String, target: File) {
        val state = loadActiveTargets()
        state.put(gameKey, JSONArray(listOf(target.name)))
        writeActiveTargets(state)
    }

    private fun clearRecordedActiveCheatFiles(gameKey: String) {
        val state = loadActiveTargets()
        state.remove(gameKey)
        writeActiveTargets(state)
    }

    private fun loadActiveTargets(): JSONObject {
        if (!activeTargetsFile.exists()) return JSONObject()
        return runCatching { JSONObject(activeTargetsFile.readText()) }.getOrDefault(JSONObject())
    }

    private fun writeActiveTargets(json: JSONObject) {
        activeTargetsFile.parentFile?.mkdirs()
        activeTargetsFile.writeText(json.toString())
    }

    private fun activeDir(): File {
        return EmulatorStorage.cheatsDir(context, preferences.getEmulatorDataPathSync())
    }

    private fun parseCheatBlocks(raw: String): List<CheatBlock> {
        val lines = raw.lineSequence().map { it.trimEnd() }.toList()
        val blocks = mutableListOf<CheatBlock>()
        var currentTitle: String? = null
        var currentLines = mutableListOf<String>()
        var index = 1

        fun flush() {
            val usefulLines = currentLines.filter { line ->
                val trimmed = line.trimStart()
                trimmed.startsWith("patch=", ignoreCase = true) ||
                    trimmed.startsWith("dpatch=", ignoreCase = true)
            }
            if (usefulLines.isEmpty()) {
                currentLines = mutableListOf()
                return
            }
            val title = currentTitle?.takeIf { it.isNotBlank() } ?: "Cheat $index"
            val slug = title.lowercase().replace(Regex("[^a-z0-9]+"), "_").trim('_')
            blocks += CheatBlock(
                id = "${slug.ifBlank { "cheat" }}_$index",
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
                trimmed.startsWith("[") && trimmed.endsWith("]") -> trimmed.removeSurrounding("[", "]").trim()
                else -> null
            }
            if (!label.isNullOrBlank()) {
                if (currentLines.any { it.trim().startsWith("patch=", ignoreCase = true) || it.trim().startsWith("dpatch=", ignoreCase = true) }) {
                    flush()
                }
                currentTitle = label
            } else if (
                trimmed.startsWith("patch=", ignoreCase = true) ||
                trimmed.startsWith("dpatch=", ignoreCase = true)
            ) {
                currentLines += trimmed
            }
        }
        flush()
        val parsedBlocks = blocks.ifEmpty {
            lines
                .mapNotNull { line ->
                    line.trim().takeIf { value ->
                        value.startsWith("patch=", ignoreCase = true) ||
                            value.startsWith("dpatch=", ignoreCase = true)
                    }
                }
                .mapIndexed { idx, line ->
                    CheatBlock(
                        id = "cheat_${idx + 1}",
                        title = "Cheat ${idx + 1}",
                        lines = listOf(line),
                        enabled = false
                    )
                }
        }
        val merged = linkedMapOf<String, CheatBlock>()
        parsedBlocks.forEach { block ->
            val key = block.title.trim().lowercase()
            val existing = merged[key]
            merged[key] = if (existing == null) {
                block
            } else {
                existing.copy(lines = (existing.lines + block.lines).distinct())
            }
        }
        return merged.values.toList()
    }

    private fun cheatBlockSignature(block: CheatBlock): String {
        return block.title.trim().lowercase() + "\u0000" + block.lines.joinToString("\n")
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

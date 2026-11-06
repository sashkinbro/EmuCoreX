package com.sbro.emucorex.data

import android.content.Context
import android.util.AtomicFile
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

/**
 * A catalog cheat pack installed for a game. The original PNACH text is kept so
 * an individual pack can later be removed without touching blocks that came
 * from other packs or from a manual import.
 */
data class InstalledCheatPack(
    val id: String,
    val title: String,
    val crc: String = "",
    val blockCount: Int = 0,
    val contents: String? = null
)

class CheatRepository(private val context: Context) {
    private val preferences = AppPreferences(context)
    private val importedDir = EmulatorStorage.importedCheatsDir(context)
    private val stateFile = File(EmulatorStorage.appStateDir(context), "cheat-state.json")
    private val activeTargetsFile = File(EmulatorStorage.appStateDir(context), "cheat-active-targets.json")
    // Catalog pack IDs installed per game. Kept on disk so the in-game cheat
    // pack browser can still show "Installed" after the game/app is restarted;
    // the pack list itself only carries transient session state.
    private val packInstallsFile = File(EmulatorStorage.appStateDir(context), "cheat-pack-installs.json")

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
        mergeWithExisting: Boolean = false,
        installedPack: InstalledCheatPack? = null
    ): Int = synchronized(CHEAT_IO_LOCK) {
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
        if (importedBlocks.isEmpty()) return@synchronized 0
        val blocks = if (mergeWithExisting) {
            mergeCheatBlocks(existingBlocks, importedBlocks)
        } else {
            importedBlocks
        }
        val storedContents = if (mergeWithExisting) serializeCheatBlocks(blocks) else contents
        target.parentFile?.mkdirs()
        writeTextAtomically(target, storedContents)
        if (!mergeWithExisting) {
            // A replaced PNACH no longer represents the packs installed before.
            clearInstalledCheatPacksLocked(installedPacksRecordKey(normalizedGameKey))
        }
        if (installedPack != null) {
            recordInstalledCheatPackLocked(
                recordKey = installedPacksRecordKey(normalizedGameKey),
                pack = installedPack.copy(contents = contents)
            )
        }
        val enabledIds = if (enableAllByDefault) {
            blocks.map { it.id }
        } else {
            blocks.filter { old.contains(it.id) || cheatBlockSignature(it) in oldEnabledSignatures }
                .map { it.id }
        }
        state.put(normalizedGameKey, JSONArray(enabledIds))
        if (normalizedGameKey != gameKey) state.remove(gameKey)
        writeEnabledIds(state)
        blocks.size
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

    fun setEnabledBlocks(gameKey: String, enabledIds: Set<String>) = synchronized(CHEAT_IO_LOCK) {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val state = loadEnabledIds()
        state.put(normalizedGameKey, JSONArray(enabledIds.toList()))
        if (normalizedGameKey != gameKey) state.remove(gameKey)
        writeEnabledIds(state)
    }

    /**
     * Adds or removes a single block from the stored enabled set. Reads the
     * latest state from disk first so toggles made in another screen (the cheat
     * manager vs. the in-game menu) are never overwritten with a stale copy.
     */
    fun setBlockEnabled(gameKey: String, blockId: String, enabled: Boolean) = synchronized(CHEAT_IO_LOCK) {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val state = loadEnabledIds()
        val enabledIds = storedValues(state, normalizedGameKey, gameKey).toMutableSet()
        if (enabled) enabledIds.add(blockId) else enabledIds.remove(blockId)
        state.put(normalizedGameKey, JSONArray(enabledIds.toList()))
        if (normalizedGameKey != gameKey) state.remove(gameKey)
        writeEnabledIds(state)
    }

    /** Enables or disables a whole group of blocks with the same read-modify-write. */
    fun setBlocksEnabled(gameKey: String, blockIds: Collection<String>, enabled: Boolean) = synchronized(CHEAT_IO_LOCK) {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val state = loadEnabledIds()
        val enabledIds = storedValues(state, normalizedGameKey, gameKey).toMutableSet()
        if (enabled) enabledIds.addAll(blockIds) else enabledIds.removeAll(blockIds.toSet())
        state.put(normalizedGameKey, JSONArray(enabledIds.toList()))
        if (normalizedGameKey != gameKey) state.remove(gameKey)
        writeEnabledIds(state)
    }

    fun syncActiveCheats(gameKey: String, serial: String?, crc: String?) = synchronized(CHEAT_IO_LOCK) {
        val source = resolveImportedFile(gameKey)
        val normalizedGameKey = source.nameWithoutExtension
        val normalizedCrc = crc?.trim()?.uppercase()?.takeIf { it.isNotBlank() } ?: return@synchronized
        val normalizedSerial = serial?.trim()?.uppercase()?.takeIf { it.isNotBlank() }
        if (!source.exists()) {
            recordedActiveCheatFiles(normalizedGameKey).forEach { if (it.exists()) it.delete() }
            clearRecordedActiveCheatFiles(normalizedGameKey)
            return@synchronized
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
            return@synchronized
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
        writeTextAtomically(target, contents)
        recordActiveCheatFile(normalizedGameKey, target)
    }

    fun deleteImportedCheats(gameKey: String, serial: String?, crc: String?) = synchronized(CHEAT_IO_LOCK) {
        val source = resolveImportedFile(gameKey)
        val normalizedGameKey = source.nameWithoutExtension
        source.delete()
        clearInstalledCheatPacksLocked(installedPacksRecordKey(normalizedGameKey))
        setEnabledBlocks(normalizedGameKey, emptySet())
        recordedActiveCheatFiles(normalizedGameKey).forEach { if (it.exists()) it.delete() }
        clearRecordedActiveCheatFiles(normalizedGameKey)
        val inferred = inferSerialAndCrc(normalizedGameKey)
        val normalizedCrc = crc?.trim()?.uppercase()?.takeIf { it.isNotBlank() }
            ?: inferred?.second
            ?: return@synchronized
        val normalizedSerial = serial?.trim()?.uppercase()?.takeIf { it.isNotBlank() }
            ?: inferred?.first
        activeCheatFileCandidates(normalizedSerial, normalizedCrc).forEach { if (it.exists()) it.delete() }
    }

    /**
     * Catalog packs recorded as installed for this game. A record whose PNACH no
     * longer exists is cleaned up, so a badge can never outlive the cheat file
     * (for example after the cheat manager deletes it).
     */
    fun getInstalledCheatPacks(gameKey: String): List<InstalledCheatPack> = synchronized(CHEAT_IO_LOCK) {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val recordKey = installedPacksRecordKey(normalizedGameKey)
        if (!resolveImportedFile(normalizedGameKey).exists()) {
            clearInstalledCheatPacksLocked(recordKey)
            return@synchronized emptyList()
        }
        parseInstalledCheatPacks(loadInstalledCheatPacks().optJSONArray(recordKey))
    }

    fun getInstalledCheatPackIds(gameKey: String): Set<String> =
        getInstalledCheatPacks(gameKey).mapTo(linkedSetOf()) { it.id }

    fun clearInstalledCheatPacks(gameKey: String) = synchronized(CHEAT_IO_LOCK) {
        clearInstalledCheatPacksLocked(installedPacksRecordKey(gameKey))
    }

    /**
     * Removes one catalog pack from a game's merged PNACH. Only lines supplied by
     * this pack are dropped; lines shared with another installed pack, or a
     * manual cheat with the same title, are kept. Returns false when the pack has
     * no recorded PNACH text and no [fallbackContents] was supplied.
     */
    fun removeInstalledCheatPack(
        gameKey: String,
        packId: String,
        fallbackContents: String? = null
    ): Boolean = synchronized(CHEAT_IO_LOCK) {
        val normalizedGameKey = normalizeGameKey(gameKey)
        val recordKey = installedPacksRecordKey(normalizedGameKey)
        val installs = loadInstalledCheatPacks()
        val packs = parseInstalledCheatPacks(installs.optJSONArray(recordKey))
        val target = packs.firstOrNull { it.id == packId } ?: return@synchronized false
        val remainingPacks = packs.filterNot { it.id == packId }
        val source = resolveImportedFile(normalizedGameKey)
        if (!source.exists()) {
            writeInstalledCheatPacksLocked(installs, recordKey, remainingPacks)
            return@synchronized false
        }
        val packContents = target.contents?.takeIf(String::isNotBlank) ?: fallbackContents
        if (packContents.isNullOrBlank()) return@synchronized false
        val packBlocks = parseCheatBlocks(packContents)
        if (packBlocks.isEmpty()) return@synchronized false

        val packLinesByTitle = packBlocks.groupBy { normalizedBlockTitle(it.title) }
            .mapValues { (_, blocks) -> blocks.flatMap { it.lines }.toSet() }
        val otherLinesByTitle = remainingPacks
            .flatMap { pack -> pack.contents?.let(::parseCheatBlocks).orEmpty() }
            .groupBy { normalizedBlockTitle(it.title) }
            .mapValues { (_, blocks) -> blocks.flatMap { it.lines }.toSet() }
        val currentBlocks = parseCheatBlocks(source.readText())
        val enabledIds = storedValues(loadEnabledIds(), normalizedGameKey, gameKey)
        val enabledTitles = currentBlocks
            .filter { it.id in enabledIds }
            .map { normalizedBlockTitle(it.title) }
            .toSet()
        val remainingBlocks = currentBlocks.mapNotNull { block ->
            val titleKey = normalizedBlockTitle(block.title)
            val packLines = packLinesByTitle[titleKey]
            if (packLines.isNullOrEmpty()) return@mapNotNull block
            val otherLines = otherLinesByTitle[titleKey].orEmpty()
            val keptLines = block.lines.filter { line -> line !in packLines || line in otherLines }
            if (keptLines.isEmpty()) null else block.copy(lines = keptLines)
        }

        if (remainingBlocks.isEmpty()) {
            source.delete()
            val state = loadEnabledIds()
            state.remove(normalizedGameKey)
            writeEnabledIds(state)
            recordedActiveCheatFiles(normalizedGameKey).forEach { if (it.exists()) it.delete() }
            clearRecordedActiveCheatFiles(normalizedGameKey)
            writeInstalledCheatPacksLocked(installs, recordKey, emptyList())
            return@synchronized true
        }

        val serialized = serializeCheatBlocks(remainingBlocks)
        writeTextAtomically(source, serialized)
        val reparsed = parseCheatBlocks(serialized)
        val newEnabledIds = reparsed
            .filter { normalizedBlockTitle(it.title) in enabledTitles }
            .map { it.id }
        val state = loadEnabledIds()
        state.put(normalizedGameKey, JSONArray(newEnabledIds))
        writeEnabledIds(state)
        recordedActiveCheatFiles(normalizedGameKey).forEach { if (it.exists()) it.delete() }
        clearRecordedActiveCheatFiles(normalizedGameKey)
        writeInstalledCheatPacksLocked(installs, recordKey, remainingPacks)
        true
    }

    private fun recordInstalledCheatPackLocked(recordKey: String, pack: InstalledCheatPack) {
        val installs = loadInstalledCheatPacks()
        val packs = parseInstalledCheatPacks(installs.optJSONArray(recordKey))
        val updated = packs.filterNot { it.id == pack.id } + pack
        installs.put(recordKey, installedCheatPacksToJson(updated))
        writeTextAtomically(packInstallsFile, installs.toString())
    }

    private fun writeInstalledCheatPacksLocked(
        installs: JSONObject,
        recordKey: String,
        packs: List<InstalledCheatPack>
    ) {
        if (packs.isEmpty()) {
            installs.remove(recordKey)
        } else {
            installs.put(recordKey, installedCheatPacksToJson(packs))
        }
        writeTextAtomically(packInstallsFile, installs.toString())
    }

    private fun parseInstalledCheatPacks(array: JSONArray?): List<InstalledCheatPack> {
        if (array == null) return emptyList()
        val packs = mutableListOf<InstalledCheatPack>()
        for (index in 0 until array.length()) {
            when (val entry = array.opt(index)) {
                is JSONObject -> {
                    val id = entry.optString("id").trim()
                    if (id.isNotEmpty()) {
                        packs += InstalledCheatPack(
                            id = id,
                            title = entry.optString("title"),
                            crc = entry.optString("crc"),
                            blockCount = entry.optInt("blockCount"),
                            contents = entry.optString("contents").takeIf(String::isNotEmpty)
                        )
                    }
                }
                // Legacy records only stored the catalog ID.
                is String -> if (entry.isNotBlank()) {
                    packs += InstalledCheatPack(id = entry, title = "")
                }
            }
        }
        return packs
    }

    private fun installedCheatPacksToJson(packs: List<InstalledCheatPack>): JSONArray {
        val array = JSONArray()
        packs.forEach { pack ->
            array.put(
                JSONObject()
                    .put("id", pack.id)
                    .put("title", pack.title)
                    .put("crc", pack.crc)
                    .put("blockCount", pack.blockCount)
                    .put("contents", pack.contents.orEmpty())
            )
        }
        return array
    }

    private fun normalizedBlockTitle(title: String): String = title.trim().lowercase(Locale.US)

    private fun clearInstalledCheatPacksLocked(recordKey: String) {
        val installs = loadInstalledCheatPacks()
        if (!installs.has(recordKey)) return
        installs.remove(recordKey)
        writeTextAtomically(packInstallsFile, installs.toString())
    }

    private fun loadInstalledCheatPacks(): JSONObject {
        if (!packInstallsFile.exists()) return JSONObject()
        return runCatching { JSONObject(packInstallsFile.readText()) }.getOrDefault(JSONObject())
    }

    private fun installedPacksRecordKey(gameKey: String): String =
        normalizeGameKey(gameKey).lowercase(Locale.US)

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
        writeTextAtomically(stateFile, json.toString())
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
        writeTextAtomically(activeTargetsFile, json.toString())
    }

    private fun writeTextAtomically(file: File, contents: String) {
        file.parentFile?.mkdirs()
        val atomicFile = AtomicFile(file)
        val output = atomicFile.startWrite()
        try {
            output.write(contents.toByteArray(Charsets.UTF_8))
            atomicFile.finishWrite(output)
        } catch (error: Throwable) {
            atomicFile.failWrite(output)
            throw error
        }
    }

    private fun activeDir(): File {
        return EmulatorStorage.cheatsDir(context, preferences.getEmulatorDataPathSync())
    }

    private companion object {
        val CHEAT_IO_LOCK = Any()
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

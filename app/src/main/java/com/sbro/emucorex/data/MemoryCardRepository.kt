package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.NativeApp
import java.io.File
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

data class MemoryCardInfo(
    val name: String,
    val path: String,
    val modifiedTime: Long,
    val type: Int,
    val fileType: Int,
    val sizeBytes: Long,
    val formatted: Boolean
)

data class MemoryCardAssignments(
    val slot1: String?,
    val slot2: String?
)

class MemoryCardRepository(
    private val context: Context,
    private val preferences: AppPreferences
) {

    suspend fun ensureDefaultCardsAssigned(): MemoryCardAssignments {
        syncNativeMemoryCardDirectory()
        val availableCards = listCards()
        val existingNames = availableCards.map { it.name }.toSet()
        val defaultSlot1 = DEFAULT_CARD_SLOT_1
        val defaultSlot2 = DEFAULT_CARD_SLOT_2

        if (defaultSlot1 !in existingNames) {
            createBlankPs2CardFile(File(EmulatorStorage.memoryCardsDir(context), defaultSlot1), 8)
        }
        if (defaultSlot2 !in existingNames) {
            createBlankPs2CardFile(File(EmulatorStorage.memoryCardsDir(context), defaultSlot2), 8)
        }

        val refreshedNames = listCards().map { it.name }.toSet()
        val current = currentAssignments()
        val resolvedSlot1 = current.slot1.takeIf { it in refreshedNames } ?: defaultSlot1
        val resolvedSlot2 = current.slot2.takeIf { it in refreshedNames && !it.equals(resolvedSlot1, ignoreCase = true) }
            ?: defaultSlot2.takeUnless { it.equals(resolvedSlot1, ignoreCase = true) }
            ?: refreshedNames.firstOrNull { !it.equals(resolvedSlot1, ignoreCase = true) }

        if (!current.slot1.equals(resolvedSlot1, ignoreCase = true) ||
            !current.slot2.equals(resolvedSlot2, ignoreCase = true)
        ) {
            assignSlots(resolvedSlot1, resolvedSlot2)
        }

        return MemoryCardAssignments(
            slot1 = resolvedSlot1,
            slot2 = resolvedSlot2
        )
    }

    suspend fun listCards(): List<MemoryCardInfo> {
        syncNativeMemoryCardDirectory()
        return NativeApp.parseMemoryCardList(NativeApp.listMemoryCards()).map {
            MemoryCardInfo(
                name = it.name,
                path = it.path,
                modifiedTime = it.modifiedTime.normalizeEpochMillis(),
                type = it.type,
                fileType = it.fileType,
                sizeBytes = it.sizeBytes,
                formatted = it.formatted
            )
        }.sortedBy { it.name.lowercase() }
    }

    fun createPs2Card(name: String, sizeMb: Int): Boolean {
        syncNativeMemoryCardDirectory()
        val normalized = buildUniqueCardName(name)
        val resolvedSizeMb = sizeMb.toSupportedPs2CardSizeMb()
        val target = File(EmulatorStorage.memoryCardsDir(context), normalized)
        return createBlankPs2CardFile(target, resolvedSizeMb)
    }

    fun importCard(uri: Uri, displayName: String? = null): Boolean {
        val resolvedName = displayName
            ?.takeIf { it.isNotBlank() }
            ?: DocumentPathResolver.getDisplayName(context, uri.toString())
        val target = File(EmulatorStorage.memoryCardsDir(context), buildUniqueCardName(resolvedName))
        return runCatching {
            context.contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            } != null
        }.getOrDefault(false)
    }

    fun duplicateCard(card: MemoryCardInfo, newName: String): Boolean {
        val source = File(card.path)
        if (!source.exists()) return false
        val target = File(EmulatorStorage.memoryCardsDir(context), buildUniqueCardName(newName))
        return runCatching {
            source.inputStream().use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            }
            true
        }.getOrDefault(false)
    }

    suspend fun renameCard(card: MemoryCardInfo, newName: String): Boolean {
        val source = File(card.path)
        if (!source.exists()) return false
        val targetName = normalizeName(newName)
        if (targetName.equals(card.name, ignoreCase = true)) return true
        val target = File(EmulatorStorage.memoryCardsDir(context), buildUniqueCardName(targetName, source.name))
        val renamed = runCatching { source.renameTo(target) }.getOrDefault(false)
        if (!renamed) return false

        val assignments = currentAssignments()
        val updatedSlot1 = assignments.slot1.renameIfMatching(card.name, target.name)
        val updatedSlot2 = assignments.slot2.renameIfMatching(card.name, target.name)
        assignSlots(updatedSlot1, updatedSlot2)
        return true
    }

    suspend fun deleteCard(card: MemoryCardInfo): Boolean {
        val file = File(card.path)
        if (!file.exists()) return false
        val deleted = runCatching { file.delete() }.getOrDefault(false)
        if (!deleted) return false

        val assignments = currentAssignments()
        val updatedSlot1 = assignments.slot1.takeUnless { it.equals(card.name, ignoreCase = true) }
        val updatedSlot2 = assignments.slot2.takeUnless { it.equals(card.name, ignoreCase = true) }
        assignSlots(updatedSlot1, updatedSlot2)
        return true
    }

    suspend fun exportCard(card: MemoryCardInfo, destination: Uri): Boolean {
        return runCatching {
            val source = File(card.path)
            if (!source.exists()) return false
            context.contentResolver.openOutputStream(destination)?.use { output ->
                source.inputStream().use { input -> input.copyTo(output) }
            } != null
        }.getOrDefault(false)
    }

    suspend fun backupCards(cards: List<MemoryCardInfo>, destination: Uri): Boolean {
        val existingCards = cards.map { File(it.path) }.filter(File::exists)
        if (existingCards.isEmpty()) return false
        return runCatching {
            context.contentResolver.openOutputStream(destination)?.use { output ->
                ZipOutputStream(output).use { zip ->
                    existingCards.forEach { file ->
                        zip.putNextEntry(ZipEntry("memory-cards/${file.name}"))
                        file.inputStream().use { it.copyTo(zip) }
                        zip.closeEntry()
                    }
                }
            } != null
        }.getOrDefault(false)
    }

    suspend fun restoreCards(source: Uri): Boolean {
        val displayName = DocumentPathResolver.getDisplayName(context, source.toString())
        val mimeType = context.contentResolver.getType(source).orEmpty().lowercase(Locale.ROOT)
        val looksLikeZip = mimeType.contains("zip") || displayName.endsWith(".zip", ignoreCase = true)
        return if (looksLikeZip) {
            restoreCardsFromZip(source)
        } else {
            importCard(source, displayName)
        }
    }

    suspend fun currentAssignments(): MemoryCardAssignments {
        val settings = preferences.exportJson()
        return MemoryCardAssignments(
            slot1 = settings.optString("memoryCardSlot1").takeIf { it.isNotBlank() },
            slot2 = settings.optString("memoryCardSlot2").takeIf { it.isNotBlank() }
        )
    }

    suspend fun assignSlots(slot1: String?, slot2: String?) {
        preferences.setMemoryCardAssignments(slot1, slot2)
        EmulatorBridge.setMemoryCardAssignments(slot1, slot2)
    }

    suspend fun assignCardToSlot(slot: Int, cardName: String?) {
        val current = currentAssignments()
        val updated = when (slot.coerceIn(1, 2)) {
            1 -> MemoryCardAssignments(
                slot1 = cardName,
                slot2 = current.slot2.takeUnless { it.equals(cardName, ignoreCase = true) }
            )
            else -> MemoryCardAssignments(
                slot1 = current.slot1.takeUnless { it.equals(cardName, ignoreCase = true) },
                slot2 = cardName
            )
        }
        assignSlots(updated.slot1, updated.slot2)
    }

    private fun restoreCardsFromZip(source: Uri): Boolean {
        var restoredCount = 0
        return runCatching {
            context.contentResolver.openInputStream(source)?.use { input ->
                ZipInputStream(input).use { zip ->
                    generateSequence { zip.nextEntry }.forEach { entry ->
                        if (entry.isDirectory) return@forEach
                        val rawName = entry.name.substringAfterLast('/').trim()
                        if (!rawName.endsWith(".ps2", ignoreCase = true)) {
                            zip.closeEntry()
                            return@forEach
                        }
                        val target = File(
                            EmulatorStorage.memoryCardsDir(context),
                            buildUniqueCardName(rawName)
                        )
                        target.parentFile?.mkdirs()
                        target.outputStream().use { output -> zip.copyTo(output) }
                        restoredCount++
                        zip.closeEntry()
                    }
                }
            } != null && restoredCount > 0
        }.getOrDefault(false)
    }

    private fun buildUniqueCardName(value: String, currentName: String? = null): String {
        val normalized = normalizeName(value)
        val directory = EmulatorStorage.memoryCardsDir(context)
        val baseName = normalized.removeSuffix(".ps2")
        var candidate = normalized
        var index = 2
        while (true) {
            val existing = File(directory, candidate)
            val isSameCard = !currentName.isNullOrBlank() && candidate.equals(currentName, ignoreCase = true)
            if (!existing.exists() || isSameCard) {
                return candidate
            }
            candidate = "$baseName ($index).ps2"
            index++
        }
    }

    private fun normalizeName(value: String): String {
        val withExt = if (value.endsWith(".ps2", ignoreCase = true)) value else "$value.ps2"
        return withExt
            .replace(Regex("[^a-zA-Z0-9._ -]"), "_")
            .replace(Regex("\\s+"), " ")
            .trim()
            .ifBlank { "Memory Card.ps2" }
    }

    private fun syncNativeMemoryCardDirectory() {
        val directory = EmulatorStorage.memoryCardsDir(context).absolutePath
        runCatching {
            NativeApp.beginSettingsBatch()
            NativeApp.setSetting("Folders", "MemoryCards", "string", directory)
        }
        runCatching {
            NativeApp.endSettingsBatch()
        }
    }

    private fun createBlankPs2CardFile(target: File, sizeMb: Int): Boolean {
        val totalBytes = sizeMb * PS2_CARD_MB_SIZE_BYTES
        val block = ByteArray(PS2_CARD_ERASE_BLOCK_BYTES) { 0xFF.toByte() }
        return runCatching {
            target.parentFile?.mkdirs()
            target.outputStream().buffered().use { output ->
                var remaining = totalBytes
                while (remaining > 0) {
                    val chunkSize = minOf(remaining, block.size)
                    output.write(block, 0, chunkSize)
                    remaining -= chunkSize
                }
                output.flush()
            }
            true
        }.getOrDefault(false)
    }

    private companion object {
        const val DEFAULT_CARD_SLOT_1 = "Mcd001.ps2"
        const val DEFAULT_CARD_SLOT_2 = "Mcd002.ps2"
        const val PS2_CARD_ERASE_BLOCK_BYTES = 528 * 16
        const val PS2_CARD_MB_SIZE_BYTES = 1024 * 528 * 2
    }
}

private fun String?.renameIfMatching(oldName: String, newName: String): String? {
    return if (this.equals(oldName, ignoreCase = true)) newName else this
}

private fun Long.normalizeEpochMillis(): Long {
    if (this <= 0L) return 0L
    return if (this < 1_000_000_000_000L) this * 1000L else this
}

private fun Int.toSupportedPs2CardSizeMb(): Int = when (this) {
    8, 16, 32, 64 -> this
    else -> 8
}

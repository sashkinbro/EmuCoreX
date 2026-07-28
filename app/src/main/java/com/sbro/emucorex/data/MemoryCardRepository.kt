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
    val formatted: Boolean,
    val isDefaultCard: Boolean
) {
    val isFolder: Boolean
        get() = type == MEMORY_CARD_TYPE_FOLDER
}

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
        val initialCards = listCards()
        val defaultSlot1 = DEFAULT_CARD_SLOT_1
        val defaultSlot2 = DEFAULT_CARD_SLOT_2

        migrateDefaultCardToStandardFileIfNeeded(defaultSlot1, initialCards)
        migrateDefaultCardToStandardFileIfNeeded(defaultSlot2, initialCards)

        val existingNames = listCards().map { it.name }.toSet()

        if (defaultSlot1 !in existingNames) {
            createStandardDefaultCard(defaultSlot1)
        }
        if (defaultSlot2 !in existingNames) {
            createStandardDefaultCard(defaultSlot2)
        }

        val refreshedNames = listCards().map { it.name }.toSet()
        val current = currentAssignments()
        val resolvedSlot1 = current.slot1.takeIf { it in refreshedNames }
            ?: defaultSlot1.takeIf { it in refreshedNames }
            ?: refreshedNames.firstOrNull()
        val resolvedSlot2 = current.slot2.takeIf { it in refreshedNames && !it.equals(resolvedSlot1, ignoreCase = true) }
            ?: defaultSlot2.takeIf { it in refreshedNames && !it.equals(resolvedSlot1, ignoreCase = true) }
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

    fun listCards(): List<MemoryCardInfo> {
        syncNativeMemoryCardDirectory()
        return NativeApp.parseMemoryCardList(NativeApp.listMemoryCards()).map {
            MemoryCardInfo(
                name = it.name,
                path = it.path,
                modifiedTime = it.modifiedTime.normalizeEpochMillis(),
                type = it.type,
                fileType = it.fileType,
                sizeBytes = it.sizeBytes,
                formatted = it.formatted,
                isDefaultCard = isDefaultMemoryCardName(it.name)
            )
        }.sortedBy { it.name.lowercase() }
    }

    fun createPs2Card(name: String, sizeMb: Int): Boolean {
        syncNativeMemoryCardDirectory()
        val normalized = buildUniqueCardName(name)
        val resolvedSizeMb = sizeMb.toSupportedPs2CardSizeMb()
        return NativeApp.createMemoryCard(
            normalized,
            MEMORY_CARD_TYPE_FILE,
            resolvedSizeMb.toNativePs2FileType()
        )
    }

    fun createFolderCard(name: String): Boolean {
        syncNativeMemoryCardDirectory()
        val normalized = buildUniqueCardName(name)
        return NativeApp.createMemoryCard(
            normalized,
            MEMORY_CARD_TYPE_FOLDER,
            MEMORY_CARD_FILE_TYPE_UNKNOWN
        )
    }

    fun importCard(uri: Uri, displayName: String? = null): Boolean {
        val resolvedName = displayName
            ?.takeIf { it.isNotBlank() }
            ?: DocumentPathResolver.getDisplayName(context, uri.toString())
        val target = File(memoryCardsDir(), normalizeName(resolvedName))
        return runCatching {
            target.deleteRecursively()
            context.contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            } != null
        }.getOrDefault(false)
    }

    fun duplicateCard(card: MemoryCardInfo, newName: String): Boolean {
        val source = File(card.path)
        if (!source.exists()) return false
        val target = File(memoryCardsDir(), buildUniqueCardName(newName))
        return runCatching {
            if (source.isDirectory) {
                source.copyRecursively(target, overwrite = false)
            } else {
                source.inputStream().use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                }
            }
            true
        }.getOrDefault(false)
    }

    suspend fun renameCard(card: MemoryCardInfo, newName: String): Boolean {
        if (card.isDefaultCard || isDefaultMemoryCardName(card.name)) return false
        val source = File(card.path)
        if (!source.exists()) return false
        val targetName = normalizeName(newName)
        if (targetName.equals(card.name, ignoreCase = true)) return true
        val target = File(memoryCardsDir(), buildUniqueCardName(targetName, source.name))
        val renamed = runCatching { source.renameTo(target) }.getOrDefault(false)
        if (!renamed) return false

        val assignments = currentAssignments()
        val updatedSlot1 = assignments.slot1.renameIfMatching(card.name, target.name)
        val updatedSlot2 = assignments.slot2.renameIfMatching(card.name, target.name)
        assignSlots(updatedSlot1, updatedSlot2)
        return true
    }

    suspend fun deleteCard(card: MemoryCardInfo): Boolean {
        if (card.isDefaultCard || isDefaultMemoryCardName(card.name)) return false
        val file = File(card.path)
        if (!file.exists()) return false
        if (!file.isSafelyInside(memoryCardsDir())) return false
        val deleted = runCatching {
            if (file.isDirectory) file.deleteRecursively() else file.delete()
        }.getOrDefault(false)
        if (!deleted) return false

        val assignments = currentAssignments()
        val updatedSlot1 = assignments.slot1.takeUnless { it.equals(card.name, ignoreCase = true) }
        val updatedSlot2 = assignments.slot2.takeUnless { it.equals(card.name, ignoreCase = true) }
        assignSlots(updatedSlot1, updatedSlot2)
        return true
    }

    fun exportCard(card: MemoryCardInfo, destination: Uri): Boolean {
        return runCatching {
            val source = File(card.path)
            if (!source.exists()) return false
            context.contentResolver.openOutputStream(destination)?.use { output ->
                if (source.isDirectory) {
                    ZipOutputStream(output).use { zip ->
                        zip.putMemoryCard(source, "memory-cards/${source.name}")
                    }
                } else {
                    source.inputStream().use { input -> input.copyTo(output) }
                }
            } != null
        }.getOrDefault(false)
    }

    fun backupCards(cards: List<MemoryCardInfo>, destination: Uri): Boolean {
        val existingCards = cards.map { File(it.path) }.filter(File::exists)
        if (existingCards.isEmpty()) return false
        return runCatching {
            context.contentResolver.openOutputStream(destination)?.use { output ->
                ZipOutputStream(output).use { zip ->
                    existingCards.forEach { file ->
                        zip.putMemoryCard(file, "memory-cards/${file.name}")
                    }
                }
            } != null
        }.getOrDefault(false)
    }

    fun restoreCards(source: Uri): Boolean {
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
                    val clearedCards = mutableSetOf<String>()
                    generateSequence { zip.nextEntry }.forEach { entry ->
                        val relativeParts = entry.safeMemoryCardZipParts()
                        if (relativeParts.isEmpty()) {
                            zip.closeEntry()
                            return@forEach
                        }

                        val cardName = relativeParts.first()
                        if (!cardName.isSupportedMemoryCardName()) {
                            zip.closeEntry()
                            return@forEach
                        }

                        val normalizedName = normalizeName(cardName)
                        val targetCardRoot = File(memoryCardsDir(), normalizedName)
                        if (clearedCards.add(normalizedName)) {
                            targetCardRoot.deleteRecursively()
                        }

                        if (relativeParts.size == 1) {
                            if (!entry.isDirectory) {
                                targetCardRoot.parentFile?.mkdirs()
                                targetCardRoot.outputStream().use { output -> zip.copyTo(output) }
                                restoredCount++
                            }
                            zip.closeEntry()
                            return@forEach
                        }

                        val target = relativeParts.drop(1).fold(targetCardRoot) { parent, child ->
                            File(parent, child)
                        }
                        if (!target.isSafelyInside(targetCardRoot)) {
                            zip.closeEntry()
                            return@forEach
                        }

                        if (entry.isDirectory) {
                            target.mkdirs()
                        } else {
                            target.parentFile?.mkdirs()
                            target.outputStream().use { output -> zip.copyTo(output) }
                        }
                        restoredCount++
                        zip.closeEntry()
                    }
                }
            } != null && restoredCount > 0
        }.getOrDefault(false)
    }

    private fun buildUniqueCardName(value: String, currentName: String? = null): String {
        val normalized = normalizeName(value)
        val directory = memoryCardsDir()
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
        val trimmed = value.trim()
        val baseName = if (trimmed.endsWith(".ps2", ignoreCase = true)) {
            trimmed.dropLast(4)
        } else {
            trimmed
        }
        val normalizedBase = baseName
            .replace(Regex("[^a-zA-Z0-9._ -]"), "_")
            .replace(Regex("\\s+"), " ")
            .trim()
            .trim('.', ' ')
            .ifBlank { "Memory Card" }
        return "$normalizedBase.ps2"
    }

    private fun syncNativeMemoryCardDirectory() {
        val directory = memoryCardsDir().absolutePath
        runCatching {
            NativeApp.beginSettingsBatch()
            NativeApp.setSetting("Folders", "MemoryCards", "string", directory)
        }
        runCatching {
            NativeApp.endSettingsBatch()
        }
    }

    private fun memoryCardsDir(): File {
        return EmulatorStorage.memoryCardsDir(context, preferences.getEmulatorDataPathSync())
    }

    private fun createStandardDefaultCard(cardName: String): Boolean {
        return NativeApp.createMemoryCard(
            cardName,
            STANDARD_DEFAULT_MEMORY_CARD_SPEC.type,
            STANDARD_DEFAULT_MEMORY_CARD_SPEC.fileType
        )
    }

    private fun migrateDefaultCardToStandardFileIfNeeded(
        cardName: String,
        availableCards: List<MemoryCardInfo>
    ) {
        val existingCard = availableCards.firstOrNull { it.name.equals(cardName, ignoreCase = true) } ?: return
        val target = File(existingCard.path)
        val migration = existingCard.defaultCardMigration(target)
        if (migration == DefaultCardMigration.None) {
            return
        }

        val preservedName = buildUniqueCardName(
            when (migration) {
                DefaultCardMigration.PreserveFolder -> "${cardName.removeSuffix(".ps2")} Folder"
                DefaultCardMigration.ReplaceBrokenFile -> "${cardName.removeSuffix(".ps2")} Legacy"
                DefaultCardMigration.None -> return
            }
        )
        val preservedCard = File(memoryCardsDir(), preservedName)

        migrateDefaultCardStorage(
            target = target,
            preservedCard = preservedCard,
            discardPreservedOnSuccess = migration == DefaultCardMigration.ReplaceBrokenFile
        ) {
            createStandardDefaultCard(cardName)
        }
    }
}

internal const val MEMORY_CARD_TYPE_FILE = 1
private const val MEMORY_CARD_TYPE_FOLDER = 2
private const val MEMORY_CARD_FILE_TYPE_UNKNOWN = 0
internal const val MEMORY_CARD_FILE_TYPE_PS2_8_MB = 1
private const val DEFAULT_CARD_SLOT_1 = "Mcd001.ps2"
private const val DEFAULT_CARD_SLOT_2 = "Mcd002.ps2"

internal data class MemoryCardCreationSpec(
    val type: Int,
    val fileType: Int
)

internal val STANDARD_DEFAULT_MEMORY_CARD_SPEC = MemoryCardCreationSpec(
    type = MEMORY_CARD_TYPE_FILE,
    fileType = MEMORY_CARD_FILE_TYPE_PS2_8_MB
)

internal enum class DefaultCardMigration {
    None,
    PreserveFolder,
    ReplaceBrokenFile
}

internal fun migrateDefaultCardStorage(
    target: File,
    preservedCard: File,
    discardPreservedOnSuccess: Boolean,
    createStandardCard: () -> Boolean
): Boolean {
    val preserved = runCatching { target.renameTo(preservedCard) }.getOrDefault(false)
    if (!preserved) {
        return false
    }

    val created = runCatching(createStandardCard).getOrDefault(false)
    if (created) {
        if (discardPreservedOnSuccess) {
            preservedCard.delete()
        }
        return true
    }

    // A failed native creation may leave a partial card behind. Remove only
    // that newly-created target, then restore the user's original card.
    runCatching {
        if (target.isDirectory) {
            target.deleteRecursively()
        } else {
            target.delete()
        }
        preservedCard.renameTo(target)
    }
    return false
}

private fun isDefaultMemoryCardName(name: String): Boolean {
    return name.equals(DEFAULT_CARD_SLOT_1, ignoreCase = true) ||
        name.equals(DEFAULT_CARD_SLOT_2, ignoreCase = true)
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

private fun Int.toNativePs2FileType(): Int = when (this) {
    16 -> 2
    32 -> 3
    64 -> 4
    else -> 1
}

private fun String.isSupportedMemoryCardName(): Boolean {
    return endsWith(".ps2", ignoreCase = true) ||
        endsWith(".mcr", ignoreCase = true) ||
        endsWith(".mcd", ignoreCase = true) ||
        endsWith(".bin", ignoreCase = true) ||
        endsWith(".mc2", ignoreCase = true)
}

private fun ZipEntry.safeMemoryCardZipParts(): List<String> {
    val parts = name
        .replace('\\', '/')
        .trim('/')
        .split('/')
        .filter { it.isNotBlank() }
        .dropWhile { it == "memory-cards" }

    return parts.takeIf { values ->
        values.isNotEmpty() && values.none { it == "." || it == ".." }
    }.orEmpty()
}

private fun File.isSafelyInside(root: File): Boolean {
    val rootPath = root.canonicalFile.toPath()
    val filePath = canonicalFile.toPath()
    return filePath != rootPath && filePath.startsWith(rootPath)
}

private fun ZipOutputStream.putMemoryCard(file: File, entryName: String) {
    val normalizedEntryName = entryName.replace('\\', '/').trim('/')
    if (file.isDirectory) {
        file.walkTopDown()
            .filter { it.isFile }
            .forEach { child ->
                val relativeName = child.relativeTo(file).invariantSeparatorsPath
                putNextEntry(ZipEntry("$normalizedEntryName/$relativeName"))
                child.inputStream().use { it.copyTo(this) }
                closeEntry()
            }
    } else {
        putNextEntry(ZipEntry(normalizedEntryName))
        file.inputStream().use { it.copyTo(this) }
        closeEntry()
    }
}

private fun File.looksLikeLegacyBlankPs2Card(): Boolean {
    if (!exists() || !isFile || length() <= 0L) return false
    return runCatching {
        inputStream().buffered().use { input ->
            val sampleSize = minOf(length(), LEGACY_BLANK_SAMPLE_BYTES.toLong()).toInt()
            val sample = ByteArray(sampleSize)
            val bytesRead = input.read(sample)
            bytesRead == sampleSize && sample.all { it == 0xFF.toByte() }
        }
    }.getOrDefault(false)
}

internal fun MemoryCardInfo.defaultCardMigration(file: File): DefaultCardMigration {
    if (!isDefaultMemoryCardName(name)) {
        return DefaultCardMigration.None
    }

    if (isFolder && file.isDirectory) {
        return DefaultCardMigration.PreserveFolder
    }

    if (!file.isFile) {
        return DefaultCardMigration.None
    }

    if (file.looksLikeLegacyBlankPs2Card()) {
        return DefaultCardMigration.ReplaceBrokenFile
    }

    val looksLikeBrokenLegacyCard = type == 1 &&
        fileType == 0 &&
        !formatted &&
        sizeBytes in 8_000_000L..9_000_000L

    return if (looksLikeBrokenLegacyCard) {
        DefaultCardMigration.ReplaceBrokenFile
    } else {
        DefaultCardMigration.None
    }
}

private const val LEGACY_BLANK_SAMPLE_BYTES = 64 * 1024

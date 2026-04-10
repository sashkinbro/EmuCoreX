package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.NativeApp
import java.io.File

data class MemoryCardInfo(
    val name: String,
    val path: String,
    val modifiedTime: Long,
    val type: Int,
    val fileType: Int,
    val sizeBytes: Long,
    val formatted: Boolean
)

class MemoryCardRepository(
    private val context: Context,
    private val preferences: AppPreferences
) {

    suspend fun listCards(): List<MemoryCardInfo> {
        return NativeApp.parseMemoryCardList(NativeApp.listMemoryCards()).map {
            MemoryCardInfo(
                name = it.name,
                path = it.path,
                modifiedTime = it.modifiedTime,
                type = it.type,
                fileType = it.fileType,
                sizeBytes = it.sizeBytes,
                formatted = it.formatted
            )
        }.sortedBy { it.name.lowercase() }
    }

    fun createPs2Card(name: String, sizeMb: Int): Boolean {
        val fileType = when (sizeMb) {
            8 -> 1
            16 -> 2
            32 -> 3
            64 -> 4
            else -> 1
        }
        val normalized = normalizeName(name)
        return NativeApp.createMemoryCard(normalized, 0, fileType)
    }

    fun importCard(uri: Uri, displayName: String): Boolean {
        val target = File(EmulatorStorage.memoryCardsDir(context), normalizeName(displayName))
        return runCatching {
            context.contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            } != null
        }.getOrDefault(false)
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

    suspend fun currentAssignments(): Pair<String?, String?> {
        val settings = preferences.exportJson()
        return settings.optString("memoryCardSlot1").takeIf { it.isNotBlank() } to
            settings.optString("memoryCardSlot2").takeIf { it.isNotBlank() }
    }

    suspend fun assignSlots(slot1: String?, slot2: String?) {
        preferences.setMemoryCardAssignments(slot1, slot2)
    }

    private fun normalizeName(value: String): String {
        val withExt = if (value.endsWith(".ps2", ignoreCase = true)) value else "$value.ps2"
        return withExt.replace(Regex("[^a-zA-Z0-9._ -]"), "_")
    }
}

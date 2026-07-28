package com.sbro.emucorex.data

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class MemoryCardRepositoryTest {

    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun `standard default card is a classic 8 MB PS2 file`() {
        assertEquals(MEMORY_CARD_TYPE_FILE, STANDARD_DEFAULT_MEMORY_CARD_SPEC.type)
        assertEquals(MEMORY_CARD_FILE_TYPE_PS2_8_MB, STANDARD_DEFAULT_MEMORY_CARD_SPEC.fileType)
    }

    @Test
    fun `existing default folder card is preserved during migration`() {
        val folder = temporaryFolder.newFolder("Mcd001.ps2")
        val card = memoryCard(
            name = "Mcd001.ps2",
            path = folder,
            type = 2,
            fileType = 0,
            sizeBytes = 0L,
            formatted = true
        )

        assertEquals(DefaultCardMigration.PreserveFolder, card.defaultCardMigration(folder))
    }

    @Test
    fun `regular folder card is not migrated`() {
        val folder = temporaryFolder.newFolder("Unlimited.ps2")
        val card = memoryCard(
            name = "Unlimited.ps2",
            path = folder,
            type = 2,
            fileType = 0,
            sizeBytes = 0L,
            formatted = true
        )

        assertEquals(DefaultCardMigration.None, card.defaultCardMigration(folder))
    }

    @Test
    fun `valid default file card is kept`() {
        val file = temporaryFolder.newFile("Mcd001.ps2").apply {
            writeBytes(byteArrayOf(0x01, 0x02, 0x03))
        }
        val card = memoryCard(
            name = "Mcd001.ps2",
            path = file,
            type = MEMORY_CARD_TYPE_FILE,
            fileType = MEMORY_CARD_FILE_TYPE_PS2_8_MB,
            sizeBytes = 8L * 1024L * 1024L,
            formatted = true
        )

        assertEquals(DefaultCardMigration.None, card.defaultCardMigration(file))
    }

    @Test
    fun `blank legacy default file is replaced`() {
        val file = temporaryFolder.newFile("Mcd002.ps2").apply {
            outputStream().use { output ->
                output.write(ByteArray(64 * 1024) { 0xFF.toByte() })
            }
        }
        val card = memoryCard(
            name = "Mcd002.ps2",
            path = file,
            type = MEMORY_CARD_TYPE_FILE,
            fileType = 0,
            sizeBytes = 8L * 1024L * 1024L,
            formatted = false
        )

        assertEquals(DefaultCardMigration.ReplaceBrokenFile, card.defaultCardMigration(file))
    }

    @Test
    fun `folder migration keeps saves and creates the standard card at the original path`() {
        val target = temporaryFolder.newFolder("active", "Mcd001.ps2")
        File(target, "BASLUS-TEST").writeText("save-data")
        val preserved = File(target.parentFile, "Mcd001 Folder.ps2")

        val migrated = migrateDefaultCardStorage(
            target = target,
            preservedCard = preserved,
            discardPreservedOnSuccess = false
        ) {
            target.writeText("standard-8mb-card")
            true
        }

        assertTrue(migrated)
        assertTrue(target.isFile)
        assertEquals("standard-8mb-card", target.readText())
        assertEquals("save-data", File(preserved, "BASLUS-TEST").readText())
    }

    @Test
    fun `failed standard card creation restores the original folder card`() {
        val target = temporaryFolder.newFolder("rollback", "Mcd002.ps2")
        File(target, "BASLUS-TEST").writeText("save-data")
        val preserved = File(target.parentFile, "Mcd002 Folder.ps2")

        val migrated = migrateDefaultCardStorage(
            target = target,
            preservedCard = preserved,
            discardPreservedOnSuccess = false
        ) {
            target.writeText("partial-card")
            false
        }

        assertFalse(migrated)
        assertTrue(target.isDirectory)
        assertEquals("save-data", File(target, "BASLUS-TEST").readText())
        assertFalse(preserved.exists())
    }

    private fun memoryCard(
        name: String,
        path: File,
        type: Int,
        fileType: Int,
        sizeBytes: Long,
        formatted: Boolean
    ) = MemoryCardInfo(
        name = name,
        path = path.absolutePath,
        modifiedTime = 0L,
        type = type,
        fileType = fileType,
        sizeBytes = sizeBytes,
        formatted = formatted,
        isDefaultCard = name.equals("Mcd001.ps2", ignoreCase = true) ||
            name.equals("Mcd002.ps2", ignoreCase = true)
    )
}

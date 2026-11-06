package com.sbro.emucorex.data

import com.sbro.emucorex.core.EmulatorStorage
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * Regression coverage for installed cheat packs: the "Installed" badge must
 * survive a restart, and a single pack must be removable from the merged PNACH
 * without touching blocks from other packs.
 */
@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class CheatPackInstallRecordTest {
    private val context = RuntimeEnvironment.getApplication()

    private val packA = InstalledCheatPack(
        id = "pack-a",
        title = "Pack A",
        crc = "652050D2",
        blockCount = 2
    )
    private val packB = InstalledCheatPack(
        id = "pack-b",
        title = "Pack B",
        crc = "652050D2",
        blockCount = 2
    )

    private val contentsA = """
        // Infinite Health
        patch=1,EE,00100000,word,00000064
        // Max Money
        patch=1,EE,00200000,word,000F423F
    """.trimIndent()

    private val contentsB = """
        // Infinite Health
        patch=1,EE,00100000,word,0000FFFF
        // Max Ammo
        patch=1,EE,00300000,word,00000063
    """.trimIndent()

    @Test
    fun `recorded packs survive repository recreation`() {
        val gameKey = "SLUS-21059_652050D2"
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, contentsA, packA)

            // A new repository instance is what a restarted game sees.
            val recreated = CheatRepository(context)
            val installed = recreated.getInstalledCheatPacks(gameKey)
            assertEquals(listOf("pack-a"), installed.map(InstalledCheatPack::id))
            assertEquals("Pack A", installed.single().title)
            assertEquals(2, installed.single().blockCount)
            assertEquals(setOf("pack-a"), recreated.getInstalledCheatPackIds(gameKey))
        } finally {
            cleanup(gameKey)
        }
    }

    @Test
    fun `records are scoped to one game`() {
        val first = "SLUS-21059_652050D2"
        val second = "SLUS-20312_BB3D833A"
        try {
            val repository = CheatRepository(context)
            install(repository, first, contentsA, packA)
            install(repository, second, contentsB, packB)

            assertEquals(setOf("pack-a"), repository.getInstalledCheatPackIds(first))
            assertEquals(setOf("pack-b"), repository.getInstalledCheatPackIds(second))
        } finally {
            cleanup(first, second)
        }
    }

    @Test
    fun `missing cheat file clears its records`() {
        val gameKey = "SLUS-21059_652050D2"
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, contentsA, packA)
            importedFile(gameKey).delete()

            assertTrue(repository.getInstalledCheatPacks(gameKey).isEmpty())

            // Re-importing the same file without catalog metadata must not
            // resurrect the stale badge.
            assertTrue(
                repository.importCheatFile(
                    gameKey = gameKey,
                    contents = contentsA,
                    mergeWithExisting = true
                ) > 0
            )
            assertTrue(repository.getInstalledCheatPacks(gameKey).isEmpty())
        } finally {
            cleanup(gameKey)
        }
    }

    @Test
    fun `replacing the cheat file clears records`() {
        val gameKey = "SLUS-21059_652050D2"
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, contentsA, packA)

            assertTrue(
                repository.importCheatFile(
                    gameKey = gameKey,
                    contents = contentsB,
                    mergeWithExisting = false
                ) > 0
            )

            assertTrue(repository.getInstalledCheatPacks(gameKey).isEmpty())
        } finally {
            cleanup(gameKey)
        }
    }

    @Test
    fun `removing one pack removes only its blocks and keeps enabled state`() {
        val gameKey = "SLUS-21059_652050D2"
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, contentsA, packA)
            install(repository, gameKey, contentsB, packB)

            val config = requireNotNull(repository.getGameConfig(gameKey, "SLUS-21059", "652050D2"))
            val health = config.blocks.first { it.title == "Infinite Health" }
            val money = config.blocks.first { it.title == "Max Money" }
            repository.setBlockEnabled(gameKey, health.id, true)
            repository.setBlockEnabled(gameKey, money.id, true)

            assertTrue(repository.removeInstalledCheatPack(gameKey, "pack-a"))

            val updated = requireNotNull(repository.getGameConfig(gameKey, "SLUS-21059", "652050D2"))
            assertEquals(listOf("Infinite Health", "Max Ammo"), updated.blocks.map(CheatBlock::title))
            val remainingHealth = updated.blocks.first { it.title == "Infinite Health" }
            assertEquals(
                listOf("patch=1,EE,00100000,word,0000FFFF"),
                remainingHealth.lines
            )
            assertTrue("An enabled block that survived removal must stay enabled", remainingHealth.enabled)
            assertFalse(updated.blocks.first { it.title == "Max Ammo" }.enabled)
            assertEquals(listOf("pack-b"), repository.getInstalledCheatPacks(gameKey).map { it.id })
        } finally {
            cleanup(gameKey)
        }
    }

    @Test
    fun `removing a pack keeps lines another pack still needs`() {
        val gameKey = "SLUS-21059_652050D2"
        val sharedLine = "patch=1,EE,00100000,word,00000064"
        val sharedA = """
            // Infinite Health
            $sharedLine
        """.trimIndent()
        val sharedB = """
            // Infinite Health
            $sharedLine
        """.trimIndent()
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, sharedA, packA)
            install(repository, gameKey, sharedB, packB)

            assertTrue(repository.removeInstalledCheatPack(gameKey, "pack-a"))

            val updated = requireNotNull(repository.getGameConfig(gameKey, "SLUS-21059", "652050D2"))
            assertEquals(listOf("Infinite Health"), updated.blocks.map(CheatBlock::title))
            assertEquals(listOf(sharedLine), updated.blocks.single().lines)
            assertEquals(listOf("pack-b"), repository.getInstalledCheatPacks(gameKey).map { it.id })
        } finally {
            cleanup(gameKey)
        }
    }

    @Test
    fun `removing the last pack deletes the cheat file`() {
        val gameKey = "SLUS-21059_652050D2"
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, contentsA, packA)

            assertTrue(repository.removeInstalledCheatPack(gameKey, "pack-a"))

            assertFalse(importedFile(gameKey).exists())
            assertNull(repository.getGameConfig(gameKey, "SLUS-21059", "652050D2"))
            assertTrue(repository.getInstalledCheatPacks(gameKey).isEmpty())
        } finally {
            cleanup(gameKey)
        }
    }

    @Test
    fun `legacy record needs fallback contents to be removed`() {
        val gameKey = "SLUS-21059_652050D2"
        try {
            val repository = CheatRepository(context)
            install(repository, gameKey, contentsA, packA)
            // Simulate a record written before pack contents were persisted.
            writeLegacyRecord(gameKey, "pack-a")

            assertFalse(repository.removeInstalledCheatPack(gameKey, "pack-a"))
            assertTrue(repository.removeInstalledCheatPack(gameKey, "pack-a", contentsA))

            assertFalse(importedFile(gameKey).exists())
            assertTrue(repository.getInstalledCheatPacks(gameKey).isEmpty())
        } finally {
            cleanup(gameKey)
        }
    }

    private fun install(
        repository: CheatRepository,
        gameKey: String,
        contents: String,
        pack: InstalledCheatPack
    ) {
        assertTrue(
            repository.importCheatFile(
                gameKey = gameKey,
                contents = contents,
                mergeWithExisting = true,
                installedPack = pack
            ) > 0
        )
    }

    private fun writeLegacyRecord(gameKey: String, packId: String) {
        val file = File(EmulatorStorage.appStateDir(context), "cheat-pack-installs.json")
        val json = if (file.exists()) runCatching { JSONObject(file.readText()) }.getOrDefault(JSONObject()) else JSONObject()
        json.put(gameKey.lowercase(), JSONArray().put(packId))
        file.writeText(json.toString())
    }

    private fun importedFile(gameKey: String): File =
        File(EmulatorStorage.importedCheatsDir(context), "$gameKey.pnach")

    private fun cleanup(vararg gameKeys: String) {
        val repository = CheatRepository(context)
        gameKeys.forEach { gameKey ->
            importedFile(gameKey).delete()
            repository.setEnabledBlocks(gameKey, emptySet())
            repository.clearInstalledCheatPacks(gameKey)
        }
    }
}

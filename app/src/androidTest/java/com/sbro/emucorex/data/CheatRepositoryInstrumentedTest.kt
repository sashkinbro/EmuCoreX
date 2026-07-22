package com.sbro.emucorex.data

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.core.EmulatorStorage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class CheatRepositoryInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    private val preferences = AppPreferences(context)
    private val repository = CheatRepository(context)

    @Test
    fun syncWritesOnlyCanonicalFileAndRemovesLegacyDuplicate() {
        val token = System.nanoTime().toString().takeLast(5).padStart(5, '0')
        val serial = "TEST-$token"
        val crc = System.nanoTime().toString(16).takeLast(8).padStart(8, '0').uppercase()
        val gameKey = "${serial}_$crc"
        val activeRoot = EmulatorStorage.cheatsDir(context, preferences.getEmulatorDataPathSync())
        val canonical = File(activeRoot, "${serial}_$crc.pnach")
        val legacy = File(activeRoot, "$crc.pnach")
        try {
            legacy.writeText("patch=1,EE,00000000,word,00000000\n")
            assertEquals(2, repository.importCheatFile(gameKey, TWO_CHEATS, enableAllByDefault = true))

            repository.syncActiveCheats(gameKey, serial, crc)

            assertTrue(canonical.exists())
            assertFalse(legacy.exists())
            assertEquals(2, canonical.readLines().count { it.startsWith("patch=") })

            repository.setEnabledBlocks(gameKey, emptySet())
            repository.syncActiveCheats(gameKey, serial, crc)
            assertFalse(canonical.exists())
            assertFalse(legacy.exists())
        } finally {
            repository.deleteImportedCheats(gameKey, serial, crc)
            canonical.delete()
            legacy.delete()
        }
    }

    @Test
    fun updateKeepsOnlyStillExistingEnabledBlocksAndDeleteWithoutIdsCleansActiveFile() {
        val token = System.nanoTime().toString().takeLast(5).padStart(5, '0')
        val serial = "TEST-$token"
        val crc = System.nanoTime().toString(16).takeLast(8).padStart(8, '0').uppercase()
        val gameKey = "${serial}_$crc"
        val activeRoot = EmulatorStorage.cheatsDir(context, preferences.getEmulatorDataPathSync())
        val canonical = File(activeRoot, "${serial}_$crc.pnach")
        try {
            repository.importCheatFile(gameKey, TWO_CHEATS, enableAllByDefault = true)
            repository.syncActiveCheats(gameKey, serial, crc)
            assertTrue(canonical.exists())

            assertEquals(0, repository.updateImportedCheatText(gameKey, "// no patch lines"))
            assertEquals(2, requireNotNull(repository.getGameConfig(gameKey, serial, crc)).blocks.size)

            assertEquals(1, repository.updateImportedCheatText(gameKey, SECOND_CHEAT_ONLY))
            val config = requireNotNull(repository.getGameConfig(gameKey, serial, crc))
            assertEquals(1, config.blocks.size)
            assertTrue(config.blocks.single().enabled)

            repository.syncActiveCheats(gameKey, serial, crc)
            assertEquals(1, canonical.readLines().count { it.startsWith("patch=") })

            repository.deleteImportedCheats(gameKey, null, null)
            assertFalse(canonical.exists())
            assertFalse(repository.listImportedCheatFiles().any { it.gameKey == gameKey })
        } finally {
            repository.deleteImportedCheats(gameKey, serial, crc)
            canonical.delete()
        }
    }

    @Test
    fun fileNameRequiringSanitizationKeepsEnabledStateAcrossManagerAndRuntimeLookups() {
        val rawGameKey = "Test Game ${System.nanoTime()}"
        val normalizedGameKey = rawGameKey.replace(Regex("[^a-zA-Z0-9._-]"), "_")
        try {
            assertEquals(2, repository.importCheatFile(rawGameKey, TWO_CHEATS, enableAllByDefault = true))
            assertTrue(repository.listImportedCheatFiles().any { it.gameKey == normalizedGameKey })

            val fromManagerKey = requireNotNull(
                repository.getGameConfig(normalizedGameKey, "TEST-00000", "12345678")
            )
            val fromRuntimeKey = requireNotNull(
                repository.getGameConfig(rawGameKey, "TEST-00000", "12345678")
            )
            assertTrue(fromManagerKey.blocks.all { it.enabled })
            assertTrue(fromRuntimeKey.blocks.all { it.enabled })
            assertEquals(normalizedGameKey, fromRuntimeKey.gameKey)
        } finally {
            repository.deleteImportedCheats(normalizedGameKey, null, null)
        }
    }

    @Test
    fun runtimeLookupIsCaseInsensitiveForImportedSerialFileNames() {
        val lowerCaseKey = "test-${System.nanoTime().toString().takeLast(5)}_abcdef12"
        try {
            repository.importCheatFile(lowerCaseKey, TWO_CHEATS, enableAllByDefault = true)

            val config = requireNotNull(
                repository.getGameConfig(lowerCaseKey.uppercase(), "TEST-00000", "ABCDEF12")
            )
            assertEquals(lowerCaseKey, config.gameKey)
            assertTrue(config.blocks.all { it.enabled })
        } finally {
            repository.deleteImportedCheats(lowerCaseKey, null, null)
        }
    }

    @Test
    fun remoteImportsMergeWithoutDuplicatingBlocksOrLosingEnabledState() {
        val token = System.nanoTime().toString().takeLast(5).padStart(5, '0')
        val serial = "TEST-$token"
        val crc = System.nanoTime().toString(16).takeLast(8).padStart(8, '0').uppercase()
        val gameKey = "${serial}_$crc"
        try {
            assertEquals(2, repository.importCheatFile(gameKey, TWO_CHEATS, enableAllByDefault = true))
            assertEquals(
                3,
                repository.importCheatFile(
                    gameKey,
                    SECOND_PACK,
                    enableAllByDefault = false,
                    mergeWithExisting = true
                )
            )

            val config = requireNotNull(repository.getGameConfig(gameKey, serial, crc))
            assertEquals(listOf("Infinite health", "Infinite money", "Max score"), config.blocks.map { it.title })
            assertTrue(config.blocks.first { it.title == "Infinite health" }.enabled)
            assertTrue(config.blocks.first { it.title == "Infinite money" }.enabled)
            assertFalse(config.blocks.first { it.title == "Max score" }.enabled)
            assertEquals(
                2,
                config.blocks.first { it.title == "Infinite money" }.lines.size
            )
        } finally {
            repository.deleteImportedCheats(gameKey, serial, crc)
        }
    }

    @Test
    fun deleteInfersSerialAndCrcToCleanFilesCreatedByOlderVersions() {
        val token = System.nanoTime().toString().takeLast(5).padStart(5, '0')
        val serial = "TEST-$token"
        val crc = System.nanoTime().toString(16).takeLast(8).padStart(8, '0').uppercase()
        val gameKey = "${serial}_$crc"
        val root = EmulatorStorage.cheatsDir(context, preferences.getEmulatorDataPathSync())
        val canonical = File(root, "${serial}_$crc.pnach")
        val legacy = File(root, "$crc.pnach")
        try {
            repository.importCheatFile(gameKey, TWO_CHEATS, enableAllByDefault = true)
            canonical.writeText("old generated file")
            legacy.writeText("old duplicate file")

            repository.deleteImportedCheats(gameKey, null, null)

            assertFalse(canonical.exists())
            assertFalse(legacy.exists())
        } finally {
            repository.deleteImportedCheats(gameKey, serial, crc)
            canonical.delete()
            legacy.delete()
        }
    }

    private companion object {
        const val TWO_CHEATS = """
// Infinite health
patch=1,EE,00100000,word,00000001

// Infinite money
patch=1,EE,00100004,word,00000002
"""
        const val SECOND_CHEAT_ONLY = """
// Infinite money
patch=1,EE,00100004,word,00000002
"""
        const val SECOND_PACK = """
// Infinite money
patch=1,EE,00100004,word,00000002
patch=1,EE,00100008,word,00000003

// Max score
patch=1,EE,0010000C,word,00000004
"""
    }
}

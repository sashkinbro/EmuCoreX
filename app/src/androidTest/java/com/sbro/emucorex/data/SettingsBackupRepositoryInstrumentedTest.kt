package com.sbro.emucorex.data

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.core.EmulatorStorage
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

@RunWith(AndroidJUnit4::class)
class SettingsBackupRepositoryInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    private val preferences = AppPreferences(context)
    private val repository = SettingsBackupRepository(
        context = context,
        preferences = preferences,
        perGameSettingsRepository = PerGameSettingsRepository(context),
        cheatRepository = CheatRepository(context)
    )

    @Test
    fun quickSavesAreExcludedByDefault() = runBlocking {
        val state = uniqueStateFile("default")
        val archive = backupFile()
        try {
            state.writeText("quick-save")

            archive.outputStream().use { repository.writeBackup(it) }
            assertFalse(zipEntries(archive).any { it.startsWith("save-states/") })
        } finally {
            state.delete()
            archive.delete()
        }
    }

    @Test
    fun selectedQuickSavesRoundTripButResumeStatesStayExcluded() = runBlocking {
        val state = uniqueStateFile("included")
        val resume = File(state.parentFile, "${state.nameWithoutExtension}.resume.p2s")
        val archive = backupFile()
        try {
            state.writeText("quick-save-content")
            resume.writeText("resume-content")

            archive.outputStream().use { repository.writeBackup(it, includeSaveStates = true) }
            val entries = zipEntries(archive)
            assertTrue("save-states/${state.name}" in entries)
            assertFalse("save-states/${resume.name}" in entries)

            state.delete()
            resume.delete()
            archive.inputStream().use { repository.restoreBackup(it) }
            assertTrue(state.readText() == "quick-save-content")
            assertFalse(resume.exists())
        } finally {
            state.delete()
            resume.delete()
            archive.delete()
        }
    }

    @Test
    fun restoreRejectsSaveStatePathTraversal() = runBlocking {
        val archive = backupFile()
        val saveStatesRoot = EmulatorStorage.saveStatesDir(
            context,
            preferences.getEmulatorDataPathSync()
        )
        val escaped = File(saveStatesRoot.parentFile, "escaped-backup-test.p2s")
        try {
            ZipOutputStream(archive.outputStream()).use { zip ->
                zip.putNextEntry(ZipEntry("save-states/../${escaped.name}"))
                zip.write("malicious".toByteArray())
                zip.closeEntry()
            }

            val result = runCatching {
                archive.inputStream().use { repository.restoreBackup(it) }
            }
            assertTrue(result.isFailure)
            assertFalse(escaped.exists())
        } finally {
            escaped.delete()
            archive.delete()
        }
    }

    @Test
    fun importedCheatFileRoundTripsWithBackup() = runBlocking {
        val cheatRepository = CheatRepository(context)
        val gameKey = "BACKUP-CHEAT-${System.nanoTime()}"
        val archive = backupFile()
        try {
            assertTrue(
                cheatRepository.importCheatFile(
                    gameKey,
                    "// Test\npatch=1,EE,00100000,word,00000001\n",
                    enableAllByDefault = true
                ) > 0
            )
            archive.outputStream().use { repository.writeBackup(it) }
            assertTrue("cheat-files/$gameKey.pnach" in zipEntries(archive))

            cheatRepository.deleteImportedCheats(gameKey, null, null)
            assertFalse(cheatRepository.listImportedCheatFiles().any { it.gameKey == gameKey })

            archive.inputStream().use { repository.restoreBackup(it) }
            assertTrue(cheatRepository.listImportedCheatFiles().any { it.gameKey == gameKey })
            assertTrue(requireNotNull(cheatRepository.getGameConfig(gameKey, "TEST-00000", "12345678"))
                .blocks.single().enabled)
        } finally {
            cheatRepository.deleteImportedCheats(gameKey, null, null)
            archive.delete()
        }
    }

    @Test
    fun restoreRejectsCheatPathTraversal() = runBlocking {
        val archive = backupFile()
        val importedRoot = EmulatorStorage.importedCheatsDir(context)
        val escaped = File(importedRoot.parentFile, "escaped-cheat-test.pnach")
        try {
            ZipOutputStream(archive.outputStream()).use { zip ->
                zip.putNextEntry(ZipEntry("cheat-files/../${escaped.name}"))
                zip.write("patch=1,EE,00100000,word,00000001".toByteArray())
                zip.closeEntry()
            }

            val result = runCatching { archive.inputStream().use { repository.restoreBackup(it) } }
            assertTrue(result.isFailure)
            assertFalse(escaped.exists())
        } finally {
            escaped.delete()
            archive.delete()
        }
    }

    private fun uniqueStateFile(suffix: String): File {
        val directory = EmulatorStorage.saveStatesDir(
            context,
            preferences.getEmulatorDataPathSync()
        )
        return File(directory, "BACKUP-TEST-${System.nanoTime()}-$suffix.00.p2s")
    }

    private fun backupFile(): File = File(context.cacheDir, "provider-settings-backup.zip")

    private fun zipEntries(archive: File): Set<String> {
        val entries = linkedSetOf<String>()
        ZipInputStream(archive.inputStream()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                entries += entry.name
                zip.closeEntry()
            }
        }
        return entries
    }
}

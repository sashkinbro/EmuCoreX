package com.sbro.emucorex.data

import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.settings.SettingsUiState
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class WorkerOptimizationSettingsTest {
    @Test fun bothOptimizationsDefaultOffAcrossFreshStates() {
        assertFalse(SettingsSnapshot().enableIopThread)
        assertFalse(SettingsSnapshot().enableVUAot)
        assertFalse(SettingsUiState().enableIopThread)
        assertFalse(SettingsUiState().enableVUAot)
        assertFalse(EmulationUiState().enableIopThread)
        assertFalse(EmulationUiState().enableVUAot)
        val profile = PerGameSettings(gameKey = "game", gameTitle = "Game")
        assertFalse(profile.enableIopThread)
        assertFalse(profile.enableVUAot)
    }

    @Test fun explicitOffOverridesEnabledGlobalsAndClearingRestoresInheritance() {
        val global = PerGameSettings(gameKey = "game", gameTitle = "Game",
            enableIopThread = true, enableVUAot = true)
        val off = global.copy(enableIopThread = false, enableVUAot = false)
            .withOnlyOverridesComparedWith(global)
        assertEquals(setOf("enableIopThread", "enableVUAot"), off.providedKeys)
        assertTrue(global.withOnlyOverridesComparedWith(global).providedKeys!!.isEmpty())
    }

    @Test fun globalSettingsSurviveBackupRestoreAndFreshPreferenceInstance() = runBlocking {
        val context = RuntimeEnvironment.getApplication()
        val preferences = AppPreferences(context)
        preferences.setEnableIopThread(true)
        preferences.setEnableVUAot(true)
        val backup = preferences.exportJson()
        assertTrue(backup.getBoolean("enableIopThread"))
        assertTrue(backup.getBoolean("enableVUAot"))
        preferences.setEnableIopThread(false)
        preferences.setEnableVUAot(false)
        preferences.importJson(backup)
        val restored = AppPreferences(context)
        assertTrue(restored.enableIopThread.first())
        assertTrue(restored.enableVUAot.first())
        // Older backups have no new keys and must retain the disabled default.
        backup.remove("enableIopThread")
        backup.remove("enableVUAot")
        preferences.importJson(backup)
        assertFalse(restored.enableIopThread.first())
        assertFalse(restored.enableVUAot.first())
    }

    @Test fun perGameBackupPreservesTrueFalseAndSparseKeys() {
        val context = RuntimeEnvironment.getApplication()
        val repository = PerGameSettingsRepository(context)
        repository.save(PerGameSettings(gameKey = "worker-test", gameTitle = "Worker test",
            enableIopThread = true, enableVUAot = false,
            providedKeys = setOf("enableIopThread", "enableVUAot")))
        val backup = repository.exportJson()
        repository.deleteAll()
        repository.importJson(backup)
        val restored = PerGameSettingsRepository(context).get("worker-test")!!
        assertTrue(restored.enableIopThread)
        assertFalse(restored.enableVUAot)
        assertEquals(setOf("enableIopThread", "enableVUAot"), restored.providedKeys)
        repository.delete("worker-test")
    }
}

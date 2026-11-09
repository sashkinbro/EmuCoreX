package com.sbro.emucorex.data

import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.utils.RetroAchievementsBridge
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class RetroAchievementsHardcorePersistenceTest {
    @Test fun nativeStartupAndUnrelatedCommitsCannotEraseGlobalRequests() = runBlocking {
        val context = RuntimeEnvironment.getApplication()
        EmulatorBridge.initializeOnce(context)
        val preferences = AppPreferences(context)
        preferences.setAchievementsEnabled(true)
        preferences.setAchievementsHardcore(true)
        RetroAchievementsBridge.notifySettingsChanged("Achievements", "ChallengeMode", "false")
        RetroAchievementsBridge.notifySettingsChanged("Achievements", "Enabled", "false")
        // The old callback launched detached IO writes; allow them to expose the regression.
        delay(250)
        val reopened = AppPreferences(context)
        assertTrue(reopened.getAchievementsEnabledSync())
        assertTrue(reopened.getAchievementsHardcoreSync())
        preferences.setAchievementsHardcore(false)
        RetroAchievementsBridge.notifySettingsChanged("Achievements", "ChallengeMode", "true")
        delay(250)
        assertFalse(AppPreferences(context).getAchievementsHardcoreSync())
    }

    @Test fun explicitChoicesSurviveSparseSavingEvenWhenTheyMatchTheGlobalDefault() {
        val defaults = PerGameSettings(gameKey = "hardcore", gameTitle = "Hardcore test")
        val enabled = defaults.copy(achievementsHardcore = true).withOnlyOverridesComparedWith(defaults)
        val disabled = defaults.copy(achievementsHardcore = false).withOnlyOverridesComparedWith(defaults)
        assertEquals(setOf("achievementsHardcore"), enabled.providedKeys)
        assertEquals(setOf("achievementsHardcore"), disabled.providedKeys)
        assertTrue(enabled.achievementsHardcore!!)
        assertFalse(disabled.achievementsHardcore!!)
        val inherited = enabled.copy(achievementsHardcore = null).withOnlyOverridesComparedWith(defaults)
        assertNull(inherited.achievementsHardcore)
        assertTrue(inherited.providedKeys!!.isEmpty())
    }

    @Test fun coldRepositoryAndBackupRoundTripPreserveEnabledDisabledAndInherit() {
        val context = RuntimeEnvironment.getApplication()
        val repository = PerGameSettingsRepository(context)
        val previous = repository.exportJson()
        try {
            val defaults = PerGameSettings(gameKey = "hardcore", gameTitle = "Hardcore test")
            for ((key, value) in listOf("enabled" to true, "disabled" to false, "inherit" to null)) {
                repository.save(defaults.copy(gameKey = key, achievementsHardcore = value)
                    .withOnlyOverridesComparedWith(defaults))
            }
            val backup = repository.exportJson()
            repository.importJson(backup)
            val reopened = PerGameSettingsRepository(context)
            assertEquals(true, reopened.get("enabled")!!.achievementsHardcore)
            assertEquals(false, reopened.get("disabled")!!.achievementsHardcore)
            assertNull(reopened.get("inherit")!!.achievementsHardcore)
            assertEquals(setOf("achievementsHardcore"), reopened.get("enabled")!!.providedKeys)
            assertEquals(setOf("achievementsHardcore"), reopened.get("disabled")!!.providedKeys)
            assertFalse("achievementsHardcore" in reopened.get("inherit")!!.providedKeys!!)
            // A pre-existing profile without this new key follows the global setting.
            repository.importJson(JSONObject().put("profiles", JSONArray().put(
                JSONObject().put("gameKey", "legacy").put("gameTitle", "Legacy"))))
            assertNull(PerGameSettingsRepository(context).get("legacy")!!.achievementsHardcore)
        } finally {
            repository.importJson(previous)
        }
    }
}

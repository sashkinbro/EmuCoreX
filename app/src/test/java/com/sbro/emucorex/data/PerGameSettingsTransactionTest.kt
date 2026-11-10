package com.sbro.emucorex.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * The settings file is rewritten as a whole for every mutation, so a read-modify-write
 * pair that overlaps another one silently drops the other's profile. These tests pin the
 * serialisation contract that [PerGameSettingsRepository.transaction] provides.
 */
@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class PerGameSettingsTransactionTest {
    private lateinit var context: Context

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        PerGameSettingsRepository(context).deleteAll()
    }

    @After
    fun tearDown() {
        PerGameSettingsRepository(context).deleteAll()
    }

    @Test
    fun concurrentReadModifyWriteKeepsEveryProfile() = runBlocking {
        val keys = (1..8).map { "transaction-game-$it" }
        val repository = PerGameSettingsRepository(context)

        coroutineScope {
            keys.forEach { key ->
                launch(Dispatchers.Default) {
                    repository.transaction {
                        val existing = repository.get(key)
                            ?: PerGameSettings(gameKey = key, gameTitle = key)
                        // A plain get/save pair would lose the profiles written by the
                        // transactions that finish inside this window.
                        Thread.sleep(2)
                        repository.save(existing.copy(upscaleMultiplier = 3f))
                    }
                }
            }
        }

        val stored = PerGameSettingsRepository(context).getAll()
        assertEquals(keys.toSet(), stored.map { it.gameKey }.toSet())
        assertTrue(stored.all { it.upscaleMultiplier == 3f })
    }

    @Test
    fun transactionSeesItsOwnWritesAndReturnsTheBlockResult() = runBlocking {
        val repository = PerGameSettingsRepository(context)

        val title = repository.transaction {
            repository.save(PerGameSettings(gameKey = "transaction-echo", gameTitle = "Echo"))
            repository.get("transaction-echo")?.gameTitle
        }

        assertEquals("Echo", title)
        assertEquals("Echo", PerGameSettingsRepository(context).get("transaction-echo")?.gameTitle)
    }

    @Test
    fun sequentialTransactionsPreserveEarlierProfiles() = runBlocking {
        val repository = PerGameSettingsRepository(context)

        repository.transaction {
            repository.save(PerGameSettings(gameKey = "transaction-a", gameTitle = "A"))
        }
        repository.transaction {
            repository.save(PerGameSettings(gameKey = "transaction-b", gameTitle = "B"))
        }

        val keys = PerGameSettingsRepository(context).getAll().map { it.gameKey }.toSet()
        assertEquals(setOf("transaction-a", "transaction-b"), keys)
    }
}

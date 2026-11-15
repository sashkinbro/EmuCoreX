package com.sbro.emucorex.data

import com.sbro.emucorex.ui.profile.shouldRefreshProfileDerived
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class CloudSyncPolicyTest {

    @Test
    fun `backoff doubles from one minute and caps at sixty`() {
        assertEquals(0L, PlayTimeFlushBackoff.delayMs(0))
        assertEquals(60_000L, PlayTimeFlushBackoff.delayMs(1))
        assertEquals(120_000L, PlayTimeFlushBackoff.delayMs(2))
        assertEquals(240_000L, PlayTimeFlushBackoff.delayMs(3))
        assertEquals(3_600_000L, PlayTimeFlushBackoff.delayMs(7))
        assertEquals(3_600_000L, PlayTimeFlushBackoff.delayMs(500))
    }

    @Test
    fun `in-flight batch survives until completed and keeps its id on retry`() = runBlocking {
        val repo = PlayTimeSyncCacheRepository(RuntimeEnvironment.getApplication())
        repo.add(PlayerPlayTimeDelta("/g/a.iso", "A", "SLUS-1", null, 5_000L, 1L, 10L))
        val first = repo.beginFlush()
        assertNotNull(first)
        assertEquals(5_000L, first!!.entries.single().durationMs)
        // Simulated failed commit: nothing is completed, a retry gets the same batch and id.
        repo.add(PlayerPlayTimeDelta("/g/a.iso", "A", "SLUS-1", null, 2_000L, 0L, 20L))
        val retry = repo.beginFlush()
        assertEquals(first.id, retry!!.id)
        assertEquals(5_000L, retry.entries.single().durationMs)
        repo.completeFlush(first.id)
        // Time added during the failed attempt is a fresh batch afterwards, nothing lost or doubled.
        val next = repo.beginFlush()
        assertTrue(next!!.id != first.id)
        assertEquals(2_000L, next.entries.single().durationMs)
        repo.completeFlush(next.id)
        assertNull(repo.beginFlush())
    }

    @Test
    fun `failure backoff is persisted and cleared on success`() = runBlocking {
        val repo = PlayTimeSyncCacheRepository(RuntimeEnvironment.getApplication())
        assertEquals(0L, repo.getNextCloudAttemptAtMs())
        assertEquals(1_000L + 60_000L, repo.recordFlushFailure(1_000L))
        assertEquals(1_000L + 120_000L, repo.recordFlushFailure(1_000L))
        assertEquals(1_000L + 120_000L, repo.getNextCloudAttemptAtMs())
        repo.clearFlushBackoff()
        assertEquals(0L, repo.getNextCloudAttemptAtMs())
    }

    @Test
    fun `pending global stats merge per game and period`() {
        val day = 1_800_000_000_000L
        fun delta(ms: Long, at: Long, first: Boolean = false) =
            GameStatsDelta("SLUS-1", "SLUS-1", "A", ms, 1L, at, first)
        val merged = mergePendingGameStats(listOf(delta(1_000, day, true)), listOf(delta(2_000, day + 1_000)))
        assertEquals(1, merged.size)
        assertEquals(3_000L, merged[0].durationMs)
        assertEquals(2L, merged[0].sessions)
        assertTrue(merged[0].firstPlay)
        assertEquals(day + 1_000, merged[0].lastPlayedAtMs)
        // Different ISO week/month must stay separate so period documents stay correct.
        val split = mergePendingGameStats(merged, listOf(delta(500, day + 40L * 86_400_000L)))
        assertEquals(2, split.size)
        assertEquals(50, mergePendingGameStats(emptyList(), (0 until 80).map {
            GameStatsDelta("K$it", null, "t", 1, 1, day, false)
        }).size)
    }

    @Test
    fun `profile derived refresh is debounced`() {
        assertTrue(shouldRefreshProfileDerived(null, 10_000L, 0L, 1L))
        assertFalse(shouldRefreshProfileDerived(100_000L, 100_000L, 0L, 1_000_000L))
        assertFalse(shouldRefreshProfileDerived(100_000L, 130_000L, 0L, 1_000_000L))
        assertFalse(shouldRefreshProfileDerived(100_000L, 200_000L, 990_000L, 1_000_000L))
        assertTrue(shouldRefreshProfileDerived(100_000L, 200_000L, 900_000L, 1_000_000L))
    }

    @Test
    fun `unlocked cache round trips`() {
        val map = mapOf("a_1" to 5L, "b" to 7L)
        assertEquals(map, parseUnlockedCache(encodeUnlockedCache(map)))
        assertEquals(emptyMap<String, Long>(), parseUnlockedCache(null))
        assertEquals(mapOf("ok" to 1L), parseUnlockedCache("bad;ok=1;x=notnum"))
    }
}

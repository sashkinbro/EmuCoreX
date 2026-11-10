package com.sbro.emucorex.core

import org.junit.Assert.*
import org.junit.Test
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.Executors

class SettingsBatchCoordinatorTest {
    @Test
    fun concurrentWritersCannotPublishAPartialBatch() {
        val coordinator = SettingsBatchCoordinator()
        val events = Collections.synchronizedList(mutableListOf<String>())
        val firstWriting = CountDownLatch(1)
        val releaseFirst = CountDownLatch(1)
        val secondAttempted = CountDownLatch(1)
        val secondEntered = CountDownLatch(1)
        val workers = Executors.newFixedThreadPool(2)
        try {
            val first = workers.submit {
                coordinator.run({ events += "A.begin" }, { events += "A.end" }) {
                    firstWriting.countDown()
                    check(releaseFirst.await(3, TimeUnit.SECONDS))
                    events += "A.write"
                }
            }
            assertTrue(firstWriting.await(3, TimeUnit.SECONDS))
            val second = workers.submit {
                secondAttempted.countDown()
                coordinator.run({ secondEntered.countDown(); events += "B.begin" }, { events += "B.end" }) {
                    events += "B.write"
                }
            }
            assertTrue(secondAttempted.await(3, TimeUnit.SECONDS))
            assertFalse(secondEntered.await(100, TimeUnit.MILLISECONDS))
            releaseFirst.countDown()
            first.get(3, TimeUnit.SECONDS)
            second.get(3, TimeUnit.SECONDS)
            assertEquals(listOf("A.begin", "A.write", "A.end", "B.begin", "B.write", "B.end"), events)
        } finally {
            releaseFirst.countDown()
            workers.shutdownNow()
        }
    }

    @Test
    fun failedWriteEndsExactlyOnceAndAllowsTheNextBatch() {
        val coordinator = SettingsBatchCoordinator()
        var ended = 0
        val failure = IllegalStateException("write failed")
        val caught = runCatching { coordinator.run({}, { ended++ }) { throw failure } }.exceptionOrNull()
        assertSame(failure, caught)
        assertEquals(1, ended)
        assertEquals(42, coordinator.run({}, { ended++ }) { 42 })
        assertEquals(2, ended)
    }
}

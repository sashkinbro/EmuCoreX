package com.sbro.emucorex.core.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

class RetroAchievementsNotificationQueueTest {
    private fun n(id: Long, title: String, message: String = "", kind: String = "achievement") =
        RetroAchievementsNotification(id, kind, title, message, null)

    @Test
    fun unlockAndMasteryArriveTogetherBothSurviveInOrder() {
        var queue = emptyList<RetroAchievementsNotification>()
        queue = RetroAchievementsNotificationQueue.enqueue(queue, n(1, "Unlock", kind = "achievement"))
        queue = RetroAchievementsNotificationQueue.enqueue(queue, n(2, "Mastered", kind = "mastery"))
        assertEquals(listOf(1L, 2L), queue.map { it.id })
        queue = RetroAchievementsNotificationQueue.dismiss(queue, 1L)
        assertEquals(listOf(2L), queue.map { it.id })
    }

    @Test
    fun duplicatesAreCoalesced() {
        val first = RetroAchievementsNotificationQueue.enqueue(emptyList(), n(1, "A", "same"))
        val second = RetroAchievementsNotificationQueue.enqueue(first, n(2, "A", "same"))
        assertSame(first, second)
    }

    @Test
    fun queueIsCappedAndKeepsTheHead() {
        var queue = emptyList<RetroAchievementsNotification>()
        for (i in 1L..25L) queue = RetroAchievementsNotificationQueue.enqueue(queue, n(i, "T$i"))
        assertEquals(RetroAchievementsNotificationQueue.MAX_SIZE, queue.size)
        assertEquals(1L, queue.first().id)
    }

    @Test
    fun dismissingUnknownIdKeepsSameList() {
        val queue = RetroAchievementsNotificationQueue.enqueue(emptyList(), n(1, "A"))
        assertSame(queue, RetroAchievementsNotificationQueue.dismiss(queue, 99L))
    }
}

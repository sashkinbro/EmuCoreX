package com.sbro.emucorex.ui.common

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ProReminderPolicyTest {
    private val hour = 60L * 60 * 1000
    private val now = 1_000_000_000_000L

    private fun show(
        last: Long = now - 4 * hour,
        streak: Int = 0,
        pro: Boolean = false,
        canPurchase: Boolean = true,
        review: Long = 0L
    ) = ProReminderPolicy.shouldShow(now, last, streak, pro, canPurchase, review)

    @Test
    fun showsAfterInterval() {
        assertTrue(show())
        assertFalse(show(last = now - 2 * hour))
    }

    @Test
    fun blockedForProOrUnavailableBilling() {
        assertFalse(show(pro = true))
        assertFalse(show(canPurchase = false))
    }

    @Test
    fun neverShownYetIsGracePeriod() {
        assertFalse(show(last = 0L))
    }

    @Test
    fun backoffAfterRepeatedDismissals() {
        assertEquals(3 * hour, ProReminderPolicy.intervalMs(4))
        assertEquals(12 * hour, ProReminderPolicy.intervalMs(5))
        assertFalse(show(streak = 5, last = now - 4 * hour))
        assertTrue(show(streak = 5, last = now - 13 * hour))
        assertTrue(show(streak = 4, last = now - 4 * hour))
    }

    @Test
    fun cardHideFollowsReminderRhythm() {
        assertEquals(3 * hour, promoCardHideMs(1))
        assertEquals(3 * hour, promoCardHideMs(4))
        assertEquals(12 * hour, promoCardHideMs(5))
    }

    @Test
    fun recentReviewAttemptSuppresses() {
        assertFalse(show(review = now - 60_000L))
        assertTrue(show(review = now - 10 * 60_000L))
    }

    @Test
    fun backoffRepeatsEveryFifthDismissal() {
        val h = 60L * 60 * 1000
        assertEquals(3 * h, ProReminderPolicy.intervalMs(1))
        assertEquals(3 * h, ProReminderPolicy.intervalMs(4))
        assertEquals(12 * h, ProReminderPolicy.intervalMs(5))
        assertEquals(3 * h, ProReminderPolicy.intervalMs(6))
        assertEquals(3 * h, ProReminderPolicy.intervalMs(9))
        assertEquals(12 * h, ProReminderPolicy.intervalMs(10))
    }
}

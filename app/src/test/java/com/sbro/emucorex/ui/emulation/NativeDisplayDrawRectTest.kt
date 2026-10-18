package com.sbro.emucorex.ui.emulation

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Test

class NativeDisplayDrawRectTest {
    @Test
    fun repeatedJniSnapshotsKeepThePublishedStateIdentity() {
        val published = floatArrayOf(240f, 0f, 1680f, 1080f)
        repeat(100) {
            assertSame(published, updatedNativeDisplayDrawRect(published, published.copyOf()))
        }
    }

    @Test
    fun viewportChangesStillPublishNewBounds() {
        val previous = floatArrayOf(240f, 0f, 1680f, 1080f)
        for (index in previous.indices) {
            val resized = previous.copyOf().also { it[index] += 1f }
            val updated = updatedNativeDisplayDrawRect(previous, resized)
            assertNotSame(previous, updated)
            assertArrayEquals(resized, updated, 0f)
        }
    }

    @Test
    fun missingOrInvalidBoundsClearThePreviousFrameAndAllowRecovery() {
        val previous = floatArrayOf(240f, 0f, 1680f, 1080f)
        for (invalid in listOf(null, floatArrayOf(), floatArrayOf(0f, 0f, 100f),
            floatArrayOf(1f, 0f, 1f, 100f), floatArrayOf(0f, 1f, 100f, 0f),
            floatArrayOf(0f, 0f, Float.NaN, 100f))) {
            assertNull(updatedNativeDisplayDrawRect(previous, invalid))
        }
        assertSame(previous, updatedNativeDisplayDrawRect(null, previous))
        assertNull(updatedNativeDisplayDrawRect(null, null))
    }
}

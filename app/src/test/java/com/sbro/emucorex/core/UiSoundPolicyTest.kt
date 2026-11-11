package com.sbro.emucorex.core

import com.sbro.emucorex.core.UiSoundPolicy.FocusOutcome
import com.sbro.emucorex.core.UiSoundPolicy.MusicBlock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UiSoundPolicyTest {
    private fun play(
        enabled: Boolean = true,
        volume: Int = 60,
        foreground: Boolean = true,
        emulation: Boolean = false,
        suppressed: Boolean = false
    ) = UiSoundPolicy.shouldPlayMusic(enabled, volume, foreground, emulation, suppressed)

    private fun block(
        enabled: Boolean = true,
        volume: Int = 60,
        foreground: Boolean = true,
        emulation: Boolean = false,
        suppressed: Boolean = false
    ) = UiSoundPolicy.musicBlock(enabled, volume, foreground, emulation, suppressed)

    @Test
    fun playsWhenEnabledVisibleAndNoGame() = assertTrue(play())

    @Test
    fun offByDefaultSetting() = assertFalse(play(enabled = false))

    @Test
    fun silentWhenVolumeIsZero() = assertFalse(play(volume = 0))

    @Test
    fun pausesWhenAppLeavesForeground() = assertFalse(play(foreground = false))

    @Test
    fun resumesWhenAppReturnsToForeground() {
        assertFalse(play(foreground = false))
        assertTrue(play(foreground = true))
    }

    @Test
    fun stopsDuringGameplay() = assertFalse(play(emulation = true))

    @Test
    fun staysQuietAfterPermanentFocusLoss() = assertFalse(play(suppressed = true))

    @Test
    fun switchingOnWhileVisibleStartsImmediately() {
        assertFalse(play(enabled = false))
        assertTrue(play(enabled = true))
    }

    @Test
    fun switchingOffStopsImmediately() {
        assertTrue(play(enabled = true))
        assertFalse(play(enabled = false))
    }

    @Test
    fun homeThenReturnStopsThenResumes() {
        assertTrue(play(foreground = true))
        assertEquals(MusicBlock.BACKGROUND, block(foreground = false))
        assertEquals(MusicBlock.NONE, block(foreground = true))
    }

    @Test
    fun blockReasonsHavePriority() {
        assertEquals(MusicBlock.NONE, block())
        assertEquals(MusicBlock.DISABLED, block(enabled = false, foreground = false, emulation = true))
        assertEquals(MusicBlock.SILENT, block(volume = 0, foreground = false))
        assertEquals(MusicBlock.BACKGROUND, block(foreground = false, emulation = true))
        assertEquals(MusicBlock.EMULATION, block(emulation = true, suppressed = true))
        assertEquals(MusicBlock.FOCUS_LOST, block(suppressed = true))
    }

    @Test
    fun grantedFocusPlays() = assertEquals(FocusOutcome.PLAY, UiSoundPolicy.focusOutcome(granted = true, delayed = false))

    @Test
    fun delayedFocusWaitsForGain() =
        assertEquals(FocusOutcome.WAIT_FOR_GAIN, UiSoundPolicy.focusOutcome(granted = false, delayed = true))

    @Test
    fun failedFocusDoesNotPlay() =
        assertEquals(FocusOutcome.DENIED, UiSoundPolicy.focusOutcome(granted = false, delayed = false))

    @Test
    fun playerStartsOnlyWhenPreparedIdleAndFocused() {
        assertTrue(UiSoundPolicy.shouldStartPlayer(prepared = true, playing = false, hasFocus = true, transientFocusLoss = false))
        assertFalse(UiSoundPolicy.shouldStartPlayer(prepared = false, playing = false, hasFocus = true, transientFocusLoss = false))
        assertFalse(UiSoundPolicy.shouldStartPlayer(prepared = true, playing = true, hasFocus = true, transientFocusLoss = false))
        assertFalse(UiSoundPolicy.shouldStartPlayer(prepared = true, playing = false, hasFocus = false, transientFocusLoss = false))
        assertFalse(UiSoundPolicy.shouldStartPlayer(prepared = true, playing = false, hasFocus = true, transientFocusLoss = true))
    }
}

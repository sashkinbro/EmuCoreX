package com.sbro.emucorex.core

/** Pure decision logic for [UiSoundManager] background music, kept Android-free so it can be unit tested. */
internal object UiSoundPolicy {
    /** Why background music is (not) allowed to play right now. [NONE] means it may play. */
    enum class MusicBlock { NONE, DISABLED, SILENT, BACKGROUND, EMULATION, FOCUS_LOST }

    /** What to do with a player after an audio focus request. */
    enum class FocusOutcome {
        /** Focus granted: start playback as soon as the player is prepared. */
        PLAY,

        /** Focus delayed (e.g. during a call): wait for the AUDIOFOCUS_GAIN callback. */
        WAIT_FOR_GAIN,

        /** Focus refused: do not play, release the player and retry later. */
        DENIED
    }

    fun musicBlock(
        enabled: Boolean,
        volumePercent: Int,
        appForeground: Boolean,
        emulationActive: Boolean,
        suppressedByFocusLoss: Boolean
    ): MusicBlock = when {
        !enabled -> MusicBlock.DISABLED
        volumePercent <= 0 -> MusicBlock.SILENT
        !appForeground -> MusicBlock.BACKGROUND
        emulationActive -> MusicBlock.EMULATION
        suppressedByFocusLoss -> MusicBlock.FOCUS_LOST
        else -> MusicBlock.NONE
    }

    /**
     * Music plays only while it is enabled and audible, the frontend is visible, no game is running and
     * another app has not permanently taken audio focus.
     */
    fun shouldPlayMusic(
        enabled: Boolean,
        volumePercent: Int,
        appForeground: Boolean,
        emulationActive: Boolean,
        suppressedByFocusLoss: Boolean
    ): Boolean = musicBlock(enabled, volumePercent, appForeground, emulationActive, suppressedByFocusLoss) ==
        MusicBlock.NONE

    fun focusOutcome(granted: Boolean, delayed: Boolean): FocusOutcome = when {
        granted -> FocusOutcome.PLAY
        delayed -> FocusOutcome.WAIT_FOR_GAIN
        else -> FocusOutcome.DENIED
    }

    /** A prepared, idle player is started only while it holds focus and no transient loss is active. */
    fun shouldStartPlayer(prepared: Boolean, playing: Boolean, hasFocus: Boolean, transientFocusLoss: Boolean): Boolean =
        prepared && !playing && hasFocus && !transientFocusLoss
}

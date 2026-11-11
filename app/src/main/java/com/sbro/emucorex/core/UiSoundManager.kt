package com.sbro.emucorex.core

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.MediaPlayer
import android.media.SoundPool
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import androidx.annotation.MainThread
import com.sbro.emucorex.R
import com.sbro.emucorex.data.UiSoundKind
import com.sbro.emucorex.data.UiSoundPresets
import com.sbro.emucorex.data.UiSoundRepository
import com.sbro.emucorex.data.UiSoundSetting
import com.sbro.emucorex.data.UiSoundSettings

/**
 * Frontend (non-emulation) sounds: looped background music, UI button clicks and the in-game menu
 * sounds. Everything is OFF by default and does nothing until [applySettings] enables a kind.
 *
 * Must be driven from the main thread (Compose callbacks, activity lifecycle, a main-dispatcher
 * settings collector). This is separate from the emulated console audio, which lives in the core.
 *
 * - Music: one looped [MediaPlayer] that only exists while music is enabled, the app is visible and
 *   no game is running. It holds audio focus while playing and is released otherwise.
 * - Click / menu sounds: short samples in a shared [SoundPool], loaded only while enabled.
 */
@MainThread
object UiSoundManager {
    private const val TAG = "UiSoundManager"
    private const val DUCK_FACTOR = 0.3f
    private const val SELECT_CANCEL_WINDOW_MS = 250L
    private const val INTERACTION_DEDUPE_MS = 60L
    private const val FOCUS_RETRY_DELAY_MS = 1_500L
    private const val MAX_FOCUS_RETRIES = 3
    private const val PREVIEW_RELEASE_DELAY_MS = 2_500L

    /** [rawIndex] picks the resource inside the selected preset (menu: open, close, select). */
    private enum class Sample(val kind: UiSoundKind, val rawIndex: Int) {
        CLICK(UiSoundKind.BUTTON_CLICK, 0),
        MENU_OPEN(UiSoundKind.GAME_MENU, 0),
        MENU_CLOSE(UiSoundKind.GAME_MENU, 1),
        MENU_SELECT(UiSoundKind.GAME_MENU, 2)
    }

    private val shortKinds = listOf(UiSoundKind.BUTTON_CLICK, UiSoundKind.GAME_MENU)

    private val mainHandler by lazy { Handler(Looper.getMainLooper()) }
    private var appContext: Context? = null
    private var repository: UiSoundRepository? = null

    private var settings = UiSoundSettings()
    private var appForeground = false
    private var emulationActive = false
    private var gameMenuOpen = false

    // Short samples.
    private var pool: SoundPool? = null
    private val sampleIds = HashMap<Sample, Int>()
    private val readySoundIds = HashSet<Int>()
    private val customSoundKinds = HashMap<Int, UiSoundKind>()
    private val loadedSignatures = HashMap<UiSoundKind, String>()
    private var lastSelectStreamId = 0
    private var lastSelectAtMs = 0L
    private var lastInteractionAtMs = 0L

    // Music.
    private var player: MediaPlayer? = null
    private var playerPrepared = false
    private var playerSignature: String? = null
    private var failedSignature: String? = null
    private var savedPositionMs = 0
    private var savedSignature: String? = null
    private var focusRequest: AudioFocusRequest? = null
    private var hasFocus = false
    private var transientFocusLoss = false
    private var ducked = false
    private var suppressedByFocusLoss = false
    private var lastMusicBlock: UiSoundPolicy.MusicBlock? = null
    private var focusRetries = 0
    private var previewPool: SoundPool? = null

    fun init(context: Context) {
        if (appContext != null) return
        val app = context.applicationContext
        appContext = app
        repository = UiSoundRepository(app)
    }

    fun applySettings(newSettings: UiSoundSettings) {
        if (appContext == null) return
        val old = settings
        settings = newSettings
        if (old.music.enabled != newSettings.music.enabled ||
            old.music.revision != newSettings.music.revision
        ) {
            failedSignature = null
            suppressedByFocusLoss = false
            focusRetries = 0
        }
        syncSamples()
        syncMusic()
    }

    /** Re-evaluates playback (e.g. from onResume) so a start that was blocked earlier is retried. */
    fun refresh() {
        focusRetries = 0
        syncMusic()
    }

    fun setAppForeground(foreground: Boolean) {
        if (appForeground == foreground) return
        appForeground = foreground
        if (foreground) {
            suppressedByFocusLoss = false
            focusRetries = 0
        }
        syncMusic()
    }

    fun setEmulationActive(active: Boolean) {
        if (emulationActive == active) return
        emulationActive = active
        if (!active) gameMenuOpen = false
        syncMusic()
    }

    /** Called when the in-game menu opens or closes; plays the matching menu sound. */
    fun setGameMenuOpen(open: Boolean) {
        if (gameMenuOpen == open) return
        gameMenuOpen = open
        // The press that toggled the menu may already have played a select tick; drop it in favour of
        // the dedicated open/close sound.
        if (lastSelectStreamId != 0 &&
            SystemClock.uptimeMillis() - lastSelectAtMs < SELECT_CANCEL_WINDOW_MS
        ) {
            pool?.stop(lastSelectStreamId)
        }
        lastSelectStreamId = 0
        play(if (open) Sample.MENU_OPEN else Sample.MENU_CLOSE)
    }

    /**
     * Central hook for a user-triggered UI press. Picks the right sound for the current context:
     * the in-game menu select sound while the game menu is open, nothing during gameplay, the
     * button click sound everywhere else in the frontend.
     */
    fun onUiInteraction() {
        // One press can reach this through nested handlers (e.g. a clickable wrapping a debounced action).
        val now = SystemClock.uptimeMillis()
        if (now - lastInteractionAtMs < INTERACTION_DEDUPE_MS) return
        lastInteractionAtMs = now
        when {
            gameMenuOpen -> {
                val streamId = play(Sample.MENU_SELECT)
                if (streamId != 0) {
                    lastSelectStreamId = streamId
                    lastSelectAtMs = SystemClock.uptimeMillis()
                }
            }
            emulationActive -> Unit
            else -> play(Sample.CLICK)
        }
    }

    /** Plays the sound of [kind] once at [volumePercent] (settings preview). Music is not previewed. */
    fun preview(kind: UiSoundKind, volumePercent: Int) {
        val sample = when (kind) {
            UiSoundKind.BUTTON_CLICK -> Sample.CLICK
            UiSoundKind.GAME_MENU -> Sample.MENU_OPEN
            UiSoundKind.BACKGROUND_MUSIC -> return
        }
        play(sample, volumePercent)
    }

    /**
     * Plays built-in preset [presetId] of [kind] once, independent of whether that kind is enabled, so a
     * preset can be auditioned from the picker. Music is not previewed (it plays live when enabled).
     */
    fun previewPreset(kind: UiSoundKind, presetId: String, volumePercent: Int) {
        val context = appContext ?: return
        if (kind == UiSoundKind.BACKGROUND_MUSIC || volumePercent <= 0) return
        val raw = UiSoundPresets.find(kind, presetId).raws[0]
        previewPool?.release()
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        val volume = volumePercent.coerceAtMost(UiSoundSettings.MAX_VOLUME) / 100f
        val transient = SoundPool.Builder().setMaxStreams(1).setAudioAttributes(attributes).build()
        previewPool = transient
        transient.setOnLoadCompleteListener { loaded, soundId, status ->
            if (status == 0) loaded.play(soundId, volume, volume, 1, 0, 1f)
            mainHandler.postDelayed({
                if (previewPool === loaded) previewPool = null
                loaded.release()
            }, PREVIEW_RELEASE_DELAY_MS)
        }
        if (transient.load(context, raw, 1) == 0) {
            previewPool = null
            transient.release()
        }
    }

    // region Short samples

    private fun syncSamples() {
        val context = appContext ?: return
        if (shortKinds.none { settings[it].enabled }) {
            releasePool()
            return
        }
        val soundPool = pool ?: createPool().also { pool = it }
        shortKinds.forEach { kind ->
            val setting = settings[kind]
            if (!setting.enabled) {
                unloadKind(soundPool, kind)
            } else if (loadedSignatures[kind] != signatureOf(setting)) {
                loadKind(context, soundPool, kind, setting)
            }
        }
    }

    private fun signatureOf(setting: UiSoundSetting) =
        if (setting.usesCustom) "custom:${setting.revision}" else "preset:${setting.preset}"

    private fun createPool(): SoundPool {
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        return SoundPool.Builder()
            .setMaxStreams(4)
            .setAudioAttributes(attributes)
            .build()
            .also { soundPool ->
                soundPool.setOnLoadCompleteListener { _, soundId, status ->
                    mainHandler.post { onSampleLoaded(soundId, status) }
                }
            }
    }

    private fun onSampleLoaded(soundId: Int, status: Int) {
        if (pool == null || soundId !in sampleIds.values) return
        if (status == 0) {
            readySoundIds += soundId
            return
        }
        Log.w(TAG, "Sound sample failed to load (status=$status)")
        val failedKind = customSoundKinds[soundId]
        val context = appContext
        val soundPool = pool
        if (failedKind != null && context != null && soundPool != null) {
            // Corrupt or unreadable custom file: fall back to the built-in default.
            loadKind(context, soundPool, failedKind, settings[failedKind], forceDefault = true)
        }
    }

    private fun loadKind(
        context: Context,
        soundPool: SoundPool,
        kind: UiSoundKind,
        setting: UiSoundSetting,
        forceDefault: Boolean = false
    ) {
        unloadKind(soundPool, kind)
        val samples = Sample.entries.filter { it.kind == kind }
        val customFile = if (setting.usesCustom && !forceDefault) repository?.installedFile(kind) else null
        val customId = customFile?.let { soundPool.load(it.absolutePath, 1) } ?: 0
        if (customId != 0) {
            customSoundKinds[customId] = kind
            samples.forEach { sampleIds[it] = customId }
        } else {
            // No custom file, or SoundPool rejected it synchronously: use the selected preset.
            val preset = UiSoundPresets.find(kind, setting.preset)
            samples.forEach { sampleIds[it] = soundPool.load(context, preset.raws[it.rawIndex], 1) }
        }
        // Recorded even when the default was substituted so a missing/broken file is not retried in a loop.
        loadedSignatures[kind] = signatureOf(setting)
    }

    private fun unloadKind(soundPool: SoundPool, kind: UiSoundKind) {
        val ids = Sample.entries.filter { it.kind == kind }.mapNotNull { sampleIds.remove(it) }.distinct()
        ids.forEach { id ->
            soundPool.unload(id)
            readySoundIds.remove(id)
            customSoundKinds.remove(id)
        }
        loadedSignatures.remove(kind)
    }

    private fun releasePool() {
        pool?.release()
        pool = null
        sampleIds.clear()
        readySoundIds.clear()
        customSoundKinds.clear()
        loadedSignatures.clear()
        lastSelectStreamId = 0
    }

    /** Returns the SoundPool stream id, or 0 when nothing was played. */
    private fun play(sample: Sample, volumePercentOverride: Int? = null): Int {
        val setting = settings[sample.kind]
        if (!setting.enabled) return 0
        val volumePercent = volumePercentOverride ?: setting.volume
        if (volumePercent <= 0) return 0
        val soundPool = pool ?: return 0
        val soundId = sampleIds[sample] ?: return 0
        if (soundId !in readySoundIds) return 0
        val volume = volumePercent.coerceAtMost(UiSoundSettings.MAX_VOLUME) / 100f
        return soundPool.play(soundId, volume, volume, 1, 0, 1f)
    }

    // endregion

    // region Music

    private fun musicBlock(): UiSoundPolicy.MusicBlock {
        val music = settings.music
        return UiSoundPolicy.musicBlock(
            enabled = music.enabled,
            volumePercent = music.volume,
            appForeground = appForeground,
            emulationActive = emulationActive,
            suppressedByFocusLoss = suppressedByFocusLoss
        )
    }

    private fun shouldPlayMusic() = musicBlock() == UiSoundPolicy.MusicBlock.NONE

    private fun syncMusic() {
        val context = appContext ?: return
        val block = musicBlock()
        if (block != lastMusicBlock) {
            lastMusicBlock = block
            Log.i(TAG, "Music state: $block (preset=${settings.music.preset}, custom=${settings.music.custom})")
        }
        if (block != UiSoundPolicy.MusicBlock.NONE) {
            mainHandler.removeCallbacks(focusRetry)
            stopMusic()
            return
        }
        val signature = signatureOf(settings.music)
        val current = player
        if (current != null && playerSignature == signature) {
            applyMusicVolume(current)
            if (UiSoundPolicy.shouldStartPlayer(playerPrepared, current.isPlaying, hasFocus, transientFocusLoss)) {
                runCatching { current.start() }
            }
            return
        }
        if (failedSignature == signature) return
        stopMusic()
        startMusic(context, signature)
    }

    private val focusRetry = Runnable { syncMusic() }

    private fun scheduleFocusRetry() {
        mainHandler.removeCallbacks(focusRetry)
        if (focusRetries >= MAX_FOCUS_RETRIES) {
            Log.w(TAG, "Audio focus still refused; music stays off until the next resume or settings change")
            return
        }
        focusRetries++
        mainHandler.postDelayed(focusRetry, FOCUS_RETRY_DELAY_MS)
    }

    private fun startMusic(context: Context, signature: String) {
        // Focus is requested first: a refusal must not leave a half-initialised player behind.
        ducked = false
        transientFocusLoss = false
        if (requestFocus(context) == UiSoundPolicy.FocusOutcome.DENIED) {
            Log.w(TAG, "Audio focus refused, not starting music (retry $focusRetries/$MAX_FOCUS_RETRIES)")
            abandonFocus(context)
            scheduleFocusRetry()
            return
        }
        focusRetries = 0
        val custom = if (settings.music.usesCustom) {
            repository?.installedFile(UiSoundKind.BACKGROUND_MUSIC)
        } else {
            null
        }
        val mediaPlayer = MediaPlayer()
        try {
            mediaPlayer.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            if (custom != null) {
                mediaPlayer.setDataSource(custom.absolutePath)
            } else {
                val presetRes = UiSoundPresets.find(UiSoundKind.BACKGROUND_MUSIC, settings.music.preset).raws[0]
                context.resources.openRawResourceFd(presetRes).use { afd ->
                    mediaPlayer.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
                }
            }
            mediaPlayer.isLooping = true
            mediaPlayer.setOnPreparedListener { prepared ->
                if (player !== prepared) return@setOnPreparedListener
                playerPrepared = true
                if (savedSignature == signature && savedPositionMs > 0) {
                    runCatching { prepared.seekTo(savedPositionMs) }
                }
                savedPositionMs = 0
                savedSignature = null
                applyMusicVolume(prepared)
                if (shouldPlayMusic() &&
                    UiSoundPolicy.shouldStartPlayer(true, prepared.isPlaying, hasFocus, transientFocusLoss)
                ) {
                    runCatching { prepared.start() }
                }
            }
            mediaPlayer.setOnErrorListener { failed, what, extra ->
                Log.w(TAG, "Background music error what=$what extra=$extra")
                if (player === failed) {
                    failedSignature = signature
                    stopMusic()
                } else {
                    runCatching { failed.release() }
                }
                true
            }
            player = mediaPlayer
            playerPrepared = false
            playerSignature = signature
            Log.i(TAG, "Starting background music (custom=${custom != null}, focus=$hasFocus)")
            mediaPlayer.prepareAsync()
        } catch (error: Exception) {
            Log.w(TAG, "Unable to start background music", error)
            failedSignature = signature
            runCatching { mediaPlayer.release() }
            if (player === mediaPlayer) {
                player = null
                playerSignature = null
            }
            abandonFocus(context)
        }
    }

    private fun applyMusicVolume(mediaPlayer: MediaPlayer) {
        val volume = (settings.music.volume / 100f) * (if (ducked) DUCK_FACTOR else 1f)
        runCatching { mediaPlayer.setVolume(volume, volume) }
    }

    private fun stopMusic() {
        val current = player
        if (current != null) {
            if (playerPrepared) {
                savedPositionMs = runCatching { current.currentPosition }.getOrDefault(0)
                savedSignature = playerSignature
            }
            current.setOnPreparedListener(null)
            current.setOnErrorListener(null)
            runCatching { current.stop() }
            runCatching { current.release() }
        }
        player = null
        playerPrepared = false
        playerSignature = null
        ducked = false
        transientFocusLoss = false
        appContext?.let(::abandonFocus)
    }

    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_GAIN -> {
                hasFocus = true
                transientFocusLoss = false
                ducked = false
                syncMusic()
                player?.let(::applyMusicVolume)
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                transientFocusLoss = true
                player?.let { runCatching { if (it.isPlaying) it.pause() } }
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                ducked = true
                player?.let(::applyMusicVolume)
            }
            AudioManager.AUDIOFOCUS_LOSS -> {
                // Another app took over for good: stay quiet until the app is reopened or settings change.
                suppressedByFocusLoss = true
                stopMusic()
            }
        }
    }

    /**
     * Requests audio focus and maps the result through [UiSoundPolicy.focusOutcome]: GRANTED plays, DELAYED
     * waits for the AUDIOFOCUS_GAIN callback (the request accepts delayed gain), FAILED does not play.
     */
    private fun requestFocus(context: Context): UiSoundPolicy.FocusOutcome {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        if (audioManager == null) {
            hasFocus = true
            return UiSoundPolicy.FocusOutcome.PLAY
        }
        val request = focusRequest ?: AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAcceptsDelayedFocusGain(true)
            .setOnAudioFocusChangeListener(focusListener, mainHandler)
            .build()
            .also { focusRequest = it }
        val result = audioManager.requestAudioFocus(request)
        val outcome = UiSoundPolicy.focusOutcome(
            granted = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED,
            delayed = result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED
        )
        Log.i(TAG, "Audio focus request result=$result outcome=$outcome audioMode=${audioManager.mode}")
        hasFocus = outcome == UiSoundPolicy.FocusOutcome.PLAY
        return outcome
    }

    private fun abandonFocus(context: Context) {
        val request = focusRequest ?: return
        focusRequest = null
        hasFocus = false
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        audioManager.abandonAudioFocusRequest(request)
    }

    // endregion
}

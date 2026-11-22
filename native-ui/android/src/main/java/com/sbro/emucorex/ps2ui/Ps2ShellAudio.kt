package com.sbro.emucorex.ps2ui

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.SoundPool
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import kotlin.concurrent.thread
import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.tanh

/**
 * Procedural 44.1 kHz PCM sound synthesizer and PS2 cosmic-ocean ambient pad
 * generator for the PS2 BIOS shell. All waveforms are synthesized cleanly in
 * code with zero bundled external/copyrighted audio assets.
 */
object Ps2ShellAudio {
    const val SFX_MOVE = 0
    const val SFX_CONFIRM = 1
    const val SFX_CANCEL = 2
    const val SFX_ERROR = 3
    const val SFX_BOOT_INTRO = 4
    const val SFX_DISC_LAUNCH = 5

    private const val SAMPLE_RATE = 44100
    private val lock = Any()

    @Volatile
    var keyToneEnabled: Boolean = true

    @Volatile
    var ambientEnabled: Boolean = true
        set(value) {
            field = value
            syncAmbientPlayback()
        }

    @Volatile
    var hostVisible: Boolean = true
        set(value) {
            field = value
            syncAmbientPlayback()
        }

    private var initialized = false
    private var pool: SoundPool? = null
    private val soundIds = IntArray(6)
    private val readyIds = HashSet<Int>()

    private var ambientThread: Thread? = null

    @Volatile
    private var ambientStopRequested = false

    fun initialize(context: Context) {
        synchronized(lock) {
            if (initialized) return
            val sfxDir = File(context.cacheDir, "ps2ui_sfx_v1").apply { mkdirs() }
            val attrs = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()
            val sp = SoundPool.Builder()
                .setMaxStreams(6)
                .setAudioAttributes(attrs)
                .build()
            sp.setOnLoadCompleteListener { _, sampleId, status ->
                if (status == 0) {
                    synchronized(lock) {
                        readyIds.add(sampleId)
                    }
                }
            }
            pool = sp

            val generators: List<Pair<Int, () -> ShortArray>> = listOf(
                SFX_MOVE to ::synthMoveSfx,
                SFX_CONFIRM to ::synthConfirmSfx,
                SFX_CANCEL to ::synthCancelSfx,
                SFX_ERROR to ::synthErrorSfx,
                SFX_BOOT_INTRO to ::synthBootIntroSfx,
                SFX_DISC_LAUNCH to ::synthDiscLaunchSfx,
            )

            for ((index, gen) in generators) {
                val wavFile = File(sfxDir, "ps2_sfx_$index.wav")
                if (!wavFile.isFile || wavFile.length() < 128L) {
                    runCatching { writeWav(wavFile, gen()) }
                }
                if (wavFile.isFile) {
                    soundIds[index] = sp.load(wavFile.absolutePath, 1)
                }
            }
            initialized = true
        }
        syncAmbientPlayback()
    }

    @JvmStatic
    fun playSfx(kind: Int) {
        if (!keyToneEnabled) return
        if (kind !in soundIds.indices) return
        val sp = pool ?: return
        val id = soundIds[kind]
        if (id == 0) return
        val isReady = synchronized(lock) { readyIds.contains(id) }
        if (!isReady) return
        val vol = when (kind) {
            SFX_MOVE -> 0.40f
            SFX_CONFIRM -> 0.52f
            SFX_CANCEL -> 0.46f
            SFX_ERROR -> 0.44f
            SFX_BOOT_INTRO -> 0.62f
            SFX_DISC_LAUNCH -> 0.56f
            else -> 0.45f
        }
        sp.play(id, vol, vol, 1, 0, 1.0f)
    }

    fun pauseAudio() {
        hostVisible = false
    }

    fun resumeAudio() {
        hostVisible = true
    }

    fun shutdown() {
        stopAmbientThread()
        synchronized(lock) {
            pool?.release()
            pool = null
            readyIds.clear()
            soundIds.fill(0)
            initialized = false
        }
    }

    private fun syncAmbientPlayback() {
        val shouldPlay = initialized && ambientEnabled && hostVisible
        if (shouldPlay) {
            startAmbientThread()
        } else {
            stopAmbientThread()
        }
    }

    private fun startAmbientThread() {
        synchronized(lock) {
            if (ambientThread?.isAlive == true) return
            ambientStopRequested = false
            ambientThread = thread(start = true, isDaemon = true, name = "Ps2CosmicAmbient") {
                runAmbientLoop()
            }
        }
    }

    private fun stopAmbientThread() {
        ambientStopRequested = true
        val t = synchronized(lock) { ambientThread }
        if (t != null && t !== Thread.currentThread()) {
            runCatching { t.join(250) }
        }
        synchronized(lock) {
            if (ambientThread === t) {
                ambientThread = null
            }
        }
    }

    private fun runAmbientLoop() {
        val minBuf = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        ).coerceAtLeast(4096)
        val track = runCatching {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .build()
                )
                .setBufferSizeInBytes(minBuf * 2)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        }.getOrNull() ?: return

        val chunk = ShortArray(2048)
        var sampleIdx = 0L
        // Ethereal PS2 cosmic-ocean drone voicing (deep D2/A2/E3/F#3/A3 airy shimmer)
        val freqs = doubleArrayOf(73.42, 110.0, 146.83, 164.81, 220.0, 293.66)
        val amps = doubleArrayOf(0.30, 0.24, 0.18, 0.14, 0.11, 0.07)
        var fadeGain = 0.0

        try {
            track.play()
            while (!ambientStopRequested) {
                for (i in chunk.indices) {
                    val t = sampleIdx.toDouble() / SAMPLE_RATE.toDouble()
                    val tideA = 0.68 + 0.32 * sin(2.0 * PI * 0.065 * t)
                    val tideB = 0.72 + 0.28 * sin(2.0 * PI * 0.043 * t + 1.8)
                    var sum = 0.0
                    for (fIdx in freqs.indices) {
                        val drift = 1.0 + 0.0018 * sin(2.0 * PI * (0.032 + fIdx * 0.013) * t + fIdx)
                        val mod = if (fIdx % 2 == 0) tideA else tideB
                        sum += amps[fIdx] * mod * sin(2.0 * PI * freqs[fIdx] * drift * t)
                    }
                    fadeGain = (fadeGain + 1.0 / (SAMPLE_RATE * 1.4)).coerceAtMost(1.0)
                    val s = tanh(sum * 0.19 * fadeGain)
                    chunk[i] = (s * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
                    sampleIdx++
                }
                track.write(chunk, 0, chunk.size)
            }
        } catch (_: Throwable) {
        } finally {
            runCatching {
                track.stop()
                track.release()
            }
        }
    }

    // Soft glassy water-drop cursor glide characteristic of PS2 Browser navigation
    private fun synthMoveSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.085).toInt()
        return ShortArray(len) { i ->
            val t = i.toDouble() / SAMPLE_RATE
            val env = (1.0 - exp(-t * 420.0)) * exp(-t * 46.0)
            val freq = 980.0 + 380.0 * (1.0 - exp(-t * 65.0))
            val tone = 0.70 * sin(2.0 * PI * freq * t) +
                0.24 * sin(2.0 * PI * (freq * 1.995) * t) +
                0.08 * sin(2.0 * PI * (freq * 3.0) * t)
            (tone * env * 0.48 * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
    }

    // Translucent ascending crystalline PS2 confirm chime
    private fun synthConfirmSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.30).toInt()
        return ShortArray(len) { i ->
            val t = i.toDouble() / SAMPLE_RATE
            val env1 = (1.0 - exp(-t * 380.0)) * exp(-t * 14.0)
            val s1 = 0.62 * sin(2.0 * PI * 783.99 * t) + 0.32 * sin(2.0 * PI * 1174.66 * t)
            val t2 = (t - 0.055).coerceAtLeast(0.0)
            val env2 = if (t >= 0.055) (1.0 - exp(-t2 * 320.0)) * exp(-t2 * 10.5) else 0.0
            val s2 = 0.60 * sin(2.0 * PI * 1174.66 * t) +
                0.35 * sin(2.0 * PI * 1567.98 * t) +
                0.15 * sin(2.0 * PI * 2349.32 * t)
            val mixed = tanh((s1 * env1 + s2 * env2) * 0.46)
            (mixed * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
    }

    // Soft descending glass chime for Back / Cancel
    private fun synthCancelSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.21).toInt()
        return ShortArray(len) { i ->
            val t = i.toDouble() / SAMPLE_RATE
            val env = (1.0 - exp(-t * 340.0)) * exp(-t * 17.5)
            val freq = 880.0 * exp(-t * 1.9)
            val tone = 0.72 * sin(2.0 * PI * freq * t) + 0.28 * sin(2.0 * PI * (freq * 1.498) * t)
            (tone * env * 0.50 * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
    }

    // Gentle muted low double thud
    private fun synthErrorSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.20).toInt()
        return ShortArray(len) { i ->
            val t = i.toDouble() / SAMPLE_RATE
            val p1 = (1.0 - exp(-t * 500.0)) * exp(-t * 36.0)
            val t2 = (t - 0.08).coerceAtLeast(0.0)
            val p2 = if (t >= 0.08) (1.0 - exp(-t2 * 500.0)) * exp(-t2 * 30.0) else 0.0
            val tone = sin(2.0 * PI * 175.0 * t) + 0.35 * sin(2.0 * PI * 262.0 * t)
            (tanh(tone * (p1 + p2) * 0.42) * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
    }

    // Iconic PS2 2-act misty ocean swell -> crystalline resonant bloom
    private fun synthBootIntroSfx(): ShortArray {
        val dur = 2.85
        val len = (SAMPLE_RATE * dur).toInt()
        val swellChord = doubleArrayOf(65.41, 98.0, 146.83, 196.0, 293.66)
        val swellWeights = doubleArrayOf(0.48, 0.36, 0.28, 0.20, 0.14)
        val resolveChord = doubleArrayOf(110.0, 164.81, 220.0, 329.63, 440.0, 587.33, 659.25)
        val resolveWeights = doubleArrayOf(0.30, 0.24, 0.22, 0.18, 0.14, 0.11, 0.08)

        return ShortArray(len) { i ->
            val t = i.toDouble() / SAMPLE_RATE
            val env1 = (1.0 - exp(-t * 2.4)) * exp(-((t - 0.85).coerceAtLeast(0.0)) * 1.25)
            var s1 = 0.0
            for (k in swellChord.indices) {
                val detune = 1.0 + 0.0025 * sin(2.0 * PI * 0.42 * t + k * 0.7)
                s1 += swellWeights[k] * sin(2.0 * PI * swellChord[k] * detune * t)
            }

            // Glassy whoosh pulse around tower flyover peak
            val whooshT = t - 0.55
            val whooshEnv = exp(-(whooshT * whooshT) * 5.2)
            val whooshTone = sin(2.0 * PI * (180.0 + 240.0 * t.coerceAtMost(1.4)) * t) * whooshEnv * 0.18

            val t2 = (t - 1.35).coerceAtLeast(0.0)
            val env2 = if (t > 1.35) (1.0 - exp(-t2 * 9.5)) * exp(-t2 * 1.45) else 0.0
            var s2 = 0.0
            if (env2 > 0.0005) {
                for (k in resolveChord.indices) {
                    val detune = 1.0 + 0.0015 * sin(2.0 * PI * 0.5 * t2 + k)
                    s2 += resolveWeights[k] * sin(2.0 * PI * resolveChord[k] * detune * t2)
                }
            }
            val shimmer = if (t > 1.42) {
                val ts = t - 1.42
                (0.18 * sin(2.0 * PI * 880.0 * ts) + 0.12 * sin(2.0 * PI * 1318.51 * ts)) *
                    (1.0 - exp(-ts * 24.0)) * exp(-ts * 2.2)
            } else {
                0.0
            }
            val mixed = tanh((s1 * env1 + whooshTone + s2 * env2 + shimmer) * 0.52)
            (mixed * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
    }

    // Luminous PS2 disc launch crystalline whoosh & harmonic resolve
    private fun synthDiscLaunchSfx(): ShortArray {
        val dur = 1.05
        val len = (SAMPLE_RATE * dur).toInt()
        return ShortArray(len) { i ->
            val t = i.toDouble() / SAMPLE_RATE
            val env = (1.0 - exp(-t * 7.5)) * exp(-((t - 0.22).coerceAtLeast(0.0)) * 3.0)
            val sweepFreq = 196.0 + 520.0 * (1.0 - exp(-t * 3.8))
            val tone = 0.48 * sin(2.0 * PI * sweepFreq * t) +
                0.32 * sin(2.0 * PI * 587.33 * t) +
                0.24 * sin(2.0 * PI * 880.0 * t) +
                0.14 * sin(2.0 * PI * 1174.66 * t)
            val mixed = tanh(tone * env * 0.55)
            (mixed * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
    }

    private fun writeWav(file: File, pcm: ShortArray) {
        val dataBytes = pcm.size * 2
        val header = ByteArrayOutputStream(44)
        fun writeAscii(s: String) = s.forEach { header.write(it.code) }
        fun writeIntLE(v: Int) {
            header.write(v and 0xFF)
            header.write((v ushr 8) and 0xFF)
            header.write((v ushr 16) and 0xFF)
            header.write((v ushr 24) and 0xFF)
        }
        fun writeShortLE(v: Int) {
            header.write(v and 0xFF)
            header.write((v ushr 8) and 0xFF)
        }
        writeAscii("RIFF")
        writeIntLE(36 + dataBytes)
        writeAscii("WAVE")
        writeAscii("fmt ")
        writeIntLE(16)
        writeShortLE(1)
        writeShortLE(1)
        writeIntLE(SAMPLE_RATE)
        writeIntLE(SAMPLE_RATE * 2)
        writeShortLE(2)
        writeShortLE(16)
        writeAscii("data")
        writeIntLE(dataBytes)

        val pcmBytes = ByteArray(dataBytes)
        var p = 0
        for (sample in pcm) {
            val s = sample.toInt()
            pcmBytes[p] = (s and 0xFF).toByte()
            pcmBytes[p + 1] = ((s ushr 8) and 0xFF).toByte()
            p += 2
        }
        FileOutputStream(file).use { out ->
            out.write(header.toByteArray())
            out.write(pcmBytes)
        }
    }
}

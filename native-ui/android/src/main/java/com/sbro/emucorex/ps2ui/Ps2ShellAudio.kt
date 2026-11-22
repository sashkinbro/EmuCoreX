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
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.tanh

/**
 * Clean-room procedural 44.1 kHz PCM sound synthesizer and PS2 subterranean ocean
 * ambient drone generator for the PlayStation 2 BIOS UI.
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
    // Recently started SFX streams, so they can be faded out and stopped when the shell is left.
    private val activeStreams = ArrayDeque<Int>()

    private var ambientThread: Thread? = null

    @Volatile
    private var ambientStopRequested = false

    fun initialize(context: Context) {
        synchronized(lock) {
            if (initialized) return
            val sfxDir = File(context.cacheDir, "ps2ui_sfx_v3").apply { mkdirs() }
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
            SFX_MOVE -> 0.42f
            SFX_CONFIRM -> 0.56f
            SFX_CANCEL -> 0.50f
            SFX_ERROR -> 0.46f
            SFX_BOOT_INTRO -> 0.68f
            SFX_DISC_LAUNCH -> 0.64f
            else -> 0.48f
        }
        val streamId = sp.play(id, vol, vol, 1, 0, 1.0f)
        if (streamId != 0) {
            synchronized(lock) {
                activeStreams.addLast(streamId)
                while (activeStreams.size > 8) activeStreams.removeFirst()
            }
        }
    }

    /** Fades every still-playing SFX out over ~200 ms and stops it (off the caller's thread). */
    private fun fadeOutSfx() {
        val sp = pool ?: return
        val streams = synchronized(lock) { activeStreams.toList().also { activeStreams.clear() } }
        if (streams.isEmpty()) return
        thread(start = true, isDaemon = true, name = "Ps2SfxFade") {
            val steps = 8
            for (step in steps - 1 downTo 0) {
                val v = 0.6f * step / steps
                streams.forEach { runCatching { sp.setVolume(it, v, v) } }
                runCatching { Thread.sleep(25) }
            }
            streams.forEach { runCatching { sp.stop(it) } }
        }
    }

    fun pauseAudio() {
        hostVisible = false
        fadeOutSfx()
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
            if (ambientThread?.isAlive == true) {
                // Still fading out: cancel the stop and keep playing.
                ambientStopRequested = false
                return
            }
            ambientStopRequested = false
            ambientThread = thread(start = true, isDaemon = true, name = "Ps2CosmicAmbient") {
                runAmbientLoop()
            }
        }
    }

    private fun stopAmbientThread() {
        // The audio thread fades itself out and drops its buffer; no blocking on the caller.
        ambientStopRequested = true
    }

    // Authentic PS2 deep subterranean oceanic wind + eerie abyssal drone (no musical triad loop!)
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
        var rng = 0x50533241u
        var brown = 0.0
        var lp1 = 0.0
        var lp2 = 0.0
        var fadeGain = 0.0
        var outGain = 1.0
        val outStep = 1.0 / (SAMPLE_RATE * 0.12)
        var lfoPhase = 0.0
        var tidePhase = 0.8
        var phase1 = 0.0
        var phase2 = 0.0
        val twoPi = 2.0 * PI
        val lfoInc = twoPi * 0.0625 / SAMPLE_RATE.toDouble()
        val tideInc = twoPi * 0.055 / SAMPLE_RATE.toDouble()
        val p1Inc = twoPi * 65.41 / SAMPLE_RATE.toDouble()
        val p2Inc = twoPi * 98.00 / SAMPLE_RATE.toDouble()

        try {
            track.play()
            while (true) {
                val stopping = ambientStopRequested
                if (!stopping) outGain = 1.0
                for (i in chunk.indices) {
                    // Calm, resonant-free 2-pole filtered brown air/surf cushion
                    rng = rng * 1664525u + 1013904223u
                    val white = ((rng shr 8).toInt() / 8388608.0) - 1.0
                    brown = (brown * 0.994 + white * 0.042).coerceIn(-1.0, 1.0)
                    val lfoCutoff = 0.010 + 0.014 * (0.5 + 0.5 * sin(lfoPhase))
                    lp1 += (brown - lp1) * lfoCutoff
                    lp2 += (lp1 - lp2) * 0.022

                    // Steady phase-accumulated deep warm PS2 pad (zero vibrato / zero pitch drift)
                    val tide = 0.68 + 0.32 * sin(tidePhase)
                    val pad = 0.055 * sin(phase1) + 0.025 * sin(phase2)

                    fadeGain = (fadeGain + 1.0 / (SAMPLE_RATE * 1.6)).coerceAtMost(1.0)
                    if (stopping) outGain = (outGain - outStep).coerceAtLeast(0.0)
                    val mix = (lp2 * 1.45 * tide + pad * tide) * 0.11 * fadeGain * outGain
                    val s = tanh(mix)
                    chunk[i] = (s * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()

                    lfoPhase += lfoInc
                    if (lfoPhase >= twoPi) lfoPhase -= twoPi
                    tidePhase += tideInc
                    if (tidePhase >= twoPi) tidePhase -= twoPi
                    phase1 += p1Inc
                    if (phase1 >= twoPi) phase1 -= twoPi
                    phase2 += p2Inc
                    if (phase2 >= twoPi) phase2 -= twoPi
                }
                track.write(chunk, 0, chunk.size)
                if (stopping && outGain <= 0.0) break
            }
        } catch (_: Throwable) {
        } finally {
            runCatching {
                // Drop whatever is still buffered so nothing keeps playing after the fade.
                track.pause()
                track.flush()
                track.stop()
                track.release()
            }
            synchronized(lock) {
                if (ambientThread === Thread.currentThread()) ambientThread = null
            }
            // Shell came back while we were fading out: start a fresh ambient stream.
            if (!ambientStopRequested) syncAmbientPlayback()
        }
    }

    // Multi-tap dark cavern hall reverb used to give PS2 chimes & boot their signature depth
    private fun applyCavernReverb(dry: DoubleArray, wetMix: Double = 0.36): ShortArray {
        val len = dry.size
        val wet = DoubleArray(len)
        val delays = intArrayOf(1323, 2117, 3191, 4793, 6827, 9419)
        val gains = doubleArrayOf(0.46, 0.38, 0.30, 0.23, 0.17, 0.12)
        for (i in 0 until len) {
            var acc = 0.0
            for (d in delays.indices) {
                val prev = i - delays[d]
                if (prev >= 0) {
                    acc += dry[prev] * gains[d]
                }
            }
            wet[i] = acc
        }
        // Darken the reverb tail slightly with 1-pole low-pass
        var lp = 0.0
        val out = ShortArray(len)
        for (i in 0 until len) {
            lp += (wet[i] - lp) * 0.24
            val mixed = tanh(dry[i] * (1.0 - wetMix * 0.35) + lp * wetMix)
            out[i] = (mixed * 32767.0).roundToInt().coerceIn(-32767, 32767).toShort()
        }
        return out
    }

    // Soft, silky, low-mid watery-glass cursor tick characteristic of PS2 OSDSYS (~58ms)
    private fun synthMoveSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.058).toInt()
        val dry = DoubleArray(len)
        for (i in 0 until len) {
            val t = i.toDouble() / SAMPLE_RATE
            val env = (1.0 - exp(-t * 620.0)) * exp(-t * 68.0)
            val freq = 310.0 + 185.0 * (1.0 - exp(-t * 95.0))
            val body = 0.72 * sin(2.0 * PI * freq * t) +
                0.20 * sin(2.0 * PI * (freq * 1.98) * t) +
                0.14 * sin(2.0 * PI * 165.0 * t) * exp(-t * 110.0)
            dry[i] = body * env * 0.56
        }
        return applyCavernReverb(dry, 0.18)
    }

    // Authentic PS2 liquid-crystal FM glassy confirm bell + airy shimmer (~420ms)
    private fun synthConfirmSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.42).toInt()
        val dry = DoubleArray(len)
        for (i in 0 until len) {
            val t = i.toDouble() / SAMPLE_RATE
            val env1 = (1.0 - exp(-t * 280.0)) * exp(-t * 11.5)
            // Soft FM glass bell modulation that settles rapidly
            val mod = 0.65 * exp(-t * 26.0) * sin(2.0 * PI * 587.33 * t)
            val chimeA = sin(2.0 * PI * 293.66 * t + mod * 0.8) * 0.38 +
                sin(2.0 * PI * 440.0 * t + mod) * 0.42 +
                sin(2.0 * PI * 659.25 * t) * 0.24
            val t2 = (t - 0.045).coerceAtLeast(0.0)
            val env2 = if (t >= 0.045) (1.0 - exp(-t2 * 220.0)) * exp(-t2 * 8.5) else 0.0
            val chimeB = sin(2.0 * PI * 880.0 * t) * 0.22 + sin(2.0 * PI * 1318.51 * t) * 0.10
            dry[i] = (chimeA * env1 + chimeB * env2) * 0.55
        }
        return applyCavernReverb(dry, 0.44)
    }

    // Smooth downward liquid-glass sweep for Back / Cancel (~300ms)
    private fun synthCancelSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.30).toInt()
        val dry = DoubleArray(len)
        for (i in 0 until len) {
            val t = i.toDouble() / SAMPLE_RATE
            val env = (1.0 - exp(-t * 240.0)) * exp(-t * 14.5)
            val freq = 480.0 * exp(-t * 2.8)
            val tone = 0.68 * sin(2.0 * PI * freq * t) +
                0.28 * sin(2.0 * PI * (freq * 1.495) * t) +
                0.10 * sin(2.0 * PI * (freq * 0.5) * t)
            dry[i] = tone * env * 0.52
        }
        return applyCavernReverb(dry, 0.38)
    }

    // Muted low hollow knock for Invalid / Unavailable (~180ms)
    private fun synthErrorSfx(): ShortArray {
        val len = (SAMPLE_RATE * 0.18).toInt()
        val dry = DoubleArray(len)
        for (i in 0 until len) {
            val t = i.toDouble() / SAMPLE_RATE
            val p1 = (1.0 - exp(-t * 480.0)) * exp(-t * 36.0)
            val t2 = (t - 0.07).coerceAtLeast(0.0)
            val p2 = if (t >= 0.07) (1.0 - exp(-t2 * 480.0)) * exp(-t2 * 32.0) else 0.0
            val tone = 0.72 * sin(2.0 * PI * 128.0 * t) + 0.28 * sin(2.0 * PI * 192.0 * t)
            dry[i] = tone * (p1 + p2 * 0.85) * 0.52
        }
        return applyCavernReverb(dry, 0.20)
    }

    // Authentic PS2 Boot: Cavernous tower wind flyover + sub-bass swell -> glassy crystal cloud (~3.4s)
    private fun synthBootIntroSfx(): ShortArray {
        val dur = 3.40
        val len = (SAMPLE_RATE * dur).toInt()
        val dry = DoubleArray(len)
        var rng = 0x42505332u
        var windLp = 0.0

        val subNotes = doubleArrayOf(55.0, 82.41, 110.0, 164.81)
        val subW = doubleArrayOf(0.55, 0.42, 0.32, 0.20)
        val crystalNotes = doubleArrayOf(220.0, 329.63, 493.88, 659.25, 880.0, 1318.51)
        val crystalW = doubleArrayOf(0.26, 0.24, 0.20, 0.16, 0.11, 0.07)

        for (i in 0 until len) {
            val t = i.toDouble() / SAMPLE_RATE

            // Filtered wind/fog sweep swelling cleanly through towers
            rng = rng * 1664525u + 1013904223u
            val white = ((rng shr 8).toInt() / 8388608.0) - 1.0
            val cutoff = 0.008 + 0.055 * exp(-((t - 0.78) * (t - 0.78)) * 3.2)
            windLp += (white - windLp) * cutoff
            val windEnv = (1.0 - exp(-t * 3.0)) * exp(-t * 0.85)

            // Deep cavernous tower chord
            val subEnv = (1.0 - exp(-t * 2.1)) * exp(-((t - 0.90).coerceAtLeast(0.0)) * 1.15)
            var subSum = 0.0
            for (k in subNotes.indices) {
                val det = 1.0 + 0.0028 * sin(2.0 * PI * 0.35 * t + k * 0.9)
                subSum += subW[k] * sin(2.0 * PI * subNotes[k] * det * t)
            }

            // Glassy crystalline bloom as PlayStation 2 wordmark illuminates
            val tCrystal = (t - 1.32).coerceAtLeast(0.0)
            val crystalEnv = if (t > 1.32) (1.0 - exp(-tCrystal * 8.5)) * exp(-tCrystal * 1.25) else 0.0
            var crysSum = 0.0
            if (crystalEnv > 0.0005) {
                for (k in crystalNotes.indices) {
                    val det = 1.0 + 0.0018 * cos(2.0 * PI * 0.42 * tCrystal + k)
                    val fm = 0.35 * exp(-tCrystal * 3.5) * sin(2.0 * PI * crystalNotes[k] * 0.5 * tCrystal)
                    crysSum += crystalW[k] * sin(2.0 * PI * crystalNotes[k] * det * tCrystal + fm)
                }
            }

            dry[i] = (windLp * 1.45 * windEnv + subSum * subEnv * 0.62 + crysSum * crystalEnv * 0.72) * 0.58
        }
        return applyCavernReverb(dry, 0.52)
    }

    // Authentic PS2 Disc-Boot Vacuum Abyss Plunge + Crystalline Harmonic Resolve (~1.25s)
    private fun synthDiscLaunchSfx(): ShortArray {
        val dur = 1.25
        val len = (SAMPLE_RATE * dur).toInt()
        val dry = DoubleArray(len)
        var rng = 0x52534450u
        var swooshLp = 0.0

        for (i in 0 until len) {
            val t = i.toDouble() / SAMPLE_RATE
            rng = rng * 1664525u + 1013904223u
            val white = ((rng shr 8).toInt() / 8388608.0) - 1.0
            swooshLp += (white - swooshLp) * (0.04 * exp(-t * 1.8) + 0.006)
            val swooshEnv = (1.0 - exp(-t * 18.0)) * exp(-t * 3.2)

            // Plunging sub-bass boom into abyss
            val subFreq = 52.0 + 96.0 * exp(-t * 4.2)
            val subBoom = sin(2.0 * PI * subFreq * t) * (1.0 - exp(-t * 35.0)) * exp(-t * 2.4) * 0.55

            // Ringing PlayStation 2 F#m7 crystal glass shimmer
            val tChime = (t - 0.08).coerceAtLeast(0.0)
            val chimeEnv = if (t > 0.08) (1.0 - exp(-tChime * 24.0)) * exp(-tChime * 2.1) else 0.0
            val chime = (0.34 * sin(2.0 * PI * 369.99 * tChime) +
                0.28 * sin(2.0 * PI * 554.37 * tChime) +
                0.22 * sin(2.0 * PI * 830.61 * tChime) +
                0.14 * sin(2.0 * PI * 987.77 * tChime)) * chimeEnv

            dry[i] = (swooshLp * 1.25 * swooshEnv + subBoom + chime) * 0.62
        }
        return applyCavernReverb(dry, 0.50)
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

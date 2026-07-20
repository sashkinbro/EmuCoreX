package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class AudioDefaultsTest {
    @Test
    fun defaultsMatchAndroidFrontendPolicy() {
        assertEquals(100, AudioDefaults.VOLUME_DEFAULT)
        assertEquals(100, AudioDefaults.BUFFER_MS_DEFAULT)
        assertEquals(50, AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT)
        assertEquals(AudioDefaults.INTERPOLATION_GAUSSIAN, AudioDefaults.INTERPOLATION_DEFAULT)
        assertEquals(AudioDefaults.SYNC_TIME_STRETCH, AudioDefaults.SYNC_DEFAULT)
        assertEquals(false, AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT)
        assertEquals(AudioDefaults.BACKEND_AAUDIO, AudioDefaults.BACKEND_DEFAULT)
    }

    @Test
    fun coreEnumNamesAreStable() {
        assertEquals("Nearest", AudioDefaults.interpolationCoreName(AudioDefaults.INTERPOLATION_NEAREST))
        assertEquals("Linear", AudioDefaults.interpolationCoreName(AudioDefaults.INTERPOLATION_LINEAR))
        assertEquals("Gaussian", AudioDefaults.interpolationCoreName(AudioDefaults.INTERPOLATION_GAUSSIAN))
        assertEquals("Cubic", AudioDefaults.interpolationCoreName(AudioDefaults.INTERPOLATION_CUBIC))
        assertEquals("Disabled", AudioDefaults.syncModeCoreName(AudioDefaults.SYNC_DISABLED))
        assertEquals("TimeStretch", AudioDefaults.syncModeCoreName(AudioDefaults.SYNC_TIME_STRETCH))
        assertEquals("SDL", AudioDefaults.backendCoreName(AudioDefaults.BACKEND_AAUDIO))
        assertEquals("OpenSLES", AudioDefaults.backendCoreName(AudioDefaults.BACKEND_OPENSLES))
    }

    @Test
    fun invalidValuesAreSanitized() {
        assertEquals(0, AudioDefaults.coerceVolume(-10))
        assertEquals(100, AudioDefaults.coerceVolume(150))
        assertEquals(AudioDefaults.INTERPOLATION_DEFAULT, AudioDefaults.coerceInterpolation(99))
        assertEquals(AudioDefaults.SYNC_DEFAULT, AudioDefaults.coerceSyncMode(99))
        assertEquals(AudioDefaults.BACKEND_DEFAULT, AudioDefaults.coerceBackend(99))
        assertEquals(10, AudioDefaults.coerceBufferMs(0))
        assertEquals(500, AudioDefaults.coerceOutputLatencyMs(999))
    }

    @Test
    fun lightweightSpu2OverridesProcessingWithoutReplacingManualValues() {
        assertEquals(
            AudioDefaults.INTERPOLATION_LINEAR,
            AudioDefaults.effectiveInterpolation(AudioDefaults.INTERPOLATION_CUBIC, true)
        )
        assertEquals(
            AudioDefaults.SYNC_DISABLED,
            AudioDefaults.effectiveSyncMode(AudioDefaults.SYNC_TIME_STRETCH, true)
        )
        assertEquals(
            AudioDefaults.INTERPOLATION_CUBIC,
            AudioDefaults.effectiveInterpolation(AudioDefaults.INTERPOLATION_CUBIC, false)
        )
        assertEquals(
            AudioDefaults.SYNC_TIME_STRETCH,
            AudioDefaults.effectiveSyncMode(AudioDefaults.SYNC_TIME_STRETCH, false)
        )
    }
}

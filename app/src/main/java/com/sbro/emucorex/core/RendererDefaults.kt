package com.sbro.emucorex.core

object RendererDefaults {
    const val AUTO = -1
    const val OPENGL = 12
    const val SOFTWARE = 13
    const val VULKAN = 14

    // Generic fallback when hardware detection is unavailable. Runtime selection
    // should use defaultForHardware() so known GPU families get the safer default.
    const val DEFAULT = OPENGL

    internal fun defaultForProfile(profile: Int): Int = when (GpuHardwareProfiles.normalize(profile)) {
        GpuHardwareProfiles.ADRENO -> VULKAN
        else -> OPENGL
    }

    fun defaultForHardware(): Int = defaultForProfile(GpuHardwareProfiles.detectHardwareProfile())

    fun normalizeAndroidRenderer(value: Int): Int {
        return when (value) {
            OPENGL, SOFTWARE, VULKAN -> value
            else -> defaultForHardware()
        }
    }
}

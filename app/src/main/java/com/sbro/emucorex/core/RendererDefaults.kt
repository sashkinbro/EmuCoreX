package com.sbro.emucorex.core

object RendererDefaults {
    const val AUTO = -1
    const val OPENGL = 12
    const val SOFTWARE = 13
    const val VULKAN = 14
    const val DEFAULT = OPENGL

    fun defaultForHardware(
        @Suppress("UNUSED_PARAMETER")
        isMediaTekHardware: Boolean = GpuHardwareProfiles.isMediaTekHardware()
    ): Int = DEFAULT

    fun normalizeAndroidRenderer(
        value: Int,
        isMediaTekHardware: Boolean = GpuHardwareProfiles.isMediaTekHardware()
    ): Int {
        return when (value) {
            OPENGL, SOFTWARE, VULKAN -> value
            else -> defaultForHardware(isMediaTekHardware)
        }
    }
}

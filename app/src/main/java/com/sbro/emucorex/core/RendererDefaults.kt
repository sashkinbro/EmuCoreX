package com.sbro.emucorex.core

object RendererDefaults {
    const val AUTO = -1
    const val OPENGL = 12
    const val SOFTWARE = 13
    const val VULKAN = 14
    // Prefer OpenGL on Mali/PowerVR, where Vulkan is not consistently faster.
    // Adreno devices retain Vulkan as the default; both backends remain selectable.
    const val DEFAULT = OPENGL

    fun defaultForHardware(): Int = when (GpuHardwareProfiles.detectHardwareProfile()) {
        GpuHardwareProfiles.ADRENO -> VULKAN
        else -> OPENGL
    }

    fun normalizeAndroidRenderer(value: Int): Int {
        return when (value) {
            OPENGL, SOFTWARE, VULKAN -> value
            else -> defaultForHardware()
        }
    }
}

package com.sbro.emucorex.core

data class PerformancePresetConfig(
    val id: Int,
    val renderer: Int,
    val upscaleMultiplier: Float,
    val eeCycleRate: Int,
    val eeCycleSkip: Int,
    val enableMtvu: Boolean,
    val enableFastCdvd: Boolean,
    val frameSkip: Int,
    val textureFiltering: Int,
    val anisotropicFiltering: Int,
    val widescreenPatches: Boolean,
    val skipDraw: Int,
    val halfPixelOffset: Int
)

object PerformancePresets {
    const val CUSTOM = 0
    const val BATTERY = 1
    const val BALANCED = 2
    const val PERFORMANCE = 3
    const val AGGRESSIVE = 4

    fun configFor(
        id: Int,
        deviceProfile: DevicePerformanceProfile = DevicePerformanceProfiles.current()
    ): PerformancePresetConfig? = when (id) {
        BATTERY -> batteryConfig(deviceProfile)
        BALANCED -> balancedConfig(deviceProfile)
        PERFORMANCE -> performanceConfig(deviceProfile)
        AGGRESSIVE -> aggressiveConfig(deviceProfile)
        else -> null
    }

    private fun batteryConfig(deviceProfile: DevicePerformanceProfile): PerformancePresetConfig {
        return if (deviceProfile.family == DeviceChipsetFamily.MEDIATEK) {
            PerformancePresetConfig(
                id = BATTERY,
                renderer = 12,
                upscaleMultiplier = 1f,
                eeCycleRate = 0,
                eeCycleSkip = 0,
                enableMtvu = true,
                enableFastCdvd = false,
                frameSkip = 1,
                textureFiltering = 0,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 0,
                halfPixelOffset = 0
            )
        } else {
            PerformancePresetConfig(
                id = BATTERY,
                renderer = 14,
                upscaleMultiplier = 1f,
                eeCycleRate = 0,
                eeCycleSkip = 0,
                enableMtvu = true,
                enableFastCdvd = false,
                frameSkip = 1,
                textureFiltering = 0,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 0,
                halfPixelOffset = 0
            )
        }
    }

    private fun balancedConfig(deviceProfile: DevicePerformanceProfile): PerformancePresetConfig {
        return if (deviceProfile.family == DeviceChipsetFamily.MEDIATEK) {
            PerformancePresetConfig(
                id = BALANCED,
                renderer = 12,
                upscaleMultiplier = 1f,
                eeCycleRate = 0,
                eeCycleSkip = 0,
                enableMtvu = true,
                enableFastCdvd = false,
                frameSkip = 0,
                textureFiltering = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 0,
                halfPixelOffset = 0
            )
        } else {
            PerformancePresetConfig(
                id = BALANCED,
                renderer = 14,
                upscaleMultiplier = 1f,
                eeCycleRate = 0,
                eeCycleSkip = 0,
                enableMtvu = true,
                enableFastCdvd = false,
                frameSkip = 0,
                textureFiltering = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 0,
                halfPixelOffset = 0
            )
        }
    }

    private fun performanceConfig(deviceProfile: DevicePerformanceProfile): PerformancePresetConfig {
        return if (deviceProfile.family == DeviceChipsetFamily.MEDIATEK) {
            PerformancePresetConfig(
                id = PERFORMANCE,
                renderer = 12,
                upscaleMultiplier = 1f,
                eeCycleRate = 1,
                eeCycleSkip = 1,
                enableMtvu = true,
                enableFastCdvd = false,
                frameSkip = 0,
                textureFiltering = 0,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 0,
                halfPixelOffset = 0
            )
        } else {
            PerformancePresetConfig(
                id = PERFORMANCE,
                renderer = 14,
                upscaleMultiplier = 1f,
                eeCycleRate = 1,
                eeCycleSkip = 1,
                enableMtvu = true,
                enableFastCdvd = true,
                frameSkip = 0,
                textureFiltering = 1,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 1,
                halfPixelOffset = 0
            )
        }
    }

    private fun aggressiveConfig(deviceProfile: DevicePerformanceProfile): PerformancePresetConfig {
        return if (deviceProfile.family == DeviceChipsetFamily.MEDIATEK) {
            PerformancePresetConfig(
                id = AGGRESSIVE,
                renderer = 12,
                upscaleMultiplier = 1f,
                eeCycleRate = 2,
                eeCycleSkip = 1,
                enableMtvu = true,
                enableFastCdvd = true,
                frameSkip = 0,
                textureFiltering = 0,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 1,
                halfPixelOffset = 0
            )
        } else {
            PerformancePresetConfig(
                id = AGGRESSIVE,
                renderer = 14,
                upscaleMultiplier = 1f,
                eeCycleRate = 2,
                eeCycleSkip = 2,
                enableMtvu = true,
                enableFastCdvd = true,
                frameSkip = 1,
                textureFiltering = 0,
                anisotropicFiltering = 0,
                widescreenPatches = false,
                skipDraw = 2,
                halfPixelOffset = 0
            )
        }
    }
}

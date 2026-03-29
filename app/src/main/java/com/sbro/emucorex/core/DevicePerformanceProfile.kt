package com.sbro.emucorex.core

import android.os.Build

enum class DeviceChipsetFamily {
    SNAPDRAGON,
    MEDIATEK,
    EXYNOS,
    TENSOR,
    GENERIC
}

data class DevicePerformanceProfile(
    val family: DeviceChipsetFamily,
    val displayName: String,
    val recommendedPresetId: Int
)

object DevicePerformanceProfiles {
    fun current(): DevicePerformanceProfile {
        val socManufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MANUFACTURER.orEmpty()
        } else {
            ""
        }
        val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MODEL.orEmpty()
        } else {
            ""
        }
        val fingerprint = listOf(
            Build.MANUFACTURER,
            Build.BRAND,
            Build.MODEL,
            Build.DEVICE,
            Build.PRODUCT,
            Build.HARDWARE,
            Build.BOARD,
            socManufacturer,
            socModel
        ).joinToString(" ")
            .lowercase()

        val family = when {
            fingerprint.contains("mediatek") ||
                fingerprint.contains("dimensity") ||
                fingerprint.contains("helio") ||
                Regex("\\bmt\\d{3,}\\b").containsMatchIn(fingerprint) ->
                DeviceChipsetFamily.MEDIATEK

            fingerprint.contains("snapdragon") ||
                fingerprint.contains("qualcomm") ||
                fingerprint.contains("qcom") ||
                fingerprint.contains("sm8") ||
                fingerprint.contains("sm7") ->
                DeviceChipsetFamily.SNAPDRAGON

            fingerprint.contains("exynos") -> DeviceChipsetFamily.EXYNOS
            fingerprint.contains("tensor") || fingerprint.contains("gs101") || fingerprint.contains("gs201") ->
                DeviceChipsetFamily.TENSOR

            else -> DeviceChipsetFamily.GENERIC
        }

        val displayName = when {
            socModel.isNotBlank() -> socModel
            Build.HARDWARE.isNotBlank() -> Build.HARDWARE
            Build.MODEL.isNotBlank() -> Build.MODEL
            else -> Build.DEVICE.orEmpty()
        }

        val recommendedPresetId = when (family) {
            DeviceChipsetFamily.MEDIATEK -> PerformancePresets.BALANCED
            DeviceChipsetFamily.SNAPDRAGON -> PerformancePresets.BALANCED
            DeviceChipsetFamily.EXYNOS -> PerformancePresets.BALANCED
            DeviceChipsetFamily.TENSOR -> PerformancePresets.BALANCED
            DeviceChipsetFamily.GENERIC -> PerformancePresets.BALANCED
        }

        return DevicePerformanceProfile(
            family = family,
            displayName = displayName,
            recommendedPresetId = recommendedPresetId
        )
    }
}

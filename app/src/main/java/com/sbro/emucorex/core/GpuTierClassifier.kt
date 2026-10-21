package com.sbro.emucorex.core

object GpuTierClassifier {
    private val ADRENO_MODEL = Regex("""adreno\s*(\d{3})""", RegexOption.IGNORE_CASE)
    private val MALI_LEGACY_MODEL = Regex("""mali-\s*(?:t)?(\d{3,4})""", RegexOption.IGNORE_CASE)
    private val MALI_VALHALL_MODEL =
        Regex("""mali-g(\d{2,4})(?:\s*((?:mc|mp)\s*\d{1,2}))?""", RegexOption.IGNORE_CASE)
    private val MALI_CORE_COUNT = Regex("""(?:mc|mp)\s*(\d{1,2})""", RegexOption.IGNORE_CASE)
    private val IMMORTALIS_MODEL = Regex("""immortalis-g(\d{3})""", RegexOption.IGNORE_CASE)
    private val MALI_NEXT_GEN = Regex("""mali-g[12](?!\d)""", RegexOption.IGNORE_CASE)
    private val XCLIPSE_MODEL = Regex("""xclipse\s*(\d{3})""", RegexOption.IGNORE_CASE)

    private val ADRENO_RED_6XX = setOf(605, 608, 610, 612, 613, 615, 616)
    private val ADRENO_YELLOW = setOf(618, 619, 620, 630, 642, 643, 644)

    fun classify(model: GpuModelInfo?): GpuTier {
        val info = model ?: return GpuTier.UNKNOWN
        return when (info.vendor) {
            GpuVendor.ADRENO -> classifyAdreno(info.displayName)
            GpuVendor.MALI -> classifyMali(info.displayName)
            GpuVendor.XCLIPSE -> classifyXclipse(info.displayName)
            GpuVendor.POWERVR -> classifyPowerVr(info.displayName)
            GpuVendor.UNKNOWN -> GpuTier.UNKNOWN
        }
    }

    private fun classifyAdreno(displayName: String): GpuTier {
        val value = ADRENO_MODEL.find(displayName)?.groupValues?.get(1)?.toIntOrNull()
            ?: return GpuTier.UNKNOWN
        return when {
            value < 600 -> GpuTier.RED
            value in ADRENO_RED_6XX -> GpuTier.RED
            value in ADRENO_YELLOW -> GpuTier.YELLOW
            value >= 640 -> GpuTier.GREEN
            else -> GpuTier.UNKNOWN
        }
    }

    private fun classifyMali(displayName: String): GpuTier {
        if (IMMORTALIS_MODEL.containsMatchIn(displayName)) return GpuTier.GREEN
        if (MALI_NEXT_GEN.containsMatchIn(displayName)) return GpuTier.GREEN
        val valhall = MALI_VALHALL_MODEL.find(displayName)
        if (valhall != null) {
            val generation = valhall.groupValues[1].toIntOrNull() ?: return GpuTier.UNKNOWN
            val coreCount = MALI_CORE_COUNT.find(valhall.groupValues[2])?.groupValues?.get(1)?.toIntOrNull()
            return when (generation) {
                31, 51, 52 -> GpuTier.RED
                57 -> if (coreCount == 1) GpuTier.RED else GpuTier.YELLOW
                71 -> GpuTier.RED
                310 -> GpuTier.RED
                68, 72, 76 -> GpuTier.YELLOW
                510 -> GpuTier.YELLOW
                610, 615, 710, 715, 720, 725, 925 -> GpuTier.GREEN
                77, 78 -> GpuTier.GREEN
                else -> GpuTier.UNKNOWN
            }
        }
        if (MALI_LEGACY_MODEL.containsMatchIn(displayName)) return GpuTier.RED
        return GpuTier.UNKNOWN
    }

    private fun classifyXclipse(displayName: String): GpuTier {
        val value = XCLIPSE_MODEL.find(displayName)?.groupValues?.get(1)?.toIntOrNull()
            ?: return GpuTier.UNKNOWN
        return when {
            value in 500..599 -> GpuTier.YELLOW
            value >= 900 -> GpuTier.GREEN
            else -> GpuTier.UNKNOWN
        }
    }

    private fun classifyPowerVr(displayName: String): GpuTier =
        if (displayName.contains("dxt", ignoreCase = true) ||
            displayName.contains("cxt", ignoreCase = true)
        ) {
            GpuTier.GREEN
        } else {
            GpuTier.RED
        }
}

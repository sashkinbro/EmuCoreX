package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class MobileSocNameMapperTest {
    @Test
    fun resolvesCurrentFlagshipPlatformCodes() {
        assertEquals("Snapdragon 8 Elite Extreme Gen 6", MobileSocNameMapper.resolve("SM8975"))
        assertEquals("Snapdragon 8 Elite Gen 6", MobileSocNameMapper.resolve("SM8950"))
        assertEquals("Snapdragon 8 Gen 6", MobileSocNameMapper.resolve("SM8845 Pro"))
        assertEquals("Snapdragon 8 Elite Gen 5", MobileSocNameMapper.resolve("SM8850-AC"))
        assertEquals("Dimensity 9600 Pro", MobileSocNameMapper.resolve("MT6995"))
        assertEquals("Dimensity 9500", MobileSocNameMapper.resolve("MT6993"))
        assertEquals("Dimensity 8500", MobileSocNameMapper.resolve("MT6899Z"))
        assertEquals("Exynos 2600", MobileSocNameMapper.resolve("s5e9965"))
        assertEquals("Exynos 1680", MobileSocNameMapper.resolve("S5E8865"))
        assertEquals("Google Tensor G6", MobileSocNameMapper.resolve("GS601"))
        assertEquals("Google Tensor G5", MobileSocNameMapper.resolve("laguna"))
    }

    @Test
    fun resolvesOlderAndAlternativeVendorCodes() {
        assertEquals("Snapdragon 835", MobileSocNameMapper.resolve("MSM8998"))
        assertEquals("Snapdragon 845", MobileSocNameMapper.resolve("SDM845"))
        assertEquals("Helio G99/G100", MobileSocNameMapper.resolve("mt6789v/cd"))
        assertEquals("Dimensity 1100", MobileSocNameMapper.resolve("MT6891Z/CZA"))
        assertEquals("Kirin 970", MobileSocNameMapper.resolve("hi3670"))
        assertEquals("Unisoc T618", MobileSocNameMapper.resolve("T618"))
        assertEquals("Rockchip RK3588", MobileSocNameMapper.resolve("rk3588s"))
    }

    @Test
    fun resolvesAndroidHandheldSpecificPlatforms() {
        assertEquals("Snapdragon G3x Gen 1", MobileSocNameMapper.resolve("QTI SG8175P"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("QCS8550"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve(null, board = "kalama"))
        assertEquals("Helio G99", MobileSocNameMapper.resolve("MT8781V/CA"))
    }

    @Test
    fun resolvesSnapdragonPartNumberSuffixesBeforeBaseModels() {
        assertEquals("Snapdragon 7s Gen 4", MobileSocNameMapper.resolve("SM7635-AC"))
        assertEquals("Snapdragon 7s Gen 3", MobileSocNameMapper.resolve("SM7635"))
        assertEquals("Snapdragon 6s Gen 4", MobileSocNameMapper.resolve("SM6435-AA"))
        assertEquals("Snapdragon 6s Gen 3", MobileSocNameMapper.resolve("SM6375-AC"))
        assertEquals("Snapdragon 695", MobileSocNameMapper.resolve("SM6375"))
        assertEquals("Snapdragon 4 Gen 4", MobileSocNameMapper.resolve("SM4450-AF"))
        assertEquals("Snapdragon 4 Gen 2", MobileSocNameMapper.resolve("SM4450"))
        assertEquals("Snapdragon 6s 4G Gen 2", MobileSocNameMapper.resolve("SM6225-AF"))
        assertEquals("Snapdragon 685", MobileSocNameMapper.resolve("SM6225-AD"))
        assertEquals("Snapdragon 680 series", MobileSocNameMapper.resolve("SM6225"))
        assertEquals("Snapdragon 870", MobileSocNameMapper.resolve("SM8250-AC"))
        assertEquals("Snapdragon 855+", MobileSocNameMapper.resolve("SM8150-AC"))
    }

    @Test
    fun keepsHandheldsOnTheirActualSnapdragonSilicon() {
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM8550", model = "AYANEO Pocket S"))
        assertEquals("Snapdragon 7+ Gen 3", MobileSocNameMapper.resolve("SM7675", model = "Retroid Pocket G2"))
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650", model = "AYANEO Pocket S2"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM8550", model = "Phone"))
    }

    @Test
    fun keepsAlreadyReadableMarketingNames() {
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("Snapdragon 8 Gen 3"))
        assertEquals("Snapdragon 7s Gen 4", MobileSocNameMapper.resolve("Snapdragon 7s Gen 4"))
        assertEquals("Exynos 2400", MobileSocNameMapper.resolve("Exynos 2400"))
    }

    @Test
    fun prefersTheReportedMarketingNameOverAGenericPlatformCode() {
        assertEquals(
            "Snapdragon 7s Gen 4",
            MobileSocNameMapper.resolve("Snapdragon 7s Gen 4", board = "sm7635")
        )
    }

    @Test
    fun neverShowsAnUnmappedRawPlatformCode() {
        assertEquals("Snapdragon", MobileSocNameMapper.resolve("SM9999", manufacturer = "Qualcomm"))
        assertEquals("MediaTek", MobileSocNameMapper.resolve("MT9999"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("future_platform"))
    }

    @Test
    fun resolvesAllwinnerPartsAndSharedBspFamilies() {
        listOf("A523", "A527", "T527", "H728", "A333", "A537", "MR527", "A133", "A133P", "H700", "H616", "H618")
            .forEach { part -> assertEquals("Allwinner $part", MobileSocNameMapper.resolve(part)) }
        assertEquals("Allwinner A537", MobileSocNameMapper.resolve("Allwinner A537PX"))
        assertEquals("Allwinner T527", MobileSocNameMapper.resolve("T527M02"))
        assertEquals("Allwinner H618", MobileSocNameMapper.resolve("Allwinner H618"))
        assertEquals("Allwinner A523", MobileSocNameMapper.resolve("allwinner,sun55i-a523"))
        assertEquals("Allwinner A523/A527/H728/T527", MobileSocNameMapper.resolve("sun55iw3p1"))
        assertEquals("Allwinner H616/H618/H700", MobileSocNameMapper.resolve("sun50iw9"))
        assertEquals("Allwinner A100/A133", MobileSocNameMapper.resolve("sun50iw10p1"))
        assertEquals(
            "Allwinner",
            MobileSocNameMapper.resolve(
                socModel = null,
                hardware = "unknown",
                manufacturer = "Allwinner",
                model = "Allwinner Game Console"
            )
        )
    }

    @Test
    fun resolvesTrimuiSmartProSWithoutGuessingOtherSun55Devices() {
        listOf("TrimUI Smart Pro S", "TG5050").forEach { model ->
            assertEquals("Allwinner A523", MobileSocNameMapper.resolve(null, hardware = "sun55iw3p1", model = model))
        }
        assertEquals("Allwinner T527", MobileSocNameMapper.resolve("T527", model = "TG5050"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve(null, model = "TrimUI Smart Pro"))
    }

    @Test
    fun resolvesEliteBoardAndSupplementalSocIdentifiers() {
        assertEquals("Snapdragon 8 Elite", MobileSocNameMapper.resolve(null, hardware = "qcom", board = "sun"))
        assertEquals("Snapdragon 8 Elite", MobileSocNameMapper.resolve("SM8750P"))
        assertEquals("Snapdragon 8 Elite", MobileSocNameMapper.resolve(null, additionalIdentifiers = listOf("qcom,sm8750")))
        assertEquals("Allwinner A523", MobileSocNameMapper.resolve(null, additionalIdentifiers = listOf("allwinner,sun55i-a523")))
        assertEquals("Allwinner A537", MobileSocNameMapper.resolve(null, hardware = "sun55iw3", additionalIdentifiers = listOf("A537")))
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650", model = "Allwinner A523"))
    }

    @Test
    fun shortBoardAndPartAliasesDoNotMatchInsideOtherNames() {
        listOf("samsung", "sunmi", "sunstone", "A5230", "notA523", "H7000").forEach {
            assertEquals(it, "Unknown SoC", MobileSocNameMapper.resolve(it))
        }
        assertEquals("Exynos 2400", MobileSocNameMapper.resolve(null, model = "Samsung SM-S928B", board = "s5e9945"))
        assertEquals(null, MobileSocNameMapper.resolveKnownCode("Samsung SM-A527"))
    }

    @Test
    fun resolvesAdditionalG52PlatformsWithoutLosingPartNumberSuffixes() {
        assertEquals("MediaTek MT8168", MobileSocNameMapper.resolve("MT8168V"))
        assertEquals("MediaTek Genio 350", MobileSocNameMapper.resolve("MT8365"))
        assertEquals("Helio P65/G80/G85", MobileSocNameMapper.resolve("mt6768v/wa"))
        assertEquals("Helio G70/G80/G81/G85/G88/G91/G92", MobileSocNameMapper.resolve("mt6769v/cz"))
        assertEquals("Amlogic A311D2", MobileSocNameMapper.resolve("amlogic,a311d2"))
        assertEquals("Amlogic A311D", MobileSocNameMapper.resolve("A311D"))
    }

    @Test
    fun codeAliasesNeverMatchInsideUnrelatedWordsOrLongerNumbers() {
        listOf("landscape", "konafoo", "notrk3566", "SM85500", "MT67680", "T6180", "RK35660")
            .forEach { assertEquals(it, null, MobileSocNameMapper.resolveKnownCode(it)) }
        assertEquals("Rockchip RK3566", MobileSocNameMapper.resolveKnownCode("rockchip,rk3566"))
        assertEquals("Snapdragon 8 Elite Gen 5", MobileSocNameMapper.resolveKnownCode("SM8850 sun"))
    }
}

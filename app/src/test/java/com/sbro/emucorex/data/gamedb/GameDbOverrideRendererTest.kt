package com.sbro.emucorex.data.gamedb

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GameDbOverrideRendererTest {
    @Test
    fun disablingOneCoreFixKeepsEveryOtherFix() {
        val yaml = render(
            baseline = listOf(coreFix("EETimingHack"), coreFix("DMABusyHack")),
            override = overrideOf("gameFixes|EETimingHack" to GameDbFixOverride(enabled = false))
        )

        assertTrue(yaml.contains("SLUS-20000:"))
        assertTrue(yaml.contains("  gameFixes:"))
        assertTrue(yaml.contains("    - DMABusyHack"))
        assertFalse(yaml.contains("    - EETimingHack"))
    }

    @Test
    fun disablingEveryCoreFixEmitsAnEmptyListSoTheCoreClearsThem() {
        val yaml = render(
            baseline = listOf(coreFix("EETimingHack"), coreFix("DMABusyHack")),
            override = overrideOf(
                "gameFixes|EETimingHack" to GameDbFixOverride(enabled = false),
                "gameFixes|DMABusyHack" to GameDbFixOverride(enabled = false)
            )
        )

        assertTrue(yaml.contains("  gameFixes: []"))
    }

    @Test
    fun anAddedFixJoinsTheBundledList() {
        val yaml = render(
            baseline = listOf(coreFix("EETimingHack")),
            override = overrideOf(
                "gameFixes|XGKickHack" to GameDbFixOverride(added = true)
            )
        )

        assertTrue(yaml.contains("    - EETimingHack"))
        assertTrue(yaml.contains("    - XGKickHack"))
    }

    @Test
    fun disablingOneGraphicsFixRewritesTheWholeMapWithoutIt() {
        val yaml = render(
            baseline = listOf(graphicsFix("autoFlush", "1"), graphicsFix("halfPixelOffset", "5")),
            override = overrideOf("gsHWFixes|autoFlush" to GameDbFixOverride(enabled = false))
        )

        assertTrue(yaml.contains("  gsHWFixes:"))
        assertTrue(yaml.contains("    halfPixelOffset: 5"))
        assertFalse(yaml.contains("    autoFlush:"))
    }

    @Test
    fun aValueOverrideReplacesTheBundledValue() {
        val yaml = render(
            baseline = listOf(graphicsFix("halfPixelOffset", "1")),
            override = overrideOf("gsHWFixes|halfPixelOffset" to GameDbFixOverride(value = "5"))
        )

        assertTrue(yaml.contains("    halfPixelOffset: 5"))
    }

    @Test
    fun aDisabledClampModeAsksTheCoreToKeepTheUserMode() {
        val yaml = render(
            baseline = listOf(clampMode("vuClampMode", "3")),
            override = overrideOf("clampModes|vuClampMode" to GameDbFixOverride(enabled = false))
        )

        assertTrue(yaml.contains("  clampModes:"))
        assertTrue(yaml.contains("    vuClampMode: -1"))
    }

    @Test
    fun anEditedRoundModeOnlyEmitsThatKey() {
        val yaml = render(
            baseline = listOf(roundMode("eeRoundMode", "1"), roundMode("vuRoundMode", "1")),
            override = overrideOf("roundModes|eeRoundMode" to GameDbFixOverride(value = "3"))
        )

        assertTrue(yaml.contains("    eeRoundMode: 3"))
        assertFalse(yaml.contains("vuRoundMode"))
    }

    @Test
    fun anUntouchedGameProducesNoOverlayAtAll() {
        val baseline = mapOf(
            "SLUS-20000" to entry("SLUS-20000", coreFix("EETimingHack"))
        )

        val yaml = GameDbOverrideRenderer.buildOverlay(
            mapOf("SLUS-20000" to GameDbGameOverride()),
            baseline
        )

        assertTrue(yaml.isBlank())
    }

    private fun render(
        baseline: List<GameDbSettingItem>,
        override: GameDbGameOverride
    ): String = GameDbOverrideRenderer.buildOverlay(
        mapOf(SERIAL to override),
        mapOf(SERIAL to entry(SERIAL, *baseline.toTypedArray()))
    )

    private fun overrideOf(vararg fixes: Pair<String, GameDbFixOverride>) =
        GameDbGameOverride(fixes.toMap())

    private fun entry(serial: String, vararg settings: GameDbSettingItem) = GameDbCatalogEntry(
        serial = serial,
        title = "Test Game",
        region = "NTSC-U",
        coverUrl = null,
        settings = settings.toList()
    )

    private fun coreFix(key: String) = GameDbSettingItem(
        category = GameDbSettingCategory.CORE_FIX,
        key = key,
        value = "Enabled",
        note = null,
        controlledByGameFixesToggle = true
    )

    private fun graphicsFix(key: String, value: String) = GameDbSettingItem(
        category = GameDbSettingCategory.GRAPHICS_FIX,
        key = key,
        value = value,
        note = null,
        controlledByGameFixesToggle = false
    )

    private fun clampMode(key: String, value: String) = GameDbSettingItem(
        category = GameDbSettingCategory.CLAMP_MODE,
        key = key,
        value = value,
        note = null,
        controlledByGameFixesToggle = true
    )

    private fun roundMode(key: String, value: String) = GameDbSettingItem(
        category = GameDbSettingCategory.ROUND_MODE,
        key = key,
        value = value,
        note = null,
        controlledByGameFixesToggle = true
    )

    private companion object {
        private const val SERIAL = "SLUS-20000"
    }
}

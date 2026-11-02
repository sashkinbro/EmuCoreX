package com.sbro.emucorex.ui.gamedb

import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.GameDbSettingItem
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The value picker and the value badge must agree: a picker option that renders differently from
 * the badge would let the user pick a value that then looks like something else.
 */
class GameDbValueCatalogTest {
    @Test
    fun everyPickerOptionRendersLikeTheBadge() {
        roundAndClampKeys().forEach { (category, key) ->
            assertOptionLabelsMatch(category, key)
        }
        GameDbFixCatalog.graphicsFixes.forEach { fix ->
            if (GameDbValueCatalog.optionsFor(GameDbSettingCategory.GRAPHICS_FIX, fix.key) != null) {
                assertOptionLabelsMatch(GameDbSettingCategory.GRAPHICS_FIX, fix.key)
            }
        }
    }

    @Test
    fun theFixesThatNeedAValueHaveOne() {
        listOf("autoFlush", "halfPixelOffset", "roundSprite", "bilinearUpscale", "hwDownloadMode")
            .forEach { key ->
                assertNotNull(
                    "$key must offer a picker",
                    GameDbValueCatalog.optionsFor(GameDbSettingCategory.GRAPHICS_FIX, key)
                )
            }
    }

    @Test
    fun everyAddedFixStartsWithAValueThePickerCanShow() {
        GameDbFixCatalog.graphicsFixes.forEach { fix ->
            val options = GameDbValueCatalog.optionsFor(GameDbSettingCategory.GRAPHICS_FIX, fix.key) ?: return@forEach
            assertTrue(
                "${fix.key} default '${fix.defaultValue}' is not one of its options",
                options.any { it.value.toString() == fix.defaultValue }
            )
        }
    }

    private fun roundAndClampKeys() = listOf(
        GameDbSettingCategory.ROUND_MODE to "eeRoundMode",
        GameDbSettingCategory.ROUND_MODE to "vuRoundMode",
        GameDbSettingCategory.ROUND_MODE to "vu0RoundMode",
        GameDbSettingCategory.ROUND_MODE to "vu1RoundMode",
        GameDbSettingCategory.CLAMP_MODE to "eeClampMode",
        GameDbSettingCategory.CLAMP_MODE to "vuClampMode",
        GameDbSettingCategory.CLAMP_MODE to "vu0ClampMode",
        GameDbSettingCategory.CLAMP_MODE to "vu1ClampMode"
    )

    private fun assertOptionLabelsMatch(category: GameDbSettingCategory, key: String) {
        val options = GameDbValueCatalog.optionsFor(category, key)
        assertNotNull("$category/$key must offer a picker", options)
        options.orEmpty().forEach { option ->
            val presentation = GameDbSettingPresentationMapper.map(
                GameDbSettingItem(
                    category = category,
                    key = key,
                    value = option.value.toString(),
                    note = null,
                    controlledByGameFixesToggle = category != GameDbSettingCategory.GRAPHICS_FIX
                )
            )
            assertEquals(
                "$category/$key=${option.value}",
                option.label,
                presentation.value.resourceId
            )
        }
    }
}

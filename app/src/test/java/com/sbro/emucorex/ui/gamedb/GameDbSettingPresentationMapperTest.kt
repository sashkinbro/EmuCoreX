package com.sbro.emucorex.ui.gamedb

import com.sbro.emucorex.R
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.GameDbSettingItem
import org.junit.Assert.assertEquals
import org.junit.Test

class GameDbSettingPresentationMapperTest {
    @Test
    fun presentsMobileGsOverlayKeysWithTheirExistingOrDedicatedLabelsAndModes() {
        assertPresentation(
            key = "coalesceRenderPasses",
            rawValue = "1",
            title = R.string.gamedb_setting_coalesce_render_passes,
            value = R.string.gamedb_browser_enabled
        )
        assertPresentation(
            key = "copyRoadMaximumBlendingLevel",
            rawValue = "0",
            title = R.string.gamedb_setting_copy_road_maximum_blending,
            value = R.string.settings_blending_accuracy_minimum
        )
        assertPresentation(
            key = "hwDownloadMode",
            rawValue = "5",
            title = R.string.settings_hw_download_mode,
            value = R.string.settings_hw_download_mode_asynchronous
        )
        assertPresentation(
            key = "bilinearDirtyUpscale",
            rawValue = "1",
            title = R.string.settings_bilinear_upscale,
            value = R.string.settings_bilinear_upscale_force_bilinear
        )
        assertPresentation(
            key = "bilinearDirtyUpscale",
            rawValue = "2",
            title = R.string.settings_bilinear_upscale,
            value = R.string.settings_bilinear_upscale_force_nearest
        )
    }

    private fun assertPresentation(key: String, rawValue: String, title: Int, value: Int) {
        val presentation = GameDbSettingPresentationMapper.map(
            GameDbSettingItem(
                category = GameDbSettingCategory.GRAPHICS_FIX,
                key = key,
                value = rawValue,
                note = null,
                controlledByGameFixesToggle = false
            )
        )
        assertEquals(title, presentation.title.resourceId)
        assertEquals(value, presentation.value.resourceId)
    }
}

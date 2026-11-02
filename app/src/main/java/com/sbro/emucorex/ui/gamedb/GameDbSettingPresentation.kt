package com.sbro.emucorex.ui.gamedb

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.sbro.emucorex.R
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.GameDbSettingItem

data class GameDbText(
    @param:StringRes val resourceId: Int,
    val argument: String? = null
)

data class GameDbSettingPresentation(
    val title: GameDbText,
    val value: GameDbText
)

/** Keeps raw GameIndex keys out of the UI and gives every bundled setting a localizable label. */
object GameDbSettingPresentationMapper {
    fun map(setting: GameDbSettingItem): GameDbSettingPresentation {
        val titleResource = titleResource(setting.key)
        return GameDbSettingPresentation(
            title = GameDbText(
                resourceId = titleResource,
                argument = setting.key.takeIf { titleResource == R.string.gamedb_setting_unknown }
            ),
            value = valueText(setting)
        )
    }

    @StringRes
    private fun titleResource(key: String): Int = when (key) {
        "BlitInternalFPSHack" -> R.string.gamedb_fix_blit_internal_fps
        "DMABusyHack" -> R.string.gamedb_fix_dma_busy
        "EETimingHack" -> R.string.gamedb_fix_ee_timing
        "FpuMulHack" -> R.string.gamedb_fix_fpu_multiply
        "FullVU0SyncHack" -> R.string.gamedb_fix_full_vu0_sync
        "GIFFIFOHack" -> R.string.gamedb_fix_gif_fifo
        "GoemonTlbHack" -> R.string.gamedb_fix_goemon_tlb
        "IbitHack" -> R.string.gamedb_fix_i_bit
        "InstantDMAHack" -> R.string.gamedb_fix_instant_dma
        "OPHFlagHack" -> R.string.gamedb_fix_oph_flag
        "SkipMPEGHack" -> R.string.gamedb_fix_skip_mpeg
        "SoftwareRendererFMVHack" -> R.string.gamedb_fix_software_renderer_fmv
        "VIF1StallHack" -> R.string.gamedb_fix_vif1_stall
        "VIFFIFOHack" -> R.string.gamedb_fix_vif_fifo
        "VuAddSubHack" -> R.string.gamedb_fix_vu_add_sub
        "VUOverflowHack" -> R.string.gamedb_fix_vu_overflow
        "VUSyncHack" -> R.string.gamedb_fix_vu_sync
        "XGKickHack" -> R.string.gamedb_fix_xgkick

        "eeRoundMode" -> R.string.settings_ee_fpu_round_mode
        "eeDivRoundMode" -> R.string.gamedb_setting_ee_div_round_mode
        "vuRoundMode" -> R.string.gamedb_setting_vu_round_mode
        "vu0RoundMode" -> R.string.settings_vu0_round_mode
        "vu1RoundMode" -> R.string.settings_vu1_round_mode
        "eeClampMode" -> R.string.settings_ee_fpu_clamping
        "vuClampMode" -> R.string.gamedb_setting_vu_clamping
        "vu0ClampMode" -> R.string.settings_vu0_clamping
        "vu1ClampMode" -> R.string.settings_vu1_clamping
        "eeCycleRate" -> R.string.settings_ee_cycle_rate
        "instantVU1" -> R.string.settings_instant_vu1
        "mtvu" -> R.string.settings_mtvu
        "mvuFlag" -> R.string.gamedb_setting_mvu_flag

        "accurateAlphaTest" -> R.string.gamedb_setting_accurate_alpha_test
        "alignSprite" -> R.string.settings_align_sprite
        "autoFlush" -> R.string.settings_auto_flush_hardware
        "beforeDraw" -> R.string.gamedb_setting_before_draw
        "bilinearUpscale" -> R.string.settings_bilinear_upscale
        "bilinearDirtyUpscale" -> R.string.settings_bilinear_upscale
        "coalesceRenderPasses" -> R.string.gamedb_setting_coalesce_render_passes
        "copyRoadMaximumBlendingLevel" -> R.string.gamedb_setting_copy_road_maximum_blending
        "cpuCLUTRender" -> R.string.settings_software_clut_render
        "cpuFramebufferConversion" -> R.string.settings_cpu_framebuffer_conversion
        "cpuSpriteRenderBW" -> R.string.settings_cpu_sprite_render_size
        "cpuSpriteRenderLevel" -> R.string.settings_cpu_sprite_render_level
        "deinterlace" -> R.string.gamedb_setting_deinterlace_mode
        "disablePartialInvalidation" -> R.string.settings_disable_partial_invalidation
        "disableDepthSupport" -> R.string.settings_disable_depth_conversion
        "trilinearFiltering" -> R.string.settings_trilinear_filtering
        "skipDrawStart" -> R.string.settings_skip_draw_start
        "skipDrawEnd" -> R.string.settings_skip_draw_end
        "estimateTextureRegion" -> R.string.settings_estimate_texture_region
        "forceEvenSpritePosition" -> R.string.settings_force_even_sprite_position
        "getSkipCount" -> R.string.gamedb_setting_skip_draw_handler
        "gpuPaletteConversion" -> R.string.settings_gpu_palette_conversion
        "gpuTargetCLUT" -> R.string.settings_gpu_target_clut
        "hwDownloadMode" -> R.string.settings_hw_download_mode
        "halfPixelOffset" -> R.string.settings_half_pixel_offset
        "limit24BitDepth" -> R.string.gamedb_setting_limit_24_bit_depth
        "maximumBlendingLevel" -> R.string.gamedb_setting_maximum_blending
        "mergeSprite" -> R.string.settings_merge_sprite
        "minimumBlendingLevel" -> R.string.gamedb_setting_minimum_blending
        "mipmap" -> R.string.settings_hw_mipmapping
        "moveHandler" -> R.string.gamedb_setting_move_handler
        "nativePaletteDraw" -> R.string.settings_native_palette_draw
        "nativeScaling" -> R.string.settings_native_scaling
        "PCRTCOffsets" -> R.string.gamedb_setting_pcrtc_offsets
        "PCRTCOverscan" -> R.string.gamedb_setting_pcrtc_overscan
        "preloadFrameData" -> R.string.settings_preload_frame_data
        "readTCOnClose" -> R.string.settings_read_targets_on_close
        "recommendedBlendingLevel" -> R.string.gamedb_setting_recommended_blending
        "roundSprite" -> R.string.settings_round_sprite
        "textureInsideRT" -> R.string.settings_texture_inside_rt
        "texturePreloading" -> R.string.settings_texture_preloading
        else -> R.string.gamedb_setting_unknown
    }

    private fun valueText(setting: GameDbSettingItem): GameDbText {
        val number = setting.value.toIntOrNull()
        if (setting.category == GameDbSettingCategory.CORE_FIX) return text(R.string.gamedb_browser_enabled)
        if (setting.category == GameDbSettingCategory.ROUND_MODE) {
            return text(
                when (number) {
                    0 -> R.string.settings_float_round_nearest
                    1 -> R.string.settings_float_round_negative
                    2 -> R.string.settings_float_round_positive
                    3 -> R.string.settings_float_round_chop
                    else -> R.string.gamedb_setting_value_mode
                }, setting.value
            )
        }
        if (setting.category == GameDbSettingCategory.CLAMP_MODE) {
            return text(
                when (number) {
                    0 -> R.string.settings_clamping_none
                    1 -> R.string.settings_clamping_normal
                    2 -> R.string.settings_clamping_extra
                    3 -> if (setting.key == "eeClampMode") R.string.settings_clamping_full else R.string.settings_clamping_extra_sign
                    else -> R.string.gamedb_setting_value_mode
                }, setting.value
            )
        }

        return when (setting.key) {
            "eeCycleRate" -> text(R.string.gamedb_setting_value_rate, setting.value)
            "instantVU1", "mtvu", "mvuFlag", "mipmap", "coalesceRenderPasses" -> booleanValue(number)
            "beforeDraw", "getSkipCount", "moveHandler" -> text(R.string.gamedb_setting_value_builtin_rule)
            "autoFlush" -> enumValue(number, mapOf(
                0 to R.string.settings_disabled_short,
                1 to R.string.settings_auto_flush_sprites,
                2 to R.string.settings_auto_flush_all
            ), setting.value)
            "bilinearUpscale", "bilinearDirtyUpscale" -> enumValue(number, mapOf(
                0 to R.string.settings_trilinear_filtering_auto,
                1 to R.string.settings_bilinear_upscale_force_bilinear,
                2 to R.string.settings_bilinear_upscale_force_nearest
            ), setting.value)
            "hwDownloadMode" -> enumValue(number, mapOf(
                0 to R.string.settings_hw_download_mode_accurate,
                1 to R.string.settings_hw_download_mode_force_full,
                2 to R.string.settings_hw_download_mode_no_readbacks,
                3 to R.string.settings_hw_download_mode_unsynchronized,
                4 to R.string.settings_hw_download_mode_disabled,
                5 to R.string.settings_hw_download_mode_asynchronous
            ), setting.value)
            "cpuCLUTRender" -> enumValue(number, mapOf(
                0 to R.string.settings_disabled_short,
                1 to R.string.settings_normal_short,
                2 to R.string.settings_aggressive_short
            ), setting.value)
            "cpuSpriteRenderLevel" -> enumValue(number, mapOf(
                0 to R.string.settings_cpu_sprite_render_level_sprites,
                1 to R.string.settings_cpu_sprite_render_level_triangles,
                2 to R.string.settings_cpu_sprite_render_level_blended
            ), setting.value)
            "gpuTargetCLUT" -> enumValue(number, mapOf(
                0 to R.string.settings_disabled_short,
                1 to R.string.settings_gpu_target_clut_exact,
                2 to R.string.settings_gpu_target_clut_inside
            ), setting.value)
            "halfPixelOffset" -> enumValue(number, mapOf(
                0 to R.string.settings_half_pixel_off,
                1 to R.string.settings_half_pixel_normal,
                2 to R.string.settings_half_pixel_special,
                3 to R.string.settings_half_pixel_special_aggressive,
                4 to R.string.settings_half_pixel_native,
                5 to R.string.settings_half_pixel_native_tex
            ), setting.value)
            "nativeScaling" -> enumValue(number, mapOf(
                0 to R.string.settings_native_scaling_off,
                1 to R.string.settings_native_scaling_normal,
                2 to R.string.settings_native_scaling_aggressive,
                3 to R.string.settings_native_scaling_normal_maintain_upscale,
                4 to R.string.settings_native_scaling_aggressive_maintain_upscale
            ), setting.value)
            "roundSprite" -> enumValue(number, mapOf(
                0 to R.string.settings_half_pixel_off,
                1 to R.string.settings_round_sprite_half,
                2 to R.string.settings_round_sprite_full
            ), setting.value)
            "textureInsideRT" -> enumValue(number, mapOf(
                0 to R.string.settings_disabled_short,
                1 to R.string.settings_texture_inside_rt_inside,
                2 to R.string.settings_texture_inside_rt_merge
            ), setting.value)
            "texturePreloading" -> enumValue(number, mapOf(
                0 to R.string.settings_texture_preloading_none,
                1 to R.string.settings_texture_preloading_partial,
                2 to R.string.settings_texture_preloading_full
            ), setting.value)
            "minimumBlendingLevel", "recommendedBlendingLevel", "maximumBlendingLevel",
            "copyRoadMaximumBlendingLevel" -> enumValue(number, mapOf(
                0 to R.string.settings_blending_accuracy_minimum,
                1 to R.string.settings_blending_accuracy_basic,
                2 to R.string.settings_blending_accuracy_medium,
                3 to R.string.settings_blending_accuracy_high,
                4 to R.string.settings_blending_accuracy_full,
                5 to R.string.settings_blending_accuracy_maximum
            ), setting.value)
            "cpuSpriteRenderBW", "deinterlace", "gpuPaletteConversion", "limit24BitDepth" ->
                text(R.string.gamedb_setting_value_mode, setting.value)
            else -> if (number == 0 || number == 1) booleanValue(number)
                else text(R.string.gamedb_setting_value_mode, setting.value)
        }
    }

    private fun booleanValue(number: Int?): GameDbText = text(
        if (number == 0) R.string.settings_disabled_short else R.string.gamedb_browser_enabled
    )

    private fun enumValue(number: Int?, values: Map<Int, Int>, raw: String): GameDbText =
        values[number]?.let(::text) ?: text(R.string.gamedb_setting_value_mode, raw)

    private fun text(@StringRes resourceId: Int, argument: String? = null) = GameDbText(resourceId, argument)
}

@Composable
fun GameDbText.resolve(): String = argument?.let { stringResource(resourceId, it) } ?: stringResource(resourceId)

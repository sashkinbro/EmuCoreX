package com.sbro.emucorex.ui.gamedb

import androidx.annotation.StringRes
import com.sbro.emucorex.R
import com.sbro.emucorex.data.gamedb.GameDbFixSection
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory

/** A GameDB line the user can add to a game that does not list it yet. */
data class GameDbAddableFix(val key: String, val defaultValue: String)

/**
 * Every fix the bundled GameDB can apply, so the browser can offer the ones a game is missing.
 *
 * Handler-style graphics fixes (`getSkipCount`, `beforeDraw`, `moveHandler`) are deliberately absent:
 * they take a function name rather than a number, so they cannot be added without a dedicated picker.
 */
object GameDbFixCatalog {
    val coreFixes: List<GameDbAddableFix> = listOf(
        "BlitInternalFPSHack",
        "DMABusyHack",
        "EETimingHack",
        "FpuMulHack",
        "FullVU0SyncHack",
        "GIFFIFOHack",
        "GoemonTlbHack",
        "IbitHack",
        "InstantDMAHack",
        "OPHFlagHack",
        "SkipMPEGHack",
        "SoftwareRendererFMVHack",
        "VIF1StallHack",
        "VIFFIFOHack",
        "VuAddSubHack",
        "VUOverflowHack",
        "VUSyncHack",
        "XGKickHack"
    ).map { GameDbAddableFix(it, "") }

    val graphicsFixes: List<GameDbAddableFix> = listOf(
        "autoFlush",
        "cpuFramebufferConversion",
        "readTCOnClose",
        "disableDepthSupport",
        "preloadFrameData",
        "disablePartialInvalidation",
        "textureInsideRT",
        "limit24BitDepth",
        "alignSprite",
        "mergeSprite",
        "mipmap",
        "accurateAlphaTest",
        "forceEvenSpritePosition",
        "bilinearUpscale",
        "nativePaletteDraw",
        "estimateTextureRegion",
        "PCRTCOffsets",
        "PCRTCOverscan",
        "trilinearFiltering",
        "skipDrawStart",
        "skipDrawEnd",
        "halfPixelOffset",
        "roundSprite",
        "nativeScaling",
        "texturePreloading",
        "deinterlace",
        "cpuSpriteRenderBW",
        "cpuSpriteRenderLevel",
        "cpuCLUTRender",
        "gpuTargetCLUT",
        "gpuPaletteConversion",
        "minimumBlendingLevel",
        "maximumBlendingLevel",
        "recommendedBlendingLevel",
        "hwDownloadMode",
        "coalesceRenderPasses",
        "copyRoadMaximumBlendingLevel"
    ).map { GameDbAddableFix(it, "1") }

    fun addable(section: GameDbFixSection): List<GameDbAddableFix> = when (section) {
        GameDbFixSection.GAME_FIXES -> coreFixes
        GameDbFixSection.GS_HW_FIXES -> graphicsFixes
        else -> emptyList()
    }

    fun supportsAdding(section: GameDbFixSection): Boolean =
        section == GameDbFixSection.GAME_FIXES || section == GameDbFixSection.GS_HW_FIXES

    fun defaultValue(section: GameDbFixSection, key: String): String =
        addable(section).firstOrNull { it.key == key }?.defaultValue ?: ""
}

data class GameDbValueOption(val value: Int, @param:StringRes val label: Int)

/**
 * The finite value sets a GameDB line accepts. Keeping them next to the labels means the badge text
 * and the picker can never drift apart.
 */
object GameDbValueCatalog {
    private val roundModes = listOf(
        GameDbValueOption(0, R.string.settings_float_round_nearest),
        GameDbValueOption(1, R.string.settings_float_round_negative),
        GameDbValueOption(2, R.string.settings_float_round_positive),
        GameDbValueOption(3, R.string.settings_float_round_chop)
    )

    private val booleans = listOf(
        GameDbValueOption(0, R.string.settings_disabled_short),
        GameDbValueOption(1, R.string.gamedb_browser_enabled)
    )

    private val blendingLevels = listOf(
        GameDbValueOption(0, R.string.settings_blending_accuracy_minimum),
        GameDbValueOption(1, R.string.settings_blending_accuracy_basic),
        GameDbValueOption(2, R.string.settings_blending_accuracy_medium),
        GameDbValueOption(3, R.string.settings_blending_accuracy_high),
        GameDbValueOption(4, R.string.settings_blending_accuracy_full),
        GameDbValueOption(5, R.string.settings_blending_accuracy_maximum)
    )

    private val graphicOptions: Map<String, List<GameDbValueOption>> = mapOf(
        "autoFlush" to listOf(
            GameDbValueOption(0, R.string.settings_disabled_short),
            GameDbValueOption(1, R.string.settings_auto_flush_sprites),
            GameDbValueOption(2, R.string.settings_auto_flush_all)
        ),
        "halfPixelOffset" to listOf(
            GameDbValueOption(0, R.string.settings_half_pixel_off),
            GameDbValueOption(1, R.string.settings_half_pixel_normal),
            GameDbValueOption(2, R.string.settings_half_pixel_special),
            GameDbValueOption(3, R.string.settings_half_pixel_special_aggressive),
            GameDbValueOption(4, R.string.settings_half_pixel_native),
            GameDbValueOption(5, R.string.settings_half_pixel_native_tex)
        ),
        "roundSprite" to listOf(
            GameDbValueOption(0, R.string.settings_half_pixel_off),
            GameDbValueOption(1, R.string.settings_round_sprite_half),
            GameDbValueOption(2, R.string.settings_round_sprite_full)
        ),
        "bilinearUpscale" to listOf(
            GameDbValueOption(0, R.string.settings_trilinear_filtering_auto),
            GameDbValueOption(1, R.string.settings_bilinear_upscale_force_bilinear),
            GameDbValueOption(2, R.string.settings_bilinear_upscale_force_nearest)
        ),
        "bilinearDirtyUpscale" to listOf(
            GameDbValueOption(0, R.string.settings_trilinear_filtering_auto),
            GameDbValueOption(1, R.string.settings_bilinear_upscale_force_bilinear),
            GameDbValueOption(2, R.string.settings_bilinear_upscale_force_nearest)
        ),
        "textureInsideRT" to listOf(
            GameDbValueOption(0, R.string.settings_disabled_short),
            GameDbValueOption(1, R.string.settings_texture_inside_rt_inside),
            GameDbValueOption(2, R.string.settings_texture_inside_rt_merge)
        ),
        "nativeScaling" to listOf(
            GameDbValueOption(0, R.string.settings_native_scaling_off),
            GameDbValueOption(1, R.string.settings_native_scaling_normal),
            GameDbValueOption(2, R.string.settings_native_scaling_aggressive),
            GameDbValueOption(3, R.string.settings_native_scaling_normal_maintain_upscale),
            GameDbValueOption(4, R.string.settings_native_scaling_aggressive_maintain_upscale)
        ),
        "cpuCLUTRender" to listOf(
            GameDbValueOption(0, R.string.settings_disabled_short),
            GameDbValueOption(1, R.string.settings_normal_short),
            GameDbValueOption(2, R.string.settings_aggressive_short)
        ),
        "gpuTargetCLUT" to listOf(
            GameDbValueOption(0, R.string.settings_disabled_short),
            GameDbValueOption(1, R.string.settings_gpu_target_clut_exact),
            GameDbValueOption(2, R.string.settings_gpu_target_clut_inside)
        ),
        "cpuSpriteRenderLevel" to listOf(
            GameDbValueOption(0, R.string.settings_cpu_sprite_render_level_sprites),
            GameDbValueOption(1, R.string.settings_cpu_sprite_render_level_triangles),
            GameDbValueOption(2, R.string.settings_cpu_sprite_render_level_blended)
        ),
        "texturePreloading" to listOf(
            GameDbValueOption(0, R.string.settings_texture_preloading_none),
            GameDbValueOption(1, R.string.settings_texture_preloading_partial),
            GameDbValueOption(2, R.string.settings_texture_preloading_full)
        ),
        "hwDownloadMode" to listOf(
            GameDbValueOption(0, R.string.settings_hw_download_mode_accurate),
            GameDbValueOption(1, R.string.settings_hw_download_mode_force_full),
            GameDbValueOption(2, R.string.settings_hw_download_mode_no_readbacks),
            GameDbValueOption(3, R.string.settings_hw_download_mode_unsynchronized),
            GameDbValueOption(4, R.string.settings_hw_download_mode_disabled),
            GameDbValueOption(5, R.string.settings_hw_download_mode_asynchronous)
        ),
        "minimumBlendingLevel" to blendingLevels,
        "recommendedBlendingLevel" to blendingLevels,
        "maximumBlendingLevel" to blendingLevels,
        "copyRoadMaximumBlendingLevel" to blendingLevels,
        "alignSprite" to booleans,
        "mergeSprite" to booleans,
        "forceEvenSpritePosition" to booleans,
        "mipmap" to booleans,
        "coalesceRenderPasses" to booleans,
        "cpuFramebufferConversion" to booleans,
        "readTCOnClose" to booleans,
        "disableDepthSupport" to booleans,
        "disableDepthSupport" to booleans,
        "preloadFrameData" to booleans,
        "disablePartialInvalidation" to booleans,
        "nativePaletteDraw" to booleans,
        "estimateTextureRegion" to booleans,
        "accurateAlphaTest" to booleans
    )

    private val speedHackOptions: Map<String, List<GameDbValueOption>> = mapOf(
        "instantVU1" to booleans,
        "mtvu" to booleans,
        "mvuFlag" to booleans,
        "vuThread" to booleans,
        "vuFlagHack" to booleans,
        "waitLoop" to booleans,
        "intcStat" to booleans,
        "fastCDVD" to booleans
    )

    private fun clampOptions(key: String): List<GameDbValueOption> = listOf(
        GameDbValueOption(0, R.string.settings_clamping_none),
        GameDbValueOption(1, R.string.settings_clamping_normal),
        GameDbValueOption(2, R.string.settings_clamping_extra),
        GameDbValueOption(
            3,
            if (key == "eeClampMode") R.string.settings_clamping_full else R.string.settings_clamping_extra_sign
        )
    )

    fun optionsFor(category: GameDbSettingCategory, key: String): List<GameDbValueOption>? = when (category) {
        GameDbSettingCategory.ROUND_MODE -> roundModes
        GameDbSettingCategory.CLAMP_MODE -> clampOptions(key)
        GameDbSettingCategory.CORE_FIX -> null
        GameDbSettingCategory.GRAPHICS_FIX -> graphicOptions[key]
        GameDbSettingCategory.SPEED_HACK -> speedHackOptions[key]
    }
}

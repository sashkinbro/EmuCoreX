package com.sbro.emucorex.core

object GsHackDefaults {
    const val DEINTERLACE_MODE_DEFAULT = 0
    const val DEINTERLACE_MODE_MIN = 0
    const val DEINTERLACE_MODE_MAX = 9
    const val DITHERING_DEFAULT = 2
    const val DITHERING_MIN = 0
    const val DITHERING_MAX = 3
    const val BILINEAR_FILTERING_DEFAULT = 2
    const val BILINEAR_FILTERING_MIN = 0
    const val BILINEAR_FILTERING_MAX = 3
    const val TRILINEAR_FILTERING_DEFAULT = -1
    const val TRILINEAR_FILTERING_MIN = -1
    const val TRILINEAR_FILTERING_MAX = 2
    const val TV_SHADER_DEFAULT = 0
    const val TV_SHADER_MIN = 0
    const val TV_SHADER_MAX = 7
    const val BLENDING_ACCURACY_DEFAULT = 1
    const val BLENDING_ACCURACY_MINIMUM = 0
    const val BLENDING_ACCURACY_MAXIMUM = 5
    const val TEXTURE_PRELOADING_DEFAULT = 2
    const val TEXTURE_PRELOADING_MIN = 0
    const val TEXTURE_PRELOADING_MAX = 2
    const val ANISOTROPIC_FILTERING_DEFAULT = 0
    const val HW_MIPMAPPING_DEFAULT = true
    const val ANTI_BLUR_DEFAULT = true
    const val HW_DOWNLOAD_MODE_DEFAULT = 4 // Disabled — GPU readbacks are slow/buggy on Android mobile GPUs
    const val HW_DOWNLOAD_MODE_MIN = 0
    const val HW_DOWNLOAD_MODE_MAX = 4
    const val FRAME_SKIP_DEFAULT = 0
    const val FRAME_SKIP_MIN = 0
    const val FRAME_SKIP_MAX = 4
    const val HALF_PIXEL_OFFSET_DEFAULT = 0
    const val NATIVE_SCALING_DEFAULT = 0
    const val NATIVE_SCALING_MIN = 0
    const val NATIVE_SCALING_MAX = 4
    const val ROUND_SPRITE_DEFAULT = 0
    const val BILINEAR_UPSCALE_DEFAULT = 0
    const val AUTO_FLUSH_DEFAULT = 0
    const val TEXTURE_INSIDE_RT_DEFAULT = 0
    const val GPU_TARGET_CLUT_DEFAULT = 0
    const val CPU_SPRITE_RENDER_SIZE_DEFAULT = 0
    const val CPU_SPRITE_RENDER_LEVEL_DEFAULT = 0
    const val SOFTWARE_CLUT_RENDER_DEFAULT = 0

    fun coerceBilinearFiltering(value: Int): Int {
        return value.coerceIn(BILINEAR_FILTERING_MIN, BILINEAR_FILTERING_MAX)
    }

    fun coerceTrilinearFiltering(value: Int): Int {
        return value.coerceIn(TRILINEAR_FILTERING_MIN, TRILINEAR_FILTERING_MAX)
    }

    fun coerceBlendingAccuracy(value: Int): Int {
        return value.coerceIn(BLENDING_ACCURACY_MINIMUM, BLENDING_ACCURACY_MAXIMUM)
    }

    fun coerceTexturePreloading(value: Int): Int {
        return value.coerceIn(TEXTURE_PRELOADING_MIN, TEXTURE_PRELOADING_MAX)
    }

    fun coerceHardwareDownloadMode(value: Int): Int {
        return value.coerceIn(HW_DOWNLOAD_MODE_MIN, HW_DOWNLOAD_MODE_MAX)
    }

    fun coerceFrameSkip(value: Int): Int {
        return value.coerceIn(FRAME_SKIP_MIN, FRAME_SKIP_MAX)
    }

    fun coerceAnisotropicFiltering(value: Int): Int {
        return if (value == 0 || value == 2 || value == 4 || value == 8 || value == 16)
            value
        else
            ANISOTROPIC_FILTERING_DEFAULT
    }

    fun coerceNativeScaling(value: Int): Int {
        return value.coerceIn(NATIVE_SCALING_MIN, NATIVE_SCALING_MAX)
    }

    fun coerceTvShader(value: Int): Int {
        return value.coerceIn(TV_SHADER_MIN, TV_SHADER_MAX)
    }

    fun coerceDeinterlaceMode(value: Int): Int {
        return if (value in DEINTERLACE_MODE_MIN..DEINTERLACE_MODE_MAX) value else DEINTERLACE_MODE_DEFAULT
    }

    fun coerceDithering(value: Int): Int {
        return if (value in DITHERING_MIN..DITHERING_MAX) value else DITHERING_DEFAULT
    }

    fun shouldEnableManualHardwareFixes(
        cpuSpriteRenderSize: Int,
        cpuSpriteRenderLevel: Int,
        softwareClutRender: Int,
        gpuTargetClutMode: Int,
        skipDrawStart: Int,
        skipDrawEnd: Int,
        autoFlushHardware: Int,
        cpuFramebufferConversion: Boolean,
        disableDepthConversion: Boolean,
        disableSafeFeatures: Boolean,
        disableRenderFixes: Boolean,
        preloadFrameData: Boolean,
        disablePartialInvalidation: Boolean,
        textureInsideRt: Int,
        readTargetsOnClose: Boolean,
        estimateTextureRegion: Boolean,
        gpuPaletteConversion: Boolean,
        halfPixelOffset: Int,
        nativeScaling: Int,
        roundSprite: Int,
        bilinearUpscale: Int,
        textureOffsetX: Int,
        textureOffsetY: Int,
        alignSprite: Boolean,
        mergeSprite: Boolean,
        forceEvenSpritePosition: Boolean,
        nativePaletteDraw: Boolean
    ): Boolean {
        return cpuSpriteRenderSize != CPU_SPRITE_RENDER_SIZE_DEFAULT ||
            cpuSpriteRenderLevel != CPU_SPRITE_RENDER_LEVEL_DEFAULT ||
            softwareClutRender != SOFTWARE_CLUT_RENDER_DEFAULT ||
            gpuTargetClutMode != GPU_TARGET_CLUT_DEFAULT ||
            skipDrawStart != 0 ||
            skipDrawEnd != 0 ||
            autoFlushHardware != AUTO_FLUSH_DEFAULT ||
            cpuFramebufferConversion ||
            disableDepthConversion ||
            disableSafeFeatures ||
            disableRenderFixes ||
            preloadFrameData ||
            disablePartialInvalidation ||
            textureInsideRt != TEXTURE_INSIDE_RT_DEFAULT ||
            readTargetsOnClose ||
            estimateTextureRegion ||
            gpuPaletteConversion ||
            halfPixelOffset != HALF_PIXEL_OFFSET_DEFAULT ||
            nativeScaling != NATIVE_SCALING_DEFAULT ||
            roundSprite != ROUND_SPRITE_DEFAULT ||
            bilinearUpscale != BILINEAR_UPSCALE_DEFAULT ||
            textureOffsetX != 0 ||
            textureOffsetY != 0 ||
            alignSprite ||
            mergeSprite ||
            forceEvenSpritePosition ||
            nativePaletteDraw
    }
}

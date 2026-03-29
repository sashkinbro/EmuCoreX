package com.sbro.emucorex.core

object GsHackDefaults {
    const val BILINEAR_FILTERING_DEFAULT = 2
    const val TRILINEAR_FILTERING_DEFAULT = 0
    const val BLENDING_ACCURACY_DEFAULT = 1
    const val TEXTURE_PRELOADING_DEFAULT = 2
    const val ANISOTROPIC_FILTERING_DEFAULT = 0
    const val HW_DOWNLOAD_MODE_DEFAULT = 0
    const val HALF_PIXEL_OFFSET_DEFAULT = 0
    const val NATIVE_SCALING_DEFAULT = 0
    const val ROUND_SPRITE_DEFAULT = 0
    const val BILINEAR_UPSCALE_DEFAULT = 0
    const val AUTO_FLUSH_DEFAULT = 0
    const val TEXTURE_INSIDE_RT_DEFAULT = 0
    const val GPU_TARGET_CLUT_DEFAULT = 0
    const val CPU_SPRITE_RENDER_SIZE_DEFAULT = 0
    const val CPU_SPRITE_RENDER_LEVEL_DEFAULT = 0
    const val SOFTWARE_CLUT_RENDER_DEFAULT = 0

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

package com.sbro.emucorex.core

import android.content.Context
import android.os.Build
import android.view.Surface
import java.io.File
import java.lang.ref.WeakReference

object EmulatorBridge {

    private val aspectRatioSettingValues = mapOf(
        0 to "Stretch",
        1 to "Auto 4:3/3:2",
        2 to "4:3",
        3 to "16:9",
        4 to "10:7"
    )

    @Volatile
    var isNativeLoaded: Boolean = false
        private set

    @Volatile
    private var isVmActive: Boolean = false

    @Volatile
    private var lastSurface: Surface? = null

    @Volatile
    private var lastSurfaceWidth: Int = 0

    @Volatile
    private var lastSurfaceHeight: Int = 0

    private val settingsCache = HashMap<String, String>()

    @Volatile
    private var shutdownRequested: Boolean = false

    private var contextRef: WeakReference<Context>? = null
    private val nativeLock = Any()

    private fun rendererName(renderer: Int): String = when (renderer) {
        12 -> "OpenGL"
        13 -> "Software"
        14 -> "Vulkan"
        15 -> "D3D12"
        3 -> "D3D11"
        else -> "Unknown($renderer)"
    }

    init {
        try {
            System.loadLibrary("emucore")
            isNativeLoaded = true
        } catch (_: UnsatisfiedLinkError) {
            isNativeLoaded = false
        }
    }

    fun initializeOnce(context: Context) {
        contextRef = WeakReference(context)
        if (!isNativeLoaded) return

        try {
            NativeApp.setNativeLibraryDir(context.applicationInfo.nativeLibraryDir ?: "")
            NativeApp.initializeOnce(context.applicationContext)
        } catch (_: Exception) { }
    }

    fun getContext(): Context? = contextRef?.get()

    fun applyRuntimeConfig(
        biosPath: String?,
        renderer: Int,
        upscaleMultiplier: Int,
        gpuDriverType: Int = 0,
        customDriverPath: String? = null,
        aspectRatio: Int = 1,
        mtvu: Boolean = true,
        fastCdvd: Boolean = false,
        enableCheats: Boolean = true,
        hwDownloadMode: Int = 0,
        eeCycleRate: Int = 0,
        eeCycleSkip: Int = 0,
        frameSkip: Int = 0,
        frameLimitEnabled: Boolean = false,
        targetFps: Int = 60,
        textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
        trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
        blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
        texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
        enableFxaa: Boolean = false,
        casMode: Int = 0,
        casSharpness: Int = 50,
        anisotropicFiltering: Int = 0,
        enableHwMipmapping: Boolean = true,
        widescreenPatches: Boolean = false,
        noInterlacingPatches: Boolean = false,
        cpuSpriteRenderSize: Int = GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT,
        cpuSpriteRenderLevel: Int = GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT,
        softwareClutRender: Int = GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT,
        gpuTargetClutMode: Int = GsHackDefaults.GPU_TARGET_CLUT_DEFAULT,
        skipDrawStart: Int = 0,
        skipDrawEnd: Int = 0,
        autoFlushHardware: Int = GsHackDefaults.AUTO_FLUSH_DEFAULT,
        cpuFramebufferConversion: Boolean = false,
        disableDepthConversion: Boolean = false,
        disableSafeFeatures: Boolean = false,
        disableRenderFixes: Boolean = false,
        preloadFrameData: Boolean = false,
        disablePartialInvalidation: Boolean = false,
        textureInsideRt: Int = GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT,
        readTargetsOnClose: Boolean = false,
        estimateTextureRegion: Boolean = false,
        gpuPaletteConversion: Boolean = false,
        halfPixelOffset: Int = GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT,
        nativeScaling: Int = GsHackDefaults.NATIVE_SCALING_DEFAULT,
        roundSprite: Int = GsHackDefaults.ROUND_SPRITE_DEFAULT,
        bilinearUpscale: Int = GsHackDefaults.BILINEAR_UPSCALE_DEFAULT,
        textureOffsetX: Int = 0,
        textureOffsetY: Int = 0,
        alignSprite: Boolean = false,
        mergeSprite: Boolean = false,
        forceEvenSpritePosition: Boolean = false,
        nativePaletteDraw: Boolean = false
    ) {
        if (!isNativeLoaded) return

        val context = getContext() ?: return
        try {
            NativeApp.setCrashContextString("emu_device_family", DevicePerformanceProfiles.current().family.name)
            NativeApp.setCrashContextString("emu_soc_model", if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL.orEmpty() else "")
            NativeApp.setCrashContextString("emu_device_model", Build.MODEL.orEmpty())
            NativeApp.setCrashContextString("emu_renderer_name", rendererName(renderer))
            NativeApp.setCrashContextString("emu_gpu_driver_mode", if (gpuDriverType == 1) "custom" else "system")
            NativeApp.logCrashBreadcrumb(
                "applyRuntimeConfig renderer=${rendererName(renderer)}($renderer) driverType=$gpuDriverType hwDownload=$hwDownloadMode mtvu=$mtvu fastCdvd=$fastCdvd"
            )
            val resolvedBiosPath = DocumentPathResolver.prepareBiosDirectory(context, biosPath)
                ?: biosPath?.let(DocumentPathResolver::resolveDirectoryPath)
            val preferredBiosFile = DocumentPathResolver.findPreferredBiosFileName(resolvedBiosPath)

            val savestatesDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "sstates").apply { mkdirs() }
            val memcardsDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "memcards").apply { mkdirs() }
            val cheatsDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "cheats").apply { mkdirs() }
            val patchesDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "patches").apply { mkdirs() }

            NativeApp.renderGpu(renderer)
            NativeApp.renderUpscalemultiplier(upscaleMultiplier.toFloat())
            NativeApp.setAspectRatio(aspectRatio)
            
            NativeApp.setSetting("EmuCore/GS", "Renderer", "int", renderer.toString())
            NativeApp.setSetting("EmuCore/GS", "upscale_multiplier", "float", upscaleMultiplier.toFloat().toString())
            NativeApp.setSetting(
                "EmuCore/GS",
                "AspectRatio",
                "string",
                aspectRatioSettingValues[aspectRatio] ?: aspectRatioSettingValues.getValue(1)
            )
            
            // Directories
            NativeApp.setSetting("Folders", "Bios", "string", resolvedBiosPath.orEmpty())
            NativeApp.setSetting("Folders", "Savestates", "string", savestatesDir.absolutePath)
            NativeApp.setSetting("Folders", "MemoryCards", "string", memcardsDir.absolutePath)
            NativeApp.setSetting("Folders", "Cheats", "string", cheatsDir.absolutePath)
            NativeApp.setSetting("Folders", "Patches", "string", patchesDir.absolutePath)
            NativeApp.setSetting("Filenames", "BIOS", "string", preferredBiosFile.orEmpty())
            NativeApp.refreshBIOS()
            
            // Speed Hacks
            NativeApp.setSetting("EmuCore/Speedhacks", "vuThread", "bool", mtvu.toString())
            NativeApp.setSetting("EmuCore/Speedhacks", "fastCDVD", "bool", fastCdvd.toString())
            NativeApp.setSetting("EmuCore", "EnableCheats", "bool", enableCheats.toString())
            NativeApp.setSetting("EmuCore/GS", "HWDownloadMode", "int", hwDownloadMode.toString())
            NativeApp.setSetting("EmuCore/Speedhacks", "EECycleRate", "int", eeCycleRate.toString())
            NativeApp.setSetting("EmuCore/Speedhacks", "EECycleSkip", "int", eeCycleSkip.toString())
            NativeApp.setSetting("EmuCore/GS", "FrameLimitEnable", "bool", frameLimitEnabled.toString())
            NativeApp.setSetting("EmuCore/GS", "FramerateNTSC", "float", targetFps.toFloat().toString())
            NativeApp.setSetting("EmuCore/GS", "FrameratePAL", "float", targetFps.toFloat().toString())
            NativeApp.setSetting("EmuCore/Framerate", "NominalScalar", "float", "1.0")
            
            // Video / GS Settings (others)
            NativeApp.setSetting("EmuCore/GS", "FrameSkip", "int", frameSkip.toString())
            NativeApp.setSetting("EmuCore/GS", "filter", "int", textureFiltering.toString())
            NativeApp.setSetting("EmuCore/GS", "TriFilter", "int", trilinearFiltering.toString())
            NativeApp.setSetting("EmuCore/GS", "accurate_blending_unit", "int", blendingAccuracy.toString())
            NativeApp.setSetting("EmuCore/GS", "texture_preloading", "int", texturePreloading.toString())
            NativeApp.setSetting("EmuCore/GS", "fxaa", "bool", enableFxaa.toString())
            NativeApp.setSetting("EmuCore/GS", "CASMode", "int", casMode.toString())
            NativeApp.setSetting("EmuCore/GS", "CASSharpness", "int", casSharpness.toString())
            NativeApp.setSetting("EmuCore/GS", "MaxAnisotropy", "int", anisotropicFiltering.toString())
            NativeApp.setSetting("EmuCore/GS", "hw_mipmap", "bool", enableHwMipmapping.toString())
            NativeApp.setSetting("EmuCore", "EnableWideScreenPatches", "bool", widescreenPatches.toString())
            NativeApp.setSetting("EmuCore", "EnableNoInterlacingPatches", "bool", noInterlacingPatches.toString())
            val manualHardwareFixes = GsHackDefaults.shouldEnableManualHardwareFixes(
                cpuSpriteRenderSize = cpuSpriteRenderSize,
                cpuSpriteRenderLevel = cpuSpriteRenderLevel,
                softwareClutRender = softwareClutRender,
                gpuTargetClutMode = gpuTargetClutMode,
                skipDrawStart = skipDrawStart,
                skipDrawEnd = skipDrawEnd,
                autoFlushHardware = autoFlushHardware,
                cpuFramebufferConversion = cpuFramebufferConversion,
                disableDepthConversion = disableDepthConversion,
                disableSafeFeatures = disableSafeFeatures,
                disableRenderFixes = disableRenderFixes,
                preloadFrameData = preloadFrameData,
                disablePartialInvalidation = disablePartialInvalidation,
                textureInsideRt = textureInsideRt,
                readTargetsOnClose = readTargetsOnClose,
                estimateTextureRegion = estimateTextureRegion,
                gpuPaletteConversion = gpuPaletteConversion,
                halfPixelOffset = halfPixelOffset,
                nativeScaling = nativeScaling,
                roundSprite = roundSprite,
                bilinearUpscale = bilinearUpscale,
                textureOffsetX = textureOffsetX,
                textureOffsetY = textureOffsetY,
                alignSprite = alignSprite,
                mergeSprite = mergeSprite,
                forceEvenSpritePosition = forceEvenSpritePosition,
                nativePaletteDraw = nativePaletteDraw
            )
            NativeApp.setSetting("EmuCore/GS", "UserHacks", "bool", manualHardwareFixes.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderBW", "int", cpuSpriteRenderSize.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", "int", cpuSpriteRenderLevel.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_CPUCLUTRender", "int", softwareClutRender.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_GPUTargetCLUTMode", "int", gpuTargetClutMode.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_SkipDraw_Start", "int", skipDrawStart.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_SkipDraw_End", "int", skipDrawEnd.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_AutoFlushLevel", "int", autoFlushHardware.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_CPU_FB_Conversion", "bool", cpuFramebufferConversion.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_DisableDepthSupport", "bool", disableDepthConversion.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_Disable_Safe_Features", "bool", disableSafeFeatures.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_DisableRenderFixes", "bool", disableRenderFixes.toString())
            NativeApp.setSetting("EmuCore/GS", "preload_frame_with_gs_data", "bool", preloadFrameData.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_DisablePartialInvalidation", "bool", disablePartialInvalidation.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_TextureInsideRt", "int", textureInsideRt.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_ReadTCOnClose", "bool", readTargetsOnClose.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_EstimateTextureRegion", "bool", estimateTextureRegion.toString())
            NativeApp.setSetting("EmuCore/GS", "paltex", "bool", gpuPaletteConversion.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_HalfPixelOffset", "int", halfPixelOffset.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_native_scaling", "int", nativeScaling.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_round_sprite_offset", "int", roundSprite.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_BilinearHack", "int", bilinearUpscale.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_TCOffsetX", "int", textureOffsetX.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_TCOffsetY", "int", textureOffsetY.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_align_sprite_X", "bool", alignSprite.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_merge_pp_sprite", "bool", mergeSprite.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_ForceEvenSpritePosition", "bool", forceEvenSpritePosition.toString())
            NativeApp.setSetting("EmuCore/GS", "UserHacks_NativePaletteDraw", "bool", nativePaletteDraw.toString())

            // EmuCoreX Meta
            NativeApp.setSetting("EmuCoreX", "BiosSource", "string", biosPath.orEmpty())
            NativeApp.setSetting("EmuCoreX", "Renderer", "int", renderer.toString())
            NativeApp.setSetting("EmuCoreX", "UpscaleMultiplier", "int", upscaleMultiplier.toString())
            NativeApp.setSetting("EmuCoreX", "HasContext", "bool", (context.applicationContext != null).toString())

            // GPU Warnings & OSD
            NativeApp.setSetting("EmuCore", "WarnAboutUnsafeSettings", "bool", "false")
            NativeApp.setSetting("EmuCore/GS", "OsdMessagesPos", "int", "0")

            // GPU Driver
            if (gpuDriverType == 1 && !customDriverPath.isNullOrBlank()) {
                NativeApp.setCustomDriverPath(customDriverPath)
            } else {
                NativeApp.setCustomDriverPath("")
            }
        } catch (_: Exception) { }
    }

    // region VM Lifecycle
    fun startEmulation(path: String): Boolean {
        if (!isNativeLoaded) return false
        val pathType = when {
            path.startsWith("content://") -> "content"
            path.isBlank() -> "bios"
            else -> "file"
        }
        NativeApp.logCrashBreadcrumb(
            "startEmulation requested pathType=$pathType vmActive=$isVmActive"
        )
        
        // Ensure the previous native thread is fully destroyed
        var waitTime = 0
        while (isVmActive && waitTime < 5000) {
            try {
                Thread.sleep(50)
                waitTime += 50
            } catch (_: Exception) { break }
        }
        
        synchronized(nativeLock) {
            isVmActive = true
            shutdownRequested = false
        }
        
        val result = try {
            NativeApp.logCrashBreadcrumb("startEmulation entering native runVMThread")
            NativeApp.runVMThread(path)
        } catch (_: Exception) {
            NativeApp.logCrashBreadcrumb("startEmulation exception before native start returned")
            false
        }
        
        synchronized(nativeLock) {
            isVmActive = false
        }
        NativeApp.logCrashBreadcrumb("startEmulation finished result=$result")
        return result
    }

    fun pause() {
        if (!isNativeLoaded || !isVmActive) return
        try { NativeApp.pause() } catch (_: Exception) { }
    }

    fun resume() {
        if (!isNativeLoaded || !isVmActive) return
        try { NativeApp.resume() } catch (_: Exception) { }
    }

    fun shutdown() {
        synchronized(nativeLock) {
            if (!isNativeLoaded || !isVmActive || shutdownRequested) return
            shutdownRequested = true
            try {
                NativeApp.shutdown()
            } catch (_: Exception) { }
        }
    }

    fun hasValidVm(): Boolean {
        if (!isNativeLoaded) return false
        return try {
            isVmActive && NativeApp.hasValidVm()
        } catch (_: Exception) {
            false
        }
    }

    fun isVmActive(): Boolean = isVmActive
    // endregion

    // region Game Info
    fun getGameTitle(path: String): String {
        return getGameMetadata(path).title
    }

    fun getGameMetadata(path: String): GameMetadata {
        val context = getContext()
        var nativePath = path
        var fallbackTitle = File(path).nameWithoutExtension

        if (path.startsWith("content://") && context != null) {
            fallbackTitle = DocumentPathResolver.getDisplayName(context, path).substringBeforeLast('.')
            nativePath = path
        }

        if (!isNativeLoaded) {
            return GameMetadata(title = fallbackTitle)
        }

        return try {
            val rawTitle = NativeApp.getGameTitle(nativePath).orEmpty()
            val segments = rawTitle.split('|')
            GameMetadata(
                title = segments.getOrNull(0)?.takeIf { it.isNotBlank() } ?: fallbackTitle,
                serial = segments.getOrNull(1)?.takeIf { it.isNotBlank() },
                serialWithCrc = segments.getOrNull(2)?.takeIf { it.isNotBlank() }
            )
        } catch (_: Exception) {
            GameMetadata(title = fallbackTitle)
        }
    }

    // endregion

    // region Save States
    fun saveState(slot: Int): Boolean {
        if (!isNativeLoaded || !isVmActive) return false
        return try { NativeApp.saveStateToSlot(slot) } catch (_: Exception) { false }
    }

    fun loadState(slot: Int): Boolean {
        if (!isNativeLoaded || !isVmActive) return false
        return try {
            NativeApp.loadStateFromSlot(slot).also { success ->
                if (success) {
                    rebindSurface()
                    Thread.sleep(30)
                    NativeApp.resume()
                }
            }
        } catch (_: Exception) { false }
    }

    fun hasSaveStateForGame(path: String, slot: Int): Boolean {
        if (!isNativeLoaded) return false
        getContext()

        if (path.startsWith("/") && !File(path).exists()) return false

        val statePath = try {
            NativeApp.getSaveStatePathForFile(path, slot)
        } catch (_: Exception) {
            null
        } ?: return false

        return File(statePath).exists()
    }
    // endregion

    // region Rendering Settings
    fun setRenderer(gpuType: Int) {
        if (!isNativeLoaded) return
        try { 
            NativeApp.setSetting("EmuCore/GS", "Renderer", "int", gpuType.toString())
            NativeApp.renderGpu(gpuType) 
        } catch (_: Exception) { }
    }

    fun setUpscaleMultiplier(multiplier: Float) {
        if (!isNativeLoaded) return
        try { 
            NativeApp.setSetting("EmuCore/GS", "upscale_multiplier", "float", multiplier.toString())
            NativeApp.renderUpscalemultiplier(multiplier) 
            
            // Force renderer to re-initialize with new multiplier
            getSetting("EmuCore/GS", "Renderer", "int")?.toIntOrNull()?.let { 
                NativeApp.renderGpu(it) 
            }
        } catch (_: Exception) { }
    }

    fun setAspectRatio(type: Int) {
        if (!isNativeLoaded) return
        try { 
            NativeApp.setSetting(
                "EmuCore/GS",
                "AspectRatio",
                "string",
                aspectRatioSettingValues[type] ?: aspectRatioSettingValues.getValue(1)
            )
            NativeApp.setAspectRatio(type) 
            
            // Force renderer refresh
            getSetting("EmuCore/GS", "Renderer", "int")?.toIntOrNull()?.let { 
                NativeApp.renderGpu(it) 
            }
        } catch (_: Exception) { }
    }

    fun setCustomDriverPath(path: String) {
        if (!isNativeLoaded) return
        try { NativeApp.setCustomDriverPath(path) } catch (_: Exception) { }
    }

    fun setFrameLimitEnabled(enabled: Boolean) {
        if (!isNativeLoaded) return
        try {
            NativeApp.setSetting("EmuCore/GS", "FrameLimitEnable", "bool", enabled.toString())
        } catch (_: Exception) { }
    }

    fun setTargetFps(targetFps: Int) {
        if (!isNativeLoaded) return
        try {
            val clampedFps = targetFps.coerceIn(20, 120)
            NativeApp.setSetting("EmuCore/GS", "FramerateNTSC", "float", clampedFps.toFloat().toString())
            NativeApp.setSetting("EmuCore/GS", "FrameratePAL", "float", clampedFps.toFloat().toString())
            NativeApp.setSetting("EmuCore/Framerate", "NominalScalar", "float", "1.0")
        } catch (_: Exception) { }
    }
    // endregion

    // region Input
    fun setPadButton(index: Int, range: Int, pressed: Boolean) {
        if (!isNativeLoaded) return
        try { NativeApp.setPadButton(index, range, pressed) } catch (_: Exception) { }
    }

    fun setPadParams(indices: IntArray, values: FloatArray) {
        if (!isNativeLoaded) return
        try { NativeApp.setPadParams(indices, values) } catch (_: Exception) { }
    }

    fun resetKeyStatus() {
        if (!isNativeLoaded) return
        try { NativeApp.resetKeyStatus() } catch (_: Exception) { }
    }

    fun setPadVibration(enabled: Boolean) {
        if (!isNativeLoaded) return
        try { NativeApp.setPadVibration(enabled) } catch (_: Exception) { }
    }
    // endregion

    // region Surface
    fun onSurfaceCreated() {
        if (!isNativeLoaded) return
        NativeApp.setCrashContextString("emu_surface_state", "created")
        NativeApp.logCrashBreadcrumb("surfaceCreated")
        try { NativeApp.onNativeSurfaceCreated() } catch (_: Exception) { }
    }

    fun onSurfaceChanged(surface: Surface, width: Int, height: Int) {
        if (!isNativeLoaded) return
        lastSurface = surface
        lastSurfaceWidth = width
        lastSurfaceHeight = height
        NativeApp.setCrashContextString("emu_surface_state", "changed")
        NativeApp.setCrashContextInt("emu_surface_width", width)
        NativeApp.setCrashContextInt("emu_surface_height", height)
        NativeApp.setCrashContextBool("emu_surface_valid", surface.isValid)
        NativeApp.logCrashBreadcrumb("surfaceChanged width=$width height=$height valid=${surface.isValid}")
        try { NativeApp.onNativeSurfaceChanged(surface, width, height) } catch (_: Exception) { }
    }

    fun onSurfaceDestroyed() {
        if (!isNativeLoaded) return
        NativeApp.setCrashContextString("emu_surface_state", "destroyed")
        NativeApp.logCrashBreadcrumb("surfaceDestroyed")
        lastSurface = null
        lastSurfaceWidth = 0
        lastSurfaceHeight = 0
        try { NativeApp.onNativeSurfaceDestroyed() } catch (_: Exception) { }
    }
    // endregion

    private fun rebindSurface() {
        val surface = lastSurface ?: return
        val width = lastSurfaceWidth
        val height = lastSurfaceHeight
        if (!surface.isValid || width <= 0 || height <= 0) return
        NativeApp.logCrashBreadcrumb("rebindSurface width=$width height=$height")
        try { NativeApp.onNativeSurfaceChanged(surface, width, height) } catch (_: Exception) { }
    }

    // region Settings
    fun setSetting(section: String, key: String, type: String, value: String) {
        if (!isNativeLoaded) return
        val cacheKey = "$section:$key"
        if (settingsCache[cacheKey] == value) return

        try {
            NativeApp.setSetting(section, key, type, value)
            settingsCache[cacheKey] = value
        } catch (_: Exception) { }
    }

    fun getSetting(section: String, key: String, type: String): String? {
        if (!isNativeLoaded) return null
        return try { NativeApp.getSetting(section, key, type) } catch (_: Exception) { null }
    }

    // endregion
}

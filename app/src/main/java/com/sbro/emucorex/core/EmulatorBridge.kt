package com.sbro.emucorex.core

import android.content.Context
import android.view.Surface
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.lang.ref.WeakReference
import java.util.Locale

object EmulatorBridge {
    const val AUTO_RENDERER = -1
    const val VULKAN_RENDERER = 14

    private val aspectRatioSettingValues = mapOf(
        0 to "Stretch",
        1 to "Auto 4:3/3:2",
        2 to "4:3",
        3 to "16:9",
        4 to "10:7"
    )

    private val serialDispatcher = Dispatchers.IO.limitedParallelism(1)
    private val serialScope = CoroutineScope(SupervisorJob() + serialDispatcher)

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

    @Volatile
    private var surfaceEventVersion: Long = 0

    @Volatile
    private var shutdownRequested: Boolean = false

    private var contextRef: WeakReference<Context>? = null
    private val settingsCache = HashMap<String, String>()

    init {
        try {
            System.loadLibrary("emucore")
            isNativeLoaded = true
        } catch (_: UnsatisfiedLinkError) {
            isNativeLoaded = false
        }
    }

    private data class RuntimeOp(val kind: String, val fields: List<String>)

    private fun settingOp(section: String, key: String, type: String, value: String) =
        RuntimeOp("setting", listOf(section, key, type, value))

    private fun rendererOp(renderer: Int) = RuntimeOp("renderer", listOf(renderer.toString()))

    private fun upscaleOp(value: Float) = RuntimeOp("upscale", listOf(value.toString()))

    private fun aspectOp(type: Int) = RuntimeOp(
        "aspect",
        listOf(type.toString(), aspectRatioSettingValues[type] ?: aspectRatioSettingValues.getValue(1))
    )

    private fun customDriverOp(path: String) = RuntimeOp("custom_driver", listOf(path))

    private fun refreshBiosOp() = RuntimeOp("refresh_bios", emptyList())

    private fun resetTargetFpsOp() = RuntimeOp("reset_target_fps", emptyList())

    private fun memoryCardSlotOp(slot: Int, fileName: String?) = RuntimeOp(
        "memory_card_slot",
        listOf(slot.toString(), fileName.orEmpty())
    )

    private suspend fun <T> runSerial(block: () -> T): T = withContext(serialDispatcher) { block() }

    private fun launchSerial(block: suspend () -> Unit) {
        serialScope.launch { block() }
    }

    private suspend fun performRuntimeOps(ops: List<RuntimeOp>) {
        if (!isNativeLoaded || ops.isEmpty()) return
        runSerial {
            NativeApp.beginSettingsBatch()
            try {
                ops.forEach { op ->
                    when (op.kind) {
                        "setting" -> {
                            val section = op.fields.getOrNull(0) ?: return@forEach
                            val key = op.fields.getOrNull(1) ?: return@forEach
                            val type = op.fields.getOrNull(2) ?: return@forEach
                            val value = toCoreSettingValue(section, key, op.fields.getOrNull(3) ?: return@forEach)
                            NativeApp.setSetting(section, key, type, value)
                        }
                        "renderer" -> {
                            val renderer = op.fields.firstOrNull()?.toIntOrNull() ?: return@forEach
                            NativeApp.renderGpu(if (renderer == AUTO_RENDERER) 0 else renderer)
                        }
                        "upscale" -> {
                            val value = op.fields.firstOrNull()?.toFloatOrNull() ?: return@forEach
                            NativeApp.renderUpscalemultiplier(normalizeUpscale(value))
                        }
                        "aspect" -> {
                            val type = op.fields.firstOrNull()?.toIntOrNull() ?: return@forEach
                            NativeApp.setAspectRatio(type)
                        }
                        "custom_driver" -> {
                            NativeApp.setCustomDriverPath(op.fields.firstOrNull().orEmpty())
                        }
                        "refresh_bios" -> {
                            NativeApp.refreshBIOS()
                        }
                        "reset_target_fps" -> {
                            NativeApp.setSetting("EmuCore/GS", "FramerateNTSC", "float", "59.94")
                            NativeApp.setSetting("EmuCore/GS", "FrameratePAL", "float", "50.0")
                        }
                        "memory_card_slot" -> {
                            val slot = op.fields.getOrNull(0)?.toIntOrNull() ?: return@forEach
                            val slotIndex = slot.coerceIn(1, 2)
                            val fileName = op.fields.getOrNull(1).orEmpty()
                            val hasCard = fileName.isNotBlank()
                            NativeApp.setSetting("MemoryCards", "Slot${slotIndex}_Enable", "bool", hasCard.toString())
                            NativeApp.setSetting("MemoryCards", "Slot${slotIndex}_Filename", "string", fileName)
                        }
                    }
                }
            } finally {
                NativeApp.endSettingsBatch()
            }
        }
    }

    private fun rendererName(renderer: Int): String = when (renderer) {
        AUTO_RENDERER -> "Vulkan"
        0 -> "Vulkan"
        12 -> "OpenGL"
        13 -> "Software"
        VULKAN_RENDERER -> "Vulkan"
        15 -> "D3D12"
        3 -> "D3D11"
        else -> "Unknown($renderer)"
    }

    private fun normalizeRenderer(renderer: Int): Int {
        return if (renderer <= 0) VULKAN_RENDERER else renderer
    }

    private fun toCoreSettingValue(section: String, key: String, value: String): String {
        if (section == "EmuCore/GS" && key == "TriFilter") {
            return when (value.toIntOrNull()) {
                0 -> "-1"
                else -> value
            }
        }
        return value
    }

    private fun fromCoreSettingValue(section: String, key: String, value: String?): String? {
        if (value == null) return null
        if (section == "EmuCore/GS" && key == "TriFilter") {
            return when (value.toIntOrNull()) {
                -1 -> "0"
                else -> value
            }
        }
        return value
    }

    fun initializeOnce(context: Context) {
        contextRef = WeakReference(context)
        if (!isNativeLoaded) return

        try {
            NativeApp.setNativeLibraryDir(context.applicationInfo.nativeLibraryDir ?: "")
            NativeApp.initializeOnce(context.applicationContext)
            val preferEnglishTitles = runBlocking {
                AppPreferences(context.applicationContext).preferEnglishGameTitles.first()
            }
            NativeApp.setSetting("UI", "PreferEnglishGameTitles", "bool", preferEnglishTitles.toString())
        } catch (_: Exception) { }
    }

    fun getContext(): Context? = contextRef?.get()

    suspend fun applyRuntimeConfig(
        biosPath: String?,
        renderer: Int,
        upscaleMultiplier: Float,
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
        targetFps: Int = 0,
        textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
        trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
        blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
        texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
        enableFxaa: Boolean = false,
        casMode: Int = 0,
        casSharpness: Int = 50,
        anisotropicFiltering: Int = 0,
        enableHwMipmapping: Boolean = GsHackDefaults.HW_MIPMAPPING_DEFAULT,
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
        nativePaletteDraw: Boolean = false,
        memoryCardSlot1: String? = null,
        memoryCardSlot2: String? = null,
        fpuClampMode: Int = 1,
        disableHardwareReadbacks: Boolean = false,
        fpuCorrectAddSub: Boolean = true
    ) {
        if (!isNativeLoaded) return

        val context = getContext() ?: return
        val resolvedRenderer = normalizeRenderer(renderer)
        val resolvedBiosPath = DocumentPathResolver.prepareBiosDirectory(context, biosPath)
            ?: biosPath?.let(DocumentPathResolver::resolveDirectoryPath)
        val preferredBiosFile = DocumentPathResolver.findPreferredBiosFileName(resolvedBiosPath)
        val savestatesDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "sstates").apply { mkdirs() }
        val memcardsDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "memcards").apply { mkdirs() }
        val cheatsDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "cheats").apply { mkdirs() }
        val patchesDir = File(context.getExternalFilesDir(null) ?: context.filesDir, "patches").apply { mkdirs() }
        val logDir = EmulatorStorage.logDir(context)
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

        NativeApp.setCrashContextString("emu_renderer_name", rendererName(resolvedRenderer))
        NativeApp.setCrashContextString("emu_gpu_driver_mode", if (gpuDriverType == 1) "custom" else "system")
        NativeApp.logCrashBreadcrumb(
            "applyRuntimeConfig renderer=${rendererName(resolvedRenderer)}($resolvedRenderer) driverType=$gpuDriverType hwDownload=$hwDownloadMode mtvu=$mtvu fastCdvd=$fastCdvd"
        )

        performRuntimeOps(
            buildList {
                add(rendererOp(resolvedRenderer))
                add(upscaleOp(upscaleMultiplier))
                add(aspectOp(aspectRatio))
                add(settingOp("Folders", "Bios", "string", resolvedBiosPath.orEmpty()))
                add(settingOp("Folders", "Savestates", "string", savestatesDir.absolutePath))
                add(settingOp("Folders", "MemoryCards", "string", memcardsDir.absolutePath))
                add(settingOp("Folders", "Cheats", "string", cheatsDir.absolutePath))
                add(settingOp("Folders", "Patches", "string", patchesDir.absolutePath))
                add(settingOp("Folders", "Logs", "string", logDir.absolutePath))
                add(memoryCardSlotOp(1, memoryCardSlot1))
                add(memoryCardSlotOp(2, memoryCardSlot2))
                add(settingOp("Filenames", "BIOS", "string", preferredBiosFile.orEmpty()))
                add(refreshBiosOp())
                add(settingOp("EmuCoreX", "OpenGLTextureDebugLog", "bool", (resolvedRenderer == 12).toString()))
                add(settingOp("EmuCore/Speedhacks", "vuThread", "bool", mtvu.toString()))
                add(settingOp("EmuCore/Speedhacks", "fastCDVD", "bool", fastCdvd.toString()))
                add(settingOp("EmuCore", "EnableCheats", "bool", enableCheats.toString()))
                add(settingOp("EmuCore/GS", "HWDownloadMode", "int", hwDownloadMode.toString()))
                add(settingOp("EmuCore/Speedhacks", "EECycleRate", "int", eeCycleRate.toString()))
                add(settingOp("EmuCore/Speedhacks", "EECycleSkip", "int", eeCycleSkip.toString()))
                add(settingOp("EmuCore/GS", "FrameLimitEnable", "bool", frameLimitEnabled.toString()))
                addAll(targetFpsOps(targetFps))
                add(settingOp("EmuCore/Framerate", "NominalScalar", "float", "1.0"))
                add(settingOp("EmuCore/CPU/Recompiler", "FPUClampMode", "int", fpuClampMode.toString()))
                add(settingOp("EmuCore/GS", "disable_hw_readbacks", "bool", disableHardwareReadbacks.toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "fpuCorrectAddSub", "bool", fpuCorrectAddSub.toString()))
                add(settingOp("EmuCore/GS", "FrameSkip", "int", frameSkip.toString()))
                add(settingOp("EmuCore/GS", "filter", "int", textureFiltering.toString()))
                add(settingOp("EmuCore/GS", "TriFilter", "int", trilinearFiltering.toString()))
                add(settingOp("EmuCore/GS", "accurate_blending_unit", "int", blendingAccuracy.toString()))
                add(settingOp("EmuCore/GS", "texture_preloading", "int", texturePreloading.toString()))
                add(settingOp("EmuCore/GS", "fxaa", "bool", enableFxaa.toString()))
                add(settingOp("EmuCore/GS", "CASMode", "int", casMode.toString()))
                add(settingOp("EmuCore/GS", "CASSharpness", "int", casSharpness.toString()))
                add(settingOp("EmuCore/GS", "MaxAnisotropy", "int", anisotropicFiltering.toString()))
                add(settingOp("EmuCore/GS", "hw_mipmap", "bool", enableHwMipmapping.toString()))
                add(settingOp("EmuCore", "EnableWideScreenPatches", "bool", widescreenPatches.toString()))
                add(settingOp("EmuCore", "EnableNoInterlacingPatches", "bool", noInterlacingPatches.toString()))
                add(settingOp("EmuCore/GS", "UserHacks", "bool", manualHardwareFixes.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_CPUSpriteRenderBW", "int", cpuSpriteRenderSize.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", "int", cpuSpriteRenderLevel.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_CPUCLUTRender", "int", softwareClutRender.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_GPUTargetCLUTMode", "int", gpuTargetClutMode.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_SkipDraw_Start", "int", skipDrawStart.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_SkipDraw_End", "int", skipDrawEnd.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_AutoFlushLevel", "int", autoFlushHardware.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_CPU_FB_Conversion", "bool", cpuFramebufferConversion.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_DisableDepthSupport", "bool", disableDepthConversion.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_Disable_Safe_Features", "bool", disableSafeFeatures.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_DisableRenderFixes", "bool", disableRenderFixes.toString()))
                add(settingOp("EmuCore/GS", "preload_frame_with_gs_data", "bool", preloadFrameData.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_DisablePartialInvalidation", "bool", disablePartialInvalidation.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_TextureInsideRt", "int", textureInsideRt.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_ReadTCOnClose", "bool", readTargetsOnClose.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_EstimateTextureRegion", "bool", estimateTextureRegion.toString()))
                add(settingOp("EmuCore/GS", "paltex", "bool", gpuPaletteConversion.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_HalfPixelOffset", "int", halfPixelOffset.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_native_scaling", "int", nativeScaling.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_round_sprite_offset", "int", roundSprite.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_BilinearHack", "int", bilinearUpscale.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_TCOffsetX", "int", textureOffsetX.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_TCOffsetY", "int", textureOffsetY.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_align_sprite_X", "bool", alignSprite.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_merge_pp_sprite", "bool", mergeSprite.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_ForceEvenSpritePosition", "bool", forceEvenSpritePosition.toString()))
                add(settingOp("EmuCore/GS", "UserHacks_NativePaletteDraw", "bool", nativePaletteDraw.toString()))
                add(settingOp("EmuCoreX", "BiosSource", "string", biosPath.orEmpty()))
                add(settingOp("EmuCoreX", "Renderer", "int", resolvedRenderer.toString()))
                add(settingOp("EmuCoreX", "UpscaleMultiplier", "float", upscaleMultiplier.toString()))
                add(settingOp("EmuCoreX", "HasContext", "bool", (context.applicationContext != null).toString()))
                add(settingOp("EmuCore", "WarnAboutUnsafeSettings", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdMessagesPos", "int", "0"))
                add(customDriverOp(if (gpuDriverType == 1) customDriverPath.orEmpty() else ""))
            }
        )
    }

    suspend fun setMemoryCardAssignments(slot1: String?, slot2: String?) {
        performRuntimeOps(
            listOf(
                memoryCardSlotOp(1, slot1),
                memoryCardSlotOp(2, slot2)
            )
        )
    }

    suspend fun startEmulation(path: String): Boolean {
        if (!isNativeLoaded) return false
        val pathType = when {
            path.startsWith("content://") -> "content"
            path.isBlank() -> "bios"
            else -> "file"
        }
        NativeApp.logCrashBreadcrumb("startEmulation requested pathType=$pathType vmActive=$isVmActive")

        return withContext(Dispatchers.IO) {
            isVmActive = true
            shutdownRequested = false
            val result = try {
                NativeApp.logCrashBreadcrumb("startEmulation entering native runVMThread")
                NativeApp.runVMThread(path)
            } catch (_: Exception) {
                NativeApp.logCrashBreadcrumb("startEmulation exception before native start returned")
                false
            }
            isVmActive = false
            NativeApp.logCrashBreadcrumb("startEmulation finished result=$result")
            result
        }
    }

    suspend fun pause() {
        if (!isNativeLoaded || !isVmActive) return
        runSerial {
            try {
                NativeApp.pause()
            } catch (_: Exception) { }
        }
    }

    suspend fun resume() {
        if (!isNativeLoaded || !isVmActive) return
        runSerial {
            try {
                rebindSurface()
                NativeApp.resume()
            } catch (_: Exception) { }
        }
    }

    suspend fun shutdown() {
        if (!isNativeLoaded || !isVmActive || shutdownRequested) return
        runSerial {
            if (!isVmActive || shutdownRequested) return@runSerial
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

    fun getGameTitle(path: String): String = getGameMetadata(path).title

    fun getGameMetadata(path: String): GameMetadata {
        val inferredMetadata = when {
            path.startsWith("content://") -> {
                val context = getContext()
                val displayName = context?.let { DocumentPathResolver.getDisplayName(it, path) } ?: path
                parseMetadataFromName(displayName)
            }
            else -> parseMetadataFromName(File(path).nameWithoutExtension)
        }

        if (!isNativeLoaded) return inferredMetadata

        return try {
            val rawTitle = NativeApp.getGameTitle(path).orEmpty()
            val segments = rawTitle.split('|')
            GameMetadata(
                title = segments.getOrNull(0)?.takeIf { it.isNotBlank() } ?: inferredMetadata.title,
                serial = segments.getOrNull(1)?.takeIf { it.isNotBlank() } ?: inferredMetadata.serial,
                serialWithCrc = segments.getOrNull(2)?.takeIf { it.isNotBlank() } ?: inferredMetadata.serialWithCrc
            )
        } catch (_: Exception) {
            inferredMetadata
        }
    }

    fun parseMetadataFromName(rawName: String): GameMetadata {
        val cleanName = rawName.substringBeforeLast('.').trim()
        val serial = extractSerialFromName(cleanName)
        val title = cleanName
            .replace(Regex("""(?i)\b([A-Z]{4})[-_. ]?(\d{3})[-_. ]?(\d{2})\b"""), " ")
            .replace(Regex("""(?i)\b([A-Z]{4})[-_. ]?(\d{5})\b"""), " ")
            .replace(Regex("""\[[^]]*]|\([^)]*\)"""), " ")
            .replace(Regex("""\b(disc|disk|cd|dvd)\s*\d+\b""", RegexOption.IGNORE_CASE), " ")
            .replace('_', ' ')
            .replace(Regex("""\s+"""), " ")
            .trim()
            .ifBlank { cleanName }
        return GameMetadata(title = title, serial = serial, serialWithCrc = serial)
    }

    private fun extractSerialFromName(value: String): String? {
        val normalized = value.uppercase(Locale.ROOT)
        val fullPattern = Regex("""\b([A-Z]{4})[-_. ]?(\d{3})[-_. ]?(\d{2})\b""")
        val compactPattern = Regex("""\b([A-Z]{4})[-_. ]?(\d{5})\b""")
        return fullPattern.find(normalized)?.let { match ->
            "${match.groupValues[1]}-${match.groupValues[2]}${match.groupValues[3]}"
        } ?: compactPattern.find(normalized)?.let { match ->
            "${match.groupValues[1]}-${match.groupValues[2]}"
        }
    }

    suspend fun saveState(slot: Int): Boolean {
        if (!isNativeLoaded || !isVmActive) return false
        return runSerial {
            try {
                NativeApp.saveStateToSlot(slot)
            } catch (_: Exception) {
                false
            }
        }
    }

    suspend fun loadState(slot: Int): Boolean {
        if (!isNativeLoaded || !isVmActive) return false
        return runSerial {
            try {
                NativeApp.loadStateFromSlot(slot).also { success ->
                    if (success) {
                        rebindSurface()
                        NativeApp.resume()
                    }
                }
            } catch (_: Exception) {
                false
            }
        }
    }

    fun hasSaveStateForGame(path: String, slot: Int): Boolean {
        if (!isNativeLoaded) return false
        if (path.startsWith("/") && !File(path).exists()) return false
        val statePath = try {
            NativeApp.getSaveStatePathForFile(path, slot)
        } catch (_: Exception) {
            null
        } ?: return false
        return File(statePath).exists()
    }

    suspend fun setRenderer(gpuType: Int) {
        val resolvedRenderer = normalizeRenderer(gpuType)
        settingsCache["EmuCore/GS:Renderer"] = resolvedRenderer.toString()
        performRuntimeOps(listOf(rendererOp(resolvedRenderer), settingOp("EmuCore/GS", "Renderer", "int", resolvedRenderer.toString())))
    }

    suspend fun setUpscaleMultiplier(multiplier: Float) {
        val normalized = normalizeUpscale(multiplier)
        settingsCache["EmuCore/GS:upscale_multiplier"] = normalized.toString()
        performRuntimeOps(listOf(upscaleOp(normalized), settingOp("EmuCore/GS", "upscale_multiplier", "float", normalized.toString())))
    }

    suspend fun setAspectRatio(type: Int) {
        val value = aspectRatioSettingValues[type] ?: aspectRatioSettingValues.getValue(1)
        settingsCache["EmuCore/GS:AspectRatio"] = value
        performRuntimeOps(listOf(aspectOp(type)))
    }

    suspend fun setCustomDriverPath(path: String) {
        settingsCache["EmuCore/GS:CustomDriverPath"] = path
        performRuntimeOps(listOf(customDriverOp(path)))
    }

    suspend fun setFrameLimitEnabled(enabled: Boolean) {
        setSetting("EmuCore/GS", "FrameLimitEnable", "bool", enabled.toString())
    }

    suspend fun setTargetFps(targetFps: Int) {
        performRuntimeOps(
            buildList {
                addAll(targetFpsOps(targetFps))
                add(settingOp("EmuCore/Framerate", "NominalScalar", "float", "1.0"))
            }
        )
    }

    fun setPadButton(index: Int, range: Int, pressed: Boolean) {
        if (!isNativeLoaded) return
        try {
            NativeApp.setPadButton(index, range, pressed)
        } catch (_: Exception) { }
    }

    fun resetKeyStatus() {
        if (!isNativeLoaded) return
        launchSerial {
            try {
                NativeApp.resetKeyStatus()
            } catch (_: Exception) { }
        }
    }

    suspend fun setPadVibration(enabled: Boolean) {
        setSetting("InputSources", "PadVibration", "bool", enabled.toString())
    }

    fun onSurfaceCreated() {
        if (!isNativeLoaded) return
        NativeApp.setCrashContextString("emu_surface_state", "created")
        NativeApp.logCrashBreadcrumb("surfaceCreated")
        launchSerial {
            try {
                NativeApp.onNativeSurfaceCreated()
            } catch (_: Exception) { }
        }
    }

    fun onSurfaceChanged(surface: Surface, width: Int, height: Int) {
        if (!isNativeLoaded) return
        val eventVersion = ++surfaceEventVersion
        lastSurface = surface
        lastSurfaceWidth = width
        lastSurfaceHeight = height
        NativeApp.setCrashContextString("emu_surface_state", "changed")
        NativeApp.setCrashContextInt("emu_surface_width", width)
        NativeApp.setCrashContextInt("emu_surface_height", height)
        NativeApp.setCrashContextBool("emu_surface_valid", surface.isValid)
        NativeApp.logCrashBreadcrumb("surfaceChanged width=$width height=$height valid=${surface.isValid}")
        launchSerial {
            if (surfaceEventVersion != eventVersion) return@launchSerial
            try {
                NativeApp.onNativeSurfaceChanged(surface, width, height)
            } catch (_: Exception) { }
        }
    }

    fun onSurfaceDestroyed() {
        if (!isNativeLoaded) return
        val eventVersion = ++surfaceEventVersion
        NativeApp.setCrashContextString("emu_surface_state", "destroyed")
        NativeApp.logCrashBreadcrumb("surfaceDestroyed")
        launchSerial {
            delay(250)
            if (surfaceEventVersion != eventVersion) return@launchSerial
            lastSurface = null
            lastSurfaceWidth = 0
            lastSurfaceHeight = 0
            try {
                NativeApp.onNativeSurfaceDestroyed()
            } catch (_: Exception) { }
        }
    }

    private fun rebindSurface() {
        val surface = lastSurface ?: return
        val width = lastSurfaceWidth
        val height = lastSurfaceHeight
        if (!surface.isValid || width <= 0 || height <= 0) return
        NativeApp.logCrashBreadcrumb("rebindSurface width=$width height=$height")
        try {
            NativeApp.onNativeSurfaceChanged(surface, width, height)
        } catch (_: Exception) { }
    }

    suspend fun setSetting(section: String, key: String, type: String, value: String) {
        if (!isNativeLoaded) return
        val cacheKey = "$section:$key"
        if (settingsCache[cacheKey] == value) return
        performRuntimeOps(listOf(settingOp(section, key, type, value)))
        settingsCache[cacheKey] = value
    }

    fun getSetting(section: String, key: String, type: String): String? {
        if (!isNativeLoaded) return null
        return try {
            fromCoreSettingValue(section, key, NativeApp.getSetting(section, key, type))
        } catch (_: Exception) {
            null
        }
    }

    private fun targetFpsOps(targetFps: Int): List<RuntimeOp> {
        if (targetFps <= 0) {
            return listOf(resetTargetFpsOp())
        }

        val manualFps = targetFps.coerceIn(20, 120).toFloat()
        return listOf(
            settingOp("EmuCore/GS", "FramerateNTSC", "float", manualFps.toString()),
            settingOp("EmuCore/GS", "FrameratePAL", "float", manualFps.toString())
        )
    }
}

package com.sbro.emucorex.core

import android.content.Context
import android.os.ParcelFileDescriptor
import android.util.Log
import android.view.Surface
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.core.utils.NetworkAdapterCollector
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.lang.ref.WeakReference
import java.util.Locale
import kotlin.time.Duration.Companion.milliseconds

object EmulatorBridge {
    private const val TAG = "EmulatorBridge"
    const val AUTO_RENDERER = RendererDefaults.AUTO
    const val OPENGL_RENDERER = RendererDefaults.OPENGL
    const val VULKAN_RENDERER = RendererDefaults.VULKAN
    private const val ANGLE_EGL_LIBRARY_NAME = "libEGL_angle.so"
    private const val ANGLE_GLES_LIBRARY_NAME = "libGLESv2_angle.so"
    private const val BOOT_SMOKE_PROBE_STEPS = 67_108_864
    private const val AUTO_PROGRESSIVE_SCAN_PAD_INDEX = 0
    private const val AUTO_PROGRESSIVE_SCAN_CROSS = 96
    private const val AUTO_PROGRESSIVE_SCAN_TRIANGLE = 100
    // Games probe the boot combo at different points; Criterion titles can do so well after
    // the PS2 logo. Keep it held through the boot sequence, matching the real-console action.
    private const val AUTO_PROGRESSIVE_SCAN_HOLD_MS = 30_000L

    private val aspectRatioSettingValues = mapOf(
        0 to "Stretch",
        1 to "Auto 4:3/3:2",
        2 to "4:3",
        3 to "16:9",
        4 to "10:7"
    )

    private val serialDispatcher = Dispatchers.IO.limitedParallelism(1)
    private val serialScope = CoroutineScope(SupervisorJob() + serialDispatcher)
    private val inputScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

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

    @Volatile
    private var autoProgressiveScanJob: Job? = null

    private var contextRef: WeakReference<Context>? = null
    private val settingsCache = HashMap<String, String>()

    init {
        isNativeLoaded = NativeApp.hasNativeCore
        if (isNativeLoaded) {
            Log.i(TAG, "${NativeApp.loadedCoreLibraryName} is ready")
        } else {
            Log.e(TAG, "${NativeApp.loadedCoreLibraryName} is unavailable")
        }
    }

    private data class RuntimeOp(val kind: String, val fields: List<String>)
    private data class PreparedMetadataPath(
        val path: String,
        val descriptor: ParcelFileDescriptor? = null
    )

    private fun settingOp(section: String, key: String, type: String, value: String) =
        RuntimeOp("setting", listOf(section, key, type, value))

    private fun upscaleOp(value: Float) = RuntimeOp("upscale", listOf(value.toString()))

    private fun frameSkipOp(value: Int) = RuntimeOp("frame_skip", listOf(value.coerceIn(0, 4).toString()))

    private fun normalizeAspectRatio(type: Int): Int {
        return if (type in aspectRatioSettingValues.keys) type else 1
    }

    private fun aspectOp(type: Int) = RuntimeOp(
        "aspect",
        normalizeAspectRatio(type).let { listOf(it.toString(), aspectRatioSettingValues.getValue(it)) }
    )

    private fun customDriverOp(path: String) = RuntimeOp("custom_driver", listOf(path))

    private fun prepareCustomDriverLibrary(path: String): String {
        if (path.isBlank()) return ""
        val file = File(path)
        if (file.isFile) {
            file.setReadable(true, true)
            file.setWritable(true, true)
            file.setExecutable(true, true)
        }
        return path
    }

    fun isBundledAngleAvailable(): Boolean {
        val context = getContext() ?: return false
        val nativeLibDir = context.applicationInfo.nativeLibraryDir ?: return false
        return File(nativeLibDir, ANGLE_EGL_LIBRARY_NAME).isFile &&
            File(nativeLibDir, ANGLE_GLES_LIBRARY_NAME).isFile
    }

    private fun shouldUseMediatekAngleOpenGl(
        requested: Boolean,
        gpuHardwareProfile: Int,
        renderer: Int
    ): Boolean {
        return requested &&
            renderer == OPENGL_RENDERER &&
            GpuHardwareProfiles.isMediatekProfile(gpuHardwareProfile) &&
            isBundledAngleAvailable()
    }

    private fun refreshBiosOp() = RuntimeOp("refresh_bios", emptyList())

    private fun regionFramerateOps(ntscFramerate: Float, palFramerate: Float): List<RuntimeOp> {
        val ntsc = sanitizeFramerate(ntscFramerate, AppPreferences.DEFAULT_NTSC_FRAMERATE)
        val pal = sanitizeFramerate(palFramerate, AppPreferences.DEFAULT_PAL_FRAMERATE)
        return listOf(
            settingOp("EmuCore/GS", "FramerateNTSC", "float", ntsc.toString()),
            settingOp("EmuCore/GS", "FrameratePAL", "float", pal.toString())
        )
    }

    private fun sanitizeFloatRoundMode(value: Int, fallback: Int): Int {
        return if (value in AppPreferences.FLOAT_ROUND_NEAREST..AppPreferences.FLOAT_ROUND_CHOP) value else fallback
    }

    private fun sanitizeClampingMode(value: Int, fallback: Int): Int {
        return if (value in AppPreferences.CLAMPING_NONE..AppPreferences.CLAMPING_FULL) value else fallback
    }

    private fun eeFpuClampingOps(value: Int): List<RuntimeOp> = listOf(
        settingOp("EmuCoreX/CPU", "EEClampMode", "int", value.toString()),
        settingOp("EmuCore/CPU/Recompiler", "fpuOverflow", "bool", (value >= AppPreferences.CLAMPING_NORMAL).toString()),
        settingOp("EmuCore/CPU/Recompiler", "fpuExtraOverflow", "bool", (value >= AppPreferences.CLAMPING_EXTRA).toString()),
        settingOp("EmuCore/CPU/Recompiler", "fpuFullMode", "bool", (value >= AppPreferences.CLAMPING_FULL).toString())
    )

    private fun vuClampingOps(vu0Value: Int, vu1Value: Int): List<RuntimeOp> = buildList {
        add(settingOp("EmuCoreX/CPU", "VU0ClampMode", "int", vu0Value.toString()))
        add(settingOp("EmuCoreX/CPU", "VU1ClampMode", "int", vu1Value.toString()))
        add(vuClampingFlagOp("vu0", vu0Value, "Overflow", AppPreferences.CLAMPING_NORMAL))
        add(vuClampingFlagOp("vu0", vu0Value, "ExtraOverflow", AppPreferences.CLAMPING_EXTRA))
        add(vuClampingFlagOp("vu0", vu0Value, "SignOverflow", AppPreferences.CLAMPING_FULL))
        add(vuClampingFlagOp("vu1", vu1Value, "Overflow", AppPreferences.CLAMPING_NORMAL))
        add(vuClampingFlagOp("vu1", vu1Value, "ExtraOverflow", AppPreferences.CLAMPING_EXTRA))
        add(vuClampingFlagOp("vu1", vu1Value, "SignOverflow", AppPreferences.CLAMPING_FULL))
    }

    private fun vuClampingFlagOp(prefix: String, value: Int, suffix: String, threshold: Int): RuntimeOp {
        return settingOp("EmuCore/CPU/Recompiler", "$prefix$suffix", "bool", (value >= threshold).toString())
    }

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
                            val value = op.fields.getOrNull(3) ?: return@forEach
                            NativeApp.setSetting(section, key, type, value)
                        }
                        "upscale" -> {
                            val value = op.fields.firstOrNull()?.toFloatOrNull() ?: return@forEach
                            NativeApp.renderUpscalemultiplier(normalizeUpscale(value))
                        }
                        "frame_skip" -> {
                            val value = op.fields.firstOrNull()?.toIntOrNull() ?: return@forEach
                            NativeApp.setFrameSkip(value.coerceIn(0, 4))
                        }
                        "aspect" -> {
                            val type = op.fields.firstOrNull()?.toIntOrNull() ?: return@forEach
                            NativeApp.setAspectRatio(type)
                            NativeApp.setSetting(
                                "EmuCore/GS",
                                "AspectRatio",
                                "string",
                                op.fields.getOrNull(1) ?: aspectRatioSettingValues.getValue(1)
                            )
                        }
                        "custom_driver" -> {
                            NativeApp.setCustomDriverPath(op.fields.firstOrNull().orEmpty())
                        }
                        "refresh_bios" -> {
                            NativeApp.refreshBIOS()
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
        AUTO_RENDERER -> "OpenGL"
        0 -> "OpenGL"
        OPENGL_RENDERER -> "OpenGL"
        13 -> "Software"
        VULKAN_RENDERER -> "Vulkan"
        15 -> "D3D12"
        3 -> "D3D11"
        else -> "Unknown($renderer)"
    }

    @Suppress("DEPRECATION")
    private fun appVersionName(context: Context): String {
        return runCatching {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        }.getOrNull()?.takeIf { it.isNotBlank() } ?: "0.0.0"
    }

    internal fun normalizeRenderer(renderer: Int): Int {
        return RendererDefaults.normalizeAndroidRenderer(renderer)
    }

    fun getMaxUpscaleMultiplier(renderer: Int): Int {
        if (!isNativeLoaded) return UPSCALE_MAX.toInt()
        val resolvedRenderer = normalizeRenderer(renderer)
        return runCatching { NativeApp.getMaxUpscaleMultiplier(resolvedRenderer) }
            .getOrDefault(UPSCALE_MAX.toInt())
            .coerceAtLeast(UPSCALE_MIN.toInt())
    }

    fun initializeOnce(context: Context) {
        contextRef = WeakReference(context)
        if (!isNativeLoaded) {
            Log.e(TAG, "initializeOnce skipped: native library is not loaded")
            return
        }

        try {
            NativeApp.setNativeLibraryDir(context.applicationInfo.nativeLibraryDir ?: "")
            NativeApp.initializeOnce(context.applicationContext)
            NativeApp.setSetting("EmuCoreX", "AppVersion", "string", appVersionName(context.applicationContext))
            val jitSmokeOk = runCatching { NativeApp.runJitExecutableMemorySmokeTest() }.getOrDefault(false)
            Log.i(TAG, "JIT executable-memory smoke result=$jitSmokeOk")
            val preferEnglishTitles = runBlocking {
                AppPreferences(context.applicationContext).preferEnglishGameTitles.first()
            }
            NativeApp.setSetting("UI", "PreferEnglishGameTitles", "bool", preferEnglishTitles.toString())
            NetworkAdapterCollector.collectAdapters(context.applicationContext)
            Log.i(TAG, "initializeOnce completed")
        } catch (error: Exception) {
            Log.e(TAG, "initializeOnce failed", error)
            CrashLogger.logError(TAG, "initializeOnce FAILED", error)
        }
    }

    fun getContext(): Context? = contextRef?.get()

    suspend fun applyRuntimeConfig(
        biosPath: String?,
        emulatorDataPath: String? = null,
        renderer: Int,
        upscaleMultiplier: Float,
        gpuDriverType: Int = 0,
        customDriverPath: String? = null,
        gpuHardwareProfile: Int = GpuHardwareProfiles.ADRENO,
        mediatekAngleOpenGl: Boolean = false,
        aspectRatio: Int = 1,
        audioVolume: Int = AudioDefaults.VOLUME_DEFAULT,
        audioFastForwardVolume: Int = AudioDefaults.VOLUME_DEFAULT,
        audioMuted: Boolean = false,
        audioInterpolation: Int = AudioDefaults.INTERPOLATION_DEFAULT,
        audioSyncMode: Int = AudioDefaults.SYNC_DEFAULT,
        audioLightweightSpu2: Boolean = AudioDefaults.LIGHTWEIGHT_SPU2_DEFAULT,
        audioBackend: Int = AudioDefaults.BACKEND_DEFAULT,
        audioBufferMs: Int = AudioDefaults.BUFFER_MS_DEFAULT,
        audioOutputLatencyMs: Int = AudioDefaults.OUTPUT_LATENCY_MS_DEFAULT,
        audioMinimalOutputLatency: Boolean = AudioDefaults.MINIMAL_OUTPUT_LATENCY_DEFAULT,
        enableEeRecompiler: Boolean = true,
        enableIopRecompiler: Boolean = true,
        enableVu0Recompiler: Boolean = true,
        enableVu1Recompiler: Boolean = true,
        eeFpuRoundMode: Int = AppPreferences.DEFAULT_EE_FPU_ROUND_MODE,
        vu0RoundMode: Int = AppPreferences.DEFAULT_VU_ROUND_MODE,
        vu1RoundMode: Int = AppPreferences.DEFAULT_VU_ROUND_MODE,
        eeFpuClampingMode: Int = AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE,
        vu0ClampingMode: Int = AppPreferences.DEFAULT_VU0_CLAMPING_MODE,
        vu1ClampingMode: Int = AppPreferences.DEFAULT_VU1_CLAMPING_MODE,
        enableGameFixes: Boolean = true,
        eeTimingHack: Boolean = false,
        enableFastmem: Boolean = true,
        waitLoopSpeedhack: Boolean = true,
        intcStatSpeedhack: Boolean = true,
        vuFlagHack: Boolean = true,
        instantVu1: Boolean = true,
        mtvu: Boolean = true,
        enableThreadPinning: Boolean = AppPreferences.DEFAULT_THREAD_PINNING,
        enableFastBoot: Boolean = true,
        fastCdvd: Boolean = false,
        enableCheats: Boolean = false,
        hwDownloadMode: Int = GsHackDefaults.HW_DOWNLOAD_MODE_DEFAULT,
        eeCycleRate: Int = 0,
        eeCycleSkip: Int = 0,
        frameSkip: Int = 0,
        skipDuplicateFrames: Boolean = true,
        frameLimitEnabled: Boolean = true,
        vSyncEnabled: Boolean = false,
        fastForwardSpeed: Float = AppPreferences.DEFAULT_FAST_FORWARD_SPEED,
        targetFps: Int = 0,
        ntscFramerate: Float = AppPreferences.DEFAULT_NTSC_FRAMERATE,
        palFramerate: Float = AppPreferences.DEFAULT_PAL_FRAMERATE,
        textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
        trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
        blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
        texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
        enableFxaa: Boolean = false,
        casMode: Int = 0,
        sgsrMode: Int = 0,
        casSharpness: Int = 50,
        tvShader: Int = GsHackDefaults.TV_SHADER_DEFAULT,
        shadeBoostEnabled: Boolean = false,
        shadeBoostBrightness: Int = 50,
        shadeBoostContrast: Int = 50,
        shadeBoostSaturation: Int = 50,
        shadeBoostGamma: Int = 50,
        deinterlaceMode: Int = GsHackDefaults.DEINTERLACE_MODE_DEFAULT,
        dithering: Int = GsHackDefaults.DITHERING_DEFAULT,
        anisotropicFiltering: Int = 0,
        enableHwMipmapping: Boolean = GsHackDefaults.HW_MIPMAPPING_DEFAULT,
        antiBlur: Boolean = GsHackDefaults.ANTI_BLUR_DEFAULT,
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
        pressureModifierAmount: Int = AppPreferences.DEFAULT_PRESSURE_MODIFIER_AMOUNT,
        memoryCardSlot1: String? = null,
        memoryCardSlot2: String? = null,
        autotestMode: Boolean = false,
        fpuCorrectAddSub: Boolean = true,
        dev9EthernetEnabled: Boolean = false,
        dev9EthernetDevice: String = "Auto",
        dev9InterceptDhcp: Boolean = false,
        dev9Dns1Mode: String = "Auto",
        dev9Dns1: String = "0.0.0.0",
        dev9Dns2Mode: String = "Auto",
        dev9Dns2: String = "0.0.0.0",
        dev9LogDhcp: Boolean = false,
        dev9LogDns: Boolean = false
    ) = withContext(serialDispatcher) {
        if (!isNativeLoaded) return@withContext

        val context = getContext() ?: return@withContext
        NetworkAdapterCollector.collectAdapters(context)
        val resolvedRenderer = normalizeRenderer(renderer)
        val preparedBios = DocumentPathResolver.prepareBiosSelection(context, biosPath)
        val resolvedBiosPath = preparedBios?.directoryPath
            ?: biosPath?.let(DocumentPathResolver::resolveDirectoryPath)
        val preferredBiosFile = preparedBios?.fileName
            ?: DocumentPathResolver.findPreferredBiosFileName(resolvedBiosPath)
        val runtimeDirectories = EmulatorStorage.runtimeDirectories(context, emulatorDataPath)
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

        val normalizedGpuHardwareProfile = GpuHardwareProfiles.normalize(gpuHardwareProfile)
        val gpuHardwareProfileOverride = GpuHardwareProfiles.coreOverrideFor(normalizedGpuHardwareProfile)
        val customDriverSupported = GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers() && !GpuHardwareProfiles.isMediatekProfile(normalizedGpuHardwareProfile)
        val effectiveGpuDriverType = if (gpuDriverType == 1 && customDriverSupported) 1 else 0
        val effectiveMediatekAngleOpenGl = shouldUseMediatekAngleOpenGl(
            requested = mediatekAngleOpenGl,
            gpuHardwareProfile = normalizedGpuHardwareProfile,
            renderer = resolvedRenderer
        )
        NativeApp.setCrashContextString("emu_renderer_name", rendererName(resolvedRenderer))
        NativeApp.setCrashContextString("emu_gpu_driver_mode", if (effectiveGpuDriverType == 1) "custom" else "system")
        NativeApp.setCrashContextString("emu_gpu_profile", gpuHardwareProfileOverride)
        NativeApp.setCrashContextBool("emu_mediatek_angle_opengl", effectiveMediatekAngleOpenGl)
        val resolvedCustomDriverPath = if (effectiveGpuDriverType == 1) {
            prepareCustomDriverLibrary(customDriverPath.orEmpty())
        } else {
            ""
        }
        val directMtvu = mtvu && enableVu1Recompiler
        val directEeFpuRoundMode = sanitizeFloatRoundMode(eeFpuRoundMode, AppPreferences.DEFAULT_EE_FPU_ROUND_MODE)
        val directVu0RoundMode = sanitizeFloatRoundMode(vu0RoundMode, AppPreferences.DEFAULT_VU_ROUND_MODE)
        val directVu1RoundMode = sanitizeFloatRoundMode(vu1RoundMode, AppPreferences.DEFAULT_VU_ROUND_MODE)
        val directEeFpuClampingMode = sanitizeClampingMode(eeFpuClampingMode, AppPreferences.DEFAULT_EE_FPU_CLAMPING_MODE)
        val directVu0ClampingMode = sanitizeClampingMode(vu0ClampingMode, AppPreferences.DEFAULT_VU0_CLAMPING_MODE)
        val directVu1ClampingMode = sanitizeClampingMode(vu1ClampingMode, AppPreferences.DEFAULT_VU1_CLAMPING_MODE)
        Log.i(
            "EmuCoreX",
            "android jit: requested={ee:$enableEeRecompiler iop:$enableIopRecompiler vu0:$enableVu0Recompiler vu1:$enableVu1Recompiler fastmem:$enableFastmem} speedhacks={waitLoop:$waitLoopSpeedhack intcStat:$intcStatSpeedhack vuFlag:$vuFlagHack mtvu:$mtvu instantVu1:$instantVu1} direct={ee:$enableEeRecompiler iop:$enableIopRecompiler vu0:$enableVu0Recompiler vu1:$enableVu1Recompiler mtvu:$directMtvu instantVu1:$instantVu1 fastmem:$enableFastmem} round={ee:$directEeFpuRoundMode vu0:$directVu0RoundMode vu1:$directVu1RoundMode} clamp={ee:$directEeFpuClampingMode vu0:$directVu0ClampingMode vu1:$directVu1ClampingMode}"
        )
        NativeApp.logCrashBreadcrumb(
            "applyRuntimeConfig renderer=${rendererName(resolvedRenderer)}($resolvedRenderer) driverType=$effectiveGpuDriverType requestedDriverType=$gpuDriverType hwDownload=$hwDownloadMode directJit={ee:$enableEeRecompiler iop:$enableIopRecompiler vu0:$enableVu0Recompiler vu1:$enableVu1Recompiler mtvu:$directMtvu instantVu1:$instantVu1 fastmem:$enableFastmem} speedhacks={waitLoop:$waitLoopSpeedhack intcStat:$intcStatSpeedhack vuFlag:$vuFlagHack fastBoot:$enableFastBoot fastCdvd:$fastCdvd} round={ee:$directEeFpuRoundMode vu0:$directVu0RoundMode vu1:$directVu1RoundMode} clamp={ee:$directEeFpuClampingMode vu0:$directVu0ClampingMode vu1:$directVu1ClampingMode} gameFixes={auto:$enableGameFixes eeTiming:$eeTimingHack} jitRequested={ee:$enableEeRecompiler iop:$enableIopRecompiler vu0:$enableVu0Recompiler vu1:$enableVu1Recompiler fastmem:$enableFastmem}"
        )
        val prefs = AppPreferences(context)
        val achievementsHardcore = prefs.getAchievementsHardcoreSync()
        val effectiveEnableCheats = enableCheats && !achievementsHardcore
        val effectiveFrameLimitEnabled = frameLimitEnabled || achievementsHardcore
        val padVibrationEnabled = prefs.padVibration.first()
        val textureReplacementsEnabled = prefs.textureReplacementsEnabled.first()
        val textureReplacementsAsync = prefs.textureReplacementsAsync.first()
        val textureReplacementsPrecache = prefs.textureReplacementsPrecache.first()
        val textureDumpingEnabled = prefs.textureDumpingEnabled.first()

        performRuntimeOps(
            buildList {
                add(settingOp("EmuCore/GS", "Renderer", "int", resolvedRenderer.toString()))
                add(settingOp("DEV9/Eth", "EthEnable", "bool", dev9EthernetEnabled.toString()))
                add(settingOp("DEV9/Eth", "EthApi", "string", "Sockets"))
                add(settingOp("DEV9/Eth", "EthDevice", "string", dev9EthernetDevice.ifBlank { "Auto" }))
                add(settingOp("DEV9/Eth", "InterceptDHCP", "bool", dev9InterceptDhcp.toString()))
                add(settingOp("DEV9/Eth", "ModeDNS1", "string", dev9Dns1Mode))
                add(settingOp("DEV9/Eth", "DNS1", "string", dev9Dns1))
                add(settingOp("DEV9/Eth", "ModeDNS2", "string", dev9Dns2Mode))
                add(settingOp("DEV9/Eth", "DNS2", "string", dev9Dns2))
                add(settingOp("DEV9/Eth", "EthLogDHCP", "bool", dev9LogDhcp.toString()))
                add(settingOp("DEV9/Eth", "EthLogDNS", "bool", dev9LogDns.toString()))
                val pressureAmount = pressureModifierAmount.coerceIn(1, 100) / 100.0f
                add(settingOp("Pad1", "PressureModifier", "float", pressureAmount.toString()))
                add(settingOp("Pad2", "PressureModifier", "float", pressureAmount.toString()))
                add(upscaleOp(upscaleMultiplier))
                add(aspectOp(aspectRatio))
                add(settingOp("SPU2/Output", "StandardVolume", "int", AudioDefaults.coerceVolume(audioVolume).toString()))
                add(settingOp("SPU2/Output", "FastForwardVolume", "int", AudioDefaults.coerceVolume(audioFastForwardVolume).toString()))
                add(settingOp("SPU2/Output", "OutputMuted", "bool", audioMuted.toString()))
                add(settingOp("SPU2/Output", "LightweightMode", "bool", audioLightweightSpu2.toString()))
                add(settingOp("SPU2/Output", "InterpolationMode", "string", AudioDefaults.interpolationCoreName(
                    AudioDefaults.effectiveInterpolation(audioInterpolation, audioLightweightSpu2)
                )))
                add(settingOp("SPU2/Output", "SyncMode", "string", AudioDefaults.syncModeCoreName(
                    AudioDefaults.effectiveSyncMode(audioSyncMode, audioLightweightSpu2)
                )))
                add(settingOp("SPU2/Output", "Backend", "string", AudioDefaults.backendCoreName(audioBackend)))
                add(settingOp("SPU2/Output", "BufferMS", "int", AudioDefaults.coerceBufferMs(audioBufferMs).toString()))
                add(settingOp("SPU2/Output", "OutputLatencyMS", "int", AudioDefaults.coerceOutputLatencyMs(audioOutputLatencyMs).toString()))
                add(settingOp("SPU2/Output", "OutputLatencyMinimal", "bool", audioMinimalOutputLatency.toString()))
                add(settingOp("Folders", "Bios", "string", resolvedBiosPath.orEmpty()))
                add(settingOp("Folders", "Savestates", "string", runtimeDirectories.saveStates.absolutePath))
                add(settingOp("Folders", "MemoryCards", "string", runtimeDirectories.memoryCards.absolutePath))
                add(settingOp("Folders", "Textures", "string", runtimeDirectories.textures.absolutePath))
                add(settingOp("Folders", "Cheats", "string", runtimeDirectories.cheats.absolutePath))
                add(settingOp("Folders", "Patches", "string", runtimeDirectories.patches.absolutePath))
                add(settingOp("Folders", "Logs", "string", runtimeDirectories.logs.absolutePath))
                add(memoryCardSlotOp(1, memoryCardSlot1))
                add(memoryCardSlotOp(2, memoryCardSlot2))
                add(settingOp("Filenames", "BIOS", "string", preferredBiosFile.orEmpty()))
                add(refreshBiosOp())
                add(settingOp("EmuCoreX", "OpenGLTextureDebugLog", "bool", (resolvedRenderer == 12).toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "EnableEE", "bool", enableEeRecompiler.toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "EnableIOP", "bool", enableIopRecompiler.toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "EnableVU0", "bool", enableVu0Recompiler.toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "EnableVU1", "bool", enableVu1Recompiler.toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "EnableFastmem", "bool", enableFastmem.toString()))
                add(settingOp("EmuCore/CPU", "FPU.Roundmode", "int", directEeFpuRoundMode.toString()))
                add(settingOp("EmuCore/CPU", "VU0.Roundmode", "int", directVu0RoundMode.toString()))
                add(settingOp("EmuCore/CPU", "VU1.Roundmode", "int", directVu1RoundMode.toString()))
                addAll(eeFpuClampingOps(directEeFpuClampingMode))
                addAll(vuClampingOps(directVu0ClampingMode, directVu1ClampingMode))
                add(settingOp("EmuCore", "EnableGameFixes", "bool", enableGameFixes.toString()))
                add(settingOp("EmuCore/Gamefixes", "EETimingHack", "bool", eeTimingHack.toString()))
                add(settingOp("EmuCore/Speedhacks", "WaitLoop", "bool", waitLoopSpeedhack.toString()))
                add(settingOp("EmuCore/Speedhacks", "IntcStat", "bool", intcStatSpeedhack.toString()))
                add(settingOp("EmuCore/Speedhacks", "vuFlagHack", "bool", vuFlagHack.toString()))
                add(settingOp("EmuCore/Speedhacks", "vuThread", "bool", directMtvu.toString()))
                add(settingOp("EmuCore/Speedhacks", "vu1Instant", "bool", instantVu1.toString()))
                add(settingOp("EmuCore", "EnableThreadPinning", "bool", enableThreadPinning.toString()))
                add(settingOp("EmuCore", "EnableFastBoot", "bool", enableFastBoot.toString()))
                add(settingOp("EmuCore/Speedhacks", "fastCDVD", "bool", fastCdvd.toString()))
                add(settingOp("EmuCore", "EnableCheats", "bool", effectiveEnableCheats.toString()))
                add(settingOp("EmuCore/GS", "HWDownloadMode", "int", hwDownloadMode.toString()))
                add(settingOp("EmuCore/GS", "deinterlace_mode", "int", GsHackDefaults.coerceDeinterlaceMode(deinterlaceMode).toString()))
                add(settingOp("EmuCore/GS", "dithering_ps2", "int", GsHackDefaults.coerceDithering(dithering).toString()))
                add(settingOp("EmuCore/GS", "AndroidGpuProfileOverride", "string", gpuHardwareProfileOverride))
                add(settingOp("EmuCore/GS", "AndroidUseAngleOpenGL", "bool", effectiveMediatekAngleOpenGl.toString()))
                add(settingOp("EmuCore/GS", "OsdShowSpeed", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowFPS", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowVPS", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowResolution", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowGSStats", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowCPU", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowGPU", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowGPUDebug", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowIndicators", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowFrameTimes", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowHardwareInfo", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowVersion", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowSettings", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowInputs", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowVideoCapture", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowInputRec", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdShowTextureReplacements", "bool", "false"))
                add(settingOp("EmuCore/GS", "OsdMessagesPos", "int", "0"))
                add(settingOp("EmuCore/GS", "OsdPerformancePos", "int", "0"))
                add(settingOp("EmuCore/Speedhacks", "EECycleRate", "int", eeCycleRate.toString()))
                add(settingOp("EmuCore/Speedhacks", "EECycleSkip", "int", eeCycleSkip.toString()))
                add(settingOp("EmuCore/GS", "FrameLimitEnable", "bool", effectiveFrameLimitEnabled.toString()))
                add(settingOp("EmuCore/GS", "VsyncEnable", "bool", vSyncEnabled.toString()))
                addAll(targetFpsOps(targetFps, ntscFramerate, palFramerate))
                add(settingOp("Framerate", "NominalScalar", "float", "1.0"))
                add(settingOp("Framerate", "TurboScalar", "float", sanitizeFastForwardSpeed(fastForwardSpeed).toString()))
                add(settingOp("EmuCore/CPU/Recompiler", "fpuCorrectAddSub", "bool", fpuCorrectAddSub.toString()))
                add(frameSkipOp(frameSkip))
                add(settingOp("EmuCore/GS", "SkipDuplicateFrames", "bool", skipDuplicateFrames.toString()))
                add(settingOp("EmuCore/GS", "filter", "int", textureFiltering.toString()))
                add(settingOp("EmuCore/GS", "TriFilter", "int", trilinearFiltering.toString()))
                add(settingOp("EmuCore/GS", "accurate_blending_unit", "int", blendingAccuracy.toString()))
                add(settingOp("EmuCore/GS", "texture_preloading", "int", texturePreloading.toString()))
                add(settingOp("EmuCore/GS", "LoadTextureReplacements", "bool", textureReplacementsEnabled.toString()))
                add(settingOp("EmuCore/GS", "LoadTextureReplacementsAsync", "bool", textureReplacementsAsync.toString()))
                add(settingOp("EmuCore/GS", "PrecacheTextureReplacements", "bool", textureReplacementsPrecache.toString()))
                add(settingOp("EmuCore/GS", "DumpReplaceableTextures", "bool", textureDumpingEnabled.toString()))
                add(settingOp("EmuCore/GS", "DumpTexturesWithFMVActive", "bool", "false"))
                add(settingOp("EmuCore/GS", "DisableShaderCache", "bool", "false"))
                add(settingOp("EmuCore/GS", "fxaa", "bool", enableFxaa.toString()))
                add(settingOp("EmuCore/GS", "CASMode", "int", casMode.toString()))
                add(settingOp("EmuCore/GS", "SGSRMode", "int", sgsrMode.coerceIn(0, 3).toString()))
                add(settingOp("EmuCore/GS", "CASSharpness", "int", casSharpness.toString()))
                add(settingOp("EmuCore/GS", "TVShader", "int", GsHackDefaults.coerceTvShader(tvShader).toString()))
                add(settingOp("EmuCore/GS", "ShadeBoost", "bool", shadeBoostEnabled.toString()))
                add(settingOp("EmuCore/GS", "ShadeBoost_Brightness", "int", shadeBoostBrightness.toString()))
                add(settingOp("EmuCore/GS", "ShadeBoost_Contrast", "int", shadeBoostContrast.toString()))
                add(settingOp("EmuCore/GS", "ShadeBoost_Saturation", "int", shadeBoostSaturation.toString()))
                add(settingOp("EmuCore/GS", "ShadeBoost_Gamma", "int", shadeBoostGamma.toString()))
                add(settingOp("EmuCore/GS", "MaxAnisotropy", "int", anisotropicFiltering.toString()))
                add(settingOp("EmuCore/GS", "hw_mipmap", "bool", enableHwMipmapping.toString()))
                add(settingOp("EmuCore/GS", "pcrtc_antiblur", "bool", antiBlur.toString()))
                add(settingOp("EmuCore", "EnableWideScreenPatches", "bool", widescreenPatches.toString()))
                add(settingOp("EmuCore", "EnableNoInterlacingPatches", "bool", noInterlacingPatches.toString()))
                add(settingOp("EmuCore/GS", "UserHacks", "bool", manualHardwareFixes.toString()))
                if (manualHardwareFixes) {
                    // Leave per-game GameIndex GS fixes as the only hack layer unless manual fixes are explicitly active.
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
                }
                add(settingOp("EmuCoreX", "BiosSource", "string", biosPath.orEmpty()))
                add(settingOp("EmuCoreX", "Renderer", "int", resolvedRenderer.toString()))
                add(settingOp("EmuCoreX", "UpscaleMultiplier", "float", upscaleMultiplier.toString()))
                add(settingOp("EmuCoreX", "GpuHardwareProfile", "int", normalizedGpuHardwareProfile.toString()))
                add(settingOp("EmuCoreX", "HasContext", "bool", (context.applicationContext != null).toString()))
                add(settingOp("EmuCoreX", "AutotestMode", "bool", autotestMode.toString()))
                add(settingOp("EmuCoreX", "AppVersion", "string", appVersionName(context)))
                add(settingOp("EmuCore", "WarnAboutUnsafeSettings", "bool", "false"))
                add(settingOp("InputSources", "PadVibration", "bool", padVibrationEnabled.toString()))
                add(settingOp("Achievements", "Enabled", "bool", prefs.getAchievementsEnabledSync().toString()))
                add(settingOp("Achievements", "ChallengeMode", "bool", achievementsHardcore.toString()))
                add(settingOp("Achievements", "Notifications", "bool", prefs.getAchievementsNotificationsSync().toString()))
                add(settingOp("Achievements", "LeaderboardNotifications", "bool", prefs.getAchievementsLeaderboardNotificationsSync().toString()))
                add(settingOp("Achievements", "Overlays", "bool", prefs.getAchievementsIndicatorsSync().toString()))
                add(settingOp("Achievements", "LBOverlays", "bool", prefs.getAchievementsLeaderboardTrackersSync().toString()))
                add(settingOp("Achievements", "SoundEffects", "bool", prefs.getAchievementsSoundEffectsSync().toString()))
                add(settingOp("Achievements", "UnlockSoundName", "string", prefs.getAchievementsUnlockSoundPathSync().orEmpty()))
                add(settingOp("Achievements", "Username", "string", prefs.getAchievementsUsernameSync().orEmpty()))
                add(settingOp("Achievements", "Token", "string", prefs.getAchievementsTokenSync().orEmpty()))
                add(customDriverOp(resolvedCustomDriverPath))
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

    suspend fun startEmulation(
        path: String,
        bootSmokeProbe: Boolean = false,
        allowBiosBoot: Boolean = false
    ): Boolean {
        if (!isNativeLoaded) {
            Log.e(TAG, "startEmulation skipped: native library is not loaded")
            return false
        }
        if (path.isBlank() && !allowBiosBoot && !bootSmokeProbe) {
            Log.e(TAG, "startEmulation rejected blank game path")
            return false
        }
        val isElf = when {
            path.substringAfterLast('.', "").equals("elf", ignoreCase = true) -> true
            path.startsWith("content://") -> {
                val context = getContext()
                val displayName = context?.let { DocumentPathResolver.getDisplayName(it, path) }.orEmpty()
                displayName.substringAfterLast('.', "").equals("elf", ignoreCase = true)
            }
            else -> false
        }
        val isIrx = when {
            path.substringAfterLast('.', "").equals("irx", ignoreCase = true) -> true
            path.startsWith("content://") -> {
                val context = getContext()
                val displayName = context?.let { DocumentPathResolver.getDisplayName(it, path) }.orEmpty()
                displayName.substringAfterLast('.', "").equals("irx", ignoreCase = true)
            }
            else -> false
        }
        val pathType = when {
            path.startsWith("content://") -> "content"
            path.isBlank() -> "bios"
            else -> "file"
        }
        NativeApp.logCrashBreadcrumb("startEmulation requested pathType=$pathType vmActive=$isVmActive")
        Log.i(TAG, "startEmulation requested pathType=$pathType bootSmoke=$bootSmokeProbe vmActive=$isVmActive")
        val shouldAutoProgressiveScanHold = shouldStartAutoProgressiveScanHold(path, bootSmokeProbe, allowBiosBoot)

        return runSerial {
            isVmActive = true
            shutdownRequested = false
            val result = try {
                NativeApp.logCrashBreadcrumb(
                    "startEmulation entering native ${
                        when {
                            bootSmokeProbe -> "runBootSmokeProbe"
                            isElf -> "bootElf"
                            isIrx -> "bootIrx"
                            else -> "runVMThread"
                        }
                    }"
                )
                when {
                    bootSmokeProbe -> NativeApp.runBootSmokeProbe(path, BOOT_SMOKE_PROBE_STEPS) != 0
                    isElf -> NativeApp.bootElf(path)
                    isIrx -> NativeApp.bootIrx(path)
                    else -> NativeApp.runVMThread(path)
                }
            } catch (error: Exception) {
                NativeApp.logCrashBreadcrumb("startEmulation exception before native start returned")
                Log.e(TAG, "startEmulation native call failed", error)
                false
            }
            if (result) {
                startAutoProgressiveScanHoldIfEnabled(shouldAutoProgressiveScanHold)
            } else {
                stopAutoProgressiveScanHold()
            }
            if (bootSmokeProbe) {
                isVmActive = false
                DocumentPathResolver.releasePreparedLaunchHandles()
            } else if (!result || !runCatching { NativeApp.hasValidVm() }.getOrDefault(false)) {
                isVmActive = false
                DocumentPathResolver.releasePreparedLaunchHandles()
            }
            NativeApp.logCrashBreadcrumb("startEmulation finished result=$result")
            Log.i(TAG, "startEmulation finished result=$result")
            result
        }
    }

    private suspend fun shouldStartAutoProgressiveScanHold(
        path: String,
        bootSmokeProbe: Boolean,
        allowBiosBoot: Boolean
    ): Boolean {
        if (bootSmokeProbe || allowBiosBoot || path.isBlank()) return false
        return runCatching {
            getContext()?.let { AppPreferences(it).autoProgressiveScan.first() } == true
        }.getOrDefault(false)
    }

    private fun startAutoProgressiveScanHoldIfEnabled(enabled: Boolean) {
        if (!enabled) return
        stopAutoProgressiveScanHold()
        val job = inputScope.launch {
            try {
                setAutoProgressiveScanButtons(pressed = true)
                delay(AUTO_PROGRESSIVE_SCAN_HOLD_MS.milliseconds)
            } finally {
                setAutoProgressiveScanButtons(pressed = false)
                if (autoProgressiveScanJob === coroutineContext[Job]) {
                    autoProgressiveScanJob = null
                }
            }
        }
        autoProgressiveScanJob = job
    }

    private fun stopAutoProgressiveScanHold() {
        autoProgressiveScanJob?.cancel()
        autoProgressiveScanJob = null
        setAutoProgressiveScanButtons(pressed = false)
    }

    private fun setAutoProgressiveScanButtons(pressed: Boolean) {
        if (!isNativeLoaded) return
        runCatching {
            NativeApp.setPadButton(
                AUTO_PROGRESSIVE_SCAN_PAD_INDEX,
                AUTO_PROGRESSIVE_SCAN_TRIANGLE,
                0,
                pressed
            )
            NativeApp.setPadButton(
                AUTO_PROGRESSIVE_SCAN_PAD_INDEX,
                AUTO_PROGRESSIVE_SCAN_CROSS,
                0,
                pressed
            )
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

    suspend fun startJitProfiler() {
        if (!isNativeLoaded || !isVmActive) return
        runSerial {
            try {
                NativeApp.startJitProfiler()
            } catch (_: Exception) { }
        }
    }

    suspend fun stopJitProfiler() {
        if (!isNativeLoaded || !isVmActive) return
        runSerial {
            try {
                NativeApp.stopJitProfiler()
            } catch (_: Exception) { }
        }
    }

    suspend fun isJitProfilerActive(): Boolean {
        if (!isNativeLoaded || !isVmActive) return false
        return runSerial {
            try {
                NativeApp.isJitProfilerActive()
            } catch (_: Exception) {
                false
            }
        }
    }

    suspend fun startHangTrace() {
        if (!isNativeLoaded || !isVmActive) return
        runSerial {
            try {
                NativeApp.startHangTrace()
            } catch (_: Exception) { }
        }
    }

    suspend fun stopHangTrace() {
        if (!isNativeLoaded || !isVmActive) return
        runSerial {
            try {
                NativeApp.stopHangTrace()
            } catch (_: Exception) { }
        }
    }

    suspend fun isHangTraceActive(): Boolean {
        if (!isNativeLoaded || !isVmActive) return false
        return runSerial {
            try {
                NativeApp.isHangTraceActive()
            } catch (_: Exception) {
                false
            }
        }
    }

    suspend fun shutdown() {
        if (!isNativeLoaded || !isVmActive || shutdownRequested) return
        stopAutoProgressiveScanHold()
        runSerial {
            if (!isVmActive || shutdownRequested) return@runSerial
            shutdownRequested = true
            try {
                NativeApp.shutdown()
            } catch (_: Exception) { }
            isVmActive = false
            shutdownRequested = false
            DocumentPathResolver.releasePreparedLaunchHandles()
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

    fun isVmActive(): Boolean {
        if (isVmActive && isNativeLoaded && !runCatching { NativeApp.hasValidVm() }.getOrDefault(false)) {
            isVmActive = false
            DocumentPathResolver.releasePreparedLaunchHandles()
        }
        return isVmActive
    }

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
        val extensionSource = if (path.startsWith("content://")) {
            getContext()?.let { DocumentPathResolver.getDisplayName(it, path) } ?: path
        } else {
            path
        }
        val extension = extensionSource.substringAfterLast('.', "").lowercase()

        if (!isNativeLoaded) return inferredMetadata
        if (isVmActive) return inferredMetadata
        if (extension == "elf") return inferredMetadata

        val preparedPath = prepareMetadataPathForNative(path) ?: return inferredMetadata
        return try {
            val rawTitle = NativeApp.getGameTitle(preparedPath.path).orEmpty()
            val segments = rawTitle.split('|')
            val nativeTitle = segments.getOrNull(0).orEmpty()
            val nativeSerial = segments.getOrNull(1)?.takeIf { it.isNotBlank() }
            val nativeSerialWithCrc = segments.getOrNull(2)?.takeIf { it.isNotBlank() }
            if (isFdMetadataArtifact(preparedPath.path, nativeTitle, nativeSerial)) {
                return inferredMetadata
            }
            val title = nativeTitle
                .takeIf { it.isNotBlank() }
                ?.let { normalizeNativeGameTitle(it, inferredMetadata.title) }
                ?: inferredMetadata.title
            GameMetadata(
                title = title,
                serial = nativeSerial ?: inferredMetadata.serial,
                serialWithCrc = nativeSerialWithCrc ?: inferredMetadata.serialWithCrc
            )
        } catch (_: Exception) {
            inferredMetadata
        } finally {
            preparedPath.descriptor?.close()
        }
    }

    private fun prepareMetadataPathForNative(path: String): PreparedMetadataPath? {
        if (!path.startsWith("content://")) return PreparedMetadataPath(path)
        val context = getContext() ?: return null
        val directPath = DocumentPathResolver.resolveFilePath(context, path)
            ?.let(::File)
            ?.takeIf { it.isFile && it.canRead() }
            ?.absolutePath
        if (!directPath.isNullOrBlank()) return PreparedMetadataPath(directPath)

        return PreparedMetadataPath(path)
    }

    private fun isFdMetadataArtifact(path: String, nativeTitle: String, nativeSerial: String?): Boolean {
        if (!path.startsWith("/proc/self/fd/")) return false
        if (!nativeSerial.isNullOrBlank()) return false
        val trimmedTitle = nativeTitle.substringBefore('|').trim()
        return trimmedTitle.isBlank() || trimmedTitle.all(Char::isDigit)
    }

    private fun normalizeNativeGameTitle(rawTitle: String, fallbackTitle: String): String {
        return cleanGameDisplayTitle(rawTitle, fallbackTitle)
    }

    fun cleanGameDisplayTitle(rawTitle: String?, fallbackNameOrPath: String? = null): String {
        val fallbackDisplay = fallbackNameOrPath
            ?.takeIf { it.isNotBlank() }
            ?.let { value ->
                if (value.startsWith("content://")) {
                    getContext()?.let { context -> DocumentPathResolver.getDisplayName(context, value) }
                        ?: DocumentPathResolver.normalizeDisplayName(value)
                } else {
                    DocumentPathResolver.normalizeDisplayName(value)
                }
            }
            .orEmpty()
        val fallbackTitle = parseMetadataFromName(fallbackDisplay).title
        val trimmed = rawTitle.orEmpty().trim()
        val source = if (trimmed.isBlank() || looksLikeStoragePath(trimmed)) {
            fallbackDisplay.ifBlank { trimmed }
        } else {
            trimmed
        }
        val normalized = if (looksLikeStoragePath(source)) {
            DocumentPathResolver.normalizeDisplayName(source)
        } else {
            source
        }
        return parseMetadataFromName(normalized).title.ifBlank { fallbackTitle }
    }

    private fun looksLikeStoragePath(value: String): Boolean {
        val trimmed = value.trim()
        return trimmed.contains("%2F", ignoreCase = true) ||
            trimmed.contains("%3A", ignoreCase = true) ||
            trimmed.startsWith("content://", ignoreCase = true) ||
            trimmed.startsWith("primary:", ignoreCase = true) ||
            trimmed.startsWith("home:", ignoreCase = true) ||
            trimmed.startsWith("raw:", ignoreCase = true) ||
            trimmed.contains("/storage/", ignoreCase = true)
    }

    fun parseMetadataFromName(rawName: String): GameMetadata {
        val ext = rawName.substringAfterLast('.', "").lowercase()
        val cleanName = if (ext in setOf("iso", "bin", "img", "mdf", "gz", "cso", "zso", "chd", "elf")) {
            rawName.substringBeforeLast('.').trim()
        } else {
            rawName.trim()
        }
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
                val success = NativeApp.loadStateFromSlot(slot)
                if (success) {
                    runCatching { rebindSurface() }
                    runCatching { NativeApp.resume() }
                }
                success
            } catch (_: Exception) {
                false
            }
        }
    }

    suspend fun setRenderer(gpuType: Int) {
        val resolvedRenderer = normalizeRenderer(gpuType)
        settingsCache["EmuCore/GS:Renderer"] = resolvedRenderer.toString()
        NativeApp.logCrashBreadcrumb(
            "renderer change requested renderer=${rendererName(resolvedRenderer)}($resolvedRenderer) vmActive=$isVmActive"
        )
        Log.i(TAG, "Renderer change requested: ${rendererName(resolvedRenderer)}($resolvedRenderer) vmActive=$isVmActive")
        performRuntimeOps(listOf(settingOp("EmuCore/GS", "Renderer", "int", resolvedRenderer.toString())))
    }

    suspend fun setUpscaleMultiplier(multiplier: Float) {
        val normalized = normalizeUpscale(multiplier)
        settingsCache["EmuCore/GS:upscale_multiplier"] = normalized.toString()
        performRuntimeOps(listOf(upscaleOp(normalized), settingOp("EmuCore/GS", "upscale_multiplier", "float", normalized.toString())))
    }

    suspend fun setAspectRatio(type: Int) {
        val normalizedType = normalizeAspectRatio(type)
        val value = aspectRatioSettingValues.getValue(normalizedType)
        settingsCache["EmuCore/GS:AspectRatio"] = value
        performRuntimeOps(listOf(aspectOp(normalizedType)))
    }

    suspend fun setCustomDriverPath(path: String) {
        val resolvedPath = if (GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers()) {
            prepareCustomDriverLibrary(path)
        } else {
            ""
        }
        settingsCache["EmuCore/GS:CustomDriverPath"] = resolvedPath
        performRuntimeOps(listOf(customDriverOp(resolvedPath)))
    }

    suspend fun setFrameLimitEnabled(enabled: Boolean) {
        if (!isNativeLoaded) return
        val value = enabled.toString()
        val cacheKey = "EmuCore/GS:FrameLimitEnable"
        if (settingsCache[cacheKey] == value) return
        runSerial {
            NativeApp.setFrameLimitEnabled(enabled)
        }
        settingsCache[cacheKey] = value
    }

    suspend fun setVSyncEnabled(enabled: Boolean) {
        setSetting("EmuCore/GS", "VsyncEnable", "bool", enabled.toString())
    }

    suspend fun setTurboModeEnabled(enabled: Boolean) {
        if (!isNativeLoaded) return
        runSerial {
            NativeApp.setTurboModeEnabled(enabled)
        }
    }

    suspend fun setFastForwardSpeed(value: Float) {
        setSetting("Framerate", "TurboScalar", "float", sanitizeFastForwardSpeed(value).toString())
    }

    suspend fun setSkipDuplicateFrames(enabled: Boolean) {
        setSetting("EmuCore/GS", "SkipDuplicateFrames", "bool", enabled.toString())
    }

    suspend fun setFrameSkip(value: Int) {
        performRuntimeOps(listOf(frameSkipOp(value)))
    }

    suspend fun setTargetFps(
        targetFps: Int,
        ntscFramerate: Float = AppPreferences.DEFAULT_NTSC_FRAMERATE,
        palFramerate: Float = AppPreferences.DEFAULT_PAL_FRAMERATE
    ) {
        performRuntimeOps(
            buildList {
                addAll(targetFpsOps(targetFps, ntscFramerate, palFramerate))
                add(settingOp("Framerate", "NominalScalar", "float", "1.0"))
            }
        )
    }

    fun setPadButton(padIndex: Int, index: Int, range: Int, pressed: Boolean) {
        if (!isNativeLoaded) return
        try {
            NativeApp.setPadButton(padIndex, index, range, pressed)
        } catch (_: Exception) { }
    }

    fun resetKeyStatus() {
        if (!isNativeLoaded || !isVmActive) return
        launchSerial {
            if (!isVmActive || !runCatching { NativeApp.hasValidVm() }.getOrDefault(false)) return@launchSerial
            try {
                NativeApp.resetKeyStatus()
            } catch (_: Exception) { }
        }
    }

    fun resetPadState(padIndex: Int) {
        if (!isNativeLoaded || !isVmActive) return
        launchSerial {
            if (!isVmActive || !runCatching { NativeApp.hasValidVm() }.getOrDefault(false)) return@launchSerial
            try {
                NativeApp.resetPadState(padIndex)
            } catch (_: Exception) { }
        }
    }

    suspend fun setPadVibration(enabled: Boolean) {
        setSetting("InputSources", "PadVibration", "bool", enabled.toString())
        runCatching { NativeApp.setPadVibration(enabled) }
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
        ++surfaceEventVersion
        lastSurface = null
        lastSurfaceWidth = 0
        lastSurfaceHeight = 0
        NativeApp.setCrashContextString("emu_surface_state", "destroyed")
        NativeApp.logCrashBreadcrumb("surfaceDestroyed")
        runCatching { NativeApp.pause() }
        // Android invalidates the BufferQueue as soon as this callback returns.
        // Detach GS synchronously so it cannot keep presenting to an abandoned Surface.
        try {
            NativeApp.onNativeSurfaceDestroyed()
        } catch (_: Exception) { }
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

    suspend fun reloadPatches() {
        if (!isNativeLoaded) return
        runSerial { NativeApp.reloadPatches() }
    }

    fun getSetting(section: String, key: String, type: String): String? {
        if (!isNativeLoaded) return null
        return try {
            NativeApp.getSetting(section, key, type)
        } catch (_: Exception) {
            null
        }
    }

    private fun targetFpsOps(targetFps: Int, ntscFramerate: Float, palFramerate: Float): List<RuntimeOp> {
        if (targetFps <= 0) {
            return regionFramerateOps(ntscFramerate, palFramerate)
        }

        val manualFps = targetFps.coerceIn(20, 120).toFloat()
        return listOf(
            settingOp("EmuCore/GS", "FramerateNTSC", "float", manualFps.toString()),
            settingOp("EmuCore/GS", "FrameratePAL", "float", manualFps.toString())
        )
    }

    private fun sanitizeFramerate(value: Float, fallback: Float): Float {
        return if (value.isFinite()) value.coerceIn(20f, 120f) else fallback
    }

    private fun sanitizeFastForwardSpeed(value: Float): Float {
        return if (value.isFinite()) {
            value.coerceIn(AppPreferences.MIN_FAST_FORWARD_SPEED, AppPreferences.MAX_FAST_FORWARD_SPEED)
        } else {
            AppPreferences.DEFAULT_FAST_FORWARD_SPEED
        }
    }
}

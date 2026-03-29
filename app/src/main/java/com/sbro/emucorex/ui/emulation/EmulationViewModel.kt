package com.sbro.emucorex.ui.emulation

import android.app.Application
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DeviceChipsetFamily
import com.sbro.emucorex.core.DevicePerformanceProfiles
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.core.PerformancePresetConfig
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.CheatBlock
import com.sbro.emucorex.data.CheatRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.File

data class EmulationUiState(
    val isRunning: Boolean = false,
    val isStarting: Boolean = false,
    val isPaused: Boolean = false,
    val showMenu: Boolean = false,
    val isActionInProgress: Boolean = false,
    val actionLabel: String? = null,
    val controlsVisible: Boolean = true,
    val showFps: Boolean = true,
    val compactControls: Boolean = true,
    val keepScreenOn: Boolean = true,
    val fps: String = "0.0",
    val fpsOverlayMode: Int = FPS_OVERLAY_MODE_DETAILED,
    val speedPercent: String = "100",
    val frameTime: String = "0.00",
    val cpuLoad: String = "0",
    val gpuLoad: String = "0",
    val toastMessage: String? = null,
    val statusMessage: String? = null,
    val currentSlot: Int = 0,
    val renderer: Int = 14, // Default to Vulkan (14)
    val upscale: Int = 1,
    val aspectRatio: Int = 1,
    val performancePreset: Int = PerformancePresets.CUSTOM,
    val enableMtvu: Boolean = true,
    val enableFastCdvd: Boolean = false,
    val enableCheats: Boolean = true,
    val hwDownloadMode: Int = 0,
    val frameSkip: Int = 0,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val casMode: Int = 0,
    val casSharpness: Int = 50,
    val anisotropicFiltering: Int = 0,
    val enableHwMipmapping: Boolean = true,
    val cpuSpriteRenderSize: Int = GsHackDefaults.CPU_SPRITE_RENDER_SIZE_DEFAULT,
    val cpuSpriteRenderLevel: Int = GsHackDefaults.CPU_SPRITE_RENDER_LEVEL_DEFAULT,
    val softwareClutRender: Int = GsHackDefaults.SOFTWARE_CLUT_RENDER_DEFAULT,
    val gpuTargetClutMode: Int = GsHackDefaults.GPU_TARGET_CLUT_DEFAULT,
    val skipDrawStart: Int = 0,
    val skipDrawEnd: Int = 0,
    val autoFlushHardware: Int = GsHackDefaults.AUTO_FLUSH_DEFAULT,
    val cpuFramebufferConversion: Boolean = false,
    val disableDepthConversion: Boolean = false,
    val disableSafeFeatures: Boolean = false,
    val disableRenderFixes: Boolean = false,
    val preloadFrameData: Boolean = false,
    val disablePartialInvalidation: Boolean = false,
    val textureInsideRt: Int = GsHackDefaults.TEXTURE_INSIDE_RT_DEFAULT,
    val readTargetsOnClose: Boolean = false,
    val estimateTextureRegion: Boolean = false,
    val gpuPaletteConversion: Boolean = false,
    val halfPixelOffset: Int = GsHackDefaults.HALF_PIXEL_OFFSET_DEFAULT,
    val nativeScaling: Int = GsHackDefaults.NATIVE_SCALING_DEFAULT,
    val roundSprite: Int = GsHackDefaults.ROUND_SPRITE_DEFAULT,
    val bilinearUpscale: Int = GsHackDefaults.BILINEAR_UPSCALE_DEFAULT,
    val textureOffsetX: Int = 0,
    val textureOffsetY: Int = 0,
    val alignSprite: Boolean = false,
    val mergeSprite: Boolean = false,
    val forceEvenSpritePosition: Boolean = false,
    val nativePaletteDraw: Boolean = false,
    val cheatsGameKey: String? = null,
    val availableCheats: List<CheatBlock> = emptyList(),
    val deviceChipsetFamily: DeviceChipsetFamily = DeviceChipsetFamily.GENERIC,
    val detectedChipsetName: String = "",
    val recommendedPresetId: Int = PerformancePresets.BALANCED,
    val frameLimitEnabled: Boolean = false,
    val targetFps: Int = 60
)

private data class EmulationLaunchConfig(
    val biosPath: String?,
    val renderer: Int,
    val upscaleMultiplier: Int,
    val gpuDriverType: Int,
    val customDriverPath: String?,
    val aspectRatio: Int,
    val mtvu: Boolean,
    val fastCdvd: Boolean,
    val enableCheats: Boolean,
    val hwDownloadMode: Int,
    val eeCycleRate: Int,
    val eeCycleSkip: Int,
    val frameSkip: Int,
    val frameLimitEnabled: Boolean,
    val targetFps: Int,
    val textureFiltering: Int,
    val trilinearFiltering: Int,
    val blendingAccuracy: Int,
    val texturePreloading: Int,
    val enableFxaa: Boolean,
    val casMode: Int,
    val casSharpness: Int,
    val anisotropicFiltering: Int,
    val enableHwMipmapping: Boolean,
    val widescreenPatches: Boolean,
    val noInterlacingPatches: Boolean,
    val cpuSpriteRenderSize: Int,
    val cpuSpriteRenderLevel: Int,
    val softwareClutRender: Int,
    val gpuTargetClutMode: Int,
    val skipDrawStart: Int,
    val skipDrawEnd: Int,
    val autoFlushHardware: Int,
    val cpuFramebufferConversion: Boolean,
    val disableDepthConversion: Boolean,
    val disableSafeFeatures: Boolean,
    val disableRenderFixes: Boolean,
    val preloadFrameData: Boolean,
    val disablePartialInvalidation: Boolean,
    val textureInsideRt: Int,
    val readTargetsOnClose: Boolean,
    val estimateTextureRegion: Boolean,
    val gpuPaletteConversion: Boolean,
    val halfPixelOffset: Int,
    val nativeScaling: Int,
    val roundSprite: Int,
    val bilinearUpscale: Int,
    val textureOffsetX: Int,
    val textureOffsetY: Int,
    val alignSprite: Boolean,
    val mergeSprite: Boolean,
    val forceEvenSpritePosition: Boolean,
    val nativePaletteDraw: Boolean
)

private data class LiveRuntimeSnapshot(
    val renderer: Int,
    val upscale: Int,
    val aspectRatio: Int,
    val performancePreset: Int,
    val enableMtvu: Boolean,
    val enableFastCdvd: Boolean,
    val enableCheats: Boolean,
    val hwDownloadMode: Int,
    val frameSkip: Int,
    val frameLimitEnabled: Boolean,
    val targetFps: Int,
    val textureFiltering: Int,
    val trilinearFiltering: Int,
    val blendingAccuracy: Int,
    val texturePreloading: Int,
    val enableFxaa: Boolean,
    val casMode: Int,
    val casSharpness: Int,
    val anisotropicFiltering: Int,
    val enableHwMipmapping: Boolean,
    val cpuSpriteRenderSize: Int,
    val cpuSpriteRenderLevel: Int,
    val softwareClutRender: Int,
    val gpuTargetClutMode: Int,
    val skipDrawStart: Int,
    val skipDrawEnd: Int,
    val autoFlushHardware: Int,
    val cpuFramebufferConversion: Boolean,
    val disableDepthConversion: Boolean,
    val disableSafeFeatures: Boolean,
    val disableRenderFixes: Boolean,
    val preloadFrameData: Boolean,
    val disablePartialInvalidation: Boolean,
    val textureInsideRt: Int,
    val readTargetsOnClose: Boolean,
    val estimateTextureRegion: Boolean,
    val gpuPaletteConversion: Boolean,
    val halfPixelOffset: Int,
    val nativeScaling: Int,
    val roundSprite: Int,
    val bilinearUpscale: Int,
    val textureOffsetX: Int,
    val textureOffsetY: Int,
    val alignSprite: Boolean,
    val mergeSprite: Boolean,
    val forceEvenSpritePosition: Boolean,
    val nativePaletteDraw: Boolean
)

class EmulationViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val cheatRepository = CheatRepository(application)
    private val deviceProfile = DevicePerformanceProfiles.current()
    private val _uiState = MutableStateFlow(EmulationUiState())
    val uiState: StateFlow<EmulationUiState> = _uiState.asStateFlow()
    private val lifecycleMutex = Mutex()
    @Volatile
    private var isShuttingDown = false
    @Volatile
    private var cancelPendingStart = false
    @Volatile
    private var currentGameTitle: String = ""
    @Volatile
    private var currentGameSerial: String = ""
    @Volatile
    private var currentGameCrc: String = ""
    @Volatile
    private var currentGameSource: String = ""

    init {
        _uiState.value = _uiState.value.copy(
            deviceChipsetFamily = deviceProfile.family,
            detectedChipsetName = deviceProfile.displayName,
            recommendedPresetId = deviceProfile.recommendedPresetId
        )
        viewModelScope.launch {
            preferences.showFps.collect { enabled ->
                _uiState.value = _uiState.value.copy(showFps = enabled)
            }
        }
        viewModelScope.launch {
            preferences.fpsOverlayMode.collect { mode ->
                _uiState.value = _uiState.value.copy(fpsOverlayMode = mode)
            }
        }
        viewModelScope.launch {
            preferences.compactControls.collect { enabled ->
                _uiState.value = _uiState.value.copy(compactControls = enabled)
            }
        }
        viewModelScope.launch {
            preferences.keepScreenOn.collect { enabled ->
                _uiState.value = _uiState.value.copy(keepScreenOn = enabled)
            }
        }
        viewModelScope.launch {
            preferences.performancePreset.collect { preset ->
                _uiState.value = _uiState.value.copy(performancePreset = preset)
            }
        }
        viewModelScope.launch {
            preferences.hwDownloadMode.collect { value ->
                _uiState.value = _uiState.value.copy(hwDownloadMode = value)
            }
        }
        viewModelScope.launch {
            preferences.renderer.collect { value ->
                _uiState.value = _uiState.value.copy(renderer = value)
            }
        }
        viewModelScope.launch {
            preferences.upscaleMultiplier.collect { value ->
                _uiState.value = _uiState.value.copy(upscale = value)
            }
        }
        viewModelScope.launch {
            preferences.aspectRatio.collect { value ->
                _uiState.value = _uiState.value.copy(aspectRatio = value)
            }
        }
        viewModelScope.launch {
            preferences.enableMtvu.collect { value ->
                _uiState.value = _uiState.value.copy(enableMtvu = value)
            }
        }
        viewModelScope.launch {
            preferences.enableFastCdvd.collect { value ->
                _uiState.value = _uiState.value.copy(enableFastCdvd = value)
            }
        }
        viewModelScope.launch {
            preferences.enableCheats.collect { value ->
                _uiState.value = _uiState.value.copy(enableCheats = value)
            }
        }
        viewModelScope.launch {
            preferences.frameSkip.collect { value ->
                _uiState.value = _uiState.value.copy(frameSkip = value)
            }
        }
        viewModelScope.launch {
            preferences.textureFiltering.collect { value ->
                _uiState.value = _uiState.value.copy(textureFiltering = value)
            }
        }
        viewModelScope.launch {
            preferences.trilinearFiltering.collect { value ->
                _uiState.value = _uiState.value.copy(trilinearFiltering = value)
            }
        }
        viewModelScope.launch {
            preferences.blendingAccuracy.collect { value ->
                _uiState.value = _uiState.value.copy(blendingAccuracy = value)
            }
        }
        viewModelScope.launch {
            preferences.texturePreloading.collect { value ->
                _uiState.value = _uiState.value.copy(texturePreloading = value)
            }
        }
        viewModelScope.launch {
            preferences.enableFxaa.collect { value ->
                _uiState.value = _uiState.value.copy(enableFxaa = value)
            }
        }
        viewModelScope.launch {
            preferences.casMode.collect { value ->
                _uiState.value = _uiState.value.copy(casMode = value)
            }
        }
        viewModelScope.launch {
            preferences.casSharpness.collect { value ->
                _uiState.value = _uiState.value.copy(casSharpness = value)
            }
        }
        viewModelScope.launch {
            preferences.anisotropicFiltering.collect { value ->
                _uiState.value = _uiState.value.copy(anisotropicFiltering = value)
            }
        }
        viewModelScope.launch {
            preferences.enableHwMipmapping.collect { value ->
                _uiState.value = _uiState.value.copy(enableHwMipmapping = value)
            }
        }
        viewModelScope.launch {
            preferences.cpuSpriteRenderSize.collect { value ->
                _uiState.value = _uiState.value.copy(cpuSpriteRenderSize = value)
            }
        }
        viewModelScope.launch {
            preferences.cpuSpriteRenderLevel.collect { value ->
                _uiState.value = _uiState.value.copy(cpuSpriteRenderLevel = value)
            }
        }
        viewModelScope.launch {
            preferences.softwareClutRender.collect { value ->
                _uiState.value = _uiState.value.copy(softwareClutRender = value)
            }
        }
        viewModelScope.launch {
            preferences.gpuTargetClutMode.collect { value ->
                _uiState.value = _uiState.value.copy(gpuTargetClutMode = value)
            }
        }
        viewModelScope.launch {
            preferences.skipDrawStart.collect { value ->
                _uiState.value = _uiState.value.copy(skipDrawStart = value)
            }
        }
        viewModelScope.launch {
            preferences.skipDrawEnd.collect { value ->
                _uiState.value = _uiState.value.copy(skipDrawEnd = value)
            }
        }
        viewModelScope.launch {
            preferences.autoFlushHardware.collect { value ->
                _uiState.value = _uiState.value.copy(autoFlushHardware = value)
            }
        }
        viewModelScope.launch {
            preferences.cpuFramebufferConversion.collect { value ->
                _uiState.value = _uiState.value.copy(cpuFramebufferConversion = value)
            }
        }
        viewModelScope.launch {
            preferences.disableDepthConversion.collect { value ->
                _uiState.value = _uiState.value.copy(disableDepthConversion = value)
            }
        }
        viewModelScope.launch {
            preferences.disableSafeFeatures.collect { value ->
                _uiState.value = _uiState.value.copy(disableSafeFeatures = value)
            }
        }
        viewModelScope.launch {
            preferences.disableRenderFixes.collect { value ->
                _uiState.value = _uiState.value.copy(disableRenderFixes = value)
            }
        }
        viewModelScope.launch {
            preferences.preloadFrameData.collect { value ->
                _uiState.value = _uiState.value.copy(preloadFrameData = value)
            }
        }
        viewModelScope.launch {
            preferences.disablePartialInvalidation.collect { value ->
                _uiState.value = _uiState.value.copy(disablePartialInvalidation = value)
            }
        }
        viewModelScope.launch {
            preferences.textureInsideRt.collect { value ->
                _uiState.value = _uiState.value.copy(textureInsideRt = value)
            }
        }
        viewModelScope.launch {
            preferences.readTargetsOnClose.collect { value ->
                _uiState.value = _uiState.value.copy(readTargetsOnClose = value)
            }
        }
        viewModelScope.launch {
            preferences.estimateTextureRegion.collect { value ->
                _uiState.value = _uiState.value.copy(estimateTextureRegion = value)
            }
        }
        viewModelScope.launch {
            preferences.gpuPaletteConversion.collect { value ->
                _uiState.value = _uiState.value.copy(gpuPaletteConversion = value)
            }
        }
        viewModelScope.launch {
            preferences.halfPixelOffset.collect { value ->
                _uiState.value = _uiState.value.copy(halfPixelOffset = value)
            }
        }
        viewModelScope.launch {
            preferences.nativeScaling.collect { value ->
                _uiState.value = _uiState.value.copy(nativeScaling = value)
            }
        }
        viewModelScope.launch {
            preferences.roundSprite.collect { value ->
                _uiState.value = _uiState.value.copy(roundSprite = value)
            }
        }
        viewModelScope.launch {
            preferences.bilinearUpscale.collect { value ->
                _uiState.value = _uiState.value.copy(bilinearUpscale = value)
            }
        }
        viewModelScope.launch {
            preferences.textureOffsetX.collect { value ->
                _uiState.value = _uiState.value.copy(textureOffsetX = value)
            }
        }
        viewModelScope.launch {
            preferences.textureOffsetY.collect { value ->
                _uiState.value = _uiState.value.copy(textureOffsetY = value)
            }
        }
        viewModelScope.launch {
            preferences.alignSprite.collect { value ->
                _uiState.value = _uiState.value.copy(alignSprite = value)
            }
        }
        viewModelScope.launch {
            preferences.mergeSprite.collect { value ->
                _uiState.value = _uiState.value.copy(mergeSprite = value)
            }
        }
        viewModelScope.launch {
            preferences.forceEvenSpritePosition.collect { value ->
                _uiState.value = _uiState.value.copy(forceEvenSpritePosition = value)
            }
        }
        viewModelScope.launch {
            preferences.nativePaletteDraw.collect { value ->
                _uiState.value = _uiState.value.copy(nativePaletteDraw = value)
            }
        }
        viewModelScope.launch {
            preferences.frameLimitEnabled.collect { enabled ->
                _uiState.value = _uiState.value.copy(frameLimitEnabled = enabled)
            }
        }
        viewModelScope.launch {
            preferences.targetFps.collect { value ->
                _uiState.value = _uiState.value.copy(targetFps = value)
            }
        }
        
        NativeApp.setPerformanceListener(object : NativeApp.PerformanceListener {
            override fun onMetricsUpdate(
                fps: Float,
                speedPercent: Float,
                frameTime: Float,
                cpuLoad: Float,
                gpuLoad: Float
            ) {
                if (_uiState.value.isRunning && !isShuttingDown && !_uiState.value.isPaused) {
                    _uiState.value = _uiState.value.copy(
                        fps = "%.1f".format(fps),
                        speedPercent = "%.0f".format(speedPercent.coerceAtLeast(0f)),
                        frameTime = "%.2f".format(frameTime),
                        cpuLoad = "%.0f".format(cpuLoad.coerceAtLeast(0f)),
                        gpuLoad = "%.0f".format(gpuLoad.coerceAtLeast(0f))
                    )
                }
            }
        })
    }

    fun startEmulation(path: String?, slotToLoad: Int? = null, bootToBios: Boolean = false) {
        if (_uiState.value.isStarting) return
        cancelPendingStart = false

        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                statusMessage = "status_preparing"
            )

            if (_uiState.value.isRunning || EmulatorBridge.hasValidVm()) {
                performShutdown()
                delay(300)
            }

            var finalLaunchPath: String? = null
            lifecycleMutex.withLock {
                if (isShuttingDown || cancelPendingStart) return@launch

                _uiState.value = _uiState.value.copy(
                    isStarting = true,
                    statusMessage = "status_checking_bios"
                )

                val config = loadLaunchConfig()

                val resolvedBiosPath = DocumentPathResolver.prepareBiosDirectory(getApplication(), config.biosPath)
                    ?: config.biosPath?.let(DocumentPathResolver::resolveDirectoryPath)
                val biosDirExists = !resolvedBiosPath.isNullOrBlank() && File(resolvedBiosPath).exists()
                val biosLooksUsable = BiosValidator.hasUsableBiosFiles(getApplication(), config.biosPath)
                if (!biosDirExists || !biosLooksUsable) {
                    _uiState.value = _uiState.value.copy(
                        isStarting = false,
                        statusMessage = null,
                        toastMessage = "bios_missing"
                    )
                    delay(2500)
                    _uiState.value = _uiState.value.copy(toastMessage = null)
                    return@withLock
                }

                _uiState.value = _uiState.value.copy(
                    statusMessage = "status_applying_config"
                )
                delay(200)

                EmulatorBridge.applyRuntimeConfig(
                    biosPath = config.biosPath,
                    renderer = config.renderer,
                    upscaleMultiplier = config.upscaleMultiplier,
                    gpuDriverType = config.gpuDriverType,
                    customDriverPath = config.customDriverPath,
                    aspectRatio = config.aspectRatio,
                    mtvu = config.mtvu,
                    fastCdvd = config.fastCdvd,
                    enableCheats = config.enableCheats,
                    hwDownloadMode = config.hwDownloadMode,
                    eeCycleRate = config.eeCycleRate,
                    eeCycleSkip = config.eeCycleSkip,
                    frameSkip = config.frameSkip,
                    frameLimitEnabled = config.frameLimitEnabled,
                    targetFps = config.targetFps,
                    textureFiltering = config.textureFiltering,
                    trilinearFiltering = config.trilinearFiltering,
                    blendingAccuracy = config.blendingAccuracy,
                    texturePreloading = config.texturePreloading,
                    enableFxaa = config.enableFxaa,
                    casMode = config.casMode,
                    casSharpness = config.casSharpness,
                    anisotropicFiltering = config.anisotropicFiltering,
                    enableHwMipmapping = config.enableHwMipmapping,
                    widescreenPatches = config.widescreenPatches,
                    noInterlacingPatches = config.noInterlacingPatches,
                    cpuSpriteRenderSize = config.cpuSpriteRenderSize,
                    cpuSpriteRenderLevel = config.cpuSpriteRenderLevel,
                    softwareClutRender = config.softwareClutRender,
                    gpuTargetClutMode = config.gpuTargetClutMode,
                    skipDrawStart = config.skipDrawStart,
                    skipDrawEnd = config.skipDrawEnd,
                    autoFlushHardware = config.autoFlushHardware,
                    cpuFramebufferConversion = config.cpuFramebufferConversion,
                    disableDepthConversion = config.disableDepthConversion,
                    disableSafeFeatures = config.disableSafeFeatures,
                    disableRenderFixes = config.disableRenderFixes,
                    preloadFrameData = config.preloadFrameData,
                    disablePartialInvalidation = config.disablePartialInvalidation,
                    textureInsideRt = config.textureInsideRt,
                    readTargetsOnClose = config.readTargetsOnClose,
                    estimateTextureRegion = config.estimateTextureRegion,
                    gpuPaletteConversion = config.gpuPaletteConversion,
                    halfPixelOffset = config.halfPixelOffset,
                    nativeScaling = config.nativeScaling,
                    roundSprite = config.roundSprite,
                    bilinearUpscale = config.bilinearUpscale,
                    textureOffsetX = config.textureOffsetX,
                    textureOffsetY = config.textureOffsetY,
                    alignSprite = config.alignSprite,
                    mergeSprite = config.mergeSprite,
                    forceEvenSpritePosition = config.forceEvenSpritePosition,
                    nativePaletteDraw = config.nativePaletteDraw
                )

                _uiState.value = _uiState.value.copy(
                    statusMessage = "status_loading_game"
                )
                delay(200)

                val launchPath = when {
                    bootToBios -> ""
                    path.isNullOrBlank() -> null
                    path.startsWith("content://") -> path
                    else -> DocumentPathResolver.resolveFilePath(getApplication(), path)
                }

                if (!bootToBios && launchPath.isNullOrBlank()) {
                    _uiState.value = _uiState.value.copy(
                        isStarting = false,
                        statusMessage = null,
                        toastMessage = "launch_path_error"
                    )
                    delay(2500)
                    _uiState.value = _uiState.value.copy(toastMessage = null)
                    return@withLock
                }
                
                finalLaunchPath = launchPath
                if (bootToBios) {
                    currentGameTitle = "PlayStation 2 BIOS"
                    currentGameSerial = ""
                    currentGameCrc = ""
                    currentGameSource = "bios_only"
                    _uiState.value = _uiState.value.copy(
                        cheatsGameKey = null,
                        availableCheats = emptyList()
                    )
                } else {
                    val safePath = path.orEmpty()
                    val metadata = EmulatorBridge.getGameMetadata(safePath)
                    currentGameTitle = metadata.title
                    currentGameSerial = metadata.serial.orEmpty()
                    currentGameCrc = metadata.serialWithCrc.extractCrc().orEmpty()
                    currentGameSource = when {
                        safePath.startsWith("content://") -> "content_uri"
                        launchPath?.startsWith("/") == true -> "file"
                        else -> "unknown"
                    }
                    refreshCurrentGameCheats(metadata)
                }
                updateCrashContext(
                    launchState = "starting",
                    launchPath = path
                )

                _uiState.value = _uiState.value.copy(
                    isRunning = true,
                    isStarting = false,
                    statusMessage = "status_starting_core"
                )
                if (!bootToBios && !path.isNullOrBlank()) {
                    val displayName = DocumentPathResolver.getDisplayName(getApplication(), path)
                    preferences.markGameLaunched(path = path, title = displayName.substringBeforeLast('.'))
                }

                val liveRuntime = loadLiveRuntimeSnapshot()

                _uiState.value = _uiState.value.copy(
                    renderer = liveRuntime.renderer,
                    upscale = liveRuntime.upscale,
                    aspectRatio = liveRuntime.aspectRatio,
                    performancePreset = liveRuntime.performancePreset,
                    enableMtvu = liveRuntime.enableMtvu,
                    enableFastCdvd = liveRuntime.enableFastCdvd,
                    enableCheats = liveRuntime.enableCheats,
                    hwDownloadMode = liveRuntime.hwDownloadMode,
                    frameSkip = liveRuntime.frameSkip,
                    textureFiltering = liveRuntime.textureFiltering,
                    trilinearFiltering = liveRuntime.trilinearFiltering,
                    blendingAccuracy = liveRuntime.blendingAccuracy,
                    texturePreloading = liveRuntime.texturePreloading,
                    enableFxaa = liveRuntime.enableFxaa,
                    casMode = liveRuntime.casMode,
                    casSharpness = liveRuntime.casSharpness,
                    anisotropicFiltering = liveRuntime.anisotropicFiltering,
                    enableHwMipmapping = liveRuntime.enableHwMipmapping,
                    cpuSpriteRenderSize = liveRuntime.cpuSpriteRenderSize,
                    cpuSpriteRenderLevel = liveRuntime.cpuSpriteRenderLevel,
                    softwareClutRender = liveRuntime.softwareClutRender,
                    gpuTargetClutMode = liveRuntime.gpuTargetClutMode,
                    skipDrawStart = liveRuntime.skipDrawStart,
                    skipDrawEnd = liveRuntime.skipDrawEnd,
                    autoFlushHardware = liveRuntime.autoFlushHardware,
                    cpuFramebufferConversion = liveRuntime.cpuFramebufferConversion,
                    disableDepthConversion = liveRuntime.disableDepthConversion,
                    disableSafeFeatures = liveRuntime.disableSafeFeatures,
                    disableRenderFixes = liveRuntime.disableRenderFixes,
                    preloadFrameData = liveRuntime.preloadFrameData,
                    disablePartialInvalidation = liveRuntime.disablePartialInvalidation,
                    textureInsideRt = liveRuntime.textureInsideRt,
                    readTargetsOnClose = liveRuntime.readTargetsOnClose,
                    estimateTextureRegion = liveRuntime.estimateTextureRegion,
                    gpuPaletteConversion = liveRuntime.gpuPaletteConversion,
                    halfPixelOffset = liveRuntime.halfPixelOffset,
                    nativeScaling = liveRuntime.nativeScaling,
                    roundSprite = liveRuntime.roundSprite,
                    bilinearUpscale = liveRuntime.bilinearUpscale,
                    textureOffsetX = liveRuntime.textureOffsetX,
                    textureOffsetY = liveRuntime.textureOffsetY,
                    alignSprite = liveRuntime.alignSprite,
                    mergeSprite = liveRuntime.mergeSprite,
                    forceEvenSpritePosition = liveRuntime.forceEvenSpritePosition,
                    nativePaletteDraw = liveRuntime.nativePaletteDraw,
                    frameLimitEnabled = liveRuntime.frameLimitEnabled,
                    targetFps = liveRuntime.targetFps
                )
                updateCrashContext(
                    launchState = "starting",
                    launchPath = path
                )
            }
            
            if (!bootToBios && slotToLoad != null) {
                viewModelScope.launch(Dispatchers.IO) {
                    var vmReadyWaitFrames = 0
                    while (vmReadyWaitFrames < 40 && isActive) {
                        try {
                            if (EmulatorBridge.hasValidVm()) break
                        } catch (_: Exception) { }
                        delay(250)
                        vmReadyWaitFrames++
                    }
                    delay(500)
                    _uiState.value = _uiState.value.copy(statusMessage = "status_loading_state")
                    EmulatorBridge.loadState(slotToLoad)
                    _uiState.value = _uiState.value.copy(
                        statusMessage = "status_running",
                        toastMessage = "loaded"
                    )
                    delay(2500)
                    if (_uiState.value.toastMessage == "loaded") {
                        _uiState.value = _uiState.value.copy(toastMessage = null)
                    }
                }
            }

            val pathToLaunch = finalLaunchPath ?: return@launch
            
            if (cancelPendingStart) {
                _uiState.value = _uiState.value.copy(
                    isRunning = false,
                    isStarting = false,
                    statusMessage = null
                )
                return@launch
            }

            viewModelScope.launch(Dispatchers.IO) {
                var waitFrames = 0
                while (waitFrames < 60 && isActive) {
                    if (EmulatorBridge.hasValidVm()) {
                        _uiState.value = _uiState.value.copy(statusMessage = "status_running")
                        delay(2000)
                        if (_uiState.value.statusMessage == "status_running") {
                            _uiState.value = _uiState.value.copy(statusMessage = null)
                        }
                        break
                    }
                    delay(250)
                    waitFrames++
                }
            }

            val started = try {
                EmulatorBridge.startEmulation(pathToLaunch)
            } catch (_: Exception) {
                false
            }
            updateCrashContext(
                launchState = if (started) "running" else "launch_failed",
                launchPath = path
            )
            
            if (!started && !_uiState.value.isPaused) {
                _uiState.value = _uiState.value.copy(
                    isRunning = false,
                    statusMessage = null,
                    toastMessage = "launch_failed"
                )
                delay(2500)
                _uiState.value = _uiState.value.copy(toastMessage = null)
            }
        }
    }

    fun togglePause() {
        val isPaused = _uiState.value.isPaused
        try {
            if (isPaused) {
                EmulatorBridge.resume()
            } else {
                EmulatorBridge.pause()
            }
        } catch (_: Exception) { }
        _uiState.value = _uiState.value.copy(
            isPaused = !isPaused,
            showMenu = if (isPaused) false else _uiState.value.showMenu
        )
        updateCrashContext(launchState = if (!isPaused) "paused" else "running")
    }

    fun toggleMenu() {
        val showMenu = !_uiState.value.showMenu
        try {
            if (showMenu) {
                EmulatorBridge.resetKeyStatus()
                EmulatorBridge.pause()
                _uiState.value = _uiState.value.copy(showMenu = true, isPaused = true)
            } else {
                EmulatorBridge.resume()
                _uiState.value = _uiState.value.copy(showMenu = false, isPaused = false)
            }
        } catch (_: Exception) { }
    }

    fun toggleControlsVisibility() {
        _uiState.value = _uiState.value.copy(
            controlsVisible = !_uiState.value.controlsVisible
        )
    }

    fun toggleFpsVisibility() {
        viewModelScope.launch {
            val newValue = !_uiState.value.showFps
            preferences.setShowFps(newValue)
            _uiState.value = _uiState.value.copy(showFps = newValue)
        }
    }

    fun setFpsOverlayMode(mode: Int) {
        viewModelScope.launch {
            preferences.setFpsOverlayMode(mode)
            _uiState.value = _uiState.value.copy(fpsOverlayMode = mode)
        }
    }

    fun setRenderer(renderer: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setRenderer(renderer)
            _uiState.value = _uiState.value.copy(renderer = renderer)
            EmulatorBridge.setRenderer(renderer)
            updateCrashContext()
        }
    }

    fun setUpscale(upscale: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setUpscaleMultiplier(upscale)
            _uiState.value = _uiState.value.copy(upscale = upscale)
            EmulatorBridge.setUpscaleMultiplier(upscale.toFloat())
            updateCrashContext()
        }
    }

    fun setAspectRatio(value: Int) {
        viewModelScope.launch {
            preferences.setAspectRatio(value)
            _uiState.value = _uiState.value.copy(aspectRatio = value)
            EmulatorBridge.setAspectRatio(value)
            updateCrashContext()
        }
    }

    fun setMtvu(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableMtvu(enabled)
            _uiState.value = _uiState.value.copy(enableMtvu = enabled)
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "vuThread", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setFastCdvd(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableFastCdvd(enabled)
            _uiState.value = _uiState.value.copy(enableFastCdvd = enabled)
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "fastCDVD", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setEnableCheats(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setEnableCheats(enabled)
            _uiState.value = _uiState.value.copy(enableCheats = enabled)
            EmulatorBridge.setSetting("EmuCore", "EnableCheats", "bool", enabled.toString())
            if (enabled) {
                syncCheatsForCurrentGame()
            }
            updateCrashContext()
        }
    }

    fun setCheatEnabled(blockId: String, enabled: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            val currentState = _uiState.value
            val gameKey = currentState.cheatsGameKey ?: return@launch
            val updatedBlocks = currentState.availableCheats.map { block ->
                if (block.id == blockId) block.copy(enabled = enabled) else block
            }
            cheatRepository.setEnabledBlocks(
                gameKey = gameKey,
                enabledIds = updatedBlocks.filter { it.enabled }.map { it.id }.toSet()
            )
            syncCheatsForCurrentGame(gameKey)
            _uiState.value = _uiState.value.copy(
                availableCheats = updatedBlocks,
                enableCheats = true
            )
            preferences.setEnableCheats(true)
            EmulatorBridge.setSetting("EmuCore", "EnableCheats", "bool", "true")
        }
    }

    fun setHwDownloadMode(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setHwDownloadMode(value)
            _uiState.value = _uiState.value.copy(hwDownloadMode = value)
            EmulatorBridge.setSetting("EmuCore/GS", "HWDownloadMode", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setFrameSkip(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setFrameSkip(value)
            _uiState.value = _uiState.value.copy(frameSkip = value)
            EmulatorBridge.setSetting("EmuCore/GS", "FrameSkip", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setFrameLimitEnabled(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setFrameLimitEnabled(enabled)
            _uiState.value = _uiState.value.copy(frameLimitEnabled = enabled)
            EmulatorBridge.setFrameLimitEnabled(enabled)
            updateCrashContext()
        }
    }

    fun setTargetFps(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(20, 120)
            preferences.setTargetFps(clamped)
            _uiState.value = _uiState.value.copy(targetFps = clamped)
            EmulatorBridge.setTargetFps(clamped)
            updateCrashContext()
        }
    }

    fun setTextureFiltering(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureFiltering(value)
            _uiState.value = _uiState.value.copy(textureFiltering = value)
            EmulatorBridge.setSetting("EmuCore/GS", "filter", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setTrilinearFiltering(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTrilinearFiltering(value)
            _uiState.value = _uiState.value.copy(trilinearFiltering = value)
            EmulatorBridge.setSetting("EmuCore/GS", "TriFilter", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setBlendingAccuracy(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setBlendingAccuracy(value)
            _uiState.value = _uiState.value.copy(blendingAccuracy = value)
            EmulatorBridge.setSetting("EmuCore/GS", "accurate_blending_unit", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setTexturePreloading(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTexturePreloading(value)
            _uiState.value = _uiState.value.copy(texturePreloading = value)
            EmulatorBridge.setSetting("EmuCore/GS", "texture_preloading", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setEnableFxaa(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableFxaa(enabled)
            _uiState.value = _uiState.value.copy(enableFxaa = enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "fxaa", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setCasMode(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCasMode(value)
            _uiState.value = _uiState.value.copy(casMode = value)
            EmulatorBridge.setSetting("EmuCore/GS", "CASMode", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setCasSharpness(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(0, 100)
            markPerformancePresetCustom()
            preferences.setCasSharpness(clamped)
            _uiState.value = _uiState.value.copy(casSharpness = clamped)
            EmulatorBridge.setSetting("EmuCore/GS", "CASSharpness", "int", clamped.toString())
            updateCrashContext()
        }
    }

    fun setAnisotropicFiltering(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setAnisotropicFiltering(value)
            _uiState.value = _uiState.value.copy(anisotropicFiltering = value)
            EmulatorBridge.setSetting("EmuCore/GS", "MaxAnisotropy", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setEnableHwMipmapping(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableHwMipmapping(enabled)
            _uiState.value = _uiState.value.copy(enableHwMipmapping = enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "hw_mipmap", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setCpuSpriteRenderSize(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCpuSpriteRenderSize(value)
            val newState = _uiState.value.copy(cpuSpriteRenderSize = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderBW", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setCpuSpriteRenderLevel(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCpuSpriteRenderLevel(value)
            val newState = _uiState.value.copy(cpuSpriteRenderLevel = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setSoftwareClutRender(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setSoftwareClutRender(value)
            val newState = _uiState.value.copy(softwareClutRender = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUCLUTRender", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setGpuTargetClutMode(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setGpuTargetClutMode(value)
            val newState = _uiState.value.copy(gpuTargetClutMode = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_GPUTargetCLUTMode", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setSkipDrawStart(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setSkipDrawStart(value)
            val newState = _uiState.value.copy(skipDrawStart = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_Start", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setSkipDrawEnd(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setSkipDrawEnd(value)
            val newState = _uiState.value.copy(skipDrawEnd = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_End", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setAutoFlushHardware(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setAutoFlushHardware(value)
            val newState = _uiState.value.copy(autoFlushHardware = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_AutoFlushLevel", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setCpuFramebufferConversion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCpuFramebufferConversion(enabled)
            val newState = _uiState.value.copy(cpuFramebufferConversion = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPU_FB_Conversion", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisableDepthConversion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisableDepthConversion(enabled)
            val newState = _uiState.value.copy(disableDepthConversion = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisableDepthSupport", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisableSafeFeatures(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisableSafeFeatures(enabled)
            val newState = _uiState.value.copy(disableSafeFeatures = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_Disable_Safe_Features", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisableRenderFixes(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisableRenderFixes(enabled)
            val newState = _uiState.value.copy(disableRenderFixes = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisableRenderFixes", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setPreloadFrameData(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setPreloadFrameData(enabled)
            val newState = _uiState.value.copy(preloadFrameData = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "preload_frame_with_gs_data", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisablePartialInvalidation(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisablePartialInvalidation(enabled)
            val newState = _uiState.value.copy(disablePartialInvalidation = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisablePartialInvalidation", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setTextureInsideRt(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureInsideRt(value)
            val newState = _uiState.value.copy(textureInsideRt = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TextureInsideRt", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setReadTargetsOnClose(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setReadTargetsOnClose(enabled)
            val newState = _uiState.value.copy(readTargetsOnClose = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_ReadTCOnClose", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setEstimateTextureRegion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEstimateTextureRegion(enabled)
            val newState = _uiState.value.copy(estimateTextureRegion = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_EstimateTextureRegion", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setGpuPaletteConversion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setGpuPaletteConversion(enabled)
            val newState = _uiState.value.copy(gpuPaletteConversion = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "paltex", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setHalfPixelOffset(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setHalfPixelOffset(value)
            val newState = _uiState.value.copy(halfPixelOffset = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_HalfPixelOffset", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setNativeScaling(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setNativeScaling(value)
            val newState = _uiState.value.copy(nativeScaling = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_native_scaling", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setRoundSprite(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setRoundSprite(value)
            val newState = _uiState.value.copy(roundSprite = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_round_sprite_offset", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setBilinearUpscale(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setBilinearUpscale(value)
            val newState = _uiState.value.copy(bilinearUpscale = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_BilinearHack", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setTextureOffsetX(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureOffsetX(value)
            val newState = _uiState.value.copy(textureOffsetX = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TCOffsetX", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setTextureOffsetY(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureOffsetY(value)
            val newState = _uiState.value.copy(textureOffsetY = value)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TCOffsetY", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setAlignSprite(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setAlignSprite(enabled)
            val newState = _uiState.value.copy(alignSprite = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_align_sprite_X", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setMergeSprite(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setMergeSprite(enabled)
            val newState = _uiState.value.copy(mergeSprite = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_merge_pp_sprite", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setForceEvenSpritePosition(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setForceEvenSpritePosition(enabled)
            val newState = _uiState.value.copy(forceEvenSpritePosition = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_ForceEvenSpritePosition", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setNativePaletteDraw(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setNativePaletteDraw(enabled)
            val newState = _uiState.value.copy(nativePaletteDraw = enabled)
            _uiState.value = newState
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_NativePaletteDraw", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun applyPerformancePreset(presetId: Int) {
        viewModelScope.launch {
            val config = PerformancePresets.configFor(presetId, deviceProfile) ?: run {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                return@launch
            }

            preferences.applyPerformancePreset(config)
            _uiState.value = _uiState.value.copy(
                performancePreset = config.id,
                renderer = config.renderer,
                upscale = config.upscaleMultiplier,
                enableMtvu = config.enableMtvu,
                enableFastCdvd = config.enableFastCdvd,
                frameSkip = config.frameSkip,
                textureFiltering = config.textureFiltering
            )
            applyRuntimePerformancePreset(config)
            updateCrashContext()
        }
    }

    fun applyRecommendedDeviceProfile() {
        applyPerformancePreset(deviceProfile.recommendedPresetId)
    }

    private suspend fun markPerformancePresetCustom() {
        if (_uiState.value.performancePreset != PerformancePresets.CUSTOM) {
            preferences.setPerformancePreset(PerformancePresets.CUSTOM)
            _uiState.value = _uiState.value.copy(performancePreset = PerformancePresets.CUSTOM)
        }
    }

    private fun refreshManualHardwareFixes(state: EmulationUiState = _uiState.value) {
        val enabled = GsHackDefaults.shouldEnableManualHardwareFixes(
            cpuSpriteRenderSize = state.cpuSpriteRenderSize,
            cpuSpriteRenderLevel = state.cpuSpriteRenderLevel,
            softwareClutRender = state.softwareClutRender,
            gpuTargetClutMode = state.gpuTargetClutMode,
            skipDrawStart = state.skipDrawStart,
            skipDrawEnd = state.skipDrawEnd,
            autoFlushHardware = state.autoFlushHardware,
            cpuFramebufferConversion = state.cpuFramebufferConversion,
            disableDepthConversion = state.disableDepthConversion,
            disableSafeFeatures = state.disableSafeFeatures,
            disableRenderFixes = state.disableRenderFixes,
            preloadFrameData = state.preloadFrameData,
            disablePartialInvalidation = state.disablePartialInvalidation,
            textureInsideRt = state.textureInsideRt,
            readTargetsOnClose = state.readTargetsOnClose,
            estimateTextureRegion = state.estimateTextureRegion,
            gpuPaletteConversion = state.gpuPaletteConversion,
            halfPixelOffset = state.halfPixelOffset,
            nativeScaling = state.nativeScaling,
            roundSprite = state.roundSprite,
            bilinearUpscale = state.bilinearUpscale,
            textureOffsetX = state.textureOffsetX,
            textureOffsetY = state.textureOffsetY,
            alignSprite = state.alignSprite,
            mergeSprite = state.mergeSprite,
            forceEvenSpritePosition = state.forceEvenSpritePosition,
            nativePaletteDraw = state.nativePaletteDraw
        )
        EmulatorBridge.setSetting("EmuCore/GS", "UserHacks", "bool", enabled.toString())
    }

    private fun applyRuntimePerformancePreset(config: PerformancePresetConfig) {
        EmulatorBridge.setSetting("EmuCore/Speedhacks", "EECycleRate", "int", config.eeCycleRate.toString())
        EmulatorBridge.setSetting("EmuCore/Speedhacks", "EECycleSkip", "int", config.eeCycleSkip.toString())
        EmulatorBridge.setSetting("EmuCore/Speedhacks", "vuThread", "bool", config.enableMtvu.toString())
        EmulatorBridge.setSetting("EmuCore/Speedhacks", "fastCDVD", "bool", config.enableFastCdvd.toString())
        EmulatorBridge.setSetting("EmuCore/GS", "FrameSkip", "int", config.frameSkip.toString())
        EmulatorBridge.setSetting("EmuCore/GS", "filter", "int", config.textureFiltering.toString())
        EmulatorBridge.setSetting("EmuCore/GS", "MaxAnisotropy", "int", config.anisotropicFiltering.toString())
        EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_Start", "int", "0")
        EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_End", "int", config.skipDraw.toString())
        EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_HalfPixelOffset", "int", config.halfPixelOffset.toString())
        EmulatorBridge.setSetting("EmuCore", "EnableWideScreenPatches", "bool", config.widescreenPatches.toString())
        EmulatorBridge.setUpscaleMultiplier(config.upscaleMultiplier.toFloat())
        EmulatorBridge.setRenderer(config.renderer)
        refreshManualHardwareFixes(
            _uiState.value.copy(
                skipDrawStart = 0,
                skipDrawEnd = config.skipDraw,
                halfPixelOffset = config.halfPixelOffset
            )
        )
    }

    private suspend fun loadLaunchConfig(): EmulationLaunchConfig {
        return EmulationLaunchConfig(
            biosPath = preferences.biosPath.first(),
            renderer = preferences.renderer.first(),
            upscaleMultiplier = preferences.upscaleMultiplier.first(),
            gpuDriverType = preferences.gpuDriverType.first(),
            customDriverPath = preferences.customDriverPath.first(),
            aspectRatio = preferences.aspectRatio.first(),
            mtvu = preferences.enableMtvu.first(),
            fastCdvd = preferences.enableFastCdvd.first(),
            enableCheats = preferences.enableCheats.first(),
            hwDownloadMode = preferences.hwDownloadMode.first(),
            eeCycleRate = preferences.eeCycleRate.first(),
            eeCycleSkip = preferences.eeCycleSkip.first(),
            frameSkip = preferences.frameSkip.first(),
            frameLimitEnabled = preferences.frameLimitEnabled.first(),
            targetFps = preferences.targetFps.first(),
            textureFiltering = preferences.textureFiltering.first(),
            trilinearFiltering = preferences.trilinearFiltering.first(),
            blendingAccuracy = preferences.blendingAccuracy.first(),
            texturePreloading = preferences.texturePreloading.first(),
            enableFxaa = preferences.enableFxaa.first(),
            casMode = preferences.casMode.first(),
            casSharpness = preferences.casSharpness.first(),
            anisotropicFiltering = preferences.anisotropicFiltering.first(),
            enableHwMipmapping = preferences.enableHwMipmapping.first(),
            widescreenPatches = preferences.enableWidescreenPatches.first(),
            noInterlacingPatches = preferences.enableNoInterlacingPatches.first(),
            cpuSpriteRenderSize = preferences.cpuSpriteRenderSize.first(),
            cpuSpriteRenderLevel = preferences.cpuSpriteRenderLevel.first(),
            softwareClutRender = preferences.softwareClutRender.first(),
            gpuTargetClutMode = preferences.gpuTargetClutMode.first(),
            skipDrawStart = preferences.skipDrawStart.first(),
            skipDrawEnd = preferences.skipDrawEnd.first(),
            autoFlushHardware = preferences.autoFlushHardware.first(),
            cpuFramebufferConversion = preferences.cpuFramebufferConversion.first(),
            disableDepthConversion = preferences.disableDepthConversion.first(),
            disableSafeFeatures = preferences.disableSafeFeatures.first(),
            disableRenderFixes = preferences.disableRenderFixes.first(),
            preloadFrameData = preferences.preloadFrameData.first(),
            disablePartialInvalidation = preferences.disablePartialInvalidation.first(),
            textureInsideRt = preferences.textureInsideRt.first(),
            readTargetsOnClose = preferences.readTargetsOnClose.first(),
            estimateTextureRegion = preferences.estimateTextureRegion.first(),
            gpuPaletteConversion = preferences.gpuPaletteConversion.first(),
            halfPixelOffset = preferences.halfPixelOffset.first(),
            nativeScaling = preferences.nativeScaling.first(),
            roundSprite = preferences.roundSprite.first(),
            bilinearUpscale = preferences.bilinearUpscale.first(),
            textureOffsetX = preferences.textureOffsetX.first(),
            textureOffsetY = preferences.textureOffsetY.first(),
            alignSprite = preferences.alignSprite.first(),
            mergeSprite = preferences.mergeSprite.first(),
            forceEvenSpritePosition = preferences.forceEvenSpritePosition.first(),
            nativePaletteDraw = preferences.nativePaletteDraw.first()
        )
    }

    private suspend fun loadLiveRuntimeSnapshot(): LiveRuntimeSnapshot {
        return LiveRuntimeSnapshot(
            renderer = preferences.renderer.first(),
            upscale = preferences.upscaleMultiplier.first(),
            aspectRatio = preferences.aspectRatio.first(),
            performancePreset = preferences.performancePreset.first(),
            enableMtvu = preferences.enableMtvu.first(),
            enableFastCdvd = preferences.enableFastCdvd.first(),
            enableCheats = preferences.enableCheats.first(),
            hwDownloadMode = preferences.hwDownloadMode.first(),
            frameSkip = preferences.frameSkip.first(),
            frameLimitEnabled = preferences.frameLimitEnabled.first(),
            targetFps = preferences.targetFps.first(),
            textureFiltering = preferences.textureFiltering.first(),
            trilinearFiltering = preferences.trilinearFiltering.first(),
            blendingAccuracy = preferences.blendingAccuracy.first(),
            texturePreloading = preferences.texturePreloading.first(),
            enableFxaa = preferences.enableFxaa.first(),
            casMode = preferences.casMode.first(),
            casSharpness = preferences.casSharpness.first(),
            anisotropicFiltering = preferences.anisotropicFiltering.first(),
            enableHwMipmapping = preferences.enableHwMipmapping.first(),
            cpuSpriteRenderSize = preferences.cpuSpriteRenderSize.first(),
            cpuSpriteRenderLevel = preferences.cpuSpriteRenderLevel.first(),
            softwareClutRender = preferences.softwareClutRender.first(),
            gpuTargetClutMode = preferences.gpuTargetClutMode.first(),
            skipDrawStart = preferences.skipDrawStart.first(),
            skipDrawEnd = preferences.skipDrawEnd.first(),
            autoFlushHardware = preferences.autoFlushHardware.first(),
            cpuFramebufferConversion = preferences.cpuFramebufferConversion.first(),
            disableDepthConversion = preferences.disableDepthConversion.first(),
            disableSafeFeatures = preferences.disableSafeFeatures.first(),
            disableRenderFixes = preferences.disableRenderFixes.first(),
            preloadFrameData = preferences.preloadFrameData.first(),
            disablePartialInvalidation = preferences.disablePartialInvalidation.first(),
            textureInsideRt = preferences.textureInsideRt.first(),
            readTargetsOnClose = preferences.readTargetsOnClose.first(),
            estimateTextureRegion = preferences.estimateTextureRegion.first(),
            gpuPaletteConversion = preferences.gpuPaletteConversion.first(),
            halfPixelOffset = preferences.halfPixelOffset.first(),
            nativeScaling = preferences.nativeScaling.first(),
            roundSprite = preferences.roundSprite.first(),
            bilinearUpscale = preferences.bilinearUpscale.first(),
            textureOffsetX = preferences.textureOffsetX.first(),
            textureOffsetY = preferences.textureOffsetY.first(),
            alignSprite = preferences.alignSprite.first(),
            mergeSprite = preferences.mergeSprite.first(),
            forceEvenSpritePosition = preferences.forceEvenSpritePosition.first(),
            nativePaletteDraw = preferences.nativePaletteDraw.first()
        )
    }

    private fun refreshCurrentGameCheats(metadata: com.sbro.emucorex.core.GameMetadata) {
        val serial = metadata.serial.orEmpty()
        val crc = metadata.serialWithCrc.extractCrc()
        val config = cheatRepository.getGameConfig(
            gameKeys = cheatLookupKeys(metadata),
            serial = serial,
            crc = crc
        )
        _uiState.value = _uiState.value.copy(
            cheatsGameKey = config?.gameKey,
            availableCheats = config?.blocks.orEmpty()
        )
        if (config != null && _uiState.value.enableCheats) {
            cheatRepository.syncActiveCheats(config.gameKey, serial, crc)
        }
    }

    private fun syncCheatsForCurrentGame(gameKeyOverride: String? = null) {
        val gameKey = gameKeyOverride ?: _uiState.value.cheatsGameKey ?: return
        cheatRepository.syncActiveCheats(
            gameKey = gameKey,
            serial = currentGameSerial.takeIf { it.isNotBlank() },
            crc = currentGameCrc.takeIf { it.isNotBlank() }
        )
    }

    private fun cheatLookupKeys(metadata: com.sbro.emucorex.core.GameMetadata): List<String> {
        val keys = linkedSetOf<String>()
        metadata.serialWithCrc?.trim()?.takeIf { it.isNotBlank() }?.let(keys::add)
        metadata.serialWithCrc.extractSerialAndCrcKey()?.let(keys::add)
        metadata.serial?.trim()?.takeIf { it.isNotBlank() }?.let(keys::add)
        metadata.title.trim().takeIf { it.isNotBlank() }?.let(keys::add)
        return keys.toList()
    }

    fun setSlot(slot: Int) {
        _uiState.value = _uiState.value.copy(currentSlot = slot.coerceIn(0, 4))
    }

    fun quickSave() {
        val slot = _uiState.value.currentSlot
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                isActionInProgress = true,
                actionLabel = "saving"
            )
            val success = lifecycleMutex.withLock {
                if (isShuttingDown) {
                    false
                } else {
                    try {
                        EmulatorBridge.saveState(slot)
                    } catch (_: Exception) { false }
                }
            }
            _uiState.value = _uiState.value.copy(
                isActionInProgress = false,
                actionLabel = null,
                toastMessage = if (success) "saved" else null
            )
            delay(2000)
            _uiState.value = _uiState.value.copy(toastMessage = null)
        }
    }

    fun quickLoad() {
        val slot = _uiState.value.currentSlot
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                isActionInProgress = true,
                actionLabel = "loading"
            )
            val success = lifecycleMutex.withLock {
                if (isShuttingDown) {
                    false
                } else {
                    try {
                        EmulatorBridge.loadState(slot)
                    } catch (_: Exception) { false }
                }
            }
            _uiState.value = _uiState.value.copy(
                isActionInProgress = false,
                actionLabel = null,
                toastMessage = if (success) "loaded" else null
            )
            delay(2000)
            _uiState.value = _uiState.value.copy(toastMessage = null)
        }
    }

    fun stopEmulation(onExit: (() -> Unit)? = null) {
        cancelPendingStart = true
        viewModelScope.launch(Dispatchers.IO) {
            performShutdown()
            if (onExit != null) {
                kotlinx.coroutines.withContext(Dispatchers.Main) {
                    onExit.invoke()
                }
            }
        }
    }

    private suspend fun performShutdown() {
        lifecycleMutex.withLock {
            if (!_uiState.value.isRunning && !_uiState.value.isStarting) return
            if (isShuttingDown) return
            isShuttingDown = true
            try {
                try {
                    EmulatorBridge.resetKeyStatus()
                } catch (_: Exception) { }
                try {
                    EmulatorBridge.shutdown()
                    var waitTime = 0
                    while (EmulatorBridge.isVmActive() && waitTime < 2000) {
                        delay(50)
                        waitTime += 50
                    }
                } catch (_: Exception) { }
                _uiState.value = _uiState.value.copy(
                    isRunning = false,
                    isStarting = false,
                    isPaused = false,
                    showMenu = false,
                    isActionInProgress = false,
                    actionLabel = null,
                    fps = "0",
                    speedPercent = "100",
                    frameTime = "0.00",
                    cpuLoad = "0",
                    gpuLoad = "0",
                    statusMessage = null
                )
                clearCrashContext()
            } finally {
                isShuttingDown = false
            }
        }
    }

    private fun updateCrashContext(
        launchState: String? = null,
        launchPath: String? = null
    ) {
        val state = _uiState.value
        NativeApp.setCrashContextString("emu_launch_state", launchState ?: when {
            state.isStarting -> "starting"
            state.isPaused -> "paused"
            state.isRunning -> "running"
            else -> "idle"
        })
        NativeApp.setCrashContextString("emu_game_title", currentGameTitle)
        NativeApp.setCrashContextString("emu_game_serial", currentGameSerial)
        NativeApp.setCrashContextString("emu_game_source", currentGameSource)
        NativeApp.setCrashContextString("emu_game_path_hint", launchPath?.let { File(it).name }.orEmpty())
        NativeApp.setCrashContextInt("emu_renderer", state.renderer)
        NativeApp.setCrashContextString("emu_renderer_name", when (state.renderer) {
            12 -> "OpenGL"
            13 -> "Software"
            14 -> "Vulkan"
            else -> "Unknown(${state.renderer})"
        })
        NativeApp.setCrashContextInt("emu_upscale", state.upscale)
        NativeApp.setCrashContextInt("emu_aspect_ratio", state.aspectRatio)
        NativeApp.setCrashContextInt("emu_performance_preset", state.performancePreset)
        NativeApp.setCrashContextBool("emu_mtvu", state.enableMtvu)
        NativeApp.setCrashContextBool("emu_fast_cdvd", state.enableFastCdvd)
        NativeApp.setCrashContextBool("emu_enable_cheats", state.enableCheats)
        NativeApp.setCrashContextInt("emu_hw_download_mode", state.hwDownloadMode)
        NativeApp.setCrashContextInt("emu_frame_skip", state.frameSkip)
        NativeApp.setCrashContextBool("emu_frame_limit_enabled", state.frameLimitEnabled)
        NativeApp.setCrashContextInt("emu_target_fps", state.targetFps)
        NativeApp.setCrashContextInt("emu_texture_filtering", state.textureFiltering)
        NativeApp.setCrashContextString("emu_device_model", Build.MODEL.orEmpty())
        NativeApp.setCrashContextString("emu_soc_model", if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else "")
        NativeApp.setCrashContextString("emu_device_family", deviceProfile.family.name)
        NativeApp.setCrashContextBool("emu_running", state.isRunning)
        NativeApp.setCrashContextBool("emu_paused", state.isPaused)
    }

    private fun clearCrashContext() {
        currentGameTitle = ""
        currentGameSerial = ""
        currentGameCrc = ""
        currentGameSource = ""
        NativeApp.setCrashContextString("emu_launch_state", "idle")
        NativeApp.setCrashContextString("emu_game_title", "")
        NativeApp.setCrashContextString("emu_game_serial", "")
        NativeApp.setCrashContextString("emu_game_source", "")
        NativeApp.setCrashContextString("emu_game_path_hint", "")
        NativeApp.setCrashContextBool("emu_running", false)
        NativeApp.setCrashContextBool("emu_paused", false)
    }

    fun onPadInput(keyCode: Int, range: Int = 0, pressed: Boolean) {
        try {
            EmulatorBridge.setPadButton(keyCode, range, pressed)
        } catch (_: Exception) { }
    }

    fun onPadInputs(indices: IntArray, values: FloatArray) {
        if (indices.isEmpty() || indices.size != values.size) return
        try {
            EmulatorBridge.setPadParams(indices, values)
        } catch (_: Exception) { }
    }

    override fun onCleared() {
        NativeApp.setPerformanceListener(null)
        super.onCleared()
        if (_uiState.value.isRunning) {
            try {
                EmulatorBridge.resetKeyStatus()
                EmulatorBridge.shutdown()
            } catch (_: Exception) { }
        }
    }
}

private fun String?.extractCrc(): String? {
    return this
        ?.substringAfter('(', "")
        ?.substringBefore(')')
        ?.trim()
        ?.takeIf { it.isNotBlank() }
}

private fun String?.extractSerialAndCrcKey(): String? {
    val raw = this?.trim().orEmpty()
    if (raw.isBlank()) return null
    val serial = raw.substringBefore('(').trim()
    val crc = raw.extractCrc()
    return if (serial.isNotBlank() && !crc.isNullOrBlank()) "${serial}_$crc" else null
}

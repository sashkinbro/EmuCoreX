package com.sbro.emucorex.ui.emulation

import android.app.Application
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.core.normalizeUpscale
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.CheatBlock
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.data.OverlayLayoutSnapshot
import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.PerGameSettingsRepository
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
    val fpsOverlayCorner: Int = AppPreferences.FPS_OVERLAY_CORNER_TOP_RIGHT,
    val overlayScale: Int = 100,
    val overlayOpacity: Int = 80,
    val hideOverlayOnGamepad: Boolean = true,
    val dpadOffset: Pair<Float, Float> = AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y,
    val lstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y,
    val rstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y,
    val actionOffset: Pair<Float, Float> = AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y,
    val lbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y,
    val rbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y,
    val centerOffset: Pair<Float, Float> = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
    val stickScale: Int = 100,
    val leftStickSensitivity: Int = 100,
    val rightStickSensitivity: Int = 100,
    val gamepadStickDeadzone: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_DEADZONE,
    val gamepadLeftStickSensitivity: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY,
    val gamepadRightStickSensitivity: Int = AppPreferences.DEFAULT_GAMEPAD_STICK_SENSITIVITY,
    val stickSurfaceMode: Boolean = false,
    val controlLayouts: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts(),
    val fps: String = "0.0",
    val fpsOverlayMode: Int = FPS_OVERLAY_MODE_DETAILED,
    val performanceOverlayText: String = "",
    val speedPercent: Float = 100f,
    val toastMessage: String? = null,
    val statusMessage: String? = null,
    val currentSlot: Int = 1,
    val renderer: Int = EmulatorBridge.DEFAULT_RENDERER,
    val upscale: Float = 1f,
    val aspectRatio: Int = 1,
    val performancePreset: Int = PerformancePresets.CUSTOM,
    val enableMtvu: Boolean = true,
    val enableFastCdvd: Boolean = false,
    val enableCheats: Boolean = true,
    val hwDownloadMode: Int = 0,
    val frameSkip: Int = 0,
    val skipDuplicateFrames: Boolean = false,
    val textureFiltering: Int = GsHackDefaults.BILINEAR_FILTERING_DEFAULT,
    val trilinearFiltering: Int = GsHackDefaults.TRILINEAR_FILTERING_DEFAULT,
    val blendingAccuracy: Int = GsHackDefaults.BLENDING_ACCURACY_DEFAULT,
    val texturePreloading: Int = GsHackDefaults.TEXTURE_PRELOADING_DEFAULT,
    val enableFxaa: Boolean = false,
    val casMode: Int = 0,
    val casSharpness: Int = 50,
    val shadeBoostEnabled: Boolean = false,
    val shadeBoostBrightness: Int = 50,
    val shadeBoostContrast: Int = 50,
    val shadeBoostSaturation: Int = 50,
    val shadeBoostGamma: Int = 50,
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
    val frameLimitEnabled: Boolean = false,
    val targetFps: Int = 0,
    val currentGameTitle: String = "",
    val currentGameSubtitle: String = "",
    val gameSettingsProfileActive: Boolean = false
)

private data class EmulationLaunchConfig(
    val biosPath: String?,
    val memoryCardSlot1: String?,
    val memoryCardSlot2: String?,
    val renderer: Int,
    val upscaleMultiplier: Float,
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
    val skipDuplicateFrames: Boolean,
    val frameLimitEnabled: Boolean,
    val targetFps: Int,
    val textureFiltering: Int,
    val trilinearFiltering: Int,
    val blendingAccuracy: Int,
    val texturePreloading: Int,
    val enableFxaa: Boolean,
    val casMode: Int,
    val casSharpness: Int,
    val shadeBoostEnabled: Boolean,
    val shadeBoostBrightness: Int,
    val shadeBoostContrast: Int,
    val shadeBoostSaturation: Int,
    val shadeBoostGamma: Int,
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
    val nativePaletteDraw: Boolean,
    val fpuClampMode: Int,
    val disableHardwareReadbacks: Boolean,
    val fpuCorrectAddSub: Boolean
)

private data class LiveRuntimeSnapshot(
    val showFps: Boolean,
    val fpsOverlayMode: Int,
    val renderer: Int,
    val upscale: Float,
    val aspectRatio: Int,
    val performancePreset: Int,
    val enableMtvu: Boolean,
    val enableFastCdvd: Boolean,
    val enableCheats: Boolean,
    val hwDownloadMode: Int,
    val frameSkip: Int,
    val skipDuplicateFrames: Boolean,
    val frameLimitEnabled: Boolean,
    val targetFps: Int,
    val textureFiltering: Int,
    val trilinearFiltering: Int,
    val blendingAccuracy: Int,
    val texturePreloading: Int,
    val enableFxaa: Boolean,
    val casMode: Int,
    val casSharpness: Int,
    val shadeBoostEnabled: Boolean,
    val shadeBoostBrightness: Int,
    val shadeBoostContrast: Int,
    val shadeBoostSaturation: Int,
    val shadeBoostGamma: Int,
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
    private val memoryCardRepository = MemoryCardRepository(application, preferences)
    private val perGameSettingsRepository = PerGameSettingsRepository(application)
    private val _uiState = MutableStateFlow(EmulationUiState())
    val uiState: StateFlow<EmulationUiState> = _uiState.asStateFlow()
    private val lifecycleMutex = Mutex()
    private var pausedForBackground = false
    @Volatile
    private var isShuttingDown = false
    @Volatile
    private var cancelPendingStart = false
    @Volatile
    private var currentGameTitle: String = ""
    @Volatile
    private var currentGamePath: String? = null
    @Volatile
    private var currentGameSerial: String = ""
    @Volatile
    private var currentGameCrc: String = ""
    @Volatile
    private var currentGameSource: String = ""

    init {
        viewModelScope.launch {
            preferences.migrateOverlayLayoutIfNeeded()
        }
    }

    private inline fun applyGlobalRuntimePreferenceUpdate(
        crossinline transform: (EmulationUiState) -> EmulationUiState
    ) {
        val current = _uiState.value
        if (current.gameSettingsProfileActive) return
        _uiState.value = transform(current)
        syncNativePerformanceOverlayState(_uiState.value)
    }

    private fun syncNativePerformanceOverlayState(state: EmulationUiState) {
        NativeApp.setPerformanceOverlayMode(
            visible = state.showFps,
            detailed = state.showFps && state.fpsOverlayMode != FPS_OVERLAY_MODE_SIMPLE
        )
    }

    init {
        viewModelScope.launch {
            preferences.overlayShow.collect { enabled ->
                _uiState.value = _uiState.value.copy(controlsVisible = enabled)
            }
        }
        viewModelScope.launch {
            preferences.showFps.collect { enabled ->
                _uiState.value = _uiState.value.copy(showFps = enabled)
                syncNativePerformanceOverlayState(_uiState.value)
            }
        }
        viewModelScope.launch {
            preferences.overlayLayoutSnapshot.collect { snapshot ->
                applyOverlayLayoutSnapshot(snapshot)
            }
        }
        viewModelScope.launch {
            preferences.fpsOverlayMode.collect { mode ->
                _uiState.value = _uiState.value.copy(fpsOverlayMode = mode)
                syncNativePerformanceOverlayState(_uiState.value)
            }
        }
        viewModelScope.launch {
            preferences.fpsOverlayCorner.collect { corner ->
                _uiState.value = _uiState.value.copy(fpsOverlayCorner = corner)
            }
        }
        viewModelScope.launch {
            preferences.compactControls.collect { enabled ->
                _uiState.value = _uiState.value.copy(compactControls = enabled)
            }
        }
        viewModelScope.launch {
            preferences.gamepadStickDeadzone.collect { value ->
                _uiState.value = _uiState.value.copy(gamepadStickDeadzone = value)
            }
        }
        viewModelScope.launch {
            preferences.gamepadLeftStickSensitivity.collect { value ->
                _uiState.value = _uiState.value.copy(gamepadLeftStickSensitivity = value)
            }
        }
        viewModelScope.launch {
            preferences.gamepadRightStickSensitivity.collect { value ->
                _uiState.value = _uiState.value.copy(gamepadRightStickSensitivity = value)
            }
        }
        viewModelScope.launch {
            preferences.keepScreenOn.collect { enabled ->
                _uiState.value = _uiState.value.copy(keepScreenOn = enabled)
            }
        }
        viewModelScope.launch {
            preferences.performancePreset.collect { preset ->
                applyGlobalRuntimePreferenceUpdate { it.copy(performancePreset = preset) }
            }
        }
        viewModelScope.launch {
            preferences.hwDownloadMode.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(hwDownloadMode = value) }
            }
        }
        viewModelScope.launch {
            preferences.renderer.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(renderer = value) }
            }
        }
        viewModelScope.launch {
            preferences.upscaleMultiplier.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(upscale = value) }
            }
        }
        viewModelScope.launch {
            preferences.aspectRatio.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(aspectRatio = value) }
            }
        }
        viewModelScope.launch {
            preferences.enableMtvu.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(enableMtvu = value) }
            }
        }
        viewModelScope.launch {
            preferences.enableFastCdvd.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(enableFastCdvd = value) }
            }
        }
        viewModelScope.launch {
            preferences.enableCheats.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(enableCheats = value) }
            }
        }
        viewModelScope.launch {
            preferences.frameSkip.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(frameSkip = value) }
            }
        }
        viewModelScope.launch {
            preferences.skipDuplicateFrames.collect { enabled ->
                applyGlobalRuntimePreferenceUpdate { it.copy(skipDuplicateFrames = enabled) }
            }
        }
        viewModelScope.launch {
            preferences.textureFiltering.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(textureFiltering = value) }
            }
        }
        viewModelScope.launch {
            preferences.trilinearFiltering.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(trilinearFiltering = value) }
            }
        }
        viewModelScope.launch {
            preferences.blendingAccuracy.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(blendingAccuracy = value) }
            }
        }
        viewModelScope.launch {
            preferences.texturePreloading.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(texturePreloading = value) }
            }
        }
        viewModelScope.launch {
            preferences.enableFxaa.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(enableFxaa = value) }
            }
        }
        viewModelScope.launch {
            preferences.casMode.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(casMode = value) }
            }
        }
        viewModelScope.launch {
            preferences.casSharpness.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(casSharpness = value) }
            }
        }
        viewModelScope.launch {
            preferences.shadeBoostEnabled.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(shadeBoostEnabled = value) }
            }
        }
        viewModelScope.launch {
            preferences.shadeBoostBrightness.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(shadeBoostBrightness = value) }
            }
        }
        viewModelScope.launch {
            preferences.shadeBoostContrast.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(shadeBoostContrast = value) }
            }
        }
        viewModelScope.launch {
            preferences.shadeBoostSaturation.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(shadeBoostSaturation = value) }
            }
        }
        viewModelScope.launch {
            preferences.shadeBoostGamma.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(shadeBoostGamma = value) }
            }
        }
        viewModelScope.launch {
            preferences.anisotropicFiltering.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(anisotropicFiltering = value) }
            }
        }
        viewModelScope.launch {
            preferences.enableHwMipmapping.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(enableHwMipmapping = value) }
            }
        }
        viewModelScope.launch {
            preferences.cpuSpriteRenderSize.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(cpuSpriteRenderSize = value) }
            }
        }
        viewModelScope.launch {
            preferences.cpuSpriteRenderLevel.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(cpuSpriteRenderLevel = value) }
            }
        }
        viewModelScope.launch {
            preferences.softwareClutRender.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(softwareClutRender = value) }
            }
        }
        viewModelScope.launch {
            preferences.gpuTargetClutMode.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(gpuTargetClutMode = value) }
            }
        }
        viewModelScope.launch {
            preferences.skipDrawStart.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(skipDrawStart = value) }
            }
        }
        viewModelScope.launch {
            preferences.skipDrawEnd.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(skipDrawEnd = value) }
            }
        }
        viewModelScope.launch {
            preferences.autoFlushHardware.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(autoFlushHardware = value) }
            }
        }
        viewModelScope.launch {
            preferences.cpuFramebufferConversion.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(cpuFramebufferConversion = value) }
            }
        }
        viewModelScope.launch {
            preferences.disableDepthConversion.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(disableDepthConversion = value) }
            }
        }
        viewModelScope.launch {
            preferences.disableSafeFeatures.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(disableSafeFeatures = value) }
            }
        }
        viewModelScope.launch {
            preferences.disableRenderFixes.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(disableRenderFixes = value) }
            }
        }
        viewModelScope.launch {
            preferences.preloadFrameData.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(preloadFrameData = value) }
            }
        }
        viewModelScope.launch {
            preferences.disablePartialInvalidation.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(disablePartialInvalidation = value) }
            }
        }
        viewModelScope.launch {
            preferences.textureInsideRt.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(textureInsideRt = value) }
            }
        }
        viewModelScope.launch {
            preferences.readTargetsOnClose.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(readTargetsOnClose = value) }
            }
        }
        viewModelScope.launch {
            preferences.estimateTextureRegion.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(estimateTextureRegion = value) }
            }
        }
        viewModelScope.launch {
            preferences.gpuPaletteConversion.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(gpuPaletteConversion = value) }
            }
        }
        viewModelScope.launch {
            preferences.halfPixelOffset.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(halfPixelOffset = value) }
            }
        }
        viewModelScope.launch {
            preferences.nativeScaling.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(nativeScaling = value) }
            }
        }
        viewModelScope.launch {
            preferences.roundSprite.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(roundSprite = value) }
            }
        }
        viewModelScope.launch {
            preferences.bilinearUpscale.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(bilinearUpscale = value) }
            }
        }
        viewModelScope.launch {
            preferences.textureOffsetX.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(textureOffsetX = value) }
            }
        }
        viewModelScope.launch {
            preferences.textureOffsetY.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(textureOffsetY = value) }
            }
        }
        viewModelScope.launch {
            preferences.alignSprite.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(alignSprite = value) }
            }
        }
        viewModelScope.launch {
            preferences.mergeSprite.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(mergeSprite = value) }
            }
        }
        viewModelScope.launch {
            preferences.forceEvenSpritePosition.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(forceEvenSpritePosition = value) }
            }
        }
        viewModelScope.launch {
            preferences.nativePaletteDraw.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(nativePaletteDraw = value) }
            }
        }
        viewModelScope.launch {
            preferences.frameLimitEnabled.collect { enabled ->
                applyGlobalRuntimePreferenceUpdate { it.copy(frameLimitEnabled = enabled) }
            }
        }
        viewModelScope.launch {
            preferences.targetFps.collect { value ->
                applyGlobalRuntimePreferenceUpdate { it.copy(targetFps = value) }
            }
        }
        
        NativeApp.setPerformanceListener(object : NativeApp.PerformanceListener {
            override fun onMetricsUpdate(
                overlayText: String,
                fps: Float,
                speedPercent: Float
            ) {
                if (_uiState.value.isRunning && !isShuttingDown && !_uiState.value.isPaused) {
                    _uiState.value = _uiState.value.copy(
                        performanceOverlayText = overlayText,
                        fps = "%.1f".format(fps),
                        speedPercent = speedPercent
                    )
                }
            }
        })
        syncNativePerformanceOverlayState(_uiState.value)
    }

    fun startEmulation(path: String?, slotToLoad: Int? = null, bootToBios: Boolean = false) {
        if (_uiState.value.isStarting) return
        val normalizedSlotToLoad = slotToLoad?.let { normalizeSaveSlot(it) }
        val hasPendingStateLoad = !bootToBios && normalizedSlotToLoad != null
        cancelPendingStart = false
        pausedForBackground = false
        currentGamePath = if (bootToBios) null else path?.takeIf { it.isNotBlank() }

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
                    memoryCardSlot1 = config.memoryCardSlot1,
                    memoryCardSlot2 = config.memoryCardSlot2,
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
                    skipDuplicateFrames = config.skipDuplicateFrames,
                    frameLimitEnabled = config.frameLimitEnabled,
                    targetFps = config.targetFps,
                    textureFiltering = config.textureFiltering,
                    trilinearFiltering = config.trilinearFiltering,
                    blendingAccuracy = config.blendingAccuracy,
                    texturePreloading = config.texturePreloading,
                    enableFxaa = config.enableFxaa,
                    casMode = config.casMode,
                    casSharpness = config.casSharpness,
                    shadeBoostEnabled = config.shadeBoostEnabled,
                    shadeBoostBrightness = config.shadeBoostBrightness,
                    shadeBoostContrast = config.shadeBoostContrast,
                    shadeBoostSaturation = config.shadeBoostSaturation,
                    shadeBoostGamma = config.shadeBoostGamma,
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
                    nativePaletteDraw = config.nativePaletteDraw,
                    fpuClampMode = config.fpuClampMode,
                    disableHardwareReadbacks = config.disableHardwareReadbacks,
                    fpuCorrectAddSub = config.fpuCorrectAddSub
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
                    currentGamePath = null
                    currentGameSerial = ""
                    currentGameCrc = ""
                    currentGameSource = "bios_only"
                    _uiState.value = _uiState.value.copy(
                        currentGameTitle = currentGameTitle,
                        currentGameSubtitle = currentGameSubtitle(),
                        gameSettingsProfileActive = false,
                        cheatsGameKey = null,
                        availableCheats = emptyList()
                    )
                } else {
                    val safePath = path.orEmpty()
                    val existingProfile = currentGamePath?.let(perGameSettingsRepository::get)
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
                    _uiState.value = _uiState.value.copy(
                        currentGameTitle = currentGameTitle,
                        currentGameSubtitle = currentGameSubtitle(),
                        gameSettingsProfileActive = existingProfile != null
                    )
                    syncCurrentGameProfileMetadata()
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
                    preferences.markGameLaunched(
                        path = path,
                        title = currentGameTitle.ifBlank {
                            DocumentPathResolver.getDisplayName(getApplication(), path).substringBeforeLast('.')
                        },
                        serial = currentGameSerial.takeIf { it.isNotBlank() }
                    )
                }

                val liveRuntime = loadLiveRuntimeSnapshot()

                _uiState.value = _uiState.value.copy(
                    showFps = liveRuntime.showFps,
                    fpsOverlayMode = liveRuntime.fpsOverlayMode,
                    renderer = liveRuntime.renderer,
                    upscale = liveRuntime.upscale,
                    aspectRatio = liveRuntime.aspectRatio,
                    performancePreset = liveRuntime.performancePreset,
                    enableMtvu = liveRuntime.enableMtvu,
                    enableFastCdvd = liveRuntime.enableFastCdvd,
                    enableCheats = liveRuntime.enableCheats,
                    hwDownloadMode = liveRuntime.hwDownloadMode,
                    frameSkip = liveRuntime.frameSkip,
                    skipDuplicateFrames = liveRuntime.skipDuplicateFrames,
                    textureFiltering = liveRuntime.textureFiltering,
                    trilinearFiltering = liveRuntime.trilinearFiltering,
                    blendingAccuracy = liveRuntime.blendingAccuracy,
                    texturePreloading = liveRuntime.texturePreloading,
                    enableFxaa = liveRuntime.enableFxaa,
                    casMode = liveRuntime.casMode,
                    casSharpness = liveRuntime.casSharpness,
                    shadeBoostEnabled = liveRuntime.shadeBoostEnabled,
                    shadeBoostBrightness = liveRuntime.shadeBoostBrightness,
                    shadeBoostContrast = liveRuntime.shadeBoostContrast,
                    shadeBoostSaturation = liveRuntime.shadeBoostSaturation,
                    shadeBoostGamma = liveRuntime.shadeBoostGamma,
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
                syncNativePerformanceOverlayState(_uiState.value)
                updateCrashContext(
                    launchState = "starting",
                    launchPath = path
                )
            }
            
            if (hasPendingStateLoad) {
                viewModelScope.launch(Dispatchers.IO) {
                    var vmReadyWaitFrames = 0
                    while (vmReadyWaitFrames < 60 && isActive) {
                        try {
                            if (EmulatorBridge.hasValidVm()) break
                        } catch (_: Exception) { }
                        delay(250)
                        vmReadyWaitFrames++
                    }
                    if (!isActive) return@launch

                    if (!EmulatorBridge.hasValidVm()) {
                        _uiState.value = _uiState.value.copy(
                            statusMessage = null,
                            toastMessage = "load_failed"
                        )
                        delay(2500)
                        if (_uiState.value.toastMessage == "load_failed") {
                            _uiState.value = _uiState.value.copy(toastMessage = null)
                        }
                        return@launch
                    }

                    delay(500)
                    val loaded = EmulatorBridge.loadState(normalizedSlotToLoad)
                    _uiState.value = _uiState.value.copy(
                        isRunning = true,
                        isPaused = false,
                        statusMessage = if (loaded) "status_running" else null,
                        toastMessage = if (loaded) null else "load_failed"
                    )
                    delay(2000)
                    if (_uiState.value.statusMessage == "status_running") {
                        _uiState.value = _uiState.value.copy(statusMessage = null)
                    }
                    if (_uiState.value.toastMessage == "load_failed") {
                        delay(500)
                        if (_uiState.value.toastMessage == "load_failed") {
                            _uiState.value = _uiState.value.copy(toastMessage = null)
                        }
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

            if (!hasPendingStateLoad) {
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
            
            if (!started &&
                !_uiState.value.isPaused &&
                !cancelPendingStart &&
                !isShuttingDown
            ) {
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
        pausedForBackground = false
        _uiState.value = _uiState.value.copy(
            isPaused = !isPaused,
            showMenu = if (isPaused) false else _uiState.value.showMenu
        )
        updateCrashContext(launchState = if (!isPaused) "paused" else "running")
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (isPaused) {
                    EmulatorBridge.resume()
                } else {
                    EmulatorBridge.pause()
                }
            } catch (_: Exception) { }
        }
    }

    fun toggleMenu() {
        val showMenu = !_uiState.value.showMenu
        if (showMenu) {
            pausedForBackground = false
            EmulatorBridge.resetKeyStatus()
            _uiState.value = _uiState.value.copy(showMenu = true, isPaused = true)
        } else {
            _uiState.value = _uiState.value.copy(showMenu = false, isPaused = false)
        }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (showMenu) {
                    EmulatorBridge.pause()
                } else {
                    EmulatorBridge.resume()
                }
            } catch (_: Exception) { }
        }
    }

    fun toggleControlsVisibility() {
        viewModelScope.launch {
            val newValue = !_uiState.value.controlsVisible
            preferences.setOverlayShow(newValue)
            _uiState.value = _uiState.value.copy(controlsVisible = newValue)
        }
    }

    fun saveCurrentGameSettingsProfile() {
        viewModelScope.launch {
            persistRuntimeState(_uiState.value)
        }
    }

    fun resetCurrentGameSettingsProfile() {
        viewModelScope.launch {
            resetCurrentGameProfile()
        }
    }

    fun setOverlayScale(value: Int) {
        viewModelScope.launch {
            preferences.setOverlayScale(value)
            _uiState.value = _uiState.value.copy(overlayScale = value.coerceIn(50, 150))
        }
    }

    fun setOverlayOpacity(value: Int) {
        viewModelScope.launch {
            preferences.setOverlayOpacity(value)
            _uiState.value = _uiState.value.copy(overlayOpacity = value.coerceIn(20, 100))
        }
    }

    fun setHideOverlayOnGamepad(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setHideOverlayOnGamepad(enabled)
            _uiState.value = _uiState.value.copy(hideOverlayOnGamepad = enabled)
        }
    }

    fun setCompactControls(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setCompactControls(enabled)
            _uiState.value = _uiState.value.copy(compactControls = enabled)
        }
    }

    fun setKeepScreenOn(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setKeepScreenOn(enabled)
            _uiState.value = _uiState.value.copy(keepScreenOn = enabled)
        }
    }

    fun setStickScale(value: Int) {
        viewModelScope.launch {
            val scaledValue = value.coerceIn(50, 200)
            val current = _uiState.value
            val updatedLayouts = current.controlLayouts.toMutableMap()
            listOf("left_stick", "right_stick").forEach { id ->
                val existing = updatedLayouts[id]
                if (existing != null) {
                    updatedLayouts[id] = existing.copy(scale = scaledValue)
                }
            }
            val updatedState = current.copy(
                stickScale = scaledValue,
                controlLayouts = updatedLayouts
            )
            preferences.setControlsLayout(
                dpadX = updatedState.dpadOffset.first,
                dpadY = updatedState.dpadOffset.second,
                lstickX = updatedState.lstickOffset.first,
                lstickY = updatedState.lstickOffset.second,
                rstickX = updatedState.rstickOffset.first,
                rstickY = updatedState.rstickOffset.second,
                actionX = updatedState.actionOffset.first,
                actionY = updatedState.actionOffset.second,
                lbtnX = updatedState.lbtnOffset.first,
                lbtnY = updatedState.lbtnOffset.second,
                rbtnX = updatedState.rbtnOffset.first,
                rbtnY = updatedState.rbtnOffset.second,
                centerX = updatedState.centerOffset.first,
                centerY = updatedState.centerOffset.second,
                stickScaleVal = updatedState.stickScale,
                controlLayouts = updatedLayouts
            )
            _uiState.value = updatedState
        }
    }

    fun setLeftStickSensitivity(value: Int) {
        viewModelScope.launch {
            val normalized = value.coerceIn(50, 200)
            preferences.setLeftStickSensitivity(normalized)
            _uiState.value = _uiState.value.copy(leftStickSensitivity = normalized)
        }
    }

    fun setRightStickSensitivity(value: Int) {
        viewModelScope.launch {
            val normalized = value.coerceIn(50, 200)
            preferences.setRightStickSensitivity(normalized)
            _uiState.value = _uiState.value.copy(rightStickSensitivity = normalized)
        }
    }

    fun setGamepadStickDeadzone(value: Int) {
        viewModelScope.launch {
            val normalized = value.coerceIn(0, 35)
            preferences.setGamepadStickDeadzone(normalized)
            _uiState.value = _uiState.value.copy(gamepadStickDeadzone = normalized)
        }
    }

    fun setGamepadLeftStickSensitivity(value: Int) {
        viewModelScope.launch {
            val normalized = value.coerceIn(50, 200)
            preferences.setGamepadLeftStickSensitivity(normalized)
            _uiState.value = _uiState.value.copy(gamepadLeftStickSensitivity = normalized)
        }
    }

    fun setGamepadRightStickSensitivity(value: Int) {
        viewModelScope.launch {
            val normalized = value.coerceIn(50, 200)
            preferences.setGamepadRightStickSensitivity(normalized)
            _uiState.value = _uiState.value.copy(gamepadRightStickSensitivity = normalized)
        }
    }

    fun toggleLeftInputMode() {
        viewModelScope.launch {
            val current = _uiState.value
            val updatedLayouts = current.controlLayouts.toMutableMap()
            val defaults = AppPreferences.defaultOverlayControlLayouts(current.stickScale)
            val leftStickLayout = updatedLayouts["left_stick"] ?: defaults["left_stick"] ?: OverlayControlLayout(scale = current.stickScale)
            val showingStick = leftStickLayout.visible

            updatedLayouts["left_stick"] = leftStickLayout.copy(visible = !showingStick)
            listOf("dpad_up", "dpad_down", "dpad_left", "dpad_right").forEach { id ->
                val currentLayout = updatedLayouts[id] ?: defaults[id] ?: OverlayControlLayout()
                updatedLayouts[id] = currentLayout.copy(visible = showingStick)
            }

            val updatedState = current.copy(
                controlLayouts = updatedLayouts,
                dpadOffset = current.lstickOffset,
                lstickOffset = current.dpadOffset
            )
            preferences.setControlsLayout(
                dpadX = updatedState.dpadOffset.first,
                dpadY = updatedState.dpadOffset.second,
                lstickX = updatedState.lstickOffset.first,
                lstickY = updatedState.lstickOffset.second,
                rstickX = updatedState.rstickOffset.first,
                rstickY = updatedState.rstickOffset.second,
                actionX = updatedState.actionOffset.first,
                actionY = updatedState.actionOffset.second,
                lbtnX = updatedState.lbtnOffset.first,
                lbtnY = updatedState.lbtnOffset.second,
                rbtnX = updatedState.rbtnOffset.first,
                rbtnY = updatedState.rbtnOffset.second,
                centerX = updatedState.centerOffset.first,
                centerY = updatedState.centerOffset.second,
                stickScaleVal = updatedState.stickScale,
                controlLayouts = updatedLayouts
            )
            _uiState.value = updatedState
        }
    }

    fun toggleFpsVisibility() {
        viewModelScope.launch {
            val newValue = !_uiState.value.showFps
            persistRuntimeState(_uiState.value.copy(showFps = newValue)) {
                preferences.setShowFps(newValue)
            }
        }
    }

    fun setFpsOverlayMode(mode: Int) {
        viewModelScope.launch {
            persistRuntimeState(_uiState.value.copy(fpsOverlayMode = mode)) {
                preferences.setFpsOverlayMode(mode)
            }
        }
    }

    fun setFpsOverlayCorner(corner: Int) {
        viewModelScope.launch {
            preferences.setFpsOverlayCorner(corner)
            _uiState.value = _uiState.value.copy(fpsOverlayCorner = corner)
        }
    }

    fun setRenderer(renderer: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(renderer = renderer)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setRenderer(renderer)
            }
            EmulatorBridge.setRenderer(renderer)
            updateCrashContext()
        }
    }

    fun setUpscale(upscale: Float) {
        viewModelScope.launch {
            val normalizedUpscale = normalizeUpscale(upscale)
            val newState = markPerformancePresetCustom(_uiState.value).copy(upscale = normalizedUpscale)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setUpscaleMultiplier(normalizedUpscale)
            }
            EmulatorBridge.setUpscaleMultiplier(normalizedUpscale)
            updateCrashContext()
        }
    }

    fun setAspectRatio(value: Int) {
        viewModelScope.launch {
            persistRuntimeState(_uiState.value.copy(aspectRatio = value)) {
                preferences.setAspectRatio(value)
            }
            EmulatorBridge.setAspectRatio(value)
            updateCrashContext()
        }
    }

    fun setMtvu(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(enableMtvu = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setEnableMtvu(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "vuThread", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setFastCdvd(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(enableFastCdvd = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setEnableFastCdvd(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "fastCDVD", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setEnableCheats(enabled: Boolean) {
        viewModelScope.launch {
            persistRuntimeState(_uiState.value.copy(enableCheats = enabled)) {
                preferences.setEnableCheats(enabled)
            }
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
            persistRuntimeState(_uiState.value.copy(
                availableCheats = updatedBlocks,
                enableCheats = true
            )) {
                preferences.setEnableCheats(true)
            }
            EmulatorBridge.setSetting("EmuCore", "EnableCheats", "bool", "true")
        }
    }

    fun setHwDownloadMode(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(hwDownloadMode = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setHwDownloadMode(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "HWDownloadMode", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setFrameSkip(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(frameSkip = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setFrameSkip(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "FrameSkip", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setSkipDuplicateFrames(enabled: Boolean) {
        viewModelScope.launch {
            persistRuntimeState(_uiState.value.copy(skipDuplicateFrames = enabled)) {
                preferences.setSkipDuplicateFrames(enabled)
            }
            EmulatorBridge.setSkipDuplicateFrames(enabled)
            updateCrashContext()
        }
    }

    fun setFrameLimitEnabled(enabled: Boolean) {
        viewModelScope.launch {
            persistRuntimeState(_uiState.value.copy(frameLimitEnabled = enabled)) {
                preferences.setFrameLimitEnabled(enabled)
            }
            EmulatorBridge.setFrameLimitEnabled(enabled)
            updateCrashContext()
        }
    }

    fun setTargetFps(value: Int) {
        viewModelScope.launch {
            val clamped = if (value <= 0) 0 else value.coerceIn(20, 120)
            persistRuntimeState(_uiState.value.copy(targetFps = clamped)) {
                preferences.setTargetFps(clamped)
            }
            EmulatorBridge.setTargetFps(clamped)
            updateCrashContext()
        }
    }

    fun setTextureFiltering(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(textureFiltering = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setTextureFiltering(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "filter", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setTrilinearFiltering(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(trilinearFiltering = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setTrilinearFiltering(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "TriFilter", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setBlendingAccuracy(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(blendingAccuracy = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setBlendingAccuracy(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "accurate_blending_unit", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setTexturePreloading(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(texturePreloading = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setTexturePreloading(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "texture_preloading", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setEnableFxaa(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(enableFxaa = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setEnableFxaa(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "fxaa", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setCasMode(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(casMode = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setCasMode(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "CASMode", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setCasSharpness(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(0, 100)
            val newState = markPerformancePresetCustom(_uiState.value).copy(casSharpness = clamped)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setCasSharpness(clamped)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "CASSharpness", "int", clamped.toString())
            updateCrashContext()
        }
    }

    fun setShadeBoostEnabled(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(shadeBoostEnabled = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setShadeBoostEnabled(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "ShadeBoost", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setShadeBoostBrightness(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(1, 100)
            val newState = markPerformancePresetCustom(_uiState.value).copy(shadeBoostBrightness = clamped)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setShadeBoostBrightness(clamped)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "ShadeBoost_Brightness", "int", clamped.toString())
            updateCrashContext()
        }
    }

    fun setShadeBoostContrast(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(1, 100)
            val newState = markPerformancePresetCustom(_uiState.value).copy(shadeBoostContrast = clamped)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setShadeBoostContrast(clamped)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "ShadeBoost_Contrast", "int", clamped.toString())
            updateCrashContext()
        }
    }

    fun setShadeBoostSaturation(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(1, 100)
            val newState = markPerformancePresetCustom(_uiState.value).copy(shadeBoostSaturation = clamped)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setShadeBoostSaturation(clamped)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "ShadeBoost_Saturation", "int", clamped.toString())
            updateCrashContext()
        }
    }

    fun setShadeBoostGamma(value: Int) {
        viewModelScope.launch {
            val clamped = value.coerceIn(1, 100)
            val newState = markPerformancePresetCustom(_uiState.value).copy(shadeBoostGamma = clamped)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setShadeBoostGamma(clamped)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "ShadeBoost_Gamma", "int", clamped.toString())
            updateCrashContext()
        }
    }

    fun setAnisotropicFiltering(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(anisotropicFiltering = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setAnisotropicFiltering(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "MaxAnisotropy", "int", value.toString())
            updateCrashContext()
        }
    }

    fun setEnableHwMipmapping(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(enableHwMipmapping = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setEnableHwMipmapping(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "hw_mipmap", "bool", enabled.toString())
            updateCrashContext()
        }
    }

    fun setCpuSpriteRenderSize(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(cpuSpriteRenderSize = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setCpuSpriteRenderSize(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderBW", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setCpuSpriteRenderLevel(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(cpuSpriteRenderLevel = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setCpuSpriteRenderLevel(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setSoftwareClutRender(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(softwareClutRender = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setSoftwareClutRender(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUCLUTRender", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setGpuTargetClutMode(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(gpuTargetClutMode = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setGpuTargetClutMode(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_GPUTargetCLUTMode", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setSkipDrawStart(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(skipDrawStart = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setSkipDrawStart(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_Start", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setSkipDrawEnd(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(skipDrawEnd = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setSkipDrawEnd(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_End", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setAutoFlushHardware(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(autoFlushHardware = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setAutoFlushHardware(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_AutoFlushLevel", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setCpuFramebufferConversion(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(cpuFramebufferConversion = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setCpuFramebufferConversion(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPU_FB_Conversion", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisableDepthConversion(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(disableDepthConversion = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setDisableDepthConversion(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisableDepthSupport", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisableSafeFeatures(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(disableSafeFeatures = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setDisableSafeFeatures(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_Disable_Safe_Features", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisableRenderFixes(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(disableRenderFixes = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setDisableRenderFixes(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisableRenderFixes", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setPreloadFrameData(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(preloadFrameData = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setPreloadFrameData(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "preload_frame_with_gs_data", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setDisablePartialInvalidation(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(disablePartialInvalidation = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setDisablePartialInvalidation(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisablePartialInvalidation", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setTextureInsideRt(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(textureInsideRt = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setTextureInsideRt(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TextureInsideRt", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setReadTargetsOnClose(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(readTargetsOnClose = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setReadTargetsOnClose(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_ReadTCOnClose", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setEstimateTextureRegion(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(estimateTextureRegion = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setEstimateTextureRegion(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_EstimateTextureRegion", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setGpuPaletteConversion(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(gpuPaletteConversion = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setGpuPaletteConversion(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "paltex", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setHalfPixelOffset(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(halfPixelOffset = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setHalfPixelOffset(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_HalfPixelOffset", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setNativeScaling(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(nativeScaling = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setNativeScaling(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_native_scaling", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setRoundSprite(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(roundSprite = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setRoundSprite(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_round_sprite_offset", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setBilinearUpscale(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(bilinearUpscale = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setBilinearUpscale(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_BilinearHack", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setTextureOffsetX(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(textureOffsetX = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setTextureOffsetX(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TCOffsetX", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setTextureOffsetY(value: Int) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(textureOffsetY = value)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setTextureOffsetY(value)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TCOffsetY", "int", value.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setAlignSprite(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(alignSprite = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setAlignSprite(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_align_sprite_X", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setMergeSprite(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(mergeSprite = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setMergeSprite(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_merge_pp_sprite", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setForceEvenSpritePosition(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(forceEvenSpritePosition = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setForceEvenSpritePosition(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_ForceEvenSpritePosition", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    fun setNativePaletteDraw(enabled: Boolean) {
        viewModelScope.launch {
            val newState = markPerformancePresetCustom(_uiState.value).copy(nativePaletteDraw = enabled)
            persistRuntimeState(newState) {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                preferences.setNativePaletteDraw(enabled)
            }
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_NativePaletteDraw", "bool", enabled.toString())
            refreshManualHardwareFixes(newState)
            updateCrashContext()
        }
    }

    private fun markPerformancePresetCustom(state: EmulationUiState): EmulationUiState {
        return if (state.performancePreset == PerformancePresets.CUSTOM) {
            state
        } else {
            state.copy(performancePreset = PerformancePresets.CUSTOM)
        }
    }

    private suspend fun refreshManualHardwareFixes(state: EmulationUiState = _uiState.value) {
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

    private fun applyOverlayLayoutSnapshot(snapshot: OverlayLayoutSnapshot) {
        _uiState.value = _uiState.value.copy(
            overlayScale = snapshot.overlayScale,
            overlayOpacity = snapshot.overlayOpacity,
            hideOverlayOnGamepad = snapshot.hideOverlayOnGamepad,
            dpadOffset = snapshot.dpadOffset,
            lstickOffset = snapshot.lstickOffset,
            rstickOffset = snapshot.rstickOffset,
            actionOffset = snapshot.actionOffset,
            lbtnOffset = snapshot.lbtnOffset,
            rbtnOffset = snapshot.rbtnOffset,
            centerOffset = snapshot.centerOffset,
            stickScale = snapshot.stickScale,
            leftStickSensitivity = snapshot.leftStickSensitivity,
            rightStickSensitivity = snapshot.rightStickSensitivity,
            stickSurfaceMode = snapshot.stickSurfaceMode,
            controlLayouts = snapshot.controlLayouts
        )
    }

    private fun currentGameSubtitle(): String = buildList {
        currentGameSerial.takeIf { it.isNotBlank() }?.let(::add)
        currentGameCrc.takeIf { it.isNotBlank() }?.let(::add)
    }.joinToString("  /  ")

    private fun activePerGameKey(): String? = currentGamePath?.takeIf { it.isNotBlank() }

    private fun resolvePerGameTitle(state: EmulationUiState): String {
        return state.currentGameTitle
            .takeIf { it.isNotBlank() && it != "PlayStation 2 BIOS" }
            ?: currentGameTitle.takeIf { it.isNotBlank() && it != "PlayStation 2 BIOS" }
            ?: activePerGameKey()?.let { DocumentPathResolver.getDisplayName(getApplication(), it).substringBeforeLast('.') }
            ?: "Unknown Game"
    }

    private suspend fun persistRuntimeState(
        updatedState: EmulationUiState,
        persistGlobal: suspend () -> Unit = {}
    ): EmulationUiState {
        val gameKey = activePerGameKey()
        return if (gameKey != null) {
            perGameSettingsRepository.save(
                updatedState.toPerGameSettings(
                    gameKey = gameKey,
                    gameTitle = resolvePerGameTitle(updatedState),
                    gameSerial = currentGameSerial.takeIf { it.isNotBlank() }
                )
            )
            val finalState = updatedState.copy(gameSettingsProfileActive = true)
            _uiState.value = finalState
            syncNativePerformanceOverlayState(finalState)
            finalState
        } else {
            persistGlobal()
            _uiState.value = updatedState
            syncNativePerformanceOverlayState(updatedState)
            updatedState
        }
    }

    private fun syncCurrentGameProfileMetadata() {
        val gameKey = activePerGameKey() ?: return
        val profile = perGameSettingsRepository.get(gameKey) ?: return
        val resolvedTitle = resolvePerGameTitle(_uiState.value)
        val serial = currentGameSerial.takeIf { it.isNotBlank() }
        if (profile.gameTitle != resolvedTitle || profile.gameSerial != serial) {
            perGameSettingsRepository.save(
                profile.copy(
                    gameTitle = resolvedTitle,
                    gameSerial = serial
                )
            )
        }
    }

    private fun resetCurrentGameProfile() {
        val gameKey = activePerGameKey() ?: return
        perGameSettingsRepository.delete(gameKey)
        _uiState.value = _uiState.value.copy(gameSettingsProfileActive = false)
    }

    private suspend fun loadLaunchConfig(): EmulationLaunchConfig {
        val profile = activePerGameKey()?.let(perGameSettingsRepository::get)
        val ensuredAssignments = memoryCardRepository.ensureDefaultCardsAssigned()
        val profileConfig = PerformanceProfiles.configFor(preferences.performanceProfile.first())
        return EmulationLaunchConfig(
            biosPath = preferences.biosPath.first(),
            memoryCardSlot1 = ensuredAssignments.slot1,
            memoryCardSlot2 = ensuredAssignments.slot2,
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
            skipDuplicateFrames = preferences.skipDuplicateFrames.first(),
            frameLimitEnabled = preferences.frameLimitEnabled.first(),
            targetFps = preferences.targetFps.first(),
            textureFiltering = preferences.textureFiltering.first(),
            trilinearFiltering = preferences.trilinearFiltering.first(),
            blendingAccuracy = preferences.blendingAccuracy.first(),
            texturePreloading = preferences.texturePreloading.first(),
            enableFxaa = preferences.enableFxaa.first(),
            casMode = preferences.casMode.first(),
            casSharpness = preferences.casSharpness.first(),
            shadeBoostEnabled = preferences.shadeBoostEnabled.first(),
            shadeBoostBrightness = preferences.shadeBoostBrightness.first(),
            shadeBoostContrast = preferences.shadeBoostContrast.first(),
            shadeBoostSaturation = preferences.shadeBoostSaturation.first(),
            shadeBoostGamma = preferences.shadeBoostGamma.first(),
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
            nativePaletteDraw = preferences.nativePaletteDraw.first(),
            fpuClampMode = profileConfig.fpuClampMode,
            disableHardwareReadbacks = profileConfig.disableHardwareReadbacks,
            fpuCorrectAddSub = profileConfig.fpuCorrectAddSub
        ).applyProfile(profile)
    }

    private suspend fun loadLiveRuntimeSnapshot(): LiveRuntimeSnapshot {
        val profile = activePerGameKey()?.let(perGameSettingsRepository::get)
        return LiveRuntimeSnapshot(
            showFps = preferences.showFps.first(),
            fpsOverlayMode = preferences.fpsOverlayMode.first(),
            renderer = preferences.renderer.first(),
            upscale = preferences.upscaleMultiplier.first(),
            aspectRatio = preferences.aspectRatio.first(),
            performancePreset = preferences.performancePreset.first(),
            enableMtvu = preferences.enableMtvu.first(),
            enableFastCdvd = preferences.enableFastCdvd.first(),
            enableCheats = preferences.enableCheats.first(),
            hwDownloadMode = preferences.hwDownloadMode.first(),
            frameSkip = preferences.frameSkip.first(),
            skipDuplicateFrames = preferences.skipDuplicateFrames.first(),
            frameLimitEnabled = preferences.frameLimitEnabled.first(),
            targetFps = preferences.targetFps.first(),
            textureFiltering = preferences.textureFiltering.first(),
            trilinearFiltering = preferences.trilinearFiltering.first(),
            blendingAccuracy = preferences.blendingAccuracy.first(),
            texturePreloading = preferences.texturePreloading.first(),
            enableFxaa = preferences.enableFxaa.first(),
            casMode = preferences.casMode.first(),
            casSharpness = preferences.casSharpness.first(),
            shadeBoostEnabled = preferences.shadeBoostEnabled.first(),
            shadeBoostBrightness = preferences.shadeBoostBrightness.first(),
            shadeBoostContrast = preferences.shadeBoostContrast.first(),
            shadeBoostSaturation = preferences.shadeBoostSaturation.first(),
            shadeBoostGamma = preferences.shadeBoostGamma.first(),
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
        ).applyProfile(profile)
    }

    private fun EmulationLaunchConfig.applyProfile(profile: PerGameSettings?): EmulationLaunchConfig {
        if (profile == null) return this
        fun <T> pick(key: String, current: T, value: PerGameSettings.() -> T): T {
            val keys = profile.providedKeys
            return if (keys == null || key in keys) profile.value() else current
        }
        return copy(
            renderer = pick("renderer", renderer) { renderer },
            upscaleMultiplier = pick("upscaleMultiplier", upscaleMultiplier) { upscaleMultiplier },
            aspectRatio = pick("aspectRatio", aspectRatio) { aspectRatio },
            mtvu = pick("enableMtvu", mtvu) { enableMtvu },
            fastCdvd = pick("enableFastCdvd", fastCdvd) { enableFastCdvd },
            enableCheats = pick("enableCheats", enableCheats) { enableCheats },
            hwDownloadMode = pick("hwDownloadMode", hwDownloadMode) { hwDownloadMode },
            eeCycleRate = pick("eeCycleRate", eeCycleRate) { eeCycleRate },
            eeCycleSkip = pick("eeCycleSkip", eeCycleSkip) { eeCycleSkip },
            frameSkip = pick("frameSkip", frameSkip) { frameSkip },
            skipDuplicateFrames = pick("skipDuplicateFrames", skipDuplicateFrames) { skipDuplicateFrames },
            frameLimitEnabled = pick("frameLimitEnabled", frameLimitEnabled) { frameLimitEnabled },
            targetFps = pick("targetFps", targetFps) { targetFps },
            textureFiltering = pick("textureFiltering", textureFiltering) { textureFiltering },
            trilinearFiltering = pick("trilinearFiltering", trilinearFiltering) { trilinearFiltering },
            blendingAccuracy = pick("blendingAccuracy", blendingAccuracy) { blendingAccuracy },
            texturePreloading = pick("texturePreloading", texturePreloading) { texturePreloading },
            enableFxaa = pick("enableFxaa", enableFxaa) { enableFxaa },
            casMode = pick("casMode", casMode) { casMode },
            casSharpness = pick("casSharpness", casSharpness) { casSharpness },
            shadeBoostEnabled = pick("shadeBoostEnabled", shadeBoostEnabled) { shadeBoostEnabled },
            shadeBoostBrightness = pick("shadeBoostBrightness", shadeBoostBrightness) { shadeBoostBrightness },
            shadeBoostContrast = pick("shadeBoostContrast", shadeBoostContrast) { shadeBoostContrast },
            shadeBoostSaturation = pick("shadeBoostSaturation", shadeBoostSaturation) { shadeBoostSaturation },
            shadeBoostGamma = pick("shadeBoostGamma", shadeBoostGamma) { shadeBoostGamma },
            anisotropicFiltering = pick("anisotropicFiltering", anisotropicFiltering) { anisotropicFiltering },
            enableHwMipmapping = pick("enableHwMipmapping", enableHwMipmapping) { enableHwMipmapping },
            widescreenPatches = pick("enableWidescreenPatches", widescreenPatches) { enableWidescreenPatches },
            noInterlacingPatches = pick("enableNoInterlacingPatches", noInterlacingPatches) { enableNoInterlacingPatches },
            cpuSpriteRenderSize = pick("cpuSpriteRenderSize", cpuSpriteRenderSize) { cpuSpriteRenderSize },
            cpuSpriteRenderLevel = pick("cpuSpriteRenderLevel", cpuSpriteRenderLevel) { cpuSpriteRenderLevel },
            softwareClutRender = pick("softwareClutRender", softwareClutRender) { softwareClutRender },
            gpuTargetClutMode = pick("gpuTargetClutMode", gpuTargetClutMode) { gpuTargetClutMode },
            skipDrawStart = pick("skipDrawStart", skipDrawStart) { skipDrawStart },
            skipDrawEnd = pick("skipDrawEnd", skipDrawEnd) { skipDrawEnd },
            autoFlushHardware = pick("autoFlushHardware", autoFlushHardware) { autoFlushHardware },
            cpuFramebufferConversion = pick("cpuFramebufferConversion", cpuFramebufferConversion) { cpuFramebufferConversion },
            disableDepthConversion = pick("disableDepthConversion", disableDepthConversion) { disableDepthConversion },
            disableSafeFeatures = pick("disableSafeFeatures", disableSafeFeatures) { disableSafeFeatures },
            disableRenderFixes = pick("disableRenderFixes", disableRenderFixes) { disableRenderFixes },
            preloadFrameData = pick("preloadFrameData", preloadFrameData) { preloadFrameData },
            disablePartialInvalidation = pick("disablePartialInvalidation", disablePartialInvalidation) { disablePartialInvalidation },
            textureInsideRt = pick("textureInsideRt", textureInsideRt) { textureInsideRt },
            readTargetsOnClose = pick("readTargetsOnClose", readTargetsOnClose) { readTargetsOnClose },
            estimateTextureRegion = pick("estimateTextureRegion", estimateTextureRegion) { estimateTextureRegion },
            gpuPaletteConversion = pick("gpuPaletteConversion", gpuPaletteConversion) { gpuPaletteConversion },
            halfPixelOffset = pick("halfPixelOffset", halfPixelOffset) { halfPixelOffset },
            nativeScaling = pick("nativeScaling", nativeScaling) { nativeScaling },
            roundSprite = pick("roundSprite", roundSprite) { roundSprite },
            bilinearUpscale = pick("bilinearUpscale", bilinearUpscale) { bilinearUpscale },
            textureOffsetX = pick("textureOffsetX", textureOffsetX) { textureOffsetX },
            textureOffsetY = pick("textureOffsetY", textureOffsetY) { textureOffsetY },
            alignSprite = pick("alignSprite", alignSprite) { alignSprite },
            mergeSprite = pick("mergeSprite", mergeSprite) { mergeSprite },
            forceEvenSpritePosition = pick("forceEvenSpritePosition", forceEvenSpritePosition) { forceEvenSpritePosition },
            nativePaletteDraw = pick("nativePaletteDraw", nativePaletteDraw) { nativePaletteDraw }
        )
    }

    private fun LiveRuntimeSnapshot.applyProfile(profile: PerGameSettings?): LiveRuntimeSnapshot {
        if (profile == null) return this
        fun <T> pick(key: String, current: T, value: PerGameSettings.() -> T): T {
            val keys = profile.providedKeys
            return if (keys == null || key in keys) profile.value() else current
        }
        return copy(
            showFps = pick("showFps", showFps) { showFps },
            fpsOverlayMode = pick("fpsOverlayMode", fpsOverlayMode) { fpsOverlayMode },
            renderer = pick("renderer", renderer) { renderer },
            upscale = pick("upscaleMultiplier", upscale) { upscaleMultiplier },
            aspectRatio = pick("aspectRatio", aspectRatio) { aspectRatio },
            enableMtvu = pick("enableMtvu", enableMtvu) { enableMtvu },
            enableFastCdvd = pick("enableFastCdvd", enableFastCdvd) { enableFastCdvd },
            enableCheats = pick("enableCheats", enableCheats) { enableCheats },
            hwDownloadMode = pick("hwDownloadMode", hwDownloadMode) { hwDownloadMode },
            frameSkip = pick("frameSkip", frameSkip) { frameSkip },
            skipDuplicateFrames = pick("skipDuplicateFrames", skipDuplicateFrames) { skipDuplicateFrames },
            frameLimitEnabled = pick("frameLimitEnabled", frameLimitEnabled) { frameLimitEnabled },
            targetFps = pick("targetFps", targetFps) { targetFps },
            textureFiltering = pick("textureFiltering", textureFiltering) { textureFiltering },
            trilinearFiltering = pick("trilinearFiltering", trilinearFiltering) { trilinearFiltering },
            blendingAccuracy = pick("blendingAccuracy", blendingAccuracy) { blendingAccuracy },
            texturePreloading = pick("texturePreloading", texturePreloading) { texturePreloading },
            enableFxaa = pick("enableFxaa", enableFxaa) { enableFxaa },
            casMode = pick("casMode", casMode) { casMode },
            casSharpness = pick("casSharpness", casSharpness) { casSharpness },
            shadeBoostEnabled = pick("shadeBoostEnabled", shadeBoostEnabled) { shadeBoostEnabled },
            shadeBoostBrightness = pick("shadeBoostBrightness", shadeBoostBrightness) { shadeBoostBrightness },
            shadeBoostContrast = pick("shadeBoostContrast", shadeBoostContrast) { shadeBoostContrast },
            shadeBoostSaturation = pick("shadeBoostSaturation", shadeBoostSaturation) { shadeBoostSaturation },
            shadeBoostGamma = pick("shadeBoostGamma", shadeBoostGamma) { shadeBoostGamma },
            anisotropicFiltering = pick("anisotropicFiltering", anisotropicFiltering) { anisotropicFiltering },
            enableHwMipmapping = pick("enableHwMipmapping", enableHwMipmapping) { enableHwMipmapping },
            cpuSpriteRenderSize = pick("cpuSpriteRenderSize", cpuSpriteRenderSize) { cpuSpriteRenderSize },
            cpuSpriteRenderLevel = pick("cpuSpriteRenderLevel", cpuSpriteRenderLevel) { cpuSpriteRenderLevel },
            softwareClutRender = pick("softwareClutRender", softwareClutRender) { softwareClutRender },
            gpuTargetClutMode = pick("gpuTargetClutMode", gpuTargetClutMode) { gpuTargetClutMode },
            skipDrawStart = pick("skipDrawStart", skipDrawStart) { skipDrawStart },
            skipDrawEnd = pick("skipDrawEnd", skipDrawEnd) { skipDrawEnd },
            autoFlushHardware = pick("autoFlushHardware", autoFlushHardware) { autoFlushHardware },
            cpuFramebufferConversion = pick("cpuFramebufferConversion", cpuFramebufferConversion) { cpuFramebufferConversion },
            disableDepthConversion = pick("disableDepthConversion", disableDepthConversion) { disableDepthConversion },
            disableSafeFeatures = pick("disableSafeFeatures", disableSafeFeatures) { disableSafeFeatures },
            disableRenderFixes = pick("disableRenderFixes", disableRenderFixes) { disableRenderFixes },
            preloadFrameData = pick("preloadFrameData", preloadFrameData) { preloadFrameData },
            disablePartialInvalidation = pick("disablePartialInvalidation", disablePartialInvalidation) { disablePartialInvalidation },
            textureInsideRt = pick("textureInsideRt", textureInsideRt) { textureInsideRt },
            readTargetsOnClose = pick("readTargetsOnClose", readTargetsOnClose) { readTargetsOnClose },
            estimateTextureRegion = pick("estimateTextureRegion", estimateTextureRegion) { estimateTextureRegion },
            gpuPaletteConversion = pick("gpuPaletteConversion", gpuPaletteConversion) { gpuPaletteConversion },
            halfPixelOffset = pick("halfPixelOffset", halfPixelOffset) { halfPixelOffset },
            nativeScaling = pick("nativeScaling", nativeScaling) { nativeScaling },
            roundSprite = pick("roundSprite", roundSprite) { roundSprite },
            bilinearUpscale = pick("bilinearUpscale", bilinearUpscale) { bilinearUpscale },
            textureOffsetX = pick("textureOffsetX", textureOffsetX) { textureOffsetX },
            textureOffsetY = pick("textureOffsetY", textureOffsetY) { textureOffsetY },
            alignSprite = pick("alignSprite", alignSprite) { alignSprite },
            mergeSprite = pick("mergeSprite", mergeSprite) { mergeSprite },
            forceEvenSpritePosition = pick("forceEvenSpritePosition", forceEvenSpritePosition) { forceEvenSpritePosition },
            nativePaletteDraw = pick("nativePaletteDraw", nativePaletteDraw) { nativePaletteDraw }
        )
    }

    private suspend fun EmulationUiState.toPerGameSettings(
        gameKey: String,
        gameTitle: String,
        gameSerial: String?
    ): PerGameSettings {
        val globalShowFps = preferences.showFps.first()
        val globalFpsOverlayMode = preferences.fpsOverlayMode.first()
        val globalEnableMtvu = preferences.enableMtvu.first()
        val globalEnableFastCdvd = preferences.enableFastCdvd.first()
        val globalEnableCheats = preferences.enableCheats.first()
        val globalHwDownloadMode = preferences.hwDownloadMode.first()
        val globalEeCycleRate = preferences.eeCycleRate.first()
        val globalEeCycleSkip = preferences.eeCycleSkip.first()
        val globalSkipDuplicateFrames = preferences.skipDuplicateFrames.first()
        val globalFrameLimitEnabled = preferences.frameLimitEnabled.first()
        val globalTargetFps = preferences.targetFps.first()
        val globalWidescreenPatches = preferences.enableWidescreenPatches.first()
        val globalNoInterlacingPatches = preferences.enableNoInterlacingPatches.first()

        val profile = PerGameSettings(
            gameKey = gameKey,
            gameTitle = gameTitle,
            gameSerial = gameSerial,
            renderer = renderer,
            upscaleMultiplier = upscale,
            aspectRatio = aspectRatio,
            showFps = showFps,
            fpsOverlayMode = fpsOverlayMode,
            enableMtvu = enableMtvu,
            enableFastCdvd = enableFastCdvd,
            enableCheats = enableCheats,
            hwDownloadMode = hwDownloadMode,
            eeCycleRate = globalEeCycleRate,
            eeCycleSkip = globalEeCycleSkip,
            frameSkip = frameSkip,
            skipDuplicateFrames = skipDuplicateFrames,
            frameLimitEnabled = frameLimitEnabled,
            targetFps = targetFps,
            textureFiltering = textureFiltering,
            trilinearFiltering = trilinearFiltering,
            blendingAccuracy = blendingAccuracy,
            texturePreloading = texturePreloading,
            enableFxaa = enableFxaa,
            casMode = casMode,
            casSharpness = casSharpness,
            shadeBoostEnabled = shadeBoostEnabled,
            shadeBoostBrightness = shadeBoostBrightness,
            shadeBoostContrast = shadeBoostContrast,
            shadeBoostSaturation = shadeBoostSaturation,
            shadeBoostGamma = shadeBoostGamma,
            anisotropicFiltering = anisotropicFiltering,
            enableHwMipmapping = enableHwMipmapping,
            enableWidescreenPatches = globalWidescreenPatches,
            enableNoInterlacingPatches = globalNoInterlacingPatches,
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

        val providedKeys = buildSet {
            if (renderer != preferences.renderer.first()) add("renderer")
            if (upscale != preferences.upscaleMultiplier.first()) add("upscaleMultiplier")
            if (aspectRatio != preferences.aspectRatio.first()) add("aspectRatio")
            if (showFps != globalShowFps) add("showFps")
            if (fpsOverlayMode != globalFpsOverlayMode) add("fpsOverlayMode")
            if (enableMtvu != globalEnableMtvu) add("enableMtvu")
            if (enableFastCdvd != globalEnableFastCdvd) add("enableFastCdvd")
            if (enableCheats != globalEnableCheats) add("enableCheats")
            if (hwDownloadMode != globalHwDownloadMode) add("hwDownloadMode")
            if (profile.frameSkip != preferences.frameSkip.first()) add("frameSkip")
            if (skipDuplicateFrames != globalSkipDuplicateFrames) add("skipDuplicateFrames")
            if (frameLimitEnabled != globalFrameLimitEnabled) add("frameLimitEnabled")
            if (targetFps != globalTargetFps) add("targetFps")
            if (textureFiltering != preferences.textureFiltering.first()) add("textureFiltering")
            if (trilinearFiltering != preferences.trilinearFiltering.first()) add("trilinearFiltering")
            if (blendingAccuracy != preferences.blendingAccuracy.first()) add("blendingAccuracy")
            if (texturePreloading != preferences.texturePreloading.first()) add("texturePreloading")
            if (enableFxaa != preferences.enableFxaa.first()) add("enableFxaa")
            if (casMode != preferences.casMode.first()) add("casMode")
            if (casSharpness != preferences.casSharpness.first()) add("casSharpness")
            if (shadeBoostEnabled != preferences.shadeBoostEnabled.first()) add("shadeBoostEnabled")
            if (shadeBoostBrightness != preferences.shadeBoostBrightness.first()) add("shadeBoostBrightness")
            if (shadeBoostContrast != preferences.shadeBoostContrast.first()) add("shadeBoostContrast")
            if (shadeBoostSaturation != preferences.shadeBoostSaturation.first()) add("shadeBoostSaturation")
            if (shadeBoostGamma != preferences.shadeBoostGamma.first()) add("shadeBoostGamma")
            if (anisotropicFiltering != preferences.anisotropicFiltering.first()) add("anisotropicFiltering")
            if (enableHwMipmapping != preferences.enableHwMipmapping.first()) add("enableHwMipmapping")
            if (profile.enableWidescreenPatches != globalWidescreenPatches) add("enableWidescreenPatches")
            if (profile.enableNoInterlacingPatches != globalNoInterlacingPatches) add("enableNoInterlacingPatches")
            if (cpuSpriteRenderSize != preferences.cpuSpriteRenderSize.first()) add("cpuSpriteRenderSize")
            if (cpuSpriteRenderLevel != preferences.cpuSpriteRenderLevel.first()) add("cpuSpriteRenderLevel")
            if (softwareClutRender != preferences.softwareClutRender.first()) add("softwareClutRender")
            if (gpuTargetClutMode != preferences.gpuTargetClutMode.first()) add("gpuTargetClutMode")
            if (skipDrawStart != preferences.skipDrawStart.first()) add("skipDrawStart")
            if (skipDrawEnd != preferences.skipDrawEnd.first()) add("skipDrawEnd")
            if (autoFlushHardware != preferences.autoFlushHardware.first()) add("autoFlushHardware")
            if (cpuFramebufferConversion != preferences.cpuFramebufferConversion.first()) add("cpuFramebufferConversion")
            if (disableDepthConversion != preferences.disableDepthConversion.first()) add("disableDepthConversion")
            if (disableSafeFeatures != preferences.disableSafeFeatures.first()) add("disableSafeFeatures")
            if (disableRenderFixes != preferences.disableRenderFixes.first()) add("disableRenderFixes")
            if (preloadFrameData != preferences.preloadFrameData.first()) add("preloadFrameData")
            if (disablePartialInvalidation != preferences.disablePartialInvalidation.first()) add("disablePartialInvalidation")
            if (textureInsideRt != preferences.textureInsideRt.first()) add("textureInsideRt")
            if (readTargetsOnClose != preferences.readTargetsOnClose.first()) add("readTargetsOnClose")
            if (estimateTextureRegion != preferences.estimateTextureRegion.first()) add("estimateTextureRegion")
            if (gpuPaletteConversion != preferences.gpuPaletteConversion.first()) add("gpuPaletteConversion")
            if (halfPixelOffset != preferences.halfPixelOffset.first()) add("halfPixelOffset")
            if (nativeScaling != preferences.nativeScaling.first()) add("nativeScaling")
            if (roundSprite != preferences.roundSprite.first()) add("roundSprite")
            if (bilinearUpscale != preferences.bilinearUpscale.first()) add("bilinearUpscale")
            if (textureOffsetX != preferences.textureOffsetX.first()) add("textureOffsetX")
            if (textureOffsetY != preferences.textureOffsetY.first()) add("textureOffsetY")
            if (alignSprite != preferences.alignSprite.first()) add("alignSprite")
            if (mergeSprite != preferences.mergeSprite.first()) add("mergeSprite")
            if (forceEvenSpritePosition != preferences.forceEvenSpritePosition.first()) add("forceEvenSpritePosition")
            if (nativePaletteDraw != preferences.nativePaletteDraw.first()) add("nativePaletteDraw")
        }

        return profile.copy(providedKeys = providedKeys)
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
        _uiState.value = _uiState.value.copy(currentSlot = normalizeSaveSlot(slot))
    }

    private fun normalizeSaveSlot(slot: Int): Int = slot.coerceIn(1, 10)

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
        pausedForBackground = false
        viewModelScope.launch(Dispatchers.IO) {
            performShutdown()
            if (onExit != null) {
                kotlinx.coroutines.withContext(Dispatchers.Main) {
                    onExit.invoke()
                }
            }
        }
    }

    fun onHostBackgrounded() {
        viewModelScope.launch(Dispatchers.IO) {
            lifecycleMutex.withLock {
                val state = _uiState.value
                if (!state.isRunning || state.isStarting || state.isPaused || state.showMenu || isShuttingDown) {
                    return@withLock
                }
                try {
                    EmulatorBridge.pause()
                    pausedForBackground = true
                    _uiState.value = state.copy(isPaused = true)
                    updateCrashContext(launchState = "paused")
                } catch (_: Exception) { }
            }
        }
    }

    fun onHostForegrounded() {
        viewModelScope.launch(Dispatchers.IO) {
            lifecycleMutex.withLock {
                val state = _uiState.value
                if (!pausedForBackground ||
                    !state.isRunning ||
                    state.isStarting ||
                    !state.isPaused ||
                    state.showMenu ||
                    isShuttingDown
                ) {
                    return@withLock
                }
                try {
                    EmulatorBridge.resume()
                    pausedForBackground = false
                    _uiState.value = state.copy(isPaused = false)
                    updateCrashContext(launchState = "running")
                } catch (_: Exception) { }
            }
        }
    }

    private suspend fun performShutdown() {
        lifecycleMutex.withLock {
            if (!_uiState.value.isRunning && !_uiState.value.isStarting) return
            if (isShuttingDown) return
            isShuttingDown = true
            pausedForBackground = false
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
                    performanceOverlayText = "",
                    speedPercent = 100f,
                    statusMessage = null
                )
                syncNativePerformanceOverlayState(_uiState.value)
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
        NativeApp.setCrashContextString("emu_upscale", state.upscale.toString())
        NativeApp.setCrashContextInt("emu_aspect_ratio", state.aspectRatio)
        NativeApp.setCrashContextBool("emu_mtvu", state.enableMtvu)
        NativeApp.setCrashContextBool("emu_fast_cdvd", state.enableFastCdvd)
        NativeApp.setCrashContextBool("emu_enable_cheats", state.enableCheats)
        NativeApp.setCrashContextInt("emu_hw_download_mode", state.hwDownloadMode)
        NativeApp.setCrashContextInt("emu_frame_skip", state.frameSkip)
        NativeApp.setCrashContextBool("emu_skip_duplicate_frames", state.skipDuplicateFrames)
        NativeApp.setCrashContextBool("emu_frame_limit_enabled", state.frameLimitEnabled)
        NativeApp.setCrashContextInt("emu_target_fps", state.targetFps)
        NativeApp.setCrashContextInt("emu_texture_filtering", state.textureFiltering)
        NativeApp.setCrashContextString("emu_device_model", Build.MODEL.orEmpty())
        NativeApp.setCrashContextString("emu_soc_model", if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else "")
        NativeApp.setCrashContextBool("emu_running", state.isRunning)
        NativeApp.setCrashContextBool("emu_paused", state.isPaused)
    }

    private fun clearCrashContext() {
        currentGameTitle = ""
        currentGamePath = null
        currentGameSerial = ""
        currentGameCrc = ""
        _uiState.value = _uiState.value.copy(
            currentGameTitle = "",
            currentGameSubtitle = "",
            gameSettingsProfileActive = false
        )
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

    override fun onCleared() {
        NativeApp.setPerformanceOverlayMode(visible = false, detailed = false)
        NativeApp.setPerformanceListener(null)
        if (_uiState.value.isRunning) {
            EmulatorBridge.resetKeyStatus()
            runCatching {
                kotlinx.coroutines.runBlocking(Dispatchers.IO) {
                    EmulatorBridge.shutdown()
                }
            }
        }
        super.onCleared()
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

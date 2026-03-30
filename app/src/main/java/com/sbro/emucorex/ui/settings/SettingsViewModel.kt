package com.sbro.emucorex.ui.settings

import android.app.Application
import android.content.Intent
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.DeviceChipsetFamily
import com.sbro.emucorex.core.DevicePerformanceProfiles
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.PerformancePresetConfig
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class SettingsUiState(
    val themeMode: ThemeMode = ThemeMode.SYSTEM,
    val languageTag: String? = null,
    val renderer: Int = 0,
    val upscaleMultiplier: Int = 1,
    val aspectRatio: Int = 1,
    val padVibration: Boolean = true,
    val showFps: Boolean = true,
    val fpsOverlayMode: Int = FPS_OVERLAY_MODE_DETAILED,
    val compactControls: Boolean = true,
    val keepScreenOn: Boolean = true,
    val biosPath: String? = null,
    val gamePath: String? = null,
    val biosValid: Boolean = false,
    val setupComplete: Boolean = false,
    val appVersion: String = "1.0.0",
    // Extended settings
    val eeCycleRate: Int = 0,
    val eeCycleSkip: Int = 0,
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
    val enableWidescreenPatches: Boolean = false,
    val enableNoInterlacingPatches: Boolean = false,
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
    val performancePreset: Int = PerformancePresets.CUSTOM,
    // Overlay
    val overlayScale: Int = 100,
    val overlayOpacity: Int = 80,
    val overlayShow: Boolean = true,
    // Gamepad
    val enableAutoGamepad: Boolean = true,
    val hideOverlayOnGamepad: Boolean = true,
    val gamepadBindings: Map<String, Int> = emptyMap(),
    val gpuDriverType: Int = 0,
    val customDriverPath: String? = null,
    val deviceChipsetFamily: DeviceChipsetFamily = DeviceChipsetFamily.GENERIC,
    val detectedChipsetName: String = "",
    val recommendedPresetId: Int = PerformancePresets.BALANCED,
    val frameLimitEnabled: Boolean = false,
    val targetFps: Int = 60
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val deviceProfile = DevicePerformanceProfiles.current()
    private val _uiState = MutableStateFlow(SettingsUiState())
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    init {
        _uiState.value = _uiState.value.copy(
            deviceChipsetFamily = deviceProfile.family,
            detectedChipsetName = deviceProfile.displayName,
            recommendedPresetId = deviceProfile.recommendedPresetId
        )
        viewModelScope.launch {
            preferences.settingsSnapshot.collect { snapshot ->
                applySettingsSnapshot(snapshot)
            }
        }

        try {
            val pInfo = application.packageManager.getPackageInfo(application.packageName, 0)
            _uiState.value = _uiState.value.copy(appVersion = pInfo.versionName ?: "1.0.0")
        } catch (_: Exception) { }
    }

    private fun applySettingsSnapshot(snapshot: SettingsSnapshot) {
        _uiState.value = _uiState.value.copy(
            themeMode = snapshot.themeMode,
            languageTag = snapshot.languageTag,
            renderer = snapshot.renderer,
            upscaleMultiplier = snapshot.upscaleMultiplier,
            aspectRatio = snapshot.aspectRatio,
            padVibration = snapshot.padVibration,
            showFps = snapshot.showFps,
            fpsOverlayMode = snapshot.fpsOverlayMode,
            compactControls = snapshot.compactControls,
            keepScreenOn = snapshot.keepScreenOn,
            biosPath = snapshot.biosPath,
            gamePath = snapshot.gamePath,
            biosValid = snapshot.biosValid,
            setupComplete = snapshot.setupComplete,
            eeCycleRate = snapshot.eeCycleRate,
            eeCycleSkip = snapshot.eeCycleSkip,
            enableMtvu = snapshot.enableMtvu,
            enableFastCdvd = snapshot.enableFastCdvd,
            enableCheats = snapshot.enableCheats,
            hwDownloadMode = snapshot.hwDownloadMode,
            frameSkip = snapshot.frameSkip,
            textureFiltering = snapshot.textureFiltering,
            trilinearFiltering = snapshot.trilinearFiltering,
            blendingAccuracy = snapshot.blendingAccuracy,
            texturePreloading = snapshot.texturePreloading,
            enableFxaa = snapshot.enableFxaa,
            casMode = snapshot.casMode,
            casSharpness = snapshot.casSharpness,
            enableWidescreenPatches = snapshot.enableWidescreenPatches,
            enableNoInterlacingPatches = snapshot.enableNoInterlacingPatches,
            anisotropicFiltering = snapshot.anisotropicFiltering,
            enableHwMipmapping = snapshot.enableHwMipmapping,
            cpuSpriteRenderSize = snapshot.cpuSpriteRenderSize,
            cpuSpriteRenderLevel = snapshot.cpuSpriteRenderLevel,
            softwareClutRender = snapshot.softwareClutRender,
            gpuTargetClutMode = snapshot.gpuTargetClutMode,
            skipDrawStart = snapshot.skipDrawStart,
            skipDrawEnd = snapshot.skipDrawEnd,
            autoFlushHardware = snapshot.autoFlushHardware,
            cpuFramebufferConversion = snapshot.cpuFramebufferConversion,
            disableDepthConversion = snapshot.disableDepthConversion,
            disableSafeFeatures = snapshot.disableSafeFeatures,
            disableRenderFixes = snapshot.disableRenderFixes,
            preloadFrameData = snapshot.preloadFrameData,
            disablePartialInvalidation = snapshot.disablePartialInvalidation,
            textureInsideRt = snapshot.textureInsideRt,
            readTargetsOnClose = snapshot.readTargetsOnClose,
            estimateTextureRegion = snapshot.estimateTextureRegion,
            gpuPaletteConversion = snapshot.gpuPaletteConversion,
            halfPixelOffset = snapshot.halfPixelOffset,
            nativeScaling = snapshot.nativeScaling,
            roundSprite = snapshot.roundSprite,
            bilinearUpscale = snapshot.bilinearUpscale,
            textureOffsetX = snapshot.textureOffsetX,
            textureOffsetY = snapshot.textureOffsetY,
            alignSprite = snapshot.alignSprite,
            mergeSprite = snapshot.mergeSprite,
            forceEvenSpritePosition = snapshot.forceEvenSpritePosition,
            nativePaletteDraw = snapshot.nativePaletteDraw,
            performancePreset = snapshot.performancePreset,
            overlayScale = snapshot.overlayScale,
            overlayOpacity = snapshot.overlayOpacity,
            overlayShow = snapshot.overlayShow,
            enableAutoGamepad = snapshot.enableAutoGamepad,
            hideOverlayOnGamepad = snapshot.hideOverlayOnGamepad,
            gamepadBindings = snapshot.gamepadBindings,
            gpuDriverType = snapshot.gpuDriverType,
            customDriverPath = snapshot.customDriverPath,
            frameLimitEnabled = snapshot.frameLimitEnabled,
            targetFps = snapshot.targetFps
        )
    }

    fun setThemeMode(mode: ThemeMode) { viewModelScope.launch { preferences.setThemeMode(mode) } }
    fun setLanguage(tag: String?) { viewModelScope.launch { preferences.setLanguageTag(tag) } }

    fun setRenderer(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setRenderer(value)
            EmulatorBridge.setRenderer(value)
        }
    }

    fun setGpuDriverType(value: Int) {
        viewModelScope.launch {
            preferences.setGpuDriverType(value)
            if (value == 0) {
                EmulatorBridge.setCustomDriverPath("")
            } else {
                _uiState.value.customDriverPath?.let { EmulatorBridge.setCustomDriverPath(it) }
            }
        }
    }

    fun setCustomDriverPath(path: String?) {
        viewModelScope.launch {
            preferences.setCustomDriverPath(path)
            if (path != null) {
                preferences.setGpuDriverType(1)
                EmulatorBridge.setCustomDriverPath(path)
            }
        }
    }

    fun setUpscaleMultiplier(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setUpscaleMultiplier(value)
            EmulatorBridge.setUpscaleMultiplier(value.toFloat())
        }
    }

    fun setAspectRatio(value: Int) {
        viewModelScope.launch {
            preferences.setAspectRatio(value)
            EmulatorBridge.setAspectRatio(value)
        }
    }

    fun setPadVibration(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setPadVibration(enabled)
            EmulatorBridge.setPadVibration(enabled)
        }
    }

    fun setGamepadBinding(actionId: String, keyCode: Int) {
        viewModelScope.launch {
            preferences.setGamepadBinding(actionId, keyCode)
        }
    }

    fun clearGamepadBinding(actionId: String) {
        viewModelScope.launch {
            preferences.clearGamepadBinding(actionId)
        }
    }

    fun resetGamepadBindings() {
        viewModelScope.launch {
            preferences.resetGamepadBindings()
        }
    }

    fun resetAllSettings() {
        viewModelScope.launch {
            preferences.resetAllSettings()
        }
    }

    fun setEnableCheats(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setEnableCheats(enabled)
            EmulatorBridge.setSetting("EmuCore", "EnableCheats", "bool", enabled.toString())
        }
    }

    fun setShowFps(enabled: Boolean) { viewModelScope.launch { preferences.setShowFps(enabled) } }
    fun setFpsOverlayMode(mode: Int) { viewModelScope.launch { preferences.setFpsOverlayMode(mode) } }
    fun setKeepScreenOn(enabled: Boolean) { viewModelScope.launch { preferences.setKeepScreenOn(enabled) } }

    // Extended settings
    fun setEeCycleRate(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEeCycleRate(value)
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "EECycleRate", "int", value.toString())
        }
    }

    fun setEeCycleSkip(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEeCycleSkip(value)
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "EECycleSkip", "int", value.toString())
        }
    }

    fun setEnableMtvu(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableMtvu(enabled)
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "vuThread", "bool", enabled.toString())
        }
    }

    fun setEnableFastCdvd(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableFastCdvd(enabled)
            EmulatorBridge.setSetting("EmuCore/Speedhacks", "fastCDVD", "bool", enabled.toString())
        }
    }

    fun setHwDownloadMode(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setHwDownloadMode(value)
            EmulatorBridge.setSetting("EmuCore/GS", "HWDownloadMode", "int", value.toString())
        }
    }

    fun setFrameSkip(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setFrameSkip(value)
            EmulatorBridge.setSetting("EmuCore/GS", "FrameSkip", "int", value.toString())
        }
    }

    fun setFrameLimitEnabled(enabled: Boolean) {
        viewModelScope.launch {
            preferences.setFrameLimitEnabled(enabled)
            EmulatorBridge.setFrameLimitEnabled(enabled)
        }
    }

    fun setTargetFps(value: Int) {
        viewModelScope.launch {
            preferences.setTargetFps(value)
            EmulatorBridge.setTargetFps(value)
        }
    }

    fun setTextureFiltering(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureFiltering(value)
            EmulatorBridge.setSetting("EmuCore/GS", "filter", "int", value.toString())
        }
    }

    fun setTrilinearFiltering(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTrilinearFiltering(value)
            EmulatorBridge.setSetting("EmuCore/GS", "TriFilter", "int", value.toString())
        }
    }

    fun setBlendingAccuracy(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setBlendingAccuracy(value)
            EmulatorBridge.setSetting("EmuCore/GS", "accurate_blending_unit", "int", value.toString())
        }
    }

    fun setTexturePreloading(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTexturePreloading(value)
            EmulatorBridge.setSetting("EmuCore/GS", "texture_preloading", "int", value.toString())
        }
    }

    fun setEnableFxaa(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableFxaa(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "fxaa", "bool", enabled.toString())
        }
    }

    fun setCasMode(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCasMode(value)
            EmulatorBridge.setSetting("EmuCore/GS", "CASMode", "int", value.toString())
        }
    }

    fun setCasSharpness(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCasSharpness(value)
            EmulatorBridge.setSetting("EmuCore/GS", "CASSharpness", "int", value.toString())
        }
    }

    fun setEnableWidescreenPatches(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableWidescreenPatches(enabled)
            EmulatorBridge.setSetting("EmuCore", "EnableWideScreenPatches", "bool", enabled.toString())
        }
    }

    fun setEnableNoInterlacingPatches(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableNoInterlacingPatches(enabled)
            EmulatorBridge.setSetting("EmuCore", "EnableNoInterlacingPatches", "bool", enabled.toString())
        }
    }

    fun setAnisotropicFiltering(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setAnisotropicFiltering(value)
            EmulatorBridge.setSetting("EmuCore/GS", "MaxAnisotropy", "int", value.toString())
        }
    }

    fun setEnableHwMipmapping(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEnableHwMipmapping(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "hw_mipmap", "bool", enabled.toString())
        }
    }

    fun setCpuSpriteRenderSize(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCpuSpriteRenderSize(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderBW", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(cpuSpriteRenderSize = value))
        }
    }

    fun setCpuSpriteRenderLevel(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCpuSpriteRenderLevel(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(cpuSpriteRenderLevel = value))
        }
    }

    fun setSoftwareClutRender(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setSoftwareClutRender(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPUCLUTRender", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(softwareClutRender = value))
        }
    }

    fun setGpuTargetClutMode(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setGpuTargetClutMode(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_GPUTargetCLUTMode", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(gpuTargetClutMode = value))
        }
    }

    fun setSkipDrawStart(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setSkipDrawStart(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_Start", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(skipDrawStart = value))
        }
    }

    fun setSkipDrawEnd(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setSkipDrawEnd(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_SkipDraw_End", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(skipDrawEnd = value))
        }
    }

    fun setAutoFlushHardware(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setAutoFlushHardware(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_AutoFlushLevel", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(autoFlushHardware = value))
        }
    }

    fun setCpuFramebufferConversion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setCpuFramebufferConversion(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_CPU_FB_Conversion", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(cpuFramebufferConversion = enabled))
        }
    }

    fun setDisableDepthConversion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisableDepthConversion(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisableDepthSupport", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(disableDepthConversion = enabled))
        }
    }

    fun setDisableSafeFeatures(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisableSafeFeatures(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_Disable_Safe_Features", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(disableSafeFeatures = enabled))
        }
    }

    fun setDisableRenderFixes(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisableRenderFixes(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisableRenderFixes", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(disableRenderFixes = enabled))
        }
    }

    fun setPreloadFrameData(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setPreloadFrameData(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "preload_frame_with_gs_data", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(preloadFrameData = enabled))
        }
    }

    fun setDisablePartialInvalidation(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setDisablePartialInvalidation(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_DisablePartialInvalidation", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(disablePartialInvalidation = enabled))
        }
    }

    fun setTextureInsideRt(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureInsideRt(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TextureInsideRt", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(textureInsideRt = value))
        }
    }

    fun setReadTargetsOnClose(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setReadTargetsOnClose(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_ReadTCOnClose", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(readTargetsOnClose = enabled))
        }
    }

    fun setEstimateTextureRegion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setEstimateTextureRegion(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_EstimateTextureRegion", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(estimateTextureRegion = enabled))
        }
    }

    fun setGpuPaletteConversion(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setGpuPaletteConversion(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "paltex", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(gpuPaletteConversion = enabled))
        }
    }

    fun setHalfPixelOffset(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setHalfPixelOffset(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_HalfPixelOffset", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(halfPixelOffset = value))
        }
    }

    fun setNativeScaling(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setNativeScaling(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_native_scaling", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(nativeScaling = value))
        }
    }

    fun setRoundSprite(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setRoundSprite(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_round_sprite_offset", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(roundSprite = value))
        }
    }

    fun setBilinearUpscale(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setBilinearUpscale(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_BilinearHack", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(bilinearUpscale = value))
        }
    }

    fun setTextureOffsetX(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureOffsetX(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TCOffsetX", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(textureOffsetX = value))
        }
    }

    fun setTextureOffsetY(value: Int) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setTextureOffsetY(value)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_TCOffsetY", "int", value.toString())
            refreshManualHardwareFixes(_uiState.value.copy(textureOffsetY = value))
        }
    }

    fun setAlignSprite(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setAlignSprite(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_align_sprite_X", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(alignSprite = enabled))
        }
    }

    fun setMergeSprite(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setMergeSprite(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_merge_pp_sprite", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(mergeSprite = enabled))
        }
    }

    fun setForceEvenSpritePosition(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setForceEvenSpritePosition(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_ForceEvenSpritePosition", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(forceEvenSpritePosition = enabled))
        }
    }

    fun setNativePaletteDraw(enabled: Boolean) {
        viewModelScope.launch {
            markPerformancePresetCustom()
            preferences.setNativePaletteDraw(enabled)
            EmulatorBridge.setSetting("EmuCore/GS", "UserHacks_NativePaletteDraw", "bool", enabled.toString())
            refreshManualHardwareFixes(_uiState.value.copy(nativePaletteDraw = enabled))
        }
    }

    fun applyPerformancePreset(presetId: Int) {
        viewModelScope.launch {
            val config = PerformancePresets.configFor(presetId, deviceProfile) ?: run {
                preferences.setPerformancePreset(PerformancePresets.CUSTOM)
                return@launch
            }

            preferences.applyPerformancePreset(config)
            applyRuntimePerformancePreset(config)
        }
    }

    fun applyRecommendedDeviceProfile() {
        applyPerformancePreset(deviceProfile.recommendedPresetId)
    }

    private suspend fun markPerformancePresetCustom() {
        if (_uiState.value.performancePreset != PerformancePresets.CUSTOM) {
            preferences.setPerformancePreset(PerformancePresets.CUSTOM)
        }
    }

    private fun refreshManualHardwareFixes(state: SettingsUiState = _uiState.value) {
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

    // Overlay
    fun setOverlayScale(value: Int) { viewModelScope.launch { preferences.setOverlayScale(value) } }
    fun setOverlayOpacity(value: Int) { viewModelScope.launch { preferences.setOverlayOpacity(value) } }

    // Gamepad
    fun setEnableAutoGamepad(enabled: Boolean) { viewModelScope.launch { preferences.setEnableAutoGamepad(enabled) } }
    fun setHideOverlayOnGamepad(enabled: Boolean) { viewModelScope.launch { preferences.setHideOverlayOnGamepad(enabled) } }

    fun setBiosPath(uri: Uri) {
        val application = getApplication<Application>()
        application.contentResolver.takePersistableUriPermission(
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION
        )
        viewModelScope.launch {
            preferences.setBiosPath(uri.toString())
            EmulatorBridge.applyRuntimeConfig(
                biosPath = uri.toString(),
                renderer = _uiState.value.renderer,
                upscaleMultiplier = _uiState.value.upscaleMultiplier,
                gpuDriverType = _uiState.value.gpuDriverType,
                customDriverPath = _uiState.value.customDriverPath,
                hwDownloadMode = _uiState.value.hwDownloadMode,
                textureFiltering = _uiState.value.textureFiltering,
                trilinearFiltering = _uiState.value.trilinearFiltering,
                blendingAccuracy = _uiState.value.blendingAccuracy,
                texturePreloading = _uiState.value.texturePreloading,
                enableFxaa = _uiState.value.enableFxaa,
                casMode = _uiState.value.casMode,
                casSharpness = _uiState.value.casSharpness,
                anisotropicFiltering = _uiState.value.anisotropicFiltering,
                enableHwMipmapping = _uiState.value.enableHwMipmapping,
                cpuSpriteRenderSize = _uiState.value.cpuSpriteRenderSize,
                cpuSpriteRenderLevel = _uiState.value.cpuSpriteRenderLevel,
                softwareClutRender = _uiState.value.softwareClutRender,
                gpuTargetClutMode = _uiState.value.gpuTargetClutMode,
                skipDrawStart = _uiState.value.skipDrawStart,
                skipDrawEnd = _uiState.value.skipDrawEnd,
                autoFlushHardware = _uiState.value.autoFlushHardware,
                cpuFramebufferConversion = _uiState.value.cpuFramebufferConversion,
                disableDepthConversion = _uiState.value.disableDepthConversion,
                disableSafeFeatures = _uiState.value.disableSafeFeatures,
                disableRenderFixes = _uiState.value.disableRenderFixes,
                preloadFrameData = _uiState.value.preloadFrameData,
                disablePartialInvalidation = _uiState.value.disablePartialInvalidation,
                textureInsideRt = _uiState.value.textureInsideRt,
                readTargetsOnClose = _uiState.value.readTargetsOnClose,
                estimateTextureRegion = _uiState.value.estimateTextureRegion,
                gpuPaletteConversion = _uiState.value.gpuPaletteConversion,
                halfPixelOffset = _uiState.value.halfPixelOffset,
                nativeScaling = _uiState.value.nativeScaling,
                roundSprite = _uiState.value.roundSprite,
                bilinearUpscale = _uiState.value.bilinearUpscale,
                textureOffsetX = _uiState.value.textureOffsetX,
                textureOffsetY = _uiState.value.textureOffsetY,
                alignSprite = _uiState.value.alignSprite,
                mergeSprite = _uiState.value.mergeSprite,
                forceEvenSpritePosition = _uiState.value.forceEvenSpritePosition,
                nativePaletteDraw = _uiState.value.nativePaletteDraw
            )
        }
    }

    fun setGamePath(uri: Uri) {
        val application = getApplication<Application>()
        application.contentResolver.takePersistableUriPermission(
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION
        )
        viewModelScope.launch { preferences.setGamePath(uri.toString()) }
    }

}

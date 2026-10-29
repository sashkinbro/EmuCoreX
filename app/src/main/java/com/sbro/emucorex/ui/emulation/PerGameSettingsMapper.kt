package com.sbro.emucorex.ui.emulation

import android.app.Application
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.EmuCoreXApp
import com.sbro.emucorex.discord.DiscordIntegration
import com.sbro.emucorex.core.AndroidGamePerformance
import com.sbro.emucorex.core.AndroidGamePhase
import com.sbro.emucorex.core.AppAnalytics
import com.sbro.emucorex.core.AudioDefaults
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.RendererDefaults
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.GpuDriverManager
import com.sbro.emucorex.core.FrameGenerationManager
import com.sbro.emucorex.core.FrameGenerationSettings
import com.sbro.emucorex.core.GsHackDefaults
import com.sbro.emucorex.core.MobileSocNameMapper
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.core.PerformancePresets
import com.sbro.emucorex.core.resolveAndroidGamePhase
import com.sbro.emucorex.core.utils.RetroAchievementsLiveStateManager
import com.sbro.emucorex.core.normalizeUpscale
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_SIMPLE
import com.sbro.emucorex.data.AppPreferences.Companion.FPS_OVERLAY_MODE_DETAILED
import com.sbro.emucorex.data.CheatBlock
import com.sbro.emucorex.data.DisplayCrop
import com.sbro.emucorex.data.OverlayControlLayout
import com.sbro.emucorex.data.CheatRepository
import com.sbro.emucorex.data.GameRepository
import com.sbro.emucorex.data.MemoryCardRepository
import com.sbro.emucorex.data.OverlayLayoutSnapshot
import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.PerGameSettingsRepository
import com.sbro.emucorex.data.resolvePerGameMemoryCardOverride
import com.sbro.emucorex.data.resolveShaderChain
import com.sbro.emucorex.data.toggleStick
import com.sbro.emucorex.data.TouchControlsLayoutProfile
import com.sbro.emucorex.data.PER_GAME_CUSTOM_TOUCH_CONTROLS_KEY
import com.sbro.emucorex.data.PER_GAME_TOUCH_CONTROLS_LAYOUT_KEY
import com.sbro.emucorex.data.saveTouchControlsLayout
import com.sbro.emucorex.data.withCustomTouchControls
import com.sbro.emucorex.data.withTouchControlsLayout
import com.sbro.emucorex.data.withoutTouchControlsLayout
import com.sbro.emucorex.data.TouchControlVisualStyle
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.CustomTouchControl
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.data.GameMenuLayoutStyle
import com.sbro.emucorex.data.GameMenuTabId
import com.sbro.emucorex.data.GameMenuSectionId
import com.sbro.emucorex.data.DefaultGameMenuTabOrder
import com.sbro.emucorex.data.DefaultGameMenuSectionOrder
import com.sbro.emucorex.data.PlayTimeSyncCacheRepository
import com.sbro.emucorex.data.PerformanceOverlayMetrics
import com.sbro.emucorex.data.PlayerPlayTimeDelta
import com.sbro.emucorex.data.PlayerProfileRepository
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale
import kotlin.time.Duration.Companion.milliseconds

    private val PER_GAME_GS_SETTING_KEYS = setOf("hwDownloadMode", "blendingAccuracy")

    internal fun isPerGameGsSettingOwned(providedKeys: Set<String>?, key: String, globalOwned: Boolean): Boolean =
        providedKeys == null || key in providedKeys || globalOwned

    internal fun mergePerGameGsProvidedKeys(
        runtimeKeys: Set<String>?,
        existingKeys: Set<String>?,
        explicitlyClaimedGsKeys: Set<String> = emptySet()
    ): Set<String>? {
        if (runtimeKeys == null) return null
        return buildSet {
            addAll(runtimeKeys)
            addAll((existingKeys ?: PER_GAME_GS_SETTING_KEYS).intersect(PER_GAME_GS_SETTING_KEYS))
            addAll(explicitlyClaimedGsKeys.intersect(PER_GAME_GS_SETTING_KEYS))
        }
    }

    internal fun EmulationLaunchConfig.applyProfile(profile: PerGameSettings?): EmulationLaunchConfig {
        if (profile == null) return this
        val resolvedShaderChain = profile.resolveShaderChain(
            globalEnabled = shaderChainEnabled,
            globalPreset = shaderChainPreset
        )
        fun <T> pick(key: String, current: T, value: PerGameSettings.() -> T): T {
            val keys = profile.providedKeys
            return if (keys == null || key in keys) profile.value() else current
        }
        val resolvedMemoryCardSlot1 = resolvePerGameMemoryCardOverride(
            profile.providedKeys,
            "memoryCardSlot1",
            memoryCardSlot1,
            profile.memoryCardSlot1
        )
        val resolvedMemoryCardSlot2 = resolvePerGameMemoryCardOverride(
            profile.providedKeys,
            "memoryCardSlot2",
            memoryCardSlot2,
            profile.memoryCardSlot2
        )
        return copy(
            renderer = pick("renderer", renderer) { renderer },
            gpuDriverType = pick("gpuDriverType", gpuDriverType) { gpuDriverType },
            customDriverPath = pick("customDriverPath", customDriverPath) { customDriverPath },
            memoryCardSlot1 = resolvedMemoryCardSlot1,
            memoryCardSlot2 = resolvedMemoryCardSlot2,
            mediatekAngleOpenGl = pick("mediatekAngleOpenGl", mediatekAngleOpenGl) { mediatekAngleOpenGl },
            upscaleMultiplier = pick("upscaleMultiplier", upscaleMultiplier) { upscaleMultiplier },
            aspectRatio = pick("aspectRatio", aspectRatio) { aspectRatio },
            localMultiplayerMode = pick("localMultiplayerMode", localMultiplayerMode) { localMultiplayerMode },
            displayCrop = pick("displayCrop", displayCrop) { displayCrop },
            instantVu1 = pick("enableInstantVu1", instantVu1) { enableInstantVu1 },
            mtvu = pick("enableMtvu", mtvu) { enableMtvu },
            enableIopThread = pick("enableIopThread", enableIopThread) { enableIopThread },
            enableVUAot = pick("enableVUAot", enableVUAot) { enableVUAot },
            enableThreadPinning = pick("enableThreadPinning", enableThreadPinning) { enableThreadPinning },
            fastCdvd = pick("enableFastCdvd", fastCdvd) { enableFastCdvd },
            enableFastBoot = pick("enableFastBoot", enableFastBoot) { enableFastBoot },
            enableCheats = pick("enableCheats", enableCheats) { enableCheats },
            enableGameFixes = pick("enableGameFixes", enableGameFixes) { enableGameFixes },
            autoGsHardwareFixes = pick("autoGsHardwareFixes", autoGsHardwareFixes) { autoGsHardwareFixes },
            eeTimingHack = pick("enableEeTimingHack", eeTimingHack) { enableEeTimingHack },
            eeFpuRoundMode = pick("eeFpuRoundMode", eeFpuRoundMode) { eeFpuRoundMode },
            vu0RoundMode = pick("vu0RoundMode", vu0RoundMode) { vu0RoundMode },
            vu1RoundMode = pick("vu1RoundMode", vu1RoundMode) { vu1RoundMode },
            eeFpuClampingMode = pick("eeFpuClampingMode", eeFpuClampingMode) { eeFpuClampingMode },
            vu0ClampingMode = pick("vu0ClampingMode", vu0ClampingMode) { vu0ClampingMode },
            vu1ClampingMode = pick("vu1ClampingMode", vu1ClampingMode) { vu1ClampingMode },
            hwDownloadMode = pick("hwDownloadMode", hwDownloadMode) { hwDownloadMode },
            hwDownloadModeUserOverride = isPerGameGsSettingOwned(
                profile.providedKeys,
                "hwDownloadMode",
                hwDownloadModeUserOverride
            ),
            eeCycleRate = pick("eeCycleRate", eeCycleRate) { eeCycleRate },
            eeCycleSkip = pick("eeCycleSkip", eeCycleSkip) { eeCycleSkip },
            frameSkip = pick("frameSkip", frameSkip) { frameSkip },
            skipDuplicateFrames = pick("skipDuplicateFrames", skipDuplicateFrames) { skipDuplicateFrames },
            lowLatencyMode = pick("lowLatencyMode", lowLatencyMode) { lowLatencyMode },
            frameLimitEnabled = pick("frameLimitEnabled", frameLimitEnabled) { frameLimitEnabled },
            targetFps = pick("targetFps", targetFps) { targetFps },
            ntscFramerate = pick("ntscFramerate", ntscFramerate) { ntscFramerate },
            palFramerate = pick("palFramerate", palFramerate) { palFramerate },
            textureFiltering = pick("textureFiltering", textureFiltering) { textureFiltering },
            trilinearFiltering = pick("trilinearFiltering", trilinearFiltering) { trilinearFiltering },
            blendingAccuracy = pick("blendingAccuracy", blendingAccuracy) { blendingAccuracy },
            blendingAccuracyUserOverride = isPerGameGsSettingOwned(
                profile.providedKeys,
                "blendingAccuracy",
                blendingAccuracyUserOverride
            ),
            texturePreloading = pick("texturePreloading", texturePreloading) { texturePreloading },
            shaderChainEnabled = resolvedShaderChain.enabled,
            shaderChainPreset = resolvedShaderChain.preset,
            enableFxaa = pick("enableFxaa", enableFxaa) { enableFxaa },
            casMode = pick("casMode", casMode) { casMode },
            sgsrMode = pick("sgsrMode", sgsrMode) { sgsrMode },
            casSharpness = pick("casSharpness", casSharpness) { casSharpness },
            tvShader = pick("tvShader", tvShader) { tvShader },
            shadeBoostEnabled = pick("shadeBoostEnabled", shadeBoostEnabled) { shadeBoostEnabled },
            shadeBoostBrightness = pick("shadeBoostBrightness", shadeBoostBrightness) { shadeBoostBrightness },
            shadeBoostContrast = pick("shadeBoostContrast", shadeBoostContrast) { shadeBoostContrast },
            shadeBoostSaturation = pick("shadeBoostSaturation", shadeBoostSaturation) { shadeBoostSaturation },
            shadeBoostGamma = pick("shadeBoostGamma", shadeBoostGamma) { shadeBoostGamma },
            deinterlaceMode = pick("deinterlaceMode", deinterlaceMode) { deinterlaceMode },
            dithering = pick("dithering", dithering) { dithering },
            anisotropicFiltering = pick("anisotropicFiltering", anisotropicFiltering) { anisotropicFiltering },
            enableHwMipmapping = pick("enableHwMipmapping", enableHwMipmapping) { enableHwMipmapping },
            antiBlur = pick("antiBlur", antiBlur) { antiBlur },
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
            nativePaletteDraw = pick("nativePaletteDraw", nativePaletteDraw) { nativePaletteDraw },
            pressureModifierAmount = pick("pressureModifierAmount", pressureModifierAmount) { pressureModifierAmount }
        )
    }

    internal fun EmulationUiState.withTouchControlsLayout(
        profile: TouchControlsLayoutProfile
    ): EmulationUiState {
        return copy(
            dpadOffset = profile.dpadOffset,
            lstickOffset = profile.lstickOffset,
            rstickOffset = profile.rstickOffset,
            actionOffset = profile.actionOffset,
            lbtnOffset = profile.lbtnOffset,
            rbtnOffset = profile.rbtnOffset,
            centerOffset = profile.centerOffset,
            stickScale = profile.stickScale,
            controlLayouts = profile.controlLayouts
        )
    }

    // Applies a per-game touch profile before the first frame so the global layout is
    // never shown while the game is still starting.
    internal fun EmulationUiState.withTouchControlsProfile(
        layout: TouchControlsLayoutProfile?,
        customControls: CustomTouchControlLibrary?
    ): EmulationUiState {
        val withLayout = layout?.let { withTouchControlsLayout(it) } ?: this
        return customControls?.let { withLayout.copy(customTouchControls = it) } ?: withLayout
    }

    internal fun LiveRuntimeSnapshot.applyProfile(profile: PerGameSettings?): LiveRuntimeSnapshot {
        if (profile == null) return this
        fun <T> pick(key: String, current: T, value: PerGameSettings.() -> T): T {
            val keys = profile.providedKeys
            return if (keys == null || key in keys) profile.value() else current
        }
        return copy(
            showFps = pick("showFps", showFps) { showFps },
            fpsOverlayMode = pick("fpsOverlayMode", fpsOverlayMode) { fpsOverlayMode },
            racingMode = pick("racingMode", racingMode) { racingMode },
            stickyButtons = pick("stickyButtons", stickyButtons) { stickyButtons },
            touchscreenRightStick = pick("touchscreenRightStick", touchscreenRightStick) { touchscreenRightStick },
            touchscreenRightStickSensitivity = pick(
                "touchscreenRightStickSensitivity",
                touchscreenRightStickSensitivity
            ) { touchscreenRightStickSensitivity },
            touchscreenLeftStick = pick("touchscreenLeftStick", touchscreenLeftStick) { touchscreenLeftStick },
            touchscreenLeftStickSensitivity = pick(
                "touchscreenLeftStickSensitivity",
                touchscreenLeftStickSensitivity
            ) { touchscreenLeftStickSensitivity },
            touchHaptics = pick("touchHaptics", touchHaptics) { touchHaptics },
            stickToggleTarget = pick("stickToggleTarget", stickToggleTarget) { stickToggleTarget },
            touchHapticsPreset = pick("touchHapticsPreset", touchHapticsPreset) { touchHapticsPreset },
            touchHapticsStrength = touchHapticsStrength,
            touchControlVisualStyle = profile.touchControlVisualStyle ?: touchControlVisualStyle,
            touchControlPressEffect = profile.touchControlPressEffect ?: touchControlPressEffect,
            gyroMode = pick("gyroMode", gyroMode) { gyroMode },
            gyroSensitivity = pick("gyroSensitivity", gyroSensitivity) { gyroSensitivity },
            gyroSmoothing = pick("gyroSmoothing", gyroSmoothing) { gyroSmoothing },
            gyroInvertX = pick("gyroInvertX", gyroInvertX) { gyroInvertX },
            gyroInvertY = pick("gyroInvertY", gyroInvertY) { gyroInvertY },
            gyroStickTarget = pick("gyroStickTarget", gyroStickTarget) {
                gyroStickTarget ?: this@applyProfile.gyroStickTarget
            },
            lightGunAim = pick("lightGunAim", lightGunAim) {
                lightGunAim ?: this@applyProfile.lightGunAim
            },
            lightGunCursorEnabled = pick("lightGunCursorEnabled", lightGunCursorEnabled) {
                lightGunCursorEnabled ?: this@applyProfile.lightGunCursorEnabled
            },
            gamepadRightStickUpToR2 = pick("gamepadRightStickUpToR2", gamepadRightStickUpToR2) { gamepadRightStickUpToR2 },
            gamepadRightStickDownToL2 = pick("gamepadRightStickDownToL2", gamepadRightStickDownToL2) { gamepadRightStickDownToL2 },
            gamepadButtonHaptics = pick("gamepadButtonHaptics", gamepadButtonHaptics) { gamepadButtonHaptics },
            gamepadStickDeadzone = pick("gamepadStickDeadzone", gamepadStickDeadzone) { gamepadStickDeadzone },
            gamepadLeftStickSensitivity = pick("gamepadLeftStickSensitivity", gamepadLeftStickSensitivity) { gamepadLeftStickSensitivity },
            gamepadRightStickSensitivity = pick("gamepadRightStickSensitivity", gamepadRightStickSensitivity) { gamepadRightStickSensitivity },
            gamepadLeftStickNegativeDeadzone = pick("gamepadLeftStickNegativeDeadzone", gamepadLeftStickNegativeDeadzone) { gamepadLeftStickNegativeDeadzone },
            gamepadRightStickNegativeDeadzone = pick("gamepadRightStickNegativeDeadzone", gamepadRightStickNegativeDeadzone) { gamepadRightStickNegativeDeadzone },
            gamepadLeftStickAntiDeadzone = pick("gamepadLeftStickAntiDeadzone", gamepadLeftStickAntiDeadzone) { gamepadLeftStickAntiDeadzone },
            gamepadRightStickAntiDeadzone = pick("gamepadRightStickAntiDeadzone", gamepadRightStickAntiDeadzone) { gamepadRightStickAntiDeadzone },
            gamepadLeftStickCurve = pick("gamepadLeftStickCurve", gamepadLeftStickCurve) { gamepadLeftStickCurve },
            gamepadRightStickCurve = pick("gamepadRightStickCurve", gamepadRightStickCurve) { gamepadRightStickCurve },
            gamepadBindingsByPad = if (profile.providedKeys == null || "gamepadBindingsByPad" in profile.providedKeys) profile.gamepadBindingsByPad else gamepadBindingsByPad,
            pressureModifierAmount = pick("pressureModifierAmount", pressureModifierAmount) { pressureModifierAmount },
            autoSaveOnExit = pick("autoSaveOnExit", autoSaveOnExit) { autoSaveOnExit },
            autoLoadOnStart = pick("autoLoadOnStart", autoLoadOnStart) { autoLoadOnStart },
            renderer = pick("renderer", renderer) { renderer },
            frameGenerationEnabled = pick("frameGenerationEnabled", frameGenerationEnabled) { frameGenerationEnabled },
            frameGenerationMultiplier = pick("frameGenerationMultiplier", frameGenerationMultiplier) { frameGenerationMultiplier },
            frameGenerationPerformance = pick("frameGenerationPerformance", frameGenerationPerformance) { frameGenerationPerformance },
            frameGenerationFlowScale = pick("frameGenerationFlowScale", frameGenerationFlowScale) { frameGenerationFlowScale },
            frameGenerationTargetRate = pick("frameGenerationTargetRate", frameGenerationTargetRate) { frameGenerationTargetRate },
            upscale = pick("upscaleMultiplier", upscale) { upscaleMultiplier },
            aspectRatio = pick("aspectRatio", aspectRatio) { aspectRatio },
            localMultiplayerMode = pick("localMultiplayerMode", localMultiplayerMode) { localMultiplayerMode },
            displayCrop = pick("displayCrop", displayCrop) { displayCrop },
            enableInstantVu1 = pick("enableInstantVu1", enableInstantVu1) { enableInstantVu1 },
            enableMtvu = pick("enableMtvu", enableMtvu) { enableMtvu },
            enableIopThread = pick("enableIopThread", enableIopThread) { enableIopThread },
            enableVUAot = pick("enableVUAot", enableVUAot) { enableVUAot },
            enableThreadPinning = pick("enableThreadPinning", enableThreadPinning) { enableThreadPinning },
            enableFastCdvd = pick("enableFastCdvd", enableFastCdvd) { enableFastCdvd },
            enableFastBoot = pick("enableFastBoot", enableFastBoot) { enableFastBoot },
            enableCheats = pick("enableCheats", enableCheats) { enableCheats },
            hwDownloadMode = pick("hwDownloadMode", hwDownloadMode) { hwDownloadMode },
            eeCycleRate = pick("eeCycleRate", eeCycleRate) { eeCycleRate },
            eeCycleSkip = pick("eeCycleSkip", eeCycleSkip) { eeCycleSkip },
            frameSkip = pick("frameSkip", frameSkip) { frameSkip },
            skipDuplicateFrames = pick("skipDuplicateFrames", skipDuplicateFrames) { skipDuplicateFrames },
            lowLatencyMode = pick("lowLatencyMode", lowLatencyMode) { lowLatencyMode },
            frameLimitEnabled = pick("frameLimitEnabled", frameLimitEnabled) { frameLimitEnabled },
            targetFps = pick("targetFps", targetFps) { targetFps },
            ntscFramerate = pick("ntscFramerate", ntscFramerate) { ntscFramerate },
            palFramerate = pick("palFramerate", palFramerate) { palFramerate },
            textureFiltering = pick("textureFiltering", textureFiltering) { textureFiltering },
            trilinearFiltering = pick("trilinearFiltering", trilinearFiltering) { trilinearFiltering },
            blendingAccuracy = pick("blendingAccuracy", blendingAccuracy) { blendingAccuracy },
            texturePreloading = pick("texturePreloading", texturePreloading) { texturePreloading },
            enableFxaa = pick("enableFxaa", enableFxaa) { enableFxaa },
            casMode = pick("casMode", casMode) { casMode },
            sgsrMode = pick("sgsrMode", sgsrMode) { sgsrMode },
            casSharpness = pick("casSharpness", casSharpness) { casSharpness },
            tvShader = pick("tvShader", tvShader) { tvShader },
            shadeBoostEnabled = pick("shadeBoostEnabled", shadeBoostEnabled) { shadeBoostEnabled },
            shadeBoostBrightness = pick("shadeBoostBrightness", shadeBoostBrightness) { shadeBoostBrightness },
            shadeBoostContrast = pick("shadeBoostContrast", shadeBoostContrast) { shadeBoostContrast },
            shadeBoostSaturation = pick("shadeBoostSaturation", shadeBoostSaturation) { shadeBoostSaturation },
            shadeBoostGamma = pick("shadeBoostGamma", shadeBoostGamma) { shadeBoostGamma },
            anisotropicFiltering = pick("anisotropicFiltering", anisotropicFiltering) { anisotropicFiltering },
            enableHwMipmapping = pick("enableHwMipmapping", enableHwMipmapping) { enableHwMipmapping },
            antiBlur = pick("antiBlur", antiBlur) { antiBlur },
            deinterlaceMode = pick("deinterlaceMode", deinterlaceMode) { deinterlaceMode },
            dithering = pick("dithering", dithering) { dithering },
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

    internal fun EmulationUiState.withLiveRuntimeSnapshot(snapshot: LiveRuntimeSnapshot): EmulationUiState = copy(
        showFps = snapshot.showFps,
        fpsOverlayMode = snapshot.fpsOverlayMode,
        confirmSaveLoadActions = snapshot.confirmSaveLoadActions,
        backButtonExitsGame = snapshot.backButtonExitsGame,
        renderer = snapshot.renderer,
        frameGenerationReady = snapshot.frameGenerationReady,
        frameGenerationEnabled = snapshot.frameGenerationEnabled,
        frameGenerationMultiplier = snapshot.frameGenerationMultiplier,
        frameGenerationPerformance = snapshot.frameGenerationPerformance,
        frameGenerationFlowScale = snapshot.frameGenerationFlowScale,
        frameGenerationTargetRate = snapshot.frameGenerationTargetRate,
        upscale = snapshot.upscale,
        aspectRatio = snapshot.aspectRatio,
        localMultiplayerMode = snapshot.localMultiplayerMode,
        displayCrop = snapshot.displayCrop,
        performancePreset = snapshot.performancePreset,
        enableInstantVu1 = snapshot.enableInstantVu1,
        enableMtvu = snapshot.enableMtvu,
        enableIopThread = snapshot.enableIopThread,
        enableVUAot = snapshot.enableVUAot,
        enableThreadPinning = snapshot.enableThreadPinning,
        enableFastCdvd = snapshot.enableFastCdvd,
        enableFastBoot = snapshot.enableFastBoot,
        enableCheats = snapshot.enableCheats,
        hwDownloadMode = snapshot.hwDownloadMode,
        hwDownloadModeUserOverride = snapshot.hwDownloadModeUserOverride,
        eeCycleRate = snapshot.eeCycleRate,
        eeCycleSkip = snapshot.eeCycleSkip,
        frameSkip = snapshot.frameSkip,
        skipDuplicateFrames = snapshot.skipDuplicateFrames,
        lowLatencyMode = snapshot.lowLatencyMode,
        textureFiltering = snapshot.textureFiltering,
        trilinearFiltering = snapshot.trilinearFiltering,
        blendingAccuracy = snapshot.blendingAccuracy,
        blendingAccuracyUserOverride = snapshot.blendingAccuracyUserOverride,
        texturePreloading = snapshot.texturePreloading,
        enableFxaa = snapshot.enableFxaa,
        casMode = snapshot.casMode,
        sgsrMode = snapshot.sgsrMode,
        casSharpness = snapshot.casSharpness,
        tvShader = snapshot.tvShader,
        shadeBoostEnabled = snapshot.shadeBoostEnabled,
        shadeBoostBrightness = snapshot.shadeBoostBrightness,
        shadeBoostContrast = snapshot.shadeBoostContrast,
        shadeBoostSaturation = snapshot.shadeBoostSaturation,
        shadeBoostGamma = snapshot.shadeBoostGamma,
        anisotropicFiltering = snapshot.anisotropicFiltering,
        enableHwMipmapping = snapshot.enableHwMipmapping,
        antiBlur = snapshot.antiBlur,
        deinterlaceMode = snapshot.deinterlaceMode,
        dithering = snapshot.dithering,
        widescreenPatches = snapshot.widescreenPatches,
        noInterlacingPatches = snapshot.noInterlacingPatches,
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
        frameLimitEnabled = snapshot.frameLimitEnabled,
        fastForwardSpeed = snapshot.fastForwardSpeed,
        racingMode = snapshot.racingMode,
        stickyButtons = snapshot.stickyButtons,
        touchscreenRightStick = snapshot.touchscreenRightStick,
        touchscreenRightStickSensitivity = snapshot.touchscreenRightStickSensitivity,
        touchscreenLeftStick = snapshot.touchscreenLeftStick,
        touchscreenLeftStickSensitivity = snapshot.touchscreenLeftStickSensitivity,
        touchHaptics = snapshot.touchHaptics,
        stickToggleTarget = snapshot.stickToggleTarget,
        touchHapticsPreset = snapshot.touchHapticsPreset,
        touchHapticsStrength = snapshot.touchHapticsStrength,
        touchControlVisualStyle = snapshot.touchControlVisualStyle,
        touchControlPressEffect = snapshot.touchControlPressEffect,
        gyroMode = snapshot.gyroMode,
        gyroSensitivity = snapshot.gyroSensitivity,
        gyroSmoothing = snapshot.gyroSmoothing,
        gyroInvertX = snapshot.gyroInvertX,
        gyroInvertY = snapshot.gyroInvertY,
        gyroStickTarget = snapshot.gyroStickTarget,
        lightGunAim = snapshot.lightGunAim,
        lightGunCursorEnabled = snapshot.lightGunCursorEnabled,
        gamepadRightStickUpToR2 = snapshot.gamepadRightStickUpToR2,
        gamepadRightStickDownToL2 = snapshot.gamepadRightStickDownToL2,
        gamepadButtonHaptics = snapshot.gamepadButtonHaptics,
        gamepadStickDeadzone = snapshot.gamepadStickDeadzone,
        gamepadLeftStickSensitivity = snapshot.gamepadLeftStickSensitivity,
        gamepadRightStickSensitivity = snapshot.gamepadRightStickSensitivity,
        gamepadLeftStickNegativeDeadzone = snapshot.gamepadLeftStickNegativeDeadzone,
        gamepadRightStickNegativeDeadzone = snapshot.gamepadRightStickNegativeDeadzone,
        gamepadLeftStickAntiDeadzone = snapshot.gamepadLeftStickAntiDeadzone,
        gamepadRightStickAntiDeadzone = snapshot.gamepadRightStickAntiDeadzone,
        gamepadLeftStickCurve = snapshot.gamepadLeftStickCurve,
        gamepadRightStickCurve = snapshot.gamepadRightStickCurve,
        gamepadBindingsByPad = snapshot.gamepadBindingsByPad,
        pressureModifierAmount = snapshot.pressureModifierAmount,
        autoSaveOnExit = snapshot.autoSaveOnExit,
        autoLoadOnStart = snapshot.autoLoadOnStart,
        targetFps = snapshot.targetFps,
        ntscFramerate = snapshot.ntscFramerate,
        palFramerate = snapshot.palFramerate
    )


    internal fun EmulationUiState.buildPerGameProfile(
        gameKey: String,
        gameTitle: String,
        gameSerial: String?,
        globalTouchControlVisualStyle: TouchControlVisualStyle,
        globalTouchControlPressEffect: TouchControlPressEffect
    ): PerGameSettings = PerGameSettings(
            gameKey = gameKey,
            gameTitle = gameTitle,
            gameSerial = gameSerial,
            renderer = renderer,
            frameGenerationEnabled = frameGenerationEnabled,
            frameGenerationMultiplier = frameGenerationMultiplier,
            frameGenerationPerformance = frameGenerationPerformance,
            frameGenerationFlowScale = frameGenerationFlowScale,
            frameGenerationTargetRate = frameGenerationTargetRate,
            upscaleMultiplier = upscale,
            aspectRatio = aspectRatio,
            localMultiplayerMode = localMultiplayerMode,
            displayCrop = displayCrop,
            showFps = showFps,
            fpsOverlayMode = fpsOverlayMode,
            enableInstantVu1 = enableInstantVu1,
            enableMtvu = enableMtvu,
            enableIopThread = enableIopThread,
            enableVUAot = enableVUAot,
            enableThreadPinning = enableThreadPinning,
            enableFastCdvd = enableFastCdvd,
            enableFastBoot = enableFastBoot,
            enableCheats = enableCheats,
            hwDownloadMode = hwDownloadMode,
            eeCycleRate = eeCycleRate,
            eeCycleSkip = eeCycleSkip,
            frameSkip = frameSkip,
            skipDuplicateFrames = skipDuplicateFrames,
            lowLatencyMode = lowLatencyMode,
            frameLimitEnabled = frameLimitEnabled,
            racingMode = racingMode,
            stickyButtons = stickyButtons,
            touchscreenRightStick = touchscreenRightStick,
            touchscreenRightStickSensitivity = touchscreenRightStickSensitivity,
            touchscreenLeftStick = touchscreenLeftStick,
            touchscreenLeftStickSensitivity = touchscreenLeftStickSensitivity,
            touchHaptics = touchHaptics,
            stickToggleTarget = stickToggleTarget,
            touchHapticsPreset = touchHapticsPreset,
            touchControlVisualStyle = touchControlVisualStyle.takeIf { it != globalTouchControlVisualStyle },
            touchControlPressEffect = touchControlPressEffect.takeIf { it != globalTouchControlPressEffect },
            gyroMode = gyroMode,
            gyroSensitivity = gyroSensitivity,
            gyroSmoothing = gyroSmoothing,
            gyroInvertX = gyroInvertX,
            gyroInvertY = gyroInvertY,
            gyroStickTarget = gyroStickTarget,
            lightGunAim = lightGunAim,
            lightGunCursorEnabled = lightGunCursorEnabled,
            gamepadRightStickUpToR2 = gamepadRightStickUpToR2,
            gamepadRightStickDownToL2 = gamepadRightStickDownToL2,
            gamepadButtonHaptics = gamepadButtonHaptics,
            gamepadStickDeadzone = gamepadStickDeadzone,
            gamepadLeftStickSensitivity = gamepadLeftStickSensitivity,
            gamepadRightStickSensitivity = gamepadRightStickSensitivity,
            gamepadLeftStickNegativeDeadzone = gamepadLeftStickNegativeDeadzone,
            gamepadRightStickNegativeDeadzone = gamepadRightStickNegativeDeadzone,
            gamepadLeftStickAntiDeadzone = gamepadLeftStickAntiDeadzone,
            gamepadRightStickAntiDeadzone = gamepadRightStickAntiDeadzone,
            gamepadLeftStickCurve = gamepadLeftStickCurve,
            gamepadRightStickCurve = gamepadRightStickCurve,
            gamepadBindingsByPad = gamepadBindingsByPad,
            pressureModifierAmount = pressureModifierAmount,
            autoSaveOnExit = autoSaveOnExit,
            autoLoadOnStart = autoLoadOnStart,
            targetFps = targetFps,
            ntscFramerate = ntscFramerate,
            palFramerate = palFramerate,
            textureFiltering = textureFiltering,
            trilinearFiltering = trilinearFiltering,
            blendingAccuracy = blendingAccuracy,
            texturePreloading = texturePreloading,
            enableFxaa = enableFxaa,
            casMode = casMode,
            sgsrMode = sgsrMode,
            casSharpness = casSharpness,
            tvShader = tvShader,
            shadeBoostEnabled = shadeBoostEnabled,
            shadeBoostBrightness = shadeBoostBrightness,
            shadeBoostContrast = shadeBoostContrast,
            shadeBoostSaturation = shadeBoostSaturation,
            shadeBoostGamma = shadeBoostGamma,
            anisotropicFiltering = anisotropicFiltering,
            enableHwMipmapping = enableHwMipmapping,
            antiBlur = antiBlur,
            deinterlaceMode = deinterlaceMode,
            dithering = dithering,
            enableWidescreenPatches = widescreenPatches,
            enableNoInterlacingPatches = noInterlacingPatches,
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
    /**
     * Whether the per-game profile should carry its gyro mode as a provided key. Light Gun is
     * always provided: the gun controls activate from the profile's effective gyro mode, and a
     * value-diff alone drops the choice whenever it equals the global setting.
     */
    internal fun perGameGyroModeIsProvided(gyroMode: Int, globalGyroMode: Int): Boolean =
        gyroMode == AppPreferences.GYRO_MODE_LIGHT_GUN || gyroMode != globalGyroMode

    internal suspend fun EmulationUiState.buildPerGameProvidedKeys(
        profile: PerGameSettings,
        preferences: AppPreferences,
        frameGenerationManager: FrameGenerationManager
    ): Set<String> {
        val settings = preferences.settingsSnapshot.first()
        val globalFrameGeneration = frameGenerationManager.snapshot().settings
        return buildSet {
            if (renderer != settings.renderer) add("renderer")
            if (frameGenerationEnabled != globalFrameGeneration.enabled) add("frameGenerationEnabled")
            if (frameGenerationMultiplier != globalFrameGeneration.multiplier) add("frameGenerationMultiplier")
            if (frameGenerationPerformance != globalFrameGeneration.performanceMode) add("frameGenerationPerformance")
            if (frameGenerationFlowScale != globalFrameGeneration.flowScalePercent) add("frameGenerationFlowScale")
            if (frameGenerationTargetRate != globalFrameGeneration.targetRefreshRate) add("frameGenerationTargetRate")
            if (upscale != settings.upscaleMultiplier) add("upscaleMultiplier")
            if (aspectRatio != settings.aspectRatio) add("aspectRatio")
            if (localMultiplayerMode != settings.localMultiplayerMode) add("localMultiplayerMode")
            if (displayCrop != settings.displayCrop) add("displayCrop")
            if (showFps != settings.showFps) add("showFps")
            if (fpsOverlayMode != settings.fpsOverlayMode) add("fpsOverlayMode")
            if (enableInstantVu1 != settings.enableInstantVu1) add("enableInstantVu1")
            if (enableMtvu != settings.enableMtvu) add("enableMtvu")
            if (enableIopThread != settings.enableIopThread) add("enableIopThread")
            if (enableVUAot != settings.enableVUAot) add("enableVUAot")
            if (enableThreadPinning != settings.enableThreadPinning) add("enableThreadPinning")
            if (enableFastCdvd != settings.enableFastCdvd) add("enableFastCdvd")
            if (enableFastBoot != settings.enableFastBoot) add("enableFastBoot")
            if (enableCheats != settings.enableCheats) add("enableCheats")
            if (hwDownloadMode != settings.hwDownloadMode) add("hwDownloadMode")
            if (eeCycleRate != settings.eeCycleRate) add("eeCycleRate")
            if (eeCycleSkip != settings.eeCycleSkip) add("eeCycleSkip")
            if (profile.frameSkip != settings.frameSkip) add("frameSkip")
            if (skipDuplicateFrames != settings.skipDuplicateFrames) add("skipDuplicateFrames")
            if (lowLatencyMode != settings.lowLatencyMode) add("lowLatencyMode")
            if (frameLimitEnabled != settings.frameLimitEnabled) add("frameLimitEnabled")
            if (racingMode != settings.racingMode) add("racingMode")
            if (stickyButtons != settings.stickyButtons) add("stickyButtons")
            if (touchscreenRightStick != settings.touchscreenRightStick) add("touchscreenRightStick")
            if (touchscreenRightStickSensitivity != settings.touchscreenRightStickSensitivity) {
                add("touchscreenRightStickSensitivity")
            }
            if (touchscreenLeftStick != settings.touchscreenLeftStick) add("touchscreenLeftStick")
            if (touchscreenLeftStickSensitivity != settings.touchscreenLeftStickSensitivity) {
                add("touchscreenLeftStickSensitivity")
            }
            if (touchHaptics != settings.touchHaptics) add("touchHaptics")
            if (stickToggleTarget != settings.stickToggleTarget) add("stickToggleTarget")
            if (touchHapticsPreset != settings.touchHapticsPreset) add("touchHapticsPreset")
            if (profile.touchControlVisualStyle != null) add("touchControlVisualStyle")
            if (profile.touchControlPressEffect != null) add("touchControlPressEffect")
            if (perGameGyroModeIsProvided(gyroMode, settings.gyroMode)) add("gyroMode")
            if (gyroSensitivity != settings.gyroSensitivity) add("gyroSensitivity")
            if (gyroSmoothing != settings.gyroSmoothing) add("gyroSmoothing")
            if (gyroInvertX != settings.gyroInvertX) add("gyroInvertX")
            if (gyroInvertY != settings.gyroInvertY) add("gyroInvertY")
            if (gyroStickTarget != settings.gyroStickTarget) add("gyroStickTarget")
            if (lightGunAim != settings.lightGunAim) add("lightGunAim")
            if (lightGunCursorEnabled != settings.lightGunCursorEnabled) add("lightGunCursorEnabled")
            if (gamepadRightStickUpToR2 != settings.gamepadRightStickUpToR2) add("gamepadRightStickUpToR2")
            if (gamepadRightStickDownToL2 != settings.gamepadRightStickDownToL2) add("gamepadRightStickDownToL2")
            if (gamepadButtonHaptics != settings.gamepadButtonHaptics) add("gamepadButtonHaptics")
            if (gamepadStickDeadzone != settings.gamepadStickDeadzone) add("gamepadStickDeadzone")
            if (gamepadLeftStickSensitivity != settings.gamepadLeftStickSensitivity) add("gamepadLeftStickSensitivity")
            if (gamepadRightStickSensitivity != settings.gamepadRightStickSensitivity) add("gamepadRightStickSensitivity")
            if (gamepadLeftStickNegativeDeadzone != settings.gamepadLeftStickNegativeDeadzone) add("gamepadLeftStickNegativeDeadzone")
            if (gamepadRightStickNegativeDeadzone != settings.gamepadRightStickNegativeDeadzone) add("gamepadRightStickNegativeDeadzone")
            if (gamepadLeftStickAntiDeadzone != settings.gamepadLeftStickAntiDeadzone) add("gamepadLeftStickAntiDeadzone")
            if (gamepadRightStickAntiDeadzone != settings.gamepadRightStickAntiDeadzone) add("gamepadRightStickAntiDeadzone")
            if (gamepadLeftStickCurve != settings.gamepadLeftStickCurve) add("gamepadLeftStickCurve")
            if (gamepadRightStickCurve != settings.gamepadRightStickCurve) add("gamepadRightStickCurve")
            if (gamepadBindingsByPad.isNotEmpty()) add("gamepadBindingsByPad")
            if (pressureModifierAmount != settings.pressureModifierAmount) add("pressureModifierAmount")
            if (autoSaveOnExit) add("autoSaveOnExit")
            if (autoLoadOnStart) add("autoLoadOnStart")
            if (targetFps != settings.targetFps) add("targetFps")
            if (ntscFramerate != settings.ntscFramerate) add("ntscFramerate")
            if (palFramerate != settings.palFramerate) add("palFramerate")
            if (textureFiltering != settings.textureFiltering) add("textureFiltering")
            if (trilinearFiltering != settings.trilinearFiltering) add("trilinearFiltering")
            if (blendingAccuracy != settings.blendingAccuracy) add("blendingAccuracy")
            if (texturePreloading != settings.texturePreloading) add("texturePreloading")
            if (enableFxaa != settings.enableFxaa) add("enableFxaa")
            if (casMode != settings.casMode) add("casMode")
            if (sgsrMode != settings.sgsrMode) add("sgsrMode")
            if (casSharpness != settings.casSharpness) add("casSharpness")
            if (tvShader != settings.tvShader) add("tvShader")
            if (shadeBoostEnabled != settings.shadeBoostEnabled) add("shadeBoostEnabled")
            if (shadeBoostBrightness != settings.shadeBoostBrightness) add("shadeBoostBrightness")
            if (shadeBoostContrast != settings.shadeBoostContrast) add("shadeBoostContrast")
            if (shadeBoostSaturation != settings.shadeBoostSaturation) add("shadeBoostSaturation")
            if (shadeBoostGamma != settings.shadeBoostGamma) add("shadeBoostGamma")
            if (anisotropicFiltering != settings.anisotropicFiltering) add("anisotropicFiltering")
            if (enableHwMipmapping != settings.enableHwMipmapping) add("enableHwMipmapping")
            if (antiBlur != settings.antiBlur) add("antiBlur")
            if (deinterlaceMode != settings.deinterlaceMode) add("deinterlaceMode")
            if (dithering != settings.dithering) add("dithering")
            if (profile.enableWidescreenPatches != settings.enableWidescreenPatches) add("enableWidescreenPatches")
            if (profile.enableNoInterlacingPatches != settings.enableNoInterlacingPatches) add("enableNoInterlacingPatches")
            if (cpuSpriteRenderSize != settings.cpuSpriteRenderSize) add("cpuSpriteRenderSize")
            if (cpuSpriteRenderLevel != settings.cpuSpriteRenderLevel) add("cpuSpriteRenderLevel")
            if (softwareClutRender != settings.softwareClutRender) add("softwareClutRender")
            if (gpuTargetClutMode != settings.gpuTargetClutMode) add("gpuTargetClutMode")
            if (skipDrawStart != settings.skipDrawStart) add("skipDrawStart")
            if (skipDrawEnd != settings.skipDrawEnd) add("skipDrawEnd")
            if (autoFlushHardware != settings.autoFlushHardware) add("autoFlushHardware")
            if (cpuFramebufferConversion != settings.cpuFramebufferConversion) add("cpuFramebufferConversion")
            if (disableDepthConversion != settings.disableDepthConversion) add("disableDepthConversion")
            if (disableSafeFeatures != settings.disableSafeFeatures) add("disableSafeFeatures")
            if (disableRenderFixes != settings.disableRenderFixes) add("disableRenderFixes")
            if (preloadFrameData != settings.preloadFrameData) add("preloadFrameData")
            if (disablePartialInvalidation != settings.disablePartialInvalidation) add("disablePartialInvalidation")
            if (textureInsideRt != settings.textureInsideRt) add("textureInsideRt")
            if (readTargetsOnClose != settings.readTargetsOnClose) add("readTargetsOnClose")
            if (estimateTextureRegion != settings.estimateTextureRegion) add("estimateTextureRegion")
            if (gpuPaletteConversion != settings.gpuPaletteConversion) add("gpuPaletteConversion")
            if (halfPixelOffset != settings.halfPixelOffset) add("halfPixelOffset")
            if (nativeScaling != settings.nativeScaling) add("nativeScaling")
            if (roundSprite != settings.roundSprite) add("roundSprite")
            if (bilinearUpscale != settings.bilinearUpscale) add("bilinearUpscale")
            if (textureOffsetX != settings.textureOffsetX) add("textureOffsetX")
            if (textureOffsetY != settings.textureOffsetY) add("textureOffsetY")
            if (alignSprite != settings.alignSprite) add("alignSprite")
            if (mergeSprite != settings.mergeSprite) add("mergeSprite")
            if (forceEvenSpritePosition != settings.forceEvenSpritePosition) add("forceEvenSpritePosition")
            if (nativePaletteDraw != settings.nativePaletteDraw) add("nativePaletteDraw")
        }
    }

    internal suspend fun EmulationUiState.toPerGameSettings(
        gameKey: String,
        gameTitle: String,
        gameSerial: String?,
        preferences: AppPreferences,
        frameGenerationManager: FrameGenerationManager
    ): PerGameSettings {
        val settings = preferences.settingsSnapshot.first()
        val globalTouchControlVisualStyle = settings.touchControlVisualStyle
        val globalTouchControlPressEffect = settings.touchControlPressEffect
        val profile = buildPerGameProfile(
            gameKey = gameKey,
            gameTitle = gameTitle,
            gameSerial = gameSerial,
            globalTouchControlVisualStyle = globalTouchControlVisualStyle,
            globalTouchControlPressEffect = globalTouchControlPressEffect
        )


        return profile.copy(providedKeys = buildPerGameProvidedKeys(profile, preferences, frameGenerationManager))
    }


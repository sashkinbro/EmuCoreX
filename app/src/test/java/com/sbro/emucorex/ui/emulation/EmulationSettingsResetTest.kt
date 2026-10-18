package com.sbro.emucorex.ui.emulation

import com.sbro.emucorex.data.DisplayCrop
import com.sbro.emucorex.data.PerGameInterfaceSettings
import com.sbro.emucorex.data.TouchControlPressEffect
import com.sbro.emucorex.data.TouchControlVisualStyle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class EmulationSettingsResetTest {
    @Test fun resetRestoresEveryRuntimeSettingFromGlobalSnapshot() {
        val snapshot = globalRuntimeSnapshot()
        val state = EmulationUiState(
            isRunning = true,
            isPaused = true,
            activePlayTimeMs = 1234L,
            showFps = true,
            autoSaveOnExit = true,
            autoLoadOnStart = true,
            renderer = 1,
            controlsVisible = false
        ).withLiveRuntimeSnapshot(snapshot)

        for (getter in LiveRuntimeSnapshot::class.java.methods) {
            if (getter.parameterCount != 0 || !getter.name.startsWith("get")) continue
            if (getter.name == "getClass") continue
            val stateGetter = runCatching {
                EmulationUiState::class.java.getMethod(getter.name)
            }.getOrNull()
            assertNotNull("No EmulationUiState property for ${getter.name}", stateGetter)
            assertEquals(
                getter.name,
                getter.invoke(snapshot),
                stateGetter!!.invoke(state)
            )
        }

        assertFalse(state.showFps)
        assertTrue(state.autoSaveOnExit)
        assertTrue(state.autoLoadOnStart)
        assertEquals(60, state.targetFps)
        assertTrue(state.isRunning)
        assertTrue(state.isPaused)
        assertEquals(1234L, state.activePlayTimeMs)
        assertFalse(state.controlsVisible)
    }

    @Test fun resetRestoresEveryInterfaceSettingFromGlobalSnapshot() {
        val settings = PerGameInterfaceSettings(
            overlayShow = false,
            overlayScale = 125,
            overlayOpacity = 25,
            hideOverlayOnGamepad = false,
            compactControls = false,
            keepScreenOn = false,
            leftStickSensitivity = 150,
            rightStickSensitivity = 120,
            invertLeftStick = true,
            invertRightStick = true,
            invertLeftStickHorizontal = true,
            invertRightStickHorizontal = true,
            fpsOverlayCorner = 2,
            fpsOverlayScale = 175,
            fpsOverlayMetrics = 0,
            autoSaveEnabled = true,
            autoSaveIntervalMinutes = 5
        )

        val state = EmulationUiState(
            controlsVisible = true,
            autoSaveEnabled = false,
            autoSaveIntervalMinutes = 1
        ).withInterfaceSettings(settings)

        assertEquals(settings, state.toInterfaceSettings())
        assertFalse(state.controlsVisible)
        assertTrue(state.autoSaveEnabled)
        assertEquals(5, state.autoSaveIntervalMinutes)
    }

    @Test fun gameProfileInheritsGlobalDefaultsWhenOwnValuesAreAbsent() {
        val global = PerGameInterfaceSettings(
            overlayShow = false,
            keepScreenOn = false,
            fpsOverlayMetrics = 0,
            autoSaveEnabled = true,
            autoSaveIntervalMinutes = 10
        )
        val inherited = PerGameInterfaceSettings().resolveAgainst(global)
        assertEquals(global, inherited)

        val perGame = PerGameInterfaceSettings(
            overlayScale = 125,
            autoSaveEnabled = false,
            providedKeys = setOf("overlayScale", "autoSaveEnabled")
        ).resolveAgainst(global)
        assertFalse(perGame.overlayShow)
        assertEquals(125, perGame.overlayScale)
        assertEquals(10, perGame.autoSaveIntervalMinutes)
        assertFalse(perGame.autoSaveEnabled)
    }

    private fun globalRuntimeSnapshot(): LiveRuntimeSnapshot = LiveRuntimeSnapshot(
        showFps = false,
        fpsOverlayMode = 0,
        confirmSaveLoadActions = false,
        backButtonExitsGame = true,
        renderer = 13,
        frameGenerationReady = true,
        frameGenerationEnabled = true,
        frameGenerationMultiplier = 4,
        frameGenerationPerformance = false,
        frameGenerationFlowScale = 75,
        frameGenerationTargetRate = 120,
        upscale = 2.5f,
        aspectRatio = 3,
        localMultiplayerMode = 4,
        displayCrop = DisplayCrop.SafeEdges,
        performancePreset = 99,
        enableInstantVu1 = false,
        enableMtvu = false,
        enableThreadPinning = true,
        enableFastCdvd = true,
        enableFastBoot = false,
        enableCheats = true,
        hwDownloadMode = 5,
        hwDownloadModeUserOverride = true,
        eeCycleRate = -2,
        eeCycleSkip = 3,
        frameSkip = 4,
        skipDuplicateFrames = false,
        lowLatencyMode = true,
        frameLimitEnabled = false,
        fastForwardSpeed = 3.5f,
        racingMode = true,
        stickyButtons = setOf("triangle"),
        touchscreenRightStick = false,
        touchscreenRightStickSensitivity = 150,
        touchHaptics = true,
        stickToggleTarget = 1,
        touchHapticsPreset = 3,
        touchHapticsStrength = 50,
        touchControlVisualStyle = TouchControlVisualStyle.MODERN,
        touchControlPressEffect = TouchControlPressEffect.GLOW,
        gyroMode = 1,
        gyroSensitivity = 150,
        gyroSmoothing = 30,
        gyroInvertX = true,
        gyroInvertY = true,
        gyroStickTarget = 0,
        lightGunAim = 1,
        lightGunCursorEnabled = false,
        gamepadRightStickUpToR2 = true,
        gamepadRightStickDownToL2 = true,
        gamepadButtonHaptics = true,
        gamepadStickDeadzone = 17,
        gamepadLeftStickSensitivity = 130,
        gamepadRightStickSensitivity = 140,
        gamepadLeftStickNegativeDeadzone = 7,
        gamepadRightStickNegativeDeadzone = 8,
        gamepadLeftStickAntiDeadzone = 9,
        gamepadRightStickAntiDeadzone = 10,
        gamepadLeftStickCurve = 120,
        gamepadRightStickCurve = 110,
        gamepadBindingsByPad = mapOf(1 to mapOf("cross" to 100)),
        pressureModifierAmount = 42,
        autoSaveOnExit = true,
        autoLoadOnStart = true,
        targetFps = 60,
        ntscFramerate = 59.94f,
        palFramerate = 50f,
        textureFiltering = 2,
        trilinearFiltering = 3,
        blendingAccuracy = 4,
        blendingAccuracyUserOverride = true,
        texturePreloading = 2,
        enableFxaa = true,
        casMode = 1,
        sgsrMode = 2,
        casSharpness = 75,
        tvShader = 5,
        shadeBoostEnabled = true,
        shadeBoostBrightness = 60,
        shadeBoostContrast = 61,
        shadeBoostSaturation = 62,
        shadeBoostGamma = 63,
        anisotropicFiltering = 8,
        enableHwMipmapping = false,
        antiBlur = true,
        deinterlaceMode = 3,
        dithering = 2,
        widescreenPatches = true,
        noInterlacingPatches = true,
        cpuSpriteRenderSize = 2,
        cpuSpriteRenderLevel = 3,
        softwareClutRender = 1,
        gpuTargetClutMode = 2,
        skipDrawStart = 1,
        skipDrawEnd = 2,
        autoFlushHardware = 1,
        cpuFramebufferConversion = true,
        disableDepthConversion = true,
        disableSafeFeatures = true,
        disableRenderFixes = true,
        preloadFrameData = true,
        disablePartialInvalidation = true,
        textureInsideRt = 2,
        readTargetsOnClose = true,
        estimateTextureRegion = true,
        gpuPaletteConversion = true,
        halfPixelOffset = 1,
        nativeScaling = 2,
        roundSprite = 1,
        bilinearUpscale = 2,
        textureOffsetX = 5,
        textureOffsetY = 6,
        alignSprite = true,
        mergeSprite = true,
        forceEvenSpritePosition = true,
        nativePaletteDraw = true
    )
}

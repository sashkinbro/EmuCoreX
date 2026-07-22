package com.sbro.emucorex

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import com.sbro.emucorex.core.AppLocaleManager
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.core.PlayInAppReviewManager
import com.sbro.emucorex.core.LocalTvUiEnvironment
import com.sbro.emucorex.core.TvInterfaceMode
import com.sbro.emucorex.core.TvUiPolicy
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.AppFontChoice
import com.sbro.emucorex.data.CustomFontRepository
import com.sbro.emucorex.navigation.AppNavigation
import com.sbro.emucorex.ui.common.GamepadUiInputRouter
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.time.Duration.Companion.milliseconds

private const val TAG = "MainActivity"
private const val LIGHT_NAVIGATION_BAR_SCRIM = 0x04000000
private const val DARK_NAVIGATION_BAR_SCRIM = 0x0A000000
private const val IN_APP_REVIEW_HOME_SETTLE_DELAY_MS = 750L

open class MainActivity : ComponentActivity() {
    protected open val launchedFromTv: Boolean = false
    private var appliedLanguageTag: String? = null
    @Volatile
    private var keepSplashVisible = true
    private var launchIntentVersion by mutableIntStateOf(0)
    private var restoredFromSavedState = false
    private var reviewRequestInFlight = false
    private var reviewRetryAfterResumeScheduled = false

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(AppLocaleManager.wrap(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        val preferences = AppPreferences(this)
        if (preferences.getProUnlockedSync()) {
            setTheme(R.style.Theme_EmuCoreX_Splash_Pro)
        }
        installSplashScreen().setKeepOnScreenCondition { keepSplashVisible }
        applyEdgeToEdge()
        super.onCreate(savedInstanceState)
        restoredFromSavedState = savedInstanceState != null

        GamepadManager.ensureInitialized(this)
        appliedLanguageTag = preferences.getStoredLanguageTagSync()

        lifecycleScope.launch {
            preferences.languageTag
                .drop(1)
                .distinctUntilChanged()
                .collect { languageTag ->
                    if (languageTag != appliedLanguageTag) {
                        appliedLanguageTag = languageTag
                        recreate()
                    }
                }
        }

        setContent {
            val customFontRepository = remember { CustomFontRepository(applicationContext) }
            val themeMode by preferences.themeMode.collectAsState(initial = ThemeMode.SYSTEM)
            val fontChoice by preferences.appFontChoice.collectAsState(initial = AppFontChoice.SYSTEM)
            val appFontScale by preferences.appFontScale.collectAsState(initial = 1f)
            val customFontRevision by preferences.customFontRevision.collectAsState(initial = 0)
            val tvInterfaceMode by preferences.tvInterfaceMode.collectAsState(initial = TvInterfaceMode.AUTO)
            val tvUiEnvironment = remember(tvInterfaceMode, launchedFromTv) {
                TvUiPolicy.resolve(
                    context = applicationContext,
                    mode = tvInterfaceMode,
                    launchedFromTv = launchedFromTv
                )
            }
            val systemDarkTheme = isSystemInDarkTheme()
            val darkTheme = when (themeMode) {
                ThemeMode.SYSTEM -> systemDarkTheme
                ThemeMode.LIGHT -> false
                ThemeMode.DARK -> true
                ThemeMode.PRO -> true
            }

            SideEffect {
                applySystemBarTheme(darkTheme)
            }

            CompositionLocalProvider(LocalTvUiEnvironment provides tvUiEnvironment) {
                EmuCoreXTheme(
                    themeMode = themeMode,
                    fontChoice = fontChoice,
                    fontScale = appFontScale,
                    customFontFile = customFontRepository.installedFile(),
                    customFontRevision = customFontRevision
                ) {
                    AppNavigation(
                        launchIntentVersion = launchIntentVersion,
                        restoredFromSavedState = restoredFromSavedState,
                        onStartupReady = {
                            keepSplashVisible = false
                        },
                        onEmulationSessionCompleted = { activePlayTimeMs ->
                            recordCompletedEmulationSession(preferences, activePlayTimeMs)
                        }
                    )
                }
            }
        }
    }

    private fun recordCompletedEmulationSession(
        preferences: AppPreferences,
        activePlayTimeMs: Long
    ) {
        val app = application as EmuCoreXApp
        app.applicationScope.launch {
            preferences.recordInAppReviewSession(activePlayTimeMs)
            withContext(Dispatchers.Main.immediate) {
                if (lifecycle.currentState == Lifecycle.State.DESTROYED) return@withContext
                lifecycleScope.launch {
                    delay(IN_APP_REVIEW_HOME_SETTLE_DELAY_MS.milliseconds)
                    awaitResumedAndAttemptInAppReview(preferences)
                }
            }
        }
    }

    private suspend fun awaitResumedAndAttemptInAppReview(preferences: AppPreferences) {
        val state = lifecycle.currentStateFlow.first { currentState ->
            currentState == Lifecycle.State.DESTROYED ||
                currentState.isAtLeast(Lifecycle.State.RESUMED)
        }
        if (state == Lifecycle.State.DESTROYED) return
        attemptInAppReview(preferences)
    }

    private suspend fun attemptInAppReview(preferences: AppPreferences) {
        if (reviewRequestInFlight) return
        if (!lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) return
        if (!PlayInAppReviewManager.canRequest(this)) return

        reviewRequestInFlight = true
        val claimedAtMs = preferences.claimInAppReviewAttempt()
        if (claimedAtMs == null) {
            reviewRequestInFlight = false
            return
        }
        if (!lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
            reviewRequestInFlight = false
            releaseReviewAttemptAndRetryWhenResumed(preferences, claimedAtMs)
            return
        }

        PlayInAppReviewManager.request(this) { result ->
            reviewRequestInFlight = false
            when (result) {
                PlayInAppReviewManager.Result.COMPLETED -> {
                    (application as EmuCoreXApp).applicationScope.launch {
                        preferences.markInAppReviewRequested(claimedAtMs)
                    }
                }
                PlayInAppReviewManager.Result.ACTIVITY_NOT_READY -> {
                    releaseReviewAttemptAndRetryWhenResumed(preferences, claimedAtMs)
                }
                PlayInAppReviewManager.Result.RETRYABLE_FAILURE -> {
                    Log.w(TAG, "In-app review failed; retry remains available after cooldown")
                }
            }
        }
    }

    private fun releaseReviewAttemptAndRetryWhenResumed(
        preferences: AppPreferences,
        claimedAtMs: Long
    ) {
        val app = application as EmuCoreXApp
        app.applicationScope.launch {
            preferences.releaseInAppReviewAttempt(claimedAtMs)
            withContext(Dispatchers.Main.immediate) {
                scheduleReviewRetryAfterResume(preferences)
            }
        }
    }

    private fun scheduleReviewRetryAfterResume(preferences: AppPreferences) {
        if (reviewRetryAfterResumeScheduled || lifecycle.currentState == Lifecycle.State.DESTROYED) return
        reviewRetryAfterResumeScheduled = true
        lifecycleScope.launch {
            try {
                val state = lifecycle.currentStateFlow.first { currentState ->
                    currentState == Lifecycle.State.DESTROYED ||
                        currentState.isAtLeast(Lifecycle.State.RESUMED)
                }
                if (state == Lifecycle.State.DESTROYED) return@launch
                delay(IN_APP_REVIEW_HOME_SETTLE_DELAY_MS.milliseconds)
                attemptInAppReview(preferences)
            } finally {
                reviewRetryAfterResumeScheduled = false
            }
        }
    }

    private fun applySystemBarTheme(darkTheme: Boolean) {
        applyEdgeToEdge()

        val controller = WindowCompat.getInsetsController(window, window.decorView)
        val useDarkIcons = !darkTheme
        controller.isAppearanceLightStatusBars = useDarkIcons
        controller.isAppearanceLightNavigationBars = useDarkIcons
    }

    private fun applyEdgeToEdge() {
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(
                Color.TRANSPARENT,
                Color.TRANSPARENT
            ),
            navigationBarStyle = SystemBarStyle.auto(
                LIGHT_NAVIGATION_BAR_SCRIM,
                DARK_NAVIGATION_BAR_SCRIM
            )
        )
    }

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        launchIntentVersion++
    }

    @SuppressLint("RestrictedApi", "GestureBackNavigation")
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (GamepadManager.isGameController(event.device)) {
            if (GamepadManager.handleBindingCapture(event)) return true
            if (GamepadManager.isEmulationInputEnabled()) {
                if (GamepadUiInputRouter.handleEmulationOverlayKeyEvent(event)) return true
                if (GamepadManager.handleKeyEvent(event)) return true
            } else {
                if (GamepadUiInputRouter.handleKeyEvent(event)) return true
                if (GamepadUiInputRouter.shouldMapToPrimaryClick(event)) {
                    return super.dispatchKeyEvent(event.withKeyCode(KeyEvent.KEYCODE_DPAD_CENTER))
                }
            }
        }

        if (GamepadManager.isEmulationInputEnabled() && shouldRouteHostKeyEvent(event)) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> NativeApp.onHostKeyEvent(event.keyCode, true)
                KeyEvent.ACTION_UP -> NativeApp.onHostKeyEvent(event.keyCode, false)
            }
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    override fun onGenericMotionEvent(event: MotionEvent?): Boolean {
        if (event != null && GamepadManager.isGameController(event.device) && !GamepadManager.isEmulationInputEnabled()) {
            if (GamepadUiInputRouter.handleMotionEvent(event)) return true
        }
        if (event != null && GamepadManager.handleMotionEvent(event)) return true
        if (event != null && GamepadManager.isEmulationInputEnabled() && handleMouseMotionEvent(event)) return true
        return super.onGenericMotionEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (GamepadManager.isEmulationInputEnabled() && handleMouseMotionEvent(event)) return true
        return super.dispatchGenericMotionEvent(event)
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (GamepadManager.isEmulationInputEnabled() && handleMouseTouchEvent(event)) return true
        return super.dispatchTouchEvent(event)
    }

    @SuppressLint("GestureBackNavigation")
    private fun shouldRouteHostKeyEvent(event: KeyEvent): Boolean {
        if (isHostPadKeyCode(event.keyCode)) return true

        val device = event.device ?: return false
        if (!device.supportsSource(android.view.InputDevice.SOURCE_KEYBOARD)) return false
        return when (event.keyCode) {
            KeyEvent.KEYCODE_BACK,
            KeyEvent.KEYCODE_HOME,
            KeyEvent.KEYCODE_APP_SWITCH,
            KeyEvent.KEYCODE_VOLUME_UP,
            KeyEvent.KEYCODE_VOLUME_DOWN,
            KeyEvent.KEYCODE_VOLUME_MUTE,
            KeyEvent.KEYCODE_POWER -> false
            else -> true
        }
    }

    private fun isHostPadKeyCode(keyCode: Int): Boolean {
        return when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP,
            KeyEvent.KEYCODE_DPAD_RIGHT,
            KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_BUTTON_Y,
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_BUTTON_X,
            KeyEvent.KEYCODE_BUTTON_SELECT,
            KeyEvent.KEYCODE_BUTTON_START,
            KeyEvent.KEYCODE_BUTTON_L1,
            KeyEvent.KEYCODE_BUTTON_L2,
            KeyEvent.KEYCODE_BUTTON_R1,
            KeyEvent.KEYCODE_BUTTON_R2,
            KeyEvent.KEYCODE_BUTTON_THUMBL,
            KeyEvent.KEYCODE_BUTTON_THUMBR -> true
            else -> false
        }
    }

    private fun handleMouseTouchEvent(event: MotionEvent): Boolean {
        if (!isMouseEvent(event)) return false

        NativeApp.onHostMousePosition(event.x, event.y)

        return when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_BUTTON_PRESS -> {
                dispatchPressedMouseButtons(event.buttonState, true)
                true
            }
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_BUTTON_RELEASE,
            MotionEvent.ACTION_CANCEL -> {
                dispatchPressedMouseButtons(event.buttonState, false)
                true
            }
            MotionEvent.ACTION_MOVE,
            MotionEvent.ACTION_HOVER_MOVE -> true
            else -> false
        }
    }

    private fun handleMouseMotionEvent(event: MotionEvent): Boolean {
        if (!isMouseEvent(event)) return false

        NativeApp.onHostMousePosition(event.x, event.y)

        if (event.actionMasked == MotionEvent.ACTION_SCROLL) {
            NativeApp.onHostMouseWheel(
                event.getAxisValue(MotionEvent.AXIS_HSCROLL),
                event.getAxisValue(MotionEvent.AXIS_VSCROLL)
            )
            return true
        }

        if (event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS || event.actionMasked == MotionEvent.ACTION_BUTTON_RELEASE) {
            dispatchPressedMouseButtons(event.buttonState, event.actionMasked == MotionEvent.ACTION_BUTTON_PRESS)
            return true
        }

        return event.actionMasked == MotionEvent.ACTION_HOVER_MOVE || event.actionMasked == MotionEvent.ACTION_MOVE
    }

    private fun dispatchPressedMouseButtons(buttonState: Int, pressed: Boolean) {
        if ((buttonState and MotionEvent.BUTTON_PRIMARY) != 0) {
            NativeApp.onHostMouseButton(MotionEvent.BUTTON_PRIMARY, pressed)
        }
        if ((buttonState and MotionEvent.BUTTON_SECONDARY) != 0) {
            NativeApp.onHostMouseButton(MotionEvent.BUTTON_SECONDARY, pressed)
        }
        if ((buttonState and MotionEvent.BUTTON_TERTIARY) != 0) {
            NativeApp.onHostMouseButton(MotionEvent.BUTTON_TERTIARY, pressed)
        }
    }

    private fun isMouseEvent(event: MotionEvent): Boolean {
        val source = event.source
        return (source and android.view.InputDevice.SOURCE_MOUSE) == android.view.InputDevice.SOURCE_MOUSE
    }

    private fun KeyEvent.withKeyCode(keyCode: Int): KeyEvent {
        return KeyEvent(
            downTime,
            eventTime,
            action,
            keyCode,
            repeatCount,
            metaState,
            deviceId,
            scanCode,
            flags,
            source
        )
    }
}

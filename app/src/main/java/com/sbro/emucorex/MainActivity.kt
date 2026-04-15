package com.sbro.emucorex

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.os.Bundle
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
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.lifecycleScope
import com.sbro.emucorex.core.AppLocaleManager
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.NativeApp
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.navigation.AppNavigation
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private var appliedLanguageTag: String? = null
    @Volatile
    private var keepSplashVisible = true
    private var launchIntentVersion by mutableIntStateOf(0)
    private var restoredFromSavedState = false

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(AppLocaleManager.wrap(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen().setKeepOnScreenCondition { keepSplashVisible }
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(
                Color.TRANSPARENT,
                Color.TRANSPARENT
            ),
            navigationBarStyle = SystemBarStyle.auto(
                Color.TRANSPARENT,
                Color.TRANSPARENT
            )
        )
        super.onCreate(savedInstanceState)
        restoredFromSavedState = savedInstanceState != null

        val preferences = AppPreferences(this)
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
            val themeMode by preferences.themeMode.collectAsState(initial = ThemeMode.SYSTEM)
            val systemDarkTheme = isSystemInDarkTheme()
            val darkTheme = when (themeMode) {
                ThemeMode.SYSTEM -> systemDarkTheme
                ThemeMode.LIGHT -> false
                ThemeMode.DARK -> true
            }

            SideEffect {
                applySystemBarTheme(darkTheme)
            }

            EmuCoreXTheme(themeMode = themeMode) {
                AppNavigation(
                    launchIntentVersion = launchIntentVersion,
                    restoredFromSavedState = restoredFromSavedState,
                    onStartupReady = {
                        keepSplashVisible = false
                    }
                )
            }
        }
    }

    private fun applySystemBarTheme(darkTheme: Boolean) {
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(
                Color.TRANSPARENT,
                Color.TRANSPARENT
            ),
            navigationBarStyle = SystemBarStyle.auto(
                Color.TRANSPARENT,
                Color.TRANSPARENT
            )
        )

        val controller = WindowCompat.getInsetsController(window, window.decorView)
        val useDarkIcons = !darkTheme
        controller.isAppearanceLightStatusBars = useDarkIcons
        controller.isAppearanceLightNavigationBars = useDarkIcons
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
                if (GamepadManager.handleKeyEvent(event)) return true
            } else {
                return true
            }
        }

        if (GamepadManager.isEmulationInputEnabled() && shouldRouteKeyboardEvent(event)) {
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
            return true
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
    private fun shouldRouteKeyboardEvent(event: KeyEvent): Boolean {
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
}

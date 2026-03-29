package com.sbro.emucorex

import android.annotation.SuppressLint
import android.content.Context
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.lifecycle.lifecycleScope
import com.sbro.emucorex.core.AppLocaleManager
import com.sbro.emucorex.core.GamepadManager
import com.sbro.emucorex.core.GamepadUiActions
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.navigation.AppNavigation
import com.sbro.emucorex.ui.theme.EmuCoreXTheme
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private var appliedLanguageTag: String? = null

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(AppLocaleManager.wrap(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(
                android.graphics.Color.TRANSPARENT,
                android.graphics.Color.TRANSPARENT
            ),
            navigationBarStyle = SystemBarStyle.auto(
                android.graphics.Color.TRANSPARENT,
                android.graphics.Color.TRANSPARENT
            )
        )
        super.onCreate(savedInstanceState)

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

            EmuCoreXTheme(themeMode = themeMode) {
                AppNavigation()
            }
        }
    }

    @SuppressLint("RestrictedApi", "GestureBackNavigation")
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (GamepadManager.isGameController(event.device)) {
            if (GamepadManager.handleBindingCapture(event)) return true
            if (GamepadManager.isEmulationInputEnabled()) {
                if (GamepadManager.handleKeyEvent(event)) return true
            } else {
                when (event.keyCode) {
                    KeyEvent.KEYCODE_BUTTON_Y,
                    KeyEvent.KEYCODE_MENU,
                    KeyEvent.KEYCODE_BUTTON_MODE -> {
                        if (event.action == KeyEvent.ACTION_UP && event.repeatCount == 0 && GamepadUiActions.toggleDrawer()) {
                            return true
                        }
                    }
                    KeyEvent.KEYCODE_BUTTON_B,
                    KeyEvent.KEYCODE_BACK -> {
                        if (event.action == KeyEvent.ACTION_UP && event.repeatCount == 0) {
                            onBackPressedDispatcher.onBackPressed()
                        }
                        return true
                    }
                }

                remapGamepadUiKey(event)?.let { mappedEvent ->
                    return super.dispatchKeyEvent(mappedEvent)
                }
            }
        }

        return super.dispatchKeyEvent(event)
    }

    override fun onGenericMotionEvent(event: MotionEvent?): Boolean {
        if (event != null && GamepadManager.handleMotionEvent(event)) return true
        return super.onGenericMotionEvent(event)
    }

    private fun remapGamepadUiKey(event: KeyEvent): KeyEvent? {
        val mappedKeyCode = when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_BUTTON_START -> KeyEvent.KEYCODE_DPAD_CENTER
            else -> null
        } ?: return null

        return KeyEvent(
            event.downTime,
            event.eventTime,
            event.action,
            mappedKeyCode,
            event.repeatCount,
            event.metaState,
            event.deviceId,
            event.scanCode,
            event.flags,
            event.source
        )
    }
}

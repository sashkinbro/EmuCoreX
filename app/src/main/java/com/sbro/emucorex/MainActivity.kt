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
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.lifecycleScope
import com.sbro.emucorex.core.AppLocaleManager
import com.sbro.emucorex.core.GamepadManager
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

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(AppLocaleManager.wrap(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen().setKeepOnScreenCondition { keepSplashVisible }
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
                AppNavigation(
                    onStartupReady = {
                        keepSplashVisible = false
                    }
                )
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
                return true
            }
        }

        return super.dispatchKeyEvent(event)
    }

    override fun onGenericMotionEvent(event: MotionEvent?): Boolean {
        if (event != null && GamepadManager.isGameController(event.device) && !GamepadManager.isEmulationInputEnabled()) {
            return true
        }
        if (event != null && GamepadManager.handleMotionEvent(event)) return true
        return super.onGenericMotionEvent(event)
    }
}

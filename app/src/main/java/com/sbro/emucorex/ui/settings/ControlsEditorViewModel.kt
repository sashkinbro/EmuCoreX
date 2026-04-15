package com.sbro.emucorex.ui.settings

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

data class ControlsLayoutState(
    val overlayScale: Int = 100,
    val dpadOffset: Pair<Float, Float> = AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y,
    val lstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y,
    val rstickOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y,
    val actionOffset: Pair<Float, Float> = AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y,
    val lbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y,
    val rbtnOffset: Pair<Float, Float> = AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y,
    val centerOffset: Pair<Float, Float> = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
    val stickScale: Int = 100,
    val stickSurfaceMode: Boolean = false,
    val controlLayouts: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts()
)

class ControlsEditorViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = AppPreferences(application)
    private val layoutMutex = Mutex()
    
    private val _layoutState = MutableStateFlow(ControlsLayoutState())

    init {
        viewModelScope.launch {
            preferences.migrateOverlayLayoutIfNeeded()
            preferences.overlayLayoutSnapshot.collect { snapshot ->
                _layoutState.value = ControlsLayoutState(
                    overlayScale = snapshot.overlayScale,
                    dpadOffset = snapshot.dpadOffset,
                    lstickOffset = snapshot.lstickOffset,
                    rstickOffset = snapshot.rstickOffset,
                    actionOffset = snapshot.actionOffset,
                    lbtnOffset = snapshot.lbtnOffset,
                    rbtnOffset = snapshot.rbtnOffset,
                    centerOffset = snapshot.centerOffset,
                    stickScale = snapshot.stickScale,
                    stickSurfaceMode = snapshot.stickSurfaceMode,
                    controlLayouts = snapshot.controlLayouts
                )
            }
        }
    }

    private suspend fun persistLayoutState(state: ControlsLayoutState) {
        preferences.setControlsLayout(
            dpadX = state.dpadOffset.first, dpadY = state.dpadOffset.second,
            lstickX = state.lstickOffset.first, lstickY = state.lstickOffset.second,
            rstickX = state.rstickOffset.first, rstickY = state.rstickOffset.second,
            actionX = state.actionOffset.first, actionY = state.actionOffset.second,
            lbtnX = state.lbtnOffset.first, lbtnY = state.lbtnOffset.second,
            rbtnX = state.rbtnOffset.first, rbtnY = state.rbtnOffset.second,
            centerX = state.centerOffset.first, centerY = state.centerOffset.second,
            stickScaleVal = state.stickScale,
            controlLayouts = state.controlLayouts
        )
    }

    private suspend fun commitLayout(transform: (ControlsLayoutState) -> ControlsLayoutState) {
        layoutMutex.withLock {
            val next = transform(_layoutState.value)
            _layoutState.value = next
            persistLayoutState(next)
        }
    }

    fun updateControlOffset(controlId: String, offset: Pair<Float, Float>) {
        viewModelScope.launch {
            commitLayout { current ->
                val updated = current.controlLayouts.toMutableMap()
                val controlDefaults = AppPreferences.defaultOverlayControlLayouts(current.stickScale)
                val control = updated[controlId] ?: controlDefaults[controlId] ?: OverlayControlLayout()
                updated[controlId] = control.copy(offset = offset)
                current.copy(controlLayouts = updated)
            }
        }
    }

    fun updateControlScale(controlId: String, scale: Int) {
        viewModelScope.launch {
            commitLayout { current ->
                val updated = current.controlLayouts.toMutableMap()
                val controlDefaults = AppPreferences.defaultOverlayControlLayouts(current.stickScale)
                val control = updated[controlId] ?: controlDefaults[controlId] ?: OverlayControlLayout()
                updated[controlId] = control.copy(scale = scale.coerceIn(50, 200))
                current.copy(controlLayouts = updated)
            }
        }
    }

    fun toggleLeftInputMode() {
        viewModelScope.launch {
            commitLayout { current ->
                val updated = current.controlLayouts.toMutableMap()
                val defaults = AppPreferences.defaultOverlayControlLayouts(current.stickScale)
                val leftStick = updated["left_stick"]
                    ?: defaults["left_stick"]
                    ?: OverlayControlLayout(scale = current.stickScale, visible = true)
                val showStick = !leftStick.visible
                updated["left_stick"] = leftStick.copy(visible = showStick)
                listOf("dpad_up", "dpad_down", "dpad_left", "dpad_right").forEach { id ->
                    val control = updated[id] ?: defaults[id] ?: OverlayControlLayout()
                    updated[id] = control.copy(visible = !showStick)
                }
                current.copy(
                    controlLayouts = updated,
                    dpadOffset = current.lstickOffset,
                    lstickOffset = current.dpadOffset
                )
            }
        }
    }

    fun setControlVisible(controlId: String, visible: Boolean) {
        viewModelScope.launch {
            commitLayout { current ->
                val updated = current.controlLayouts.toMutableMap()
                val controlDefaults = AppPreferences.defaultOverlayControlLayouts(current.stickScale)
                val control = updated[controlId] ?: controlDefaults[controlId] ?: OverlayControlLayout()
                updated[controlId] = control.copy(visible = visible)
                current.copy(controlLayouts = updated)
            }
        }
    }

    fun setStickSurfaceMode(enabled: Boolean) {
        viewModelScope.launch {
            _layoutState.value = _layoutState.value.copy(stickSurfaceMode = enabled)
            preferences.setStickSurfaceMode(enabled)
        }
    }

    fun resetLayout() {
        viewModelScope.launch { preferences.resetControlsLayout() }
    }
}

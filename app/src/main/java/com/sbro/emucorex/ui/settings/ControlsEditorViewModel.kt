package com.sbro.emucorex.ui.settings

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

data class ControlsLayoutState(
    val dpadOffset: Pair<Float, Float> = 0f to 0f,
    val lstickOffset: Pair<Float, Float> = 0f to 0f,
    val rstickOffset: Pair<Float, Float> = 0f to 0f,
    val actionOffset: Pair<Float, Float> = 0f to 0f,
    val lbtnOffset: Pair<Float, Float> = 0f to 0f,
    val rbtnOffset: Pair<Float, Float> = 0f to 0f,
    val centerOffset: Pair<Float, Float> = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
    val stickScale: Int = 100,
    val controlLayouts: Map<String, OverlayControlLayout> = emptyMap()
)

class ControlsEditorViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = AppPreferences(application)
    
    private val _layoutState = MutableStateFlow(ControlsLayoutState())
    val layoutState: StateFlow<ControlsLayoutState> = _layoutState.asStateFlow()

    init {
        viewModelScope.launch {
            preferences.migrateOverlayLayoutIfNeeded()
            val snapshot = preferences.overlayLayoutSnapshot.first()
            
            _layoutState.value = ControlsLayoutState(
                dpadOffset = snapshot.dpadOffset,
                lstickOffset = snapshot.lstickOffset,
                rstickOffset = snapshot.rstickOffset,
                actionOffset = snapshot.actionOffset,
                lbtnOffset = snapshot.lbtnOffset,
                rbtnOffset = snapshot.rbtnOffset,
                centerOffset = snapshot.centerOffset,
                stickScale = snapshot.stickScale,
                controlLayouts = snapshot.controlLayouts
            )
        }
    }

    fun updateDpadOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(dpadOffset = offset)
    }
    
    fun updateLstickOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(lstickOffset = offset)
    }
    
    fun updateRstickOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(rstickOffset = offset)
    }
    
    fun updateActionOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(actionOffset = offset)
    }
    
    fun updateLbtnOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(lbtnOffset = offset)
    }
    
    fun updateRbtnOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(rbtnOffset = offset)
    }
    
    fun updateCenterOffset(offset: Pair<Float, Float>) {
        _layoutState.value = _layoutState.value.copy(centerOffset = offset)
    }
    
    fun updateStickScale(scale: Int) {
        _layoutState.value = _layoutState.value.copy(stickScale = scale)
    }

    fun updateControlOffset(controlId: String, offset: Pair<Float, Float>) {
        val updated = _layoutState.value.controlLayouts.toMutableMap()
        val current = updated[controlId] ?: OverlayControlLayout()
        updated[controlId] = current.copy(offset = offset)
        _layoutState.value = _layoutState.value.copy(controlLayouts = updated)
    }

    fun updateControlScale(controlId: String, scale: Int) {
        val updated = _layoutState.value.controlLayouts.toMutableMap()
        val current = updated[controlId] ?: OverlayControlLayout()
        updated[controlId] = current.copy(scale = scale.coerceIn(50, 200))
        _layoutState.value = _layoutState.value.copy(controlLayouts = updated)
    }

    fun setControlVisible(controlId: String, visible: Boolean) {
        val updated = _layoutState.value.controlLayouts.toMutableMap()
        val current = updated[controlId] ?: OverlayControlLayout()
        updated[controlId] = current.copy(visible = visible)
        _layoutState.value = _layoutState.value.copy(controlLayouts = updated)
    }

    fun resetControl(controlId: String) {
        val updated = _layoutState.value.controlLayouts.toMutableMap()
        updated.remove(controlId)
        _layoutState.value = _layoutState.value.copy(controlLayouts = updated)
    }

    suspend fun saveLayout(controlLayouts: Map<String, OverlayControlLayout> = _layoutState.value.controlLayouts) {
        val s = _layoutState.value
        preferences.setControlsLayout(
            dpadX = s.dpadOffset.first, dpadY = s.dpadOffset.second,
            lstickX = s.lstickOffset.first, lstickY = s.lstickOffset.second,
            rstickX = s.rstickOffset.first, rstickY = s.rstickOffset.second,
            actionX = s.actionOffset.first, actionY = s.actionOffset.second,
            lbtnX = s.lbtnOffset.first, lbtnY = s.lbtnOffset.second,
            rbtnX = s.rbtnOffset.first, rbtnY = s.rbtnOffset.second,
            centerX = s.centerOffset.first, centerY = s.centerOffset.second,
            stickScaleVal = s.stickScale,
            controlLayouts = controlLayouts
        )
    }
    
    fun resetLayout() {
        viewModelScope.launch {
            preferences.resetControlsLayout()
            _layoutState.value = ControlsLayoutState()
        }
    }
}

package com.sbro.emucorex.ui.settings

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.AppPreferences
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
    val centerOffset: Pair<Float, Float> = 0f to 0f,
    val stickScale: Int = 100
)

class ControlsEditorViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = AppPreferences(application)
    
    private val _layoutState = MutableStateFlow(ControlsLayoutState())
    val layoutState: StateFlow<ControlsLayoutState> = _layoutState.asStateFlow()

    init {
        viewModelScope.launch {
            val dpad = preferences.dpadOffset.first()
            val lstick = preferences.lstickOffset.first()
            val rstick = preferences.rstickOffset.first()
            val action = preferences.actionOffset.first()
            val lbtn = preferences.lbtnOffset.first()
            val rbtn = preferences.rbtnOffset.first()
            val center = preferences.centerOffset.first()
            val scale = preferences.stickScale.first()
            
            _layoutState.value = ControlsLayoutState(
                dpadOffset = dpad,
                lstickOffset = lstick,
                rstickOffset = rstick,
                actionOffset = action,
                lbtnOffset = lbtn,
                rbtnOffset = rbtn,
                centerOffset = center,
                stickScale = scale
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

    suspend fun saveLayout() {
        val s = _layoutState.value
        preferences.setControlsLayout(
            dpadX = s.dpadOffset.first, dpadY = s.dpadOffset.second,
            lstickX = s.lstickOffset.first, lstickY = s.lstickOffset.second,
            rstickX = s.rstickOffset.first, rstickY = s.rstickOffset.second,
            actionX = s.actionOffset.first, actionY = s.actionOffset.second,
            lbtnX = s.lbtnOffset.first, lbtnY = s.lbtnOffset.second,
            rbtnX = s.rbtnOffset.first, rbtnY = s.rbtnOffset.second,
            centerX = s.centerOffset.first, centerY = s.centerOffset.second,
            stickScaleVal = s.stickScale
        )
    }
    
    fun resetLayout() {
        viewModelScope.launch {
            preferences.resetControlsLayout()
            _layoutState.value = ControlsLayoutState()
        }
    }
}

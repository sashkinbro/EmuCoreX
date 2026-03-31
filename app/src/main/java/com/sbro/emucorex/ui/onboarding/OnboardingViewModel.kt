package com.sbro.emucorex.ui.onboarding

import android.app.Application
import android.content.Intent
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.BiosValidator
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.core.SetupValidator
import com.sbro.emucorex.data.AppPreferences
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class OnboardingUiState(
    val biosPath: String? = null,
    val gamePath: String? = null,
    val biosValid: Boolean = false,
    val gamePathValid: Boolean = false,
    val canContinue: Boolean = false,
    val currentPage: Int = 0,
    val totalPages: Int = 4
)

class OnboardingViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val _uiState = MutableStateFlow(OnboardingUiState())
    val uiState: StateFlow<OnboardingUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            launch {
                preferences.biosPath.collect { path ->
                    updateState(
                        biosPath = path,
                        biosValid = BiosValidator.hasUsableBiosFiles(getApplication(), path)
                    )
                }
            }
            launch {
                preferences.gamePath.collect { path ->
                    updateState(
                        gamePath = path,
                        gamePathValid = SetupValidator.isGameFolderAccessible(getApplication(), path)
                    )
                }
            }
        }
    }

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
                renderer = EmulatorBridge.getSetting("EmuCoreX", "Renderer", "int")?.toIntOrNull() ?: 0,
                upscaleMultiplier = EmulatorBridge.getSetting("EmuCoreX", "UpscaleMultiplier", "float")?.toFloatOrNull()
                    ?: EmulatorBridge.getSetting("EmuCoreX", "UpscaleMultiplier", "int")?.toIntOrNull()?.toFloat()
                    ?: 1f
            )
        }
    }

    fun setGamePath(uri: Uri) {
        val application = getApplication<Application>()
        application.contentResolver.takePersistableUriPermission(
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION
        )

        viewModelScope.launch {
            preferences.setGamePath(uri.toString())
        }
    }

    fun goToNextPage() {
        val currentState = _uiState.value
        if (currentState.currentPage < currentState.totalPages - 1) {
            _uiState.value = currentState.copy(currentPage = currentState.currentPage + 1)
        }
    }

    fun goToPreviousPage() {
        val currentState = _uiState.value
        if (currentState.currentPage > 0) {
            _uiState.value = currentState.copy(currentPage = currentState.currentPage - 1)
        }
    }

    fun setCurrentPage(page: Int) {
        val currentState = _uiState.value
        _uiState.value = currentState.copy(currentPage = page.coerceIn(0, currentState.totalPages - 1))
    }

    fun completeOnboarding(onFinished: () -> Unit) {
        if (!_uiState.value.canContinue) return
        viewModelScope.launch {
            preferences.setOnboardingCompleted(true)
            onFinished()
        }
    }

    private fun updateState(
        biosPath: String? = _uiState.value.biosPath,
        gamePath: String? = _uiState.value.gamePath,
        biosValid: Boolean = _uiState.value.biosValid,
        gamePathValid: Boolean = _uiState.value.gamePathValid,
        currentPage: Int = _uiState.value.currentPage
    ) {
        _uiState.value = OnboardingUiState(
            biosPath = biosPath,
            gamePath = gamePath,
            biosValid = biosValid,
            gamePathValid = gamePathValid,
            canContinue = biosValid && gamePathValid,
            currentPage = currentPage,
            totalPages = 4
        )
    }
}

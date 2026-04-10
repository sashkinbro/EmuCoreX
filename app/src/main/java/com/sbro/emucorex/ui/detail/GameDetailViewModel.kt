package com.sbro.emucorex.ui.detail

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.data.ps2.Ps2CatalogDetails
import com.sbro.emucorex.data.ps2.Ps2CatalogRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class GameDetailUiState(
    val isLoading: Boolean = false,
    val catalogDetails: Ps2CatalogDetails? = null,
    val isCatalogAvailable: Boolean = false
)

class GameDetailViewModel(application: Application) : AndroidViewModel(application) {

    private val catalogRepository = Ps2CatalogRepository(application)
    private val _uiState = MutableStateFlow(GameDetailUiState())
    val uiState: StateFlow<GameDetailUiState> = _uiState.asStateFlow()
    private var lastCatalogGameId: Long? = null

    fun loadGame(catalogGameId: Long) {
        if (lastCatalogGameId == catalogGameId && (_uiState.value.isLoading || _uiState.value.catalogDetails != null)) {
            return
        }
        lastCatalogGameId = catalogGameId

        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = GameDetailUiState(isLoading = true)
            val hasCatalog = catalogRepository.hasCatalog()
            val details = if (hasCatalog) catalogRepository.getDetails(catalogGameId) else null
            _uiState.value = GameDetailUiState(
                isLoading = false,
                catalogDetails = details,
                isCatalogAvailable = hasCatalog
            )
        }
    }

    override fun onCleared() {
        super.onCleared()
        catalogRepository.close()
    }
}

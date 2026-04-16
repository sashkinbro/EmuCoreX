package com.sbro.emucorex.ui.detail

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.firebase.firestore.ListenerRegistration
import com.sbro.emucorex.data.GameComment
import com.sbro.emucorex.data.GameCommentsRepository
import com.sbro.emucorex.data.ps2.Ps2CatalogDetails
import com.sbro.emucorex.data.ps2.Ps2CatalogRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class GameDetailUiState(
    val isLoading: Boolean = false,
    val catalogDetails: Ps2CatalogDetails? = null,
    val isCatalogAvailable: Boolean = false,
    val comments: List<GameComment> = emptyList(),
    val isCommentsLoading: Boolean = false,
    val commentsLoadFailed: Boolean = false
)

class GameDetailViewModel(application: Application) : AndroidViewModel(application) {

    private val catalogRepository = Ps2CatalogRepository(application)
    private val commentsRepository = GameCommentsRepository()
    private val _uiState = MutableStateFlow(GameDetailUiState())
    val uiState: StateFlow<GameDetailUiState> = _uiState.asStateFlow()
    private var lastCatalogGameId: Long? = null
    private var commentsRegistration: ListenerRegistration? = null

    fun loadGame(catalogGameId: Long) {
        if (lastCatalogGameId == catalogGameId && (_uiState.value.isLoading || _uiState.value.catalogDetails != null)) {
            return
        }
        lastCatalogGameId = catalogGameId
        commentsRegistration?.remove()
        _uiState.value = GameDetailUiState(isLoading = true, isCommentsLoading = true)
        observeComments(catalogGameId)

        viewModelScope.launch(Dispatchers.IO) {
            val hasCatalog = catalogRepository.hasCatalog()
            val details = if (hasCatalog) catalogRepository.getDetails(catalogGameId) else null
            _uiState.update { state ->
                state.copy(
                    isLoading = false,
                    catalogDetails = details,
                    isCatalogAvailable = hasCatalog
                )
            }
        }
    }

    private fun observeComments(catalogGameId: Long) {
        commentsRegistration = commentsRepository.observeComments(
            gameId = catalogGameId,
            onUpdate = { comments ->
                _uiState.update { state ->
                    state.copy(
                        comments = comments,
                        isCommentsLoading = false,
                        commentsLoadFailed = false
                    )
                }
            },
            onError = {
                _uiState.update { state ->
                    state.copy(
                        comments = emptyList(),
                        isCommentsLoading = false,
                        commentsLoadFailed = true
                    )
                }
            }
        )
    }

    override fun onCleared() {
        super.onCleared()
        commentsRegistration?.remove()
        catalogRepository.close()
    }
}

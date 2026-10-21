package com.sbro.emucorex.ui.detail

import android.app.Application
import android.app.ActivityManager
import android.content.Context
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.ListenerRegistration
import com.sbro.emucorex.data.GameComment
import com.sbro.emucorex.data.GameCommentDeviceInfo
import com.sbro.emucorex.data.GameCommentsRepository
import com.sbro.emucorex.data.PlayerAccount
import com.sbro.emucorex.core.MobileSocNameMapper
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
    val commentsLoadFailed: Boolean = false,
    val account: PlayerAccount? = null,
    val isCommentSubmitting: Boolean = false,
    val commentSubmitError: String? = null
)

class GameDetailViewModel(application: Application) : AndroidViewModel(application) {

    private val catalogRepository = Ps2CatalogRepository(application)
    private val commentsRepository = GameCommentsRepository()
    private val auth = FirebaseAuth.getInstance()
    private val _uiState = MutableStateFlow(GameDetailUiState())
    val uiState: StateFlow<GameDetailUiState> = _uiState.asStateFlow()
    private var lastCatalogGameId: Long? = null
    private var commentsRegistration: ListenerRegistration? = null
    private val authListener = FirebaseAuth.AuthStateListener { firebaseAuth ->
        val user = firebaseAuth.currentUser
        _uiState.update { state ->
            state.copy(
                account = user?.let {
                    PlayerAccount(
                        uid = it.uid,
                        email = it.email,
                        displayName = it.displayName ?: it.email?.substringBefore('@') ?: "Player",
                        photoURL = it.photoUrl?.toString()
                    )
                }
            )
        }
    }

    init {
        auth.addAuthStateListener(authListener)
    }

    fun loadGame(catalogGameId: Long) {
        if (lastCatalogGameId == catalogGameId && (_uiState.value.isLoading || _uiState.value.catalogDetails != null)) {
            return
        }
        lastCatalogGameId = catalogGameId
        commentsRegistration?.remove()
        _uiState.value = GameDetailUiState(
            isLoading = true,
            isCommentsLoading = true,
            account = _uiState.value.account
        )
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

    fun submitComment(rating: Int, text: String) {
        val gameId = lastCatalogGameId ?: return
        viewModelScope.launch {
            _uiState.update { it.copy(isCommentSubmitting = true, commentSubmitError = null) }
            runCatching {
                commentsRepository.addComment(
                    gameId = gameId,
                    rating = rating,
                    text = text,
                    device = buildDeviceInfo()
                )
            }.onSuccess {
                _uiState.update { it.copy(isCommentSubmitting = false, commentSubmitError = null) }
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        isCommentSubmitting = false,
                        commentSubmitError = error.localizedMessage ?: "Could not post comment."
                    )
                }
            }
        }
    }

    fun clearCommentSubmitError() {
        _uiState.update { it.copy(commentSubmitError = null) }
    }

    private fun buildDeviceInfo(): GameCommentDeviceInfo {
        val app = getApplication<Application>()
        val memoryInfo = ActivityManager.MemoryInfo()
        val activityManager = app.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
        activityManager?.getMemoryInfo(memoryInfo)
        val totalRamGb = if (memoryInfo.totalMem > 0L) {
            val gb = memoryInfo.totalMem.toDouble() / (1024.0 * 1024.0 * 1024.0)
            String.format(java.util.Locale.US, "%.1f GB", gb)
        } else {
            ""
        }
        val brand = Build.BRAND.orEmpty()
        val model = Build.MODEL.orEmpty()
        return GameCommentDeviceInfo(
            phoneBrand = brand,
            phoneId = Build.DEVICE.orEmpty().ifBlank { Build.ID.orEmpty() },
            phoneModel = model,
            phoneName = listOf(brand, model).filter { it.isNotBlank() }.joinToString(" "),
            phoneCpu = MobileSocNameMapper.currentDeviceName(),
            phoneRam = totalRamGb
        )
    }

    override fun onCleared() {
        auth.removeAuthStateListener(authListener)
        commentsRegistration?.remove()
        catalogRepository.close()
    }
}

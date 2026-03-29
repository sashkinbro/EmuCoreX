package com.sbro.emucorex.ui.detail

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorBridge
import com.sbro.emucorex.data.CoverArtRepository
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.data.GameRepository
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityEntry
import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityRepository
import com.sbro.emucorex.data.ps2.Ps2CatalogDetails
import com.sbro.emucorex.data.ps2.Ps2CatalogRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class GameDetailUiState(
    val isLoading: Boolean = false,
    val game: GameItem? = null,
    val formattedSize: String = "",
    val saveSlots: List<Boolean> = List(5) { false },
    val catalogDetails: Ps2CatalogDetails? = null,
    val pcsx2Compatibility: Pcsx2CompatibilityEntry? = null,
    val isCatalogAvailable: Boolean = false,
    val isInstalledGame: Boolean = false
)

class GameDetailViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = GameRepository()
    private val catalogRepository = Ps2CatalogRepository(application)
    private val compatibilityRepository = Pcsx2CompatibilityRepository(application)
    private val _uiState = MutableStateFlow(GameDetailUiState())
    val uiState: StateFlow<GameDetailUiState> = _uiState.asStateFlow()
    private var lastRequest: DetailRequest? = null

    fun loadGame(path: String?, catalogGameId: Long? = null) {
        val request = DetailRequest(path = path, catalogGameId = catalogGameId)
        val state = _uiState.value
        if (lastRequest == request && (state.isLoading || state.game != null || state.catalogDetails != null)) {
            return
        }
        lastRequest = request

        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = GameDetailUiState(isLoading = true)
            val context = getApplication<Application>()
            val isInstalledGame = !path.isNullOrBlank()

            if (isInstalledGame) {
                val metadata = EmulatorBridge.getGameMetadata(path)
                val title = metadata.title
                val displayName = DocumentPathResolver.getDisplayName(context, path)
                val fileSize = DocumentPathResolver.getFileSize(context, path)

                val (coverPath, catalogDetails, slots, compatibility) = coroutineScope {
                    val coverDeferred = async {
                        repository.findCoverForGame(
                            path = path,
                            context = context,
                            serial = metadata.serial,
                            title = title
                        ) ?: CoverArtRepository(context).downloadCover(metadata.serial)
                    }
                    val catalogDeferred = async {
                        catalogGameId?.let(catalogRepository::getDetails)
                            ?: catalogRepository.findBestMatch(
                                title = title,
                                fileName = displayName,
                                serial = metadata.serial
                            )?.details
                    }
                    val saveSlotsDeferred = async {
                        (0 until 5).map { slot ->
                            EmulatorBridge.hasSaveStateForGame(path, slot)
                        }
                    }
                    val compatibilityDeferred = async {
                        val resolvedCatalog = catalogDeferred.await()
                        compatibilityRepository.findBest(
                            serial = metadata.serial,
                            title = resolvedCatalog?.name ?: title
                        )
                    }
                    Quadruple(
                        coverDeferred.await(),
                        catalogDeferred.await(),
                        saveSlotsDeferred.await(),
                        compatibilityDeferred.await()
                    )
                }
                val game = GameItem(
                    title = title,
                    path = path,
                    fileName = displayName,
                    fileSize = fileSize,
                    coverArtPath = coverPath ?: catalogDetails?.coverUrl,
                    serial = metadata.serial,
                    catalogGameId = catalogDetails?.igdbId,
                    catalogYear = catalogDetails?.year,
                    catalogRating = catalogDetails?.rating,
                    pcsx2Compatibility = compatibility
                )
                _uiState.value = GameDetailUiState(
                    isLoading = false,
                    game = game,
                    formattedSize = repository.formatFileSize(game.fileSize),
                    saveSlots = slots,
                    catalogDetails = catalogDetails,
                    pcsx2Compatibility = compatibility,
                    isCatalogAvailable = catalogRepository.hasCatalog(),
                    isInstalledGame = true
                )
            } else {
                val catalogDetails = catalogGameId?.let(catalogRepository::getDetails)
                _uiState.value = GameDetailUiState(
                    isLoading = false,
                    game = catalogDetails?.let {
                        GameItem(
                            title = it.name,
                            path = "",
                            fileName = "",
                            fileSize = 0L,
                            coverArtPath = it.coverUrl,
                            serial = it.primarySerial,
                            catalogGameId = it.igdbId,
                            catalogYear = it.year,
                            catalogRating = it.rating,
                            pcsx2Compatibility = it.pcsx2Compatibility
                        )
                    },
                    formattedSize = "",
                    saveSlots = List(5) { false },
                    catalogDetails = catalogDetails,
                    pcsx2Compatibility = catalogDetails?.pcsx2Compatibility,
                    isCatalogAvailable = catalogRepository.hasCatalog(),
                    isInstalledGame = false
                )
            }
        }
    }
}

private data class DetailRequest(
    val path: String?,
    val catalogGameId: Long?
)

private data class Quadruple<A, B, C, D>(
    val first: A,
    val second: B,
    val third: C,
    val fourth: D
)

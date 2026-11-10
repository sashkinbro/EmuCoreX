package com.sbro.emucorex.ui.emulation

/** A completed settings write must not restore the session that existed before its disk I/O. */
internal fun EmulationUiState.withCurrentSession(current: EmulationUiState): EmulationUiState = copy(
    isRunning = current.isRunning,
    isStarting = current.isStarting,
    isPaused = current.isPaused,
    showMenu = current.showMenu,
    isActionInProgress = current.isActionInProgress,
    actionLabel = current.actionLabel,
    transportMode = current.transportMode,
    toastMessage = current.toastMessage,
    statusMessage = current.statusMessage,
    currentSlot = current.currentSlot,
    currentGameTitle = current.currentGameTitle,
    currentGameSubtitle = current.currentGameSubtitle,
    currentGameCoverArtPath = current.currentGameCoverArtPath,
    currentSlotLastModified = current.currentSlotLastModified,
    currentSlotPreviewPath = current.currentSlotPreviewPath,
    autoSaveLastModified = current.autoSaveLastModified,
    autoSavePreviewPath = current.autoSavePreviewPath,
    isAutoSaveInProgress = current.isAutoSaveInProgress,
    activePlayTimeMs = current.activePlayTimeMs
)

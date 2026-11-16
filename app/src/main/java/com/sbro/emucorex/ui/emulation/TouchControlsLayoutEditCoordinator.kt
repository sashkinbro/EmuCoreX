package com.sbro.emucorex.ui.emulation

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Orders the complete read, save and publication of each touch layout edit. */
internal class TouchControlsLayoutEditCoordinator {
    private val mutex = Mutex()

    suspend fun edit(block: suspend () -> Unit) = mutex.withLock { block() }
}

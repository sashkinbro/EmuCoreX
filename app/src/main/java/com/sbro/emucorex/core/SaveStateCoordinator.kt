package com.sbro.emucorex.core

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** Orders snapshots and writes without retaining the emulation control dispatcher during ZIP. */
internal class SaveStateCoordinator {
    private val saves = Mutex()

    suspend fun save(snapshot: suspend () -> Long, awaitCompletion: (Long) -> Boolean): Boolean =
        saves.withLock {
            // Once snapshotting begins, always consume its native completion ticket, even if
            // the menu/activity scope is cancelled. Saves to the same slot cannot overtake it.
            withContext(NonCancellable) {
                val ticket = snapshot()
                if (ticket == 0L) return@withContext false
                withContext(Dispatchers.IO) { awaitCompletion(ticket) }
            }
        }
}

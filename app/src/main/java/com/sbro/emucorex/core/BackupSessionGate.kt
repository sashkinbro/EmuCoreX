package com.sbro.emucorex.core

import com.sbro.emucorex.data.drive.DriveBackupException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Shares one exclusion boundary between VM startup and backup/restore filesystem changes. */
object BackupSessionGate {
    private val mutex = Mutex()
    @Volatile private var running = false
    @Volatile private var starting = false
    val gameBusy get() = running || starting

    suspend fun <T> whileStopped(block: suspend () -> T): T = mutex.withLock {
        checkpoint()
        block()
    }

    suspend fun start(active: () -> Boolean, block: suspend () -> Boolean): Boolean {
        starting = true
        return try {
            mutex.withLock {
                running = true
                try { block().also { running = active() } }
                catch (error: Throwable) { running = active(); throw error }
            }
        } finally { starting = false }
    }

    fun stopped() { running = false }
    fun checkpoint() {
        if (gameBusy) throw DriveBackupException("busy")
    }
}

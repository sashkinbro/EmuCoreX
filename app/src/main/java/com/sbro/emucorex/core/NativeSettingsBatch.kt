package com.sbro.emucorex.core

/** Serializes the whole transaction, including callers outside the coroutine control lane. */
internal class SettingsBatchCoordinator {
    private val lock = Any()

    fun <T> run(begin: () -> Unit, end: () -> Unit, write: () -> T): T = synchronized(lock) {
        begin()
        try {
            write()
        } finally {
            end()
        }
    }
}

internal object NativeSettingsBatch {
    private val coordinator = SettingsBatchCoordinator()

    fun <T> run(write: () -> T): T = coordinator.run(
        begin = NativeApp::beginSettingsBatch,
        end = NativeApp::endSettingsBatch,
        write = write
    )
}

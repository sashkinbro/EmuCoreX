package com.sbro.emucorex.ui.emulation

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Serializes native job admission and observation against route-exit cleanup. */
internal class ShaderPreparationLifecycleGate {
    private val mutex = Mutex()
    @Volatile
    var mayBeActive: Boolean = false
        private set

    suspend fun <T> admitAndObserve(
        admit: suspend () -> Boolean,
        observe: suspend (accepted: Boolean) -> T
    ): T = mutex.withLock {
        mayBeActive = true
        val accepted = admit()
        observe(accepted)
    }

    suspend fun <T> withCleanup(block: suspend (mayBeActive: Boolean) -> T): T = mutex.withLock {
        block(mayBeActive)
    }

    suspend fun <T> withReadinessProbe(block: suspend () -> T): T = mutex.withLock {
        mayBeActive = true
        try {
            block()
        } finally {
            mayBeActive = false
        }
    }

    fun markInactive() {
        mayBeActive = false
    }

    fun setMayBeActive(value: Boolean) {
        mayBeActive = value
    }
}

internal enum class ShaderPreparationReadinessResult {
    Prepared,
    Offer,
    Aborted
}

/** Testable seam for native cache validation and route-exit serialization. */
internal object ShaderPreparationReadinessCoordinator {
    suspend fun check(
        lifecycleGate: ShaderPreparationLifecycleGate,
        isAborted: () -> Boolean,
        probe: suspend () -> Boolean
    ): ShaderPreparationReadinessResult {
        if (isAborted()) return ShaderPreparationReadinessResult.Aborted
        val validatedPrepared = try {
            lifecycleGate.withReadinessProbe {
                if (isAborted()) return@withReadinessProbe false
                probe()
            }
        } catch (cancelled: kotlinx.coroutines.CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            false
        }
        if (isAborted()) return ShaderPreparationReadinessResult.Aborted
        return if (validatedPrepared) {
            ShaderPreparationReadinessResult.Prepared
        } else {
            ShaderPreparationReadinessResult.Offer
        }
    }
}

internal object ShaderPreparationRouteExitPolicy {
    fun shouldStopEmulation(isChangingConfigurations: Boolean): Boolean = !isChangingConfigurations
}

internal data class ShaderPreparationLaunchRequest(
    val path: String?,
    val slotToLoad: Int?,
    val bootToBios: Boolean,
    val bootSmokeProbe: Boolean,
    val autotestMode: Boolean,
    val enableEeRecompilerOverride: Boolean?,
    val enableIopRecompilerOverride: Boolean?,
    val enableVu0RecompilerOverride: Boolean?,
    val enableVu1RecompilerOverride: Boolean?,
    val enableFastmemOverride: Boolean?,
    val enableMtvuOverride: Boolean?,
    val enableIopThreadOverride: Boolean? = null,
    val enableVUAotOverride: Boolean? = null,
    val rendererOverride: Int?,
    val gsDumpFrames: Int?,
    val gsDumpDelayMs: Int?,
    val vu0FamilyMaskOverride: Int?,
    val vu1FamilyMaskOverride: Int?,
    val restoredAfterProcessDeath: Boolean
)

internal object ShaderPreparationLaunchRetention {
    fun shouldIgnoreRecreatedLaunch(
        current: ShaderPreparationLaunchRequest?,
        requested: ShaderPreparationLaunchRequest,
        isStarting: Boolean,
        isRunning: Boolean,
        preparationVisible: Boolean
    ): Boolean = (isStarting || isRunning || preparationVisible) && current == requested
}

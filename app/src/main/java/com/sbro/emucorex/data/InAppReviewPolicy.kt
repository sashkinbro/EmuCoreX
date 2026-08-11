package com.sbro.emucorex.data

internal data class InAppReviewProgress(
    val qualifyingSessionCount: Int = 0,
    val totalActivePlayTimeMs: Long = 0L,
    val reviewRequested: Boolean = false,
    val lastAttemptAtMs: Long = 0L
)

internal object InAppReviewPolicy {
    const val MIN_QUALIFYING_SESSION_COUNT = 1
    const val MIN_QUALIFYING_SESSION_DURATION_MS = 5 * 60_000L
    const val MIN_TOTAL_ACTIVE_PLAY_TIME_MS = 5 * 60_000L
    const val RETRY_COOLDOWN_MS = 24 * 60 * 60_000L

    fun recordSession(progress: InAppReviewProgress, activePlayTimeMs: Long): InAppReviewProgress {
        if (progress.reviewRequested) return progress

        val safeDurationMs = activePlayTimeMs.coerceAtLeast(0L)
        val updatedSessionCount = if (safeDurationMs >= MIN_QUALIFYING_SESSION_DURATION_MS) {
            saturatingIncrement(progress.qualifyingSessionCount)
        } else {
            progress.qualifyingSessionCount.coerceAtLeast(0)
        }
        return progress.copy(
            qualifyingSessionCount = updatedSessionCount,
            totalActivePlayTimeMs = saturatingAdd(
                progress.totalActivePlayTimeMs.coerceAtLeast(0L),
                safeDurationMs
            )
        )
    }

    fun canAttempt(progress: InAppReviewProgress, nowMs: Long): Boolean {
        if (progress.reviewRequested) return false
        if (progress.qualifyingSessionCount < MIN_QUALIFYING_SESSION_COUNT) return false
        if (progress.totalActivePlayTimeMs < MIN_TOTAL_ACTIVE_PLAY_TIME_MS) return false

        val lastAttemptAtMs = progress.lastAttemptAtMs
        if (lastAttemptAtMs <= 0L) return true
        if (nowMs < lastAttemptAtMs) return true
        return nowMs - lastAttemptAtMs >= RETRY_COOLDOWN_MS
    }

    private fun saturatingIncrement(value: Int): Int = when {
        value < 0 -> 1
        value == Int.MAX_VALUE -> Int.MAX_VALUE
        else -> value + 1
    }

    private fun saturatingAdd(left: Long, right: Long): Long {
        if (right <= 0L) return left
        return if (left > Long.MAX_VALUE - right) Long.MAX_VALUE else left + right
    }
}

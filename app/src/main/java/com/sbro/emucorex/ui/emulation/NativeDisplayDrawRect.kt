package com.sbro.emucorex.ui.emulation

internal fun updatedNativeDisplayDrawRect(previous: FloatArray?, polled: FloatArray?): FloatArray? {
    val next = polled?.takeIf { it.size >= 4 && it[2] > it[0] && it[3] > it[1] }
    // JNI returns a new array on every poll. Preserve the published identity
    // when its coordinates match so Compose does not invalidate the screen.
    return if (previous.contentEquals(next)) previous else next
}

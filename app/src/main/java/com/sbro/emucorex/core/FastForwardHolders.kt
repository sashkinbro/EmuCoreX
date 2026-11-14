package com.sbro.emucorex.core

/**
 * Tracks everything currently holding fast-forward (Start long-press per pad, the dedicated
 * fast-forward button per device/key). Callers synchronise externally.
 */
internal class FastForwardHolders {
    private val holders = mutableSetOf<String>()

    /** Returns the new overall fast-forward state when it changed, otherwise null. */
    fun set(id: String, held: Boolean): Boolean? {
        val before = holders.isNotEmpty()
        if (held) holders.add(id) else holders.remove(id)
        return changeSince(before)
    }

    fun removeWhere(predicate: (String) -> Boolean): Boolean? {
        val before = holders.isNotEmpty()
        holders.removeAll(predicate)
        return changeSince(before)
    }

    private fun changeSince(before: Boolean): Boolean? {
        val after = holders.isNotEmpty()
        return if (before != after) after else null
    }
}

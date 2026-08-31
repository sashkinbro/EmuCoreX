package com.sbro.emucorex.data

import java.io.File

/** Renames stay on the texture filesystem. Recovery also runs before VM startup. */
object TexturePackTransactions {
    private val lock = Any()
    private const val PREVIOUS = ".replacements-previous"

    fun recover(root: File) = synchronized(lock) {
        root.listFiles()?.filter { it.isDirectory && it.name.matches(Regex("[A-Z]{4}-\\d{5}")) }
            ?.forEach(::recoverGame)
    }

    private fun recoverGame(game: File) {
        val previous = File(game, PREVIOUS)
        val target = File(game, "replacements")
        if (previous.isDirectory) {
            if (!target.exists()) check(previous.renameTo(target)) { "Could not recover texture installation" }
            else previous.deleteRecursively()
        }
    }

    fun activate(game: File, staged: File) = synchronized(lock) {
        game.mkdirs()
        recoverGame(game)
        val target = File(game, "replacements")
        val previous = File(game, PREVIOUS)
        val oldMoved = target.exists()
        if (oldMoved) check(target.renameTo(previous)) { "Could not prepare texture update" }
        try {
            check(staged.renameTo(target)) { "Could not activate texture pack" }
        } catch (error: Throwable) {
            if (oldMoved && !target.exists()) check(previous.renameTo(target)) { "Could not restore previous texture pack" }
            throw error
        }
        previous.deleteRecursively()
    }
}

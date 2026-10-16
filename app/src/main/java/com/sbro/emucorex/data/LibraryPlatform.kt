package com.sbro.emucorex.data

import java.util.Locale

enum class LibraryPlatform {
    PS2,
    ARCADE,
}

object LibraryPlatformResolver {
    private const val ARCADE_MANIFEST_EXTENSION = "acgame"
    private const val ARCADE_SERIAL_PREFIX = "NM"

    fun resolve(game: GameItem): LibraryPlatform {
        val extension = game.fileName.substringAfterLast('.', "").lowercase(Locale.ROOT)
        val serial = game.serial.orEmpty().uppercase(Locale.ROOT)
        return if (extension == ARCADE_MANIFEST_EXTENSION || serial.startsWith(ARCADE_SERIAL_PREFIX)) {
            LibraryPlatform.ARCADE
        } else {
            LibraryPlatform.PS2
        }
    }
}

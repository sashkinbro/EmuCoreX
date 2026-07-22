package com.sbro.emucorex.ui.common

import java.util.Locale

private val GAME_SERIAL_IN_TITLE = Regex("(?i)\\b[A-Z]{4}[-_ .]?\\d{5}\\b")
private val GAME_REGION_IN_TITLE = Regex(
    "(?i)\\b(?:NTSC(?:[-_ ]?[UJ])?|PAL(?:[-_ ]?[A-Z])?|USA|EUR(?:OPE)?|JAPAN)\\b"
)
private val GAME_DISC_IN_TITLE = Regex("(?i)\\b(?:disc|disk)\\s*\\d+(?:\\s*of\\s*\\d+)?\\b")

internal fun String.contentCatalogTitleKey(): String =
    lowercase(Locale.US)
        .replace(Regex("[^\\p{L}\\p{N}]+"), " ")
        .trim()

internal fun String.cheatCatalogGameTitleKey(): String =
    replace(GAME_SERIAL_IN_TITLE, " ")
        .replace(GAME_REGION_IN_TITLE, " ")
        .replace(GAME_DISC_IN_TITLE, " ")
        .contentCatalogTitleKey()

// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

package com.sbro.emucorex.network

import java.security.SecureRandom
import java.util.Locale

internal const val MULTIPLAYER_ROOM_CODE_LENGTH = 8
internal const val MULTIPLAYER_ROOM_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

internal fun sanitizeMultiplayerRoomCode(value: String): String = value
    .uppercase(Locale.ROOT)
    .filter { it in MULTIPLAYER_ROOM_ALPHABET }
    .take(MULTIPLAYER_ROOM_CODE_LENGTH)

internal fun generateMultiplayerRoomCode(random: SecureRandom): String = buildString(
    MULTIPLAYER_ROOM_CODE_LENGTH
) {
    repeat(MULTIPLAYER_ROOM_CODE_LENGTH) {
        append(MULTIPLAYER_ROOM_ALPHABET[random.nextInt(MULTIPLAYER_ROOM_ALPHABET.length)])
    }
}

// SPDX-License-Identifier: GPL-3.0+
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
package com.sbro.emucorex.core

/** Stable GS aspect IDs. Native GSOptions uses these exact persisted names. */
object GSAspectRatioSettings {
    val settingValues: Map<Int, String> = mapOf(
        0 to "Stretch", 1 to "Auto 4:3/3:2", 2 to "4:3", 3 to "16:9", 4 to "10:7",
        5 to "21:9", 6 to "20:9", 7 to "19.5:9", 8 to "Custom"
    )
    fun normalize(value: Int?): Int = value?.takeIf(settingValues::containsKey) ?: 1
}

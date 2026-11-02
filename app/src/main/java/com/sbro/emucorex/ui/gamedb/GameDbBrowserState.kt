package com.sbro.emucorex.ui.gamedb

import com.sbro.emucorex.data.gamedb.GameDbCatalogEntry
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory

/** One GameDB line as the browser shows it, after the user's own edits are applied. */
data class GameDbSettingUi(
    val id: String,
    val category: GameDbSettingCategory,
    val enabled: Boolean,
    val added: Boolean,
    val value: String,
    val valueEdited: Boolean,
    val presentation: GameDbSettingPresentation,
    val valueOptions: List<GameDbValueOption>?
)

data class GameDbEntryUi(
    val entry: GameDbCatalogEntry,
    val settings: List<GameDbSettingUi>,
    val enabledCount: Int,
    val customizedCount: Int,
    val disabledCount: Int,
    val searchableText: String
) {
    val customized: Boolean get() = customizedCount > 0

    val totalCount: Int get() = settings.size

    val coreSettingCount: Int = settings.count { it.category != GameDbSettingCategory.GRAPHICS_FIX }

    val graphicsSettingCount: Int = settings.size - coreSettingCount
}

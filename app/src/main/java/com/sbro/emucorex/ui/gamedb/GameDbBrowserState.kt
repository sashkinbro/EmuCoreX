package com.sbro.emucorex.ui.gamedb

import com.sbro.emucorex.data.gamedb.GameDbCatalogEntry
import com.sbro.emucorex.data.gamedb.GameDbSettingCategory
import com.sbro.emucorex.data.gamedb.section

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

/** A category header together with the lines under it, in the browser's display order. */
data class GameDbSettingSection(
    val category: GameDbSettingCategory,
    val settings: List<GameDbSettingUi>
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

    /**
     * Categories in the same order the GameDB file declares them. A clamp section stays visible
     * even when the game lists no clamp line, because its header owns the "+" that adds the first
     * one; every other empty section stays hidden as before.
     */
    val sections: List<GameDbSettingSection>
        get() {
            val settingsByCategory = settings.groupBy { it.category }
            return GameDbSettingCategory.entries.mapNotNull { category ->
                val categorySettings = settingsByCategory[category].orEmpty()
                val canAddFirstLine = category == GameDbSettingCategory.CLAMP_MODE &&
                    GameDbFixCatalog.supportsAdding(category.section)
                if (categorySettings.isEmpty() && !canAddFirstLine) {
                    null
                } else {
                    GameDbSettingSection(category, categorySettings)
                }
            }
        }
}

package com.sbro.emucorex.ui.cheats

import androidx.annotation.StringRes
import com.sbro.emucorex.R
import com.sbro.emucorex.data.CheatBlock
import java.util.Locale

/** UI grouping for installed cheats, shared by the manager and the in-game menu. */
enum class CheatCategory(@StringRes val titleRes: Int) {
    PLAYER(R.string.cheat_manager_category_player),
    ITEMS(R.string.cheat_manager_category_items),
    WORLD(R.string.cheat_manager_category_world),
    PROGRESS(R.string.cheat_manager_category_progress),
    VEHICLES(R.string.cheat_manager_category_vehicles),
    STATS(R.string.cheat_manager_category_stats),
    HOTKEYS(R.string.cheat_manager_category_hotkeys),
    OTHER(R.string.cheat_manager_category_other)
}

fun CheatBlock.category(): CheatCategory {
    val value = title.lowercase(Locale.US)
    return when {
        value.containsAny(" press ", "press ", "hold ", "button", "{l1}", "{l2}", "{r1}", "{r2}", "{select}") ->
            CheatCategory.HOTKEYS
        value.containsAny("health", "money", "pocket change", "stamina", "energy", "trouble", "wanted", "player", "character") ->
            CheatCategory.PLAYER
        value.containsAny("weapon", "ammo", "inventory", "item", "fire cracker", "spud", "slingshot", "projectile", "outfit", "clothing") ->
            CheatCategory.ITEMS
        value.containsAny("time", "hour", "clock", "weather", "day", "night", "season") ->
            CheatCategory.WORLD
        value.containsAny("mission", "chapter", "unlock", "class", "grade", "complete", "progress", "troph", "collectible") ->
            CheatCategory.PROGRESS
        value.containsAny("vehicle", "bike", "bicycle", "car", "kart", "race", "skateboard", "lawnmower") ->
            CheatCategory.VEHICLES
        value.startsWith("max ") || value.startsWith("no ") ||
            value.containsAny("stat", "record", "times ", "distance", "earned", "spent", "attempted", "hits", "killed", "thrown", "purchased") ->
            CheatCategory.STATS
        else -> CheatCategory.OTHER
    }
}

/** Cheats ordered by [CheatCategory]; categories without cheats are omitted. */
fun groupCheatBlocks(blocks: List<CheatBlock>): List<Pair<CheatCategory, List<CheatBlock>>> =
    CheatCategory.entries.mapNotNull { category ->
        blocks.filter { block -> block.category() == category }
            .takeIf(List<CheatBlock>::isNotEmpty)
            ?.let { category to it }
    }

private fun String.containsAny(vararg needles: String): Boolean = needles.any(::contains)

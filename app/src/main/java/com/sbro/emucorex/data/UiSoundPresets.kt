package com.sbro.emucorex.data

import androidx.annotation.RawRes
import androidx.annotation.StringRes
import com.sbro.emucorex.R

/**
 * A built-in, synthesized sound for one [UiSoundKind]. [raws] holds one resource for click and music
 * and the open / close / select triplet (in that order) for the game menu.
 */
class UiSoundPreset(
    val id: String,
    @StringRes val labelRes: Int,
    @RawRes val raws: IntArray
)

object UiSoundPresets {
    const val DEFAULT_ID = "default"

    private val click = listOf(
        UiSoundPreset(DEFAULT_ID, R.string.ui_sound_preset_default, intArrayOf(R.raw.ui_click)),
        UiSoundPreset("soft", R.string.ui_sound_preset_soft, intArrayOf(R.raw.ui_click_soft)),
        UiSoundPreset("pop", R.string.ui_sound_preset_pop, intArrayOf(R.raw.ui_click_pop)),
        UiSoundPreset("mechanical", R.string.ui_sound_preset_mechanical, intArrayOf(R.raw.ui_click_mechanical)),
        UiSoundPreset("retro", R.string.ui_sound_preset_retro, intArrayOf(R.raw.ui_click_retro)),
        UiSoundPreset("glass", R.string.ui_sound_preset_glass, intArrayOf(R.raw.ui_click_glass))
    )

    private val menu = listOf(
        UiSoundPreset(
            DEFAULT_ID, R.string.ui_sound_preset_classic,
            intArrayOf(R.raw.ui_menu_open, R.raw.ui_menu_close, R.raw.ui_menu_select)
        ),
        UiSoundPreset(
            "retro", R.string.ui_sound_preset_retro,
            intArrayOf(R.raw.ui_menu_retro_open, R.raw.ui_menu_retro_close, R.raw.ui_menu_retro_select)
        ),
        UiSoundPreset(
            "soft", R.string.ui_sound_preset_soft,
            intArrayOf(R.raw.ui_menu_soft_open, R.raw.ui_menu_soft_close, R.raw.ui_menu_soft_select)
        ),
        UiSoundPreset(
            "scifi", R.string.ui_sound_preset_scifi,
            intArrayOf(R.raw.ui_menu_scifi_open, R.raw.ui_menu_scifi_close, R.raw.ui_menu_scifi_select)
        )
    )

    private val music = listOf(
        UiSoundPreset(DEFAULT_ID, R.string.ui_sound_preset_default, intArrayOf(R.raw.ui_music_default)),
        UiSoundPreset("console", R.string.ui_sound_preset_console, intArrayOf(R.raw.ui_music_console)),
        UiSoundPreset("aurora", R.string.ui_sound_preset_aurora, intArrayOf(R.raw.ui_music_aurora)),
        UiSoundPreset("nocturne", R.string.ui_sound_preset_nocturne, intArrayOf(R.raw.ui_music_nocturne)),
        UiSoundPreset("drift", R.string.ui_sound_preset_drift, intArrayOf(R.raw.ui_music_drift))
    )

    fun forKind(kind: UiSoundKind): List<UiSoundPreset> = when (kind) {
        UiSoundKind.BACKGROUND_MUSIC -> music
        UiSoundKind.BUTTON_CLICK -> click
        UiSoundKind.GAME_MENU -> menu
    }

    /** Unknown or stale ids (old backups, removed presets) resolve to the default preset. */
    fun find(kind: UiSoundKind, id: String?): UiSoundPreset =
        forKind(kind).firstOrNull { it.id == id } ?: forKind(kind).first()

    fun sanitizeId(kind: UiSoundKind, id: String?): String = find(kind, id).id
}

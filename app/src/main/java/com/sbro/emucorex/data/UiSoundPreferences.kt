package com.sbro.emucorex.data

/** The three frontend sounds configurable from Settings -> Customization. */
enum class UiSoundKind(val id: String) {
    BACKGROUND_MUSIC("music"),
    BUTTON_CLICK("click"),
    GAME_MENU("menu");

    /** Click-like sounds are short samples (SoundPool); music is streamed and looped. */
    val isShortSample: Boolean get() = this != BACKGROUND_MUSIC

    companion object {
        fun fromId(id: String?): UiSoundKind? = entries.firstOrNull { it.id == id }
    }
}

data class UiSoundSetting(
    val enabled: Boolean = false,
    /** 0..100. */
    val volume: Int = UiSoundSettings.DEFAULT_VOLUME,
    /** True when an imported custom audio file is selected instead of the built-in default. */
    val custom: Boolean = false,
    /** Id of the selected built-in preset (see [UiSoundPresets]); ignored while [custom] is set. */
    val preset: String = UiSoundPresets.DEFAULT_ID,
    /** Display name of the custom file; may be null even when [custom] (shown as a generic label). */
    val customName: String? = null,
    /** Bumped whenever the custom file changes so players can reload it. */
    val revision: Int = 0
) {
    val usesCustom: Boolean get() = custom
}

data class UiSoundSettings(
    val music: UiSoundSetting = UiSoundSetting(),
    val click: UiSoundSetting = UiSoundSetting(),
    val menu: UiSoundSetting = UiSoundSetting()
) {
    operator fun get(kind: UiSoundKind): UiSoundSetting = when (kind) {
        UiSoundKind.BACKGROUND_MUSIC -> music
        UiSoundKind.BUTTON_CLICK -> click
        UiSoundKind.GAME_MENU -> menu
    }

    fun with(kind: UiSoundKind, setting: UiSoundSetting): UiSoundSettings = when (kind) {
        UiSoundKind.BACKGROUND_MUSIC -> copy(music = setting)
        UiSoundKind.BUTTON_CLICK -> copy(click = setting)
        UiSoundKind.GAME_MENU -> copy(menu = setting)
    }

    companion object {
        const val DEFAULT_VOLUME = 60
        const val MIN_VOLUME = 0
        const val MAX_VOLUME = 100
    }
}

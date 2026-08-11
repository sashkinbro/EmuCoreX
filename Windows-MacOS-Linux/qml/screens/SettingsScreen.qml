import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property string currentTab: "general"
    function openGameFolderPicker() { gameFolderDialog.open() }
    readonly property var tabs: [
        { key: "general", label: I18n.get("settings_general_tab") },
        { key: "video", label: I18n.get("settings_graphics_tab") },
        { key: "controls", label: I18n.get("settings_controls_tab") },
        { key: "emulation", label: I18n.get("settings_emulation_tab") },
        { key: "audio", label: I18n.get("settings_audio_tab") },
        { key: "fixes", label: I18n.get("settings_fixes_tab") },
        { key: "library", label: I18n.get("settings_library_tab") },
        { key: "network", label: I18n.get("settings_network_tab") },
        { key: "customization", label: I18n.get("settings_customization_tab") },
        { key: "game-menu", label: I18n.get("settings_game_menu_tab") },
        { key: "updates", label: I18n.get("settings_updates_tab") },
        { key: "about", label: I18n.get("settings_about") }
    ]

    function coverStyleLabel(style) {
        if (style === 0) return I18n.get("settings_cover_art_style_off")
        if (style === 2) return I18n.get("settings_cover_art_style_3d")
        return I18n.get("settings_cover_art_style_flat")
    }

    function languageLabel(code) {
        var suffixes = {
            "en": "english", "uk": "ukrainian", "ru": "russian", "es": "spanish",
            "fr": "french", "de": "german", "pt": "portuguese", "it": "italian",
            "in": "indonesian", "hi": "hindi", "zh": "traditional_chinese", "ar": "arabic",
            "fa": "persian", "ja": "japanese", "ko": "korean", "pl": "polish",
            "cs": "czech", "tr": "turkish"
        }
        var suffix = suffixes[code]
        if (!suffix) return I18n.nativeName(code)
        var localized = I18n.get("settings_language_" + suffix)
        var nativeLabel = I18n.get("settings_language_native_" + suffix)
        return localized === nativeLabel ? localized : localized + "  ·  " + nativeLabel
    }

    function languageOptions() {
        return I18n.availableLanguages.map(function(code) { return root.languageLabel(code) })
    }

    function rows(tab) {
        switch (tab) {
        case "general": return [
            { title: I18n.get("settings_language"), desc: I18n.get("settings_language_screen_subtitle"), icon: "hub", type: "combo", key: "general/language", value: root.languageLabel(Preferences.language), options: root.languageOptions() },
            { title: "Confirm before closing a running game", desc: "Prevent accidental shutdown from keyboard shortcuts or window controls.", icon: "play", type: "switch", key: "general/confirmExit", value: Preferences.value("general/confirmExit", true) },
            { title: "Start with the operating system", desc: "Launch EmuCoreX in the background after sign-in.", icon: "refresh", type: "switch", key: "general/autostart", value: Preferences.value("general/autostart", false) },
            { title: "Discord Rich Presence", desc: "Show the current game and session status in Discord.", icon: "chat", type: "switch", key: "general/discord", value: Preferences.value("general/discord", true) }
        ]
        case "video": return [
            { title: I18n.get("settings_renderer"), desc: I18n.get("settings_help_renderer"), icon: "chip", type: "combo", key: "video/renderer", value: Preferences.value("video/renderer", I18n.get("settings_renderer_auto")), options: [I18n.get("settings_renderer_auto"), I18n.get("settings_renderer_vulkan"), I18n.get("settings_renderer_d3d12"), I18n.get("settings_renderer_opengl"), I18n.get("settings_renderer_software")] },
            { title: I18n.get("settings_upscale"), desc: I18n.get("settings_help_upscale"), icon: "image", type: "combo", key: "video/resolution", value: Preferences.value("video/resolution", I18n.get("settings_upscale_3x")), options: [I18n.get("settings_upscale_native"), I18n.get("settings_upscale_2x"), I18n.get("settings_upscale_3x"), I18n.get("settings_upscale_4x"), I18n.get("settings_upscale_6x")] },
            { title: I18n.get("settings_aspect_ratio"), desc: I18n.get("settings_help_aspect_ratio"), icon: "image", type: "combo", key: "video/aspect", value: Preferences.value("video/aspect", I18n.get("settings_aspect_ratio_auto")), options: [I18n.get("settings_aspect_ratio_auto"), I18n.get("settings_aspect_ratio_43"), I18n.get("settings_aspect_ratio_169"), I18n.get("settings_aspect_ratio_107")] },
            { title: I18n.get("settings_vsync"), desc: I18n.get("settings_vsync_desc"), icon: "refresh", type: "switch", key: "video/vsync", value: Preferences.value("video/vsync", true) },
            { title: I18n.get("settings_fxaa"), desc: I18n.get("settings_fxaa_desc"), icon: "image", type: "switch", key: "video/fxaa", value: Preferences.value("video/fxaa", false) },
            { title: I18n.get("settings_anisotropic_filtering"), desc: I18n.get("settings_help_anisotropic_filtering"), icon: "tune", type: "combo", key: "video/aniso", value: Preferences.value("video/aniso", "8x"), options: [I18n.get("common_off"), "2x", "4x", "8x", "16x"] }
        ]
        case "controls": return [
            { title: "Controller port 1", desc: "Automatic SDL controller mapping and device selection.", icon: "play", type: "combo", key: "controls/port1", value: Preferences.value("controls/port1", "Automatic"), options: ["Automatic", "Keyboard", "Disconnected"] },
            { title: "Controller vibration", desc: "Enable rumble on supported SDL gamepads.", icon: "refresh", type: "switch", key: "controls/vibration", value: Preferences.value("controls/vibration", true) },
            { title: "Gamepad UI navigation", desc: "Navigate every desktop screen without a mouse.", icon: "library", type: "switch", key: "controls/uiGamepad", value: Preferences.value("controls/uiGamepad", true) },
            { title: "Hotkeys", desc: "Configure pause, fullscreen, save-state and speed controls.", icon: "tune", type: "action", key: "controls/hotkeys", action: "Configure" }
        ]
        case "emulation": return [
            { title: "EE recompiler", desc: "Dynamic recompilation for the Emotion Engine CPU.", icon: "chip", type: "switch", key: "emu/ee", value: Preferences.value("emu/ee", true) },
            { title: "IOP recompiler", desc: "Dynamic recompilation for the PS2 I/O processor.", icon: "chip", type: "switch", key: "emu/iop", value: Preferences.value("emu/iop", true) },
            { title: "VU recompilers", desc: "Use the architecture-specific vector-unit recompilers.", icon: "chip", type: "switch", key: "emu/vu", value: Preferences.value("emu/vu", true) },
            { title: "Fast boot", desc: "Skip the BIOS intro when starting a game.", icon: "play", type: "switch", key: "emu/fastBoot", value: Preferences.value("emu/fastBoot", true) },
            { title: "Enable cheats", desc: "Load active PNACH patches for the current game.", icon: "code", type: "switch", key: "emu/cheats", value: Preferences.value("emu/cheats", true) },
            { title: "Enable host filesystem", desc: "Allow homebrew software to access configured host folders.", icon: "folder", type: "switch", key: "emu/hostFs", value: Preferences.value("emu/hostFs", false) }
        ]
        case "audio": return [
            { title: I18n.get("settings_audio_backend"), desc: "Native desktop audio output backend.", icon: "play", type: "combo", key: "audio/backend", value: Preferences.value("audio/backend", "Automatic"), options: ["Automatic", "Cubeb", "SDL", "Null"] },
            { title: I18n.get("settings_audio_sync_mode"), desc: "Keep audio synchronized when emulation speed changes.", icon: "refresh", type: "combo", key: "audio/sync", value: Preferences.value("audio/sync", "TimeStretch"), options: ["TimeStretch", "Async Mix", "None"] },
            { title: I18n.get("settings_audio_mute"), desc: I18n.get("settings_audio_mute_desc"), icon: "close", type: "switch", key: "audio/mute", value: Preferences.value("audio/mute", false) },
            { title: I18n.get("settings_audio_minimal_latency"), desc: I18n.get("settings_audio_minimal_latency_desc"), icon: "tune", type: "switch", key: "audio/minLatency", value: Preferences.value("audio/minLatency", false) }
        ]
        case "fixes": return [
            { title: "Automatic game fixes", desc: "Apply PCSX2 GameIndex fixes for the detected serial.", icon: "tune", type: "switch", key: "fixes/auto", value: Preferences.value("fixes/auto", true) },
            { title: "Widescreen patches", desc: "Load bundled widescreen patches when available.", icon: "image", type: "switch", key: "fixes/widescreen", value: Preferences.value("fixes/widescreen", false) },
            { title: "No-interlacing patches", desc: "Prefer progressive output for supported games.", icon: "image", type: "switch", key: "fixes/noInterlace", value: Preferences.value("fixes/noInterlace", false) },
            { title: "Manual game fixes", desc: "Advanced compatibility overrides for troubleshooting.", icon: "settings", type: "action", key: "fixes/manual", action: "Open" }
        ]
        case "library": return [
            { title: I18n.get("settings_game_path"), desc: I18n.get("settings_game_path_desc"), icon: "folder", type: "action", key: "library/folders", action: I18n.get("home_add_folder") },
            { title: I18n.get("settings_cover_art_style"), desc: I18n.get("settings_help_cover_art_style"), icon: "image", type: "combo", key: "library/coverStyle", value: root.coverStyleLabel(Preferences.coverArtStyle), options: [I18n.get("settings_cover_art_style_off"), I18n.get("settings_cover_art_style_flat"), I18n.get("settings_cover_art_style_3d")] },
            { title: I18n.get("settings_prefer_english_game_titles"), desc: I18n.get("settings_prefer_english_game_titles_desc"), icon: "hub", type: "switch", key: "library/englishTitles", value: Preferences.value("library/englishTitles", false) },
            { title: I18n.get("settings_clear_cover_cache"), desc: I18n.get("settings_clear_cover_cache_desc"), icon: "refresh", type: "action", key: "library/clearCovers", action: I18n.get("settings_clear_cover_cache_action") },
            { title: I18n.get("home_refresh"), desc: I18n.get("home_library_desc"), icon: "refresh", type: "action", key: "library/refresh", action: I18n.get("home_refresh") }
        ]
        case "network": return [
            { title: I18n.get("settings_network_enable"), desc: I18n.get("settings_network_enable_desc"), icon: "hub", type: "switch", key: "network/enabled", value: Preferences.value("network/enabled", false) },
            { title: I18n.get("settings_network_api"), desc: "DEV9 Ethernet implementation.", icon: "tune", type: "combo", key: "network/api", value: Preferences.value("network/api", "Sockets"), options: ["Sockets"] },
            { title: I18n.get("settings_network_dns_preset"), desc: I18n.get("settings_network_dns_preset_help"), icon: "hub", type: "combo", key: "network/dns", value: Preferences.value("network/dns", "System / automatic"), options: ["System / automatic", "PS2 Online", "PSRewired"] },
            { title: I18n.get("settings_network_log_dns"), desc: I18n.get("settings_network_log_dns_desc"), icon: "file", type: "switch", key: "network/logDns", value: Preferences.value("network/logDns", false) }
        ]
        case "customization": return [
            { title: I18n.get("settings_theme"), desc: "Use the dark, light or operating-system appearance.", icon: "palette", type: "combo", key: "appearance/theme", value: Preferences.themeMode, options: ["dark", "light", "system"] },
            { title: "Accent color", desc: "Choose the primary highlight used across EmuCoreX.", icon: "palette", type: "combo", key: "appearance/accent", value: Preferences.accentColor, options: ["#8B5CF6", "#7C3AED", "#2563EB", "#0891B2", "#059669", "#DC2626"] },
            { title: I18n.get("settings_customization_grid_size"), desc: I18n.get("settings_customization_grid_size_help"), icon: "library", type: "combo", key: "appearance/grid", value: Preferences.value("appearance/grid", "Medium"), options: ["Small", "Medium", "Large"] },
            { title: I18n.get("settings_customization_drawer_style"), desc: I18n.get("settings_customization_drawer_summary"), icon: "menu", type: "combo", key: "appearance/sidebar", value: Preferences.compactSidebar ? "Compact" : "Expanded", options: ["Expanded", "Compact"] },
            { title: I18n.get("settings_customization_font"), desc: I18n.get("settings_customization_font_help"), icon: "file", type: "combo", key: "appearance/font", value: Preferences.value("appearance/font", "Rubik"), options: ["Rubik", "Exo 2", "System default", "Custom font"] },
            { title: I18n.get("settings_customization_background"), desc: I18n.get("settings_customization_background_help"), icon: "image", type: "action", key: "appearance/background", action: "Choose file" },
            { title: I18n.get("settings_customization_reset"), desc: I18n.get("settings_customization_reset_desc"), icon: "refresh", type: "action", key: "appearance/reset", action: "Reset" }
        ]
        case "game-menu": return [
            { title: I18n.get("settings_game_menu_layout_section"), desc: I18n.get("settings_game_menu_layout_help"), icon: "menu", type: "combo", key: "gameMenu/layout", value: Preferences.value("gameMenu/layout", "Dashboard"), options: ["Sidebar", "Dashboard", "Command center", "Compact"] },
            { title: I18n.get("settings_game_menu_section_save_states"), desc: "Show save slots in the in-game overlay.", icon: "save", type: "switch", key: "gameMenu/saves", value: Preferences.value("gameMenu/saves", true) },
            { title: I18n.get("settings_game_menu_section_game_profile"), desc: "Expose per-game settings during a session.", icon: "tune", type: "switch", key: "gameMenu/profile", value: Preferences.value("gameMenu/profile", true) },
            { title: I18n.get("settings_game_menu_section_debug_tools"), desc: "Show performance and developer diagnostics.", icon: "code", type: "switch", key: "gameMenu/debug", value: Preferences.value("gameMenu/debug", false) }
        ]
        case "updates": return [
            { title: "Automatic update checks", desc: I18n.get("settings_updates_source_body"), icon: "refresh", type: "switch", key: "updates/auto", value: Preferences.value("updates/auto", true) },
            { title: "Update channel", desc: "Choose stable or preview desktop releases.", icon: "hub", type: "combo", key: "updates/channel", value: Preferences.value("updates/channel", "Stable"), options: ["Stable", "Preview"] },
            { title: I18n.get("settings_updates_history_title"), desc: I18n.get("settings_updates_history_body"), icon: "file", type: "action", key: "updates/history", action: "Open" }
        ]
        case "about": return [
            { title: I18n.get("settings_about_app"), desc: I18n.get("settings_about_app_desc"), icon: "library", type: "action", key: "about/app", action: App.buildDescription },
            { title: I18n.get("settings_about_website"), desc: I18n.get("settings_about_website_desc"), icon: "hub", type: "action", key: "about/website", action: I18n.get("settings_about_website_link") },
            { title: I18n.get("settings_about_app_source"), desc: I18n.get("settings_about_app_source_desc"), icon: "code", type: "action", key: "about/source", action: I18n.get("settings_about_app_source_link") },
            { title: I18n.get("settings_about_core_source"), desc: I18n.get("settings_about_core_source_desc"), icon: "chip", type: "action", key: "about/core", action: I18n.get("settings_about_core_source_link") }
        ]
        }
        return []
    }

    FolderDialog {
        id: gameFolderDialog
        title: I18n.get("settings_game_path")
        onAccepted: GameLibrary.addFolder(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        PageHeader {
            Layout.fillWidth: true
            Layout.leftMargin: 26
            Layout.rightMargin: 26
            Layout.topMargin: 26
            title: I18n.get("settings_title")
            subtitle: ""
        }

        Flickable {
            id: tabFlick
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            clip: true
            contentWidth: tabsRow.width + 52
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            function centerTab(item) {
                var centered = tabsRow.x + item.x + item.width / 2 - width / 2
                contentX = Math.max(0, Math.min(contentWidth - width, centered))
            }

            Behavior on contentX {
                enabled: !tabFlick.dragging && !tabFlick.flicking
                NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
            }

            Row {
                id: tabsRow
                x: 26
                spacing: 7
                Repeater {
                    model: root.tabs
                    AppButton {
                        text: modelData.label
                        primary: root.currentTab === modelData.key
                        onClicked: {
                            root.currentTab = modelData.key
                            tabFlick.centerTab(this)
                        }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 26
            Layout.rightMargin: 26
            Layout.bottomMargin: 20
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                    width: Math.max(0, parent.width - 12)
                    spacing: 10
                    Repeater {
                        model: root.rows(root.currentTab)
                        SettingRow {
                            Layout.fillWidth: true
                            title: modelData.title
                            description: modelData.desc || ""
                            iconName: modelData.icon || "settings"
                            controlType: modelData.type
                            checked: Boolean(modelData.value)
                            options: modelData.options || []
                            currentValue: modelData.type === "action" ? modelData.action : String(modelData.value)
                            onToggled: Preferences.setValue(modelData.key, value)
                            onValueSelected: function(value) {
                                if (modelData.key === "general/language") {
                                    var languageIndex = root.languageOptions().indexOf(value)
                                    if (languageIndex >= 0) Preferences.language = I18n.availableLanguages[languageIndex]
                                }
                                else if (modelData.key === "appearance/theme") Preferences.themeMode = value
                                else if (modelData.key === "appearance/accent") Preferences.accentColor = value
                                else if (modelData.key === "appearance/sidebar") Preferences.compactSidebar = value === "Compact"
                                else if (modelData.key === "library/coverStyle") {
                                    Preferences.coverArtStyle = value === I18n.get("settings_cover_art_style_off") ? 0 : (value === I18n.get("settings_cover_art_style_3d") ? 2 : 1)
                                }
                                else Preferences.setValue(modelData.key, value)
                            }
                            onActionTriggered: {
                                if (modelData.key === "library/refresh") GameLibrary.refresh()
                                else if (modelData.key === "library/folders") root.openGameFolderPicker()
                                else if (modelData.key === "appearance/reset") Preferences.resetDesktopPreferences()
                                else if (modelData.key === "about/website") App.openExternalUrl(I18n.get("settings_about_website_url"))
                                else if (modelData.key === "about/source") App.openExternalUrl(I18n.get("settings_about_app_source_url"))
                                else if (modelData.key === "about/core") App.openExternalUrl(I18n.get("settings_about_core_source_url"))
                            }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.minimumHeight: 12 }
            }
        }
    }
}

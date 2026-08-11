import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property string currentTab: "general"
    property string pendingTab: "general"

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

    function t(key) { return I18n.get(key) }
    function toggle(title, description, icon, key, fallback) {
        return { title: t(title), desc: t(description), icon: icon, type: "switch", key: key,
            value: Preferences.value(key, fallback) }
    }
    function choice(title, description, icon, key, fallback, options) {
        return { title: t(title), desc: t(description), icon: icon, type: "combo", key: key,
            value: Preferences.value(key, fallback), options: options }
    }
    function range(title, description, icon, key, fallback, from, to, step, suffix) {
        return { title: t(title), desc: t(description), icon: icon, type: "slider", key: key,
            value: Number(Preferences.value(key, fallback)), from: from, to: to, step: step, suffix: suffix || "" }
    }
    function action(title, description, icon, key, label) {
        return { title: t(title), desc: t(description), icon: icon, type: "action", key: key, action: t(label) }
    }

    function languageLabel(code) {
        const suffixes = {
            "en": "english", "uk": "ukrainian", "ru": "russian", "es": "spanish",
            "fr": "french", "de": "german", "pt": "portuguese", "it": "italian",
            "in": "indonesian", "hi": "hindi", "zh": "traditional_chinese", "ar": "arabic",
            "fa": "persian", "ja": "japanese", "ko": "korean", "pl": "polish",
            "cs": "czech", "tr": "turkish"
        }
        const suffix = suffixes[code]
        if (!suffix) return I18n.nativeName(code)
        const localized = t("settings_language_" + suffix)
        const nativeLabel = t("settings_language_native_" + suffix)
        return localized === nativeLabel ? localized : localized + "  ·  " + nativeLabel
    }

    function languageOptions() {
        return I18n.availableLanguages.map(function(code) { return root.languageLabel(code) })
    }

    function coverStyleLabel(style) {
        if (style === 0) return t("settings_cover_art_style_off")
        if (style === 2) return t("settings_cover_art_style_3d")
        return t("settings_cover_art_style_flat")
    }

    function fontLabel(fontId) {
        if (fontId === "system") return t("settings_customization_font_system")
        if (fontId === "exo2") return t("settings_customization_font_exo2")
        return t("settings_customization_font_rubik")
    }

    function rows(tab) {
        switch (tab) {
        case "general":
            return [
                { title: t("settings_language"), desc: t("settings_language_screen_subtitle"), icon: "hub",
                    type: "combo", key: "general/language", value: languageLabel(Preferences.language), options: languageOptions() },
                toggle("settings_confirm_save_load_actions", "settings_help_confirm_save_load_actions", "save", "general/confirmSaveLoad", true),
                toggle("settings_show_recent_games", "settings_help_recent_games", "refresh", "general/showRecent", true),
                toggle("settings_show_home_search", "settings_help_home_search", "search", "general/showSearch", true),
                toggle("settings_prefer_english_game_titles", "settings_help_prefer_english_game_titles", "hub", "library/preferEnglish", true)
            ]
        case "video":
            return [
                choice("settings_renderer", "settings_help_renderer", "chip", "video/renderer", t("settings_renderer_auto"),
                    [t("settings_renderer_auto"), t("settings_renderer_vulkan"), t("settings_renderer_d3d12"), t("settings_renderer_d3d11"), t("settings_renderer_opengl"), t("settings_renderer_software")]),
                choice("settings_upscale", "settings_help_upscale", "image", "video/upscale", t("settings_upscale_3x"),
                    [t("settings_upscale_native"), t("settings_upscale_2x"), t("settings_upscale_3x"), t("settings_upscale_4x"), t("settings_upscale_5x"), t("settings_upscale_6x")]),
                choice("settings_aspect_ratio", "settings_help_aspect_ratio", "image", "video/aspect", t("settings_aspect_ratio_auto"),
                    [t("settings_aspect_ratio_auto"), t("settings_aspect_ratio_43"), t("settings_aspect_ratio_169"), t("settings_aspect_ratio_107")]),
                choice("settings_bilinear_filtering", "settings_help_bilinear_filtering", "image", "video/bilinear", t("settings_bilinear_filtering_ps2"),
                    [t("settings_bilinear_filtering_nearest"), t("settings_bilinear_filtering_ps2"), t("settings_bilinear_filtering_forced"), t("settings_bilinear_filtering_no_sprite")]),
                choice("settings_trilinear_filtering", "settings_help_trilinear_filtering", "image", "video/trilinear", t("settings_trilinear_filtering_auto"),
                    [t("settings_trilinear_filtering_auto"), t("settings_trilinear_filtering_off"), t("settings_trilinear_filtering_ps2"), t("settings_trilinear_filtering_forced")]),
                choice("settings_blending_accuracy", "settings_help_blending_accuracy", "tune", "video/blending", t("settings_blending_accuracy_basic"),
                    [t("settings_blending_accuracy_minimum"), t("settings_blending_accuracy_basic"), t("settings_blending_accuracy_medium"), t("settings_blending_accuracy_high"), t("settings_blending_accuracy_full"), t("settings_blending_accuracy_maximum")]),
                choice("settings_anisotropic_filtering", "settings_help_anisotropic_filtering", "tune", "video/aniso", "8x",
                    [t("settings_dithering_off"), "2x", "4x", "8x", "16x"]),
                toggle("settings_fxaa", "settings_help_fxaa", "image", "video/fxaa", false),
                choice("settings_cas", "settings_help_cas", "image", "video/cas", t("settings_cas_mode_off"),
                    [t("settings_cas_mode_off"), t("settings_cas_mode_sharpen_only"), t("settings_cas_mode_sharpen_resize")]),
                choice("settings_texture_preloading", "settings_help_texture_preloading", "image", "video/preloading", t("settings_texture_preloading_partial"),
                    [t("settings_texture_preloading_none"), t("settings_texture_preloading_partial"), t("settings_texture_preloading_full")]),
                toggle("settings_hw_mipmapping", "settings_help_hw_mipmapping", "image", "video/mipmapping", true)
            ]
        case "controls":
            return [
                action("settings_gamepad_mapping_title", "settings_gamepad_mapping_disconnected", "play", "controls/mapping", "settings_gamepad_mapping_auto_format"),
                toggle("settings_pad_vibration", "settings_help_pad_vibration", "play", "controls/vibration", true),
                range("settings_pad_vibration_strength", "settings_help_pad_vibration", "tune", "controls/vibrationStrength", 100, 0, 150, 5, "%"),
                range("settings_gamepad_stick_deadzone", "settings_help_gamepad_stick_deadzone", "tune", "controls/deadzone", 10, 0, 40, 1, "%"),
                range("settings_gamepad_left_stick_sensitivity", "settings_help_gamepad_left_stick_sensitivity", "tune", "controls/leftSensitivity", 100, 50, 200, 5, "%"),
                range("settings_gamepad_right_stick_sensitivity", "settings_help_gamepad_right_stick_sensitivity", "tune", "controls/rightSensitivity", 100, 50, 200, 5, "%")
            ]
        case "emulation":
            return [
                toggle("settings_show_fps", "settings_show_fps_desc", "file", "emulation/showFps", false),
                choice("settings_fps_overlay_mode", "settings_fps_overlay_metrics", "file", "emulation/fpsMode", t("settings_fps_overlay_mode_simple"),
                    [t("settings_fps_overlay_mode_simple"), t("settings_fps_overlay_mode_detailed")]),
                choice("settings_fps_overlay_position", "settings_fps_overlay_metrics", "file", "emulation/fpsPosition", t("settings_fps_overlay_corner_top_right"),
                    [t("settings_fps_overlay_corner_top_left"), t("settings_fps_overlay_corner_top_right"), t("settings_fps_overlay_corner_bottom_left"), t("settings_fps_overlay_corner_bottom_right")]),
                toggle("settings_enable_ee_recompiler", "settings_help_enable_ee_recompiler", "chip", "emulation/eeRecompiler", true),
                toggle("settings_enable_iop_recompiler", "settings_help_enable_iop_recompiler", "chip", "emulation/iopRecompiler", true),
                toggle("settings_enable_vu0_recompiler", "settings_help_enable_vu0_recompiler", "chip", "emulation/vu0Recompiler", true),
                toggle("settings_enable_vu1_recompiler", "settings_help_enable_vu1_recompiler", "chip", "emulation/vu1Recompiler", true),
                toggle("settings_enable_fastmem", "settings_help_enable_fastmem", "chip", "emulation/fastmem", true),
                toggle("settings_game_fixes", "settings_game_fixes_desc", "tune", "emulation/gameFixes", true),
                toggle("settings_frame_limiter", "settings_help_frame_limiter", "refresh", "emulation/frameLimiter", true),
                choice("settings_fast_forward_speed", "settings_help_fast_forward_speed", "play", "emulation/fastForward", "2x", ["1.5x", "2x", "3x", "4x", "5x"]),
                range("settings_target_fps", "settings_help_target_fps", "refresh", "emulation/targetFps", 60, 30, 240, 1, " FPS"),
                toggle("settings_mtvu", "settings_help_mtvu", "chip", "emulation/mtvu", true),
                toggle("settings_fast_cdvd", "settings_help_fast_cdvd", "play", "emulation/fastCdvd", false),
                toggle("settings_enable_cheats", "settings_help_cheats", "code", "emulation/cheats", true)
            ]
        case "audio":
            return [
                range("settings_audio_volume", "settings_help_audio_volume", "play", "audio/volume", 100, 0, 100, 1, "%"),
                range("settings_audio_fast_forward_volume", "settings_help_audio_fast_forward_volume", "play", "audio/fastForwardVolume", 100, 0, 100, 1, "%"),
                toggle("settings_audio_mute", "settings_audio_mute_desc", "close", "audio/mute", false),
                choice("settings_audio_interpolation", "settings_help_audio_interpolation", "tune", "audio/interpolation", t("settings_audio_interpolation_gaussian"),
                    [t("settings_audio_interpolation_nearest"), t("settings_audio_interpolation_linear"), t("settings_audio_interpolation_gaussian"), t("settings_audio_interpolation_cubic")]),
                choice("settings_audio_sync_mode", "settings_help_audio_sync_mode", "refresh", "audio/sync", t("settings_audio_sync_time_stretch"),
                    [t("settings_audio_sync_time_stretch"), t("settings_audio_sync_disabled")]),
                range("settings_audio_buffer_size", "settings_help_audio_buffer_size", "tune", "audio/buffer", 100, 20, 300, 5, " ms"),
                toggle("settings_audio_minimal_latency", "settings_audio_minimal_latency_desc", "tune", "audio/minimalLatency", false),
                range("settings_audio_output_latency", "settings_help_audio_output_latency", "tune", "audio/outputLatency", 40, 10, 200, 5, " ms")
            ]
        case "fixes":
            return [
                toggle("settings_widescreen_patches", "settings_help_widescreen_patches", "image", "fixes/widescreen", false),
                toggle("settings_no_interlacing_patches", "settings_help_no_interlacing_patches", "image", "fixes/noInterlacing", false),
                choice("settings_deinterlacing", "settings_help_deinterlacing", "image", "fixes/deinterlacing", t("settings_deinterlacing_automatic"),
                    [t("settings_deinterlacing_automatic"), t("settings_deinterlacing_off"), t("settings_deinterlacing_adaptive_tff"), t("settings_deinterlacing_bob_tff"), t("settings_deinterlacing_blend_tff")]),
                choice("settings_dithering", "settings_help_dithering", "image", "fixes/dithering", t("settings_dithering_unscaled"),
                    [t("settings_dithering_off"), t("settings_dithering_scaled"), t("settings_dithering_unscaled"), t("settings_dithering_force_32bit")]),
                toggle("settings_anti_blur", "settings_help_anti_blur", "image", "fixes/antiBlur", true),
                choice("settings_bilinear_upscale", "settings_help_bilinear_upscale", "image", "fixes/bilinearUpscale", t("settings_bilinear_upscale_force_bilinear"),
                    [t("settings_bilinear_upscale_force_bilinear"), t("settings_bilinear_upscale_force_nearest")])
            ]
        case "library":
            return [
                action("settings_bios_path", "settings_help_bios_path", "chip", "library/bios", "settings_bios_path"),
                action("settings_game_path", "settings_help_game_path", "folder", "library/folders", "home_add_folder"),
                action("emulator_data_location_title", "emulator_data_location_description", "folder", "library/data", "emulator_data_location_title"),
                action("settings_memory_cards_tab", "settings_memory_cards_open_desc", "card", "library/memoryCards", "settings_memory_cards_open"),
                toggle("settings_library_click_details_title", "settings_library_click_details_desc", "library", "library/showDetailsOnClick", true),
                { title: t("settings_cover_art_style"), desc: t("settings_help_cover_art_style"), icon: "image", type: "combo",
                    key: "library/coverStyle", value: coverStyleLabel(Preferences.coverArtStyle),
                    options: [t("settings_cover_art_style_off"), t("settings_cover_art_style_flat"), t("settings_cover_art_style_3d")] },
                action("settings_clear_cover_cache", "settings_clear_cover_cache_desc", "refresh", "library/clearCovers", "settings_clear_cover_cache_action"),
                action("settings_backup_export_title", "settings_backup_export_desc", "save", "library/export", "settings_backup_export_action"),
                action("settings_backup_restore_title", "settings_backup_restore_desc", "refresh", "library/restore", "settings_backup_restore_title")
            ]
        case "network":
            return [
                toggle("settings_network_enable", "settings_network_summary", "hub", "network/enabled", false),
                choice("settings_network_mode", "settings_network_summary", "hub", "network/mode", t("settings_network_mode_online"),
                    [t("settings_network_mode_online"), t("settings_network_mode_local_host"), t("settings_network_mode_local_join")]),
                choice("settings_network_api", "settings_network_summary", "tune", "network/api", t("settings_network_api_sockets"), [t("settings_network_api_sockets")]),
                choice("settings_network_dns_preset", "settings_network_dns_preset_help", "hub", "network/dnsPreset", t("settings_network_dns_preset_system"),
                    [t("settings_network_dns_preset_system"), t("settings_network_dns_preset_ps2online"), t("settings_network_dns_preset_psrewired")]),
                toggle("settings_network_intercept_dhcp", "settings_network_intercept_dhcp_desc", "hub", "network/interceptDhcp", true),
                toggle("settings_network_log_dhcp", "settings_network_log_dhcp_desc", "file", "network/logDhcp", false),
                toggle("settings_network_log_dns", "settings_network_log_dns_desc", "file", "network/logDns", false)
            ]
        case "customization":
            return [
                choice("settings_theme", "theme_manager_preview_body", "palette", "appearance/theme", Preferences.themeMode,
                    [t("settings_theme_dark"), t("settings_theme_light"), t("settings_theme_system")]),
                { title: t("theme_manager_color_primary"), desc: t("theme_manager_usage_primary"), icon: "palette", type: "colors",
                    key: "appearance/accent", value: Preferences.accentColor,
                    options: ["#C4203A", "#2F66BE", "#8669D9", "#168A8A", "#2E8B57", "#B17B24"] },
                action("settings_customization_background", "settings_customization_background_help", "image", "appearance/background", "settings_customization_background"),
                range("settings_customization_background_dim", "settings_customization_background_help", "image", "appearance/backgroundDim", Preferences.backgroundDim, 0, 85, 5, "%"),
                action("settings_customization_remove_background", "settings_customization_remove_background_desc", "close", "appearance/removeBackground", "settings_customization_remove_background"),
                range("settings_customization_grid_size", "settings_customization_grid_size_help", "library", "appearance/gridScale", Preferences.gridScale * 100, 65, 155, 5, "%"),
                choice("settings_customization_drawer_style", "settings_customization_drawer_summary", "menu", "appearance/drawerStyle", t("settings_drawer_style_classic"),
                    [t("settings_drawer_style_classic"), t("settings_drawer_style_compact")]),
                choice("settings_customization_font", "settings_customization_font_help", "file", "appearance/font",
                    fontLabel(Preferences.value("appearance/font", "rubik")),
                    [t("settings_customization_font_system"), t("settings_customization_font_rubik"), t("settings_customization_font_exo2")]),
                range("settings_customization_font_size", "settings_customization_font_help", "file", "appearance/fontScale", Preferences.fontScale * 100, 85, 130, 5, "%"),
                action("settings_customization_reset", "settings_customization_reset_desc", "refresh", "appearance/reset", "settings_customization_reset")
            ]
        case "game-menu":
            return [
                choice("settings_game_menu_layout_section", "settings_game_menu_layout_help", "menu", "gameMenu/layout", t("settings_game_menu_layout_dashboard"),
                    [t("settings_game_menu_layout_sidebar"), t("settings_game_menu_layout_dashboard"), t("settings_game_menu_layout_command_center"), t("settings_game_menu_layout_compact")]),
                toggle("settings_game_menu_section_save_states", "settings_game_menu_content_summary", "save", "gameMenu/saveStates", true),
                toggle("settings_game_menu_section_auto_save", "settings_game_menu_content_summary", "save", "gameMenu/autoSave", true),
                toggle("settings_game_menu_section_quick_actions", "settings_game_menu_content_summary", "play", "gameMenu/quickActions", true),
                toggle("settings_game_menu_section_automation", "settings_game_menu_content_summary", "refresh", "gameMenu/automation", true),
                toggle("settings_game_menu_section_game_profile", "settings_game_menu_content_summary", "tune", "gameMenu/gameProfile", true),
                toggle("settings_game_menu_section_debug_tools", "settings_game_menu_content_summary", "code", "gameMenu/debug", false),
                action("settings_game_menu_reset", "settings_game_menu_reset_desc", "refresh", "gameMenu/reset", "settings_game_menu_reset")
            ]
        case "updates":
            return [
                action("settings_updates_history_title", "settings_updates_history_body", "refresh", "updates/history", "settings_updates_open_release"),
                action("settings_updates_version_label", "settings_updates_source_body", "file", "updates/version", "settings_updates_version_label")
            ]
        case "about":
            return [
                action("settings_about_app", "onboarding_page_1_subtitle", "library", "about/app", "settings_about_app"),
                action("settings_about_studio", "settings_about_studio_desc", "profile", "about/studio", "settings_about_studio"),
                action("settings_about_website", "settings_about_website_desc", "hub", "about/website", "settings_about_website_link"),
                action("settings_about_privacy_policy", "settings_about_privacy_policy_desc", "file", "about/privacy", "settings_about_privacy_policy_link"),
                action("settings_about_app_source", "settings_about_app_source_desc", "code", "about/source", "settings_about_app_source_link"),
                action("settings_about_core_source", "settings_about_core_source_link", "chip", "about/core", "settings_about_core_source_link")
            ]
        }
        return []
    }

    function activateTab(key, item) {
        tabFlick.centerTab(item)
        if (currentTab === key) return
        pendingTab = key
        tabTransition.restart()
    }

    function applyChoice(key, value) {
        if (key === "general/language") {
            const index = languageOptions().indexOf(value)
            if (index >= 0) Preferences.language = I18n.availableLanguages[index]
        } else if (key === "appearance/theme") {
            if (value === t("settings_theme_light")) Preferences.themeMode = "light"
            else if (value === t("settings_theme_system")) Preferences.themeMode = "system"
            else Preferences.themeMode = "dark"
        } else if (key === "appearance/accent") {
            Preferences.accentColor = value
        } else if (key === "library/coverStyle") {
            Preferences.coverArtStyle = value === t("settings_cover_art_style_off") ? 0
                : (value === t("settings_cover_art_style_3d") ? 2 : 1)
        } else if (key === "appearance/drawerStyle") {
            Preferences.compactSidebar = value === t("settings_drawer_style_compact")
            Preferences.setValue(key, value)
        } else if (key === "appearance/font") {
            Preferences.setValue(key, value === t("settings_customization_font_system") ? "system"
                : (value === t("settings_customization_font_exo2") ? "exo2" : "rubik"))
        } else {
            Preferences.setValue(key, value)
        }
    }

    function applyNumber(key, value) {
        if (key === "appearance/gridScale") Preferences.gridScale = value / 100
        else if (key === "appearance/fontScale") Preferences.fontScale = value / 100
        else if (key === "appearance/backgroundDim") Preferences.backgroundDim = value
        else Preferences.setValue(key, value)
    }

    function handleAction(key) {
        if (key === "library/bios") biosDialog.open()
        else if (key === "library/folders") gameFolderDialog.open()
        else if (key === "library/data") dataFolderDialog.open()
        else if (key === "library/memoryCards") App.replaceRoute("memory-cards")
        else if (key === "library/clearCovers") {
            GameLibrary.invalidateCovers()
            CoverArt.invalidate()
        }
        else if (key === "controls/mapping") App.replaceRoute("gamepad-mapping")
        else if (key === "appearance/background") backgroundDialog.open()
        else if (key === "appearance/removeBackground") Preferences.backgroundPath = ""
        else if (key === "appearance/reset") Preferences.resetDesktopPreferences()
        else if (key === "gameMenu/reset") {
            Preferences.setValue("gameMenu/layout", t("settings_game_menu_layout_dashboard"))
            Preferences.setValue("gameMenu/saveStates", true)
            Preferences.setValue("gameMenu/autoSave", true)
            Preferences.setValue("gameMenu/quickActions", true)
            Preferences.setValue("gameMenu/automation", true)
            Preferences.setValue("gameMenu/gameProfile", true)
            Preferences.setValue("gameMenu/debug", false)
        } else if (key === "updates/history") App.openExternalUrl(t("settings_about_app_source_url") + "/releases")
        else if (key === "about/website") App.openExternalUrl(t("settings_about_website_url"))
        else if (key === "about/privacy") App.openExternalUrl(t("settings_about_privacy_policy_url"))
        else if (key === "about/source") App.openExternalUrl(t("settings_about_app_source_url"))
        else if (key === "about/core") App.openExternalUrl(t("settings_about_core_source_url"))
    }

    FolderDialog {
        id: gameFolderDialog
        title: t("settings_game_path")
        onAccepted: GameLibrary.addFolder(selectedFolder)
    }
    FolderDialog {
        id: dataFolderDialog
        title: t("emulator_data_location_title")
        onAccepted: Preferences.emulatorDataPath = selectedFolder.toString().replace(/^file:\/\//, "")
    }
    FileDialog {
        id: biosDialog
        title: t("settings_bios_path")
        nameFilters: ["PlayStation 2 BIOS (*.bin *.rom *.nvm)", "All files (*)"]
        onAccepted: Preferences.biosPath = selectedFile.toString().replace(/^file:\/\//, "")
    }
    FileDialog {
        id: backgroundDialog
        title: t("settings_customization_background")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif)", "All files (*)"]
        onAccepted: Preferences.backgroundPath = selectedFile.toString()
    }

    SequentialAnimation {
        id: tabTransition
        NumberAnimation { target: settingsContent; property: "opacity"; to: 0; duration: Theme.durationFast }
        ScriptAction { script: root.currentTab = root.pendingTab }
        ParallelAnimation {
            NumberAnimation { target: settingsContent; property: "opacity"; from: 0; to: 1; duration: Theme.duration }
            NumberAnimation { target: settingsContent; property: "scale"; from: 0.985; to: 1; duration: Theme.duration; easing.type: Easing.OutCubic }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 24
            Layout.bottomMargin: 16
            title: t("settings_title")
            subtitle: ""
        }

        Flickable {
            id: tabFlick
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            clip: true
            contentWidth: tabsRow.width + 56
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            function centerTab(item) {
                const centered = tabsRow.x + item.x + item.width / 2 - width / 2
                contentX = Math.max(0, Math.min(contentWidth - width, centered))
            }

            Behavior on contentX {
                enabled: !tabFlick.dragging && !tabFlick.flicking
                NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
            }

            Row {
                id: tabsRow
                x: 28
                spacing: 8
                Repeater {
                    model: root.tabs
                    AppButton {
                        text: modelData.label
                        primary: root.currentTab === modelData.key
                        onClicked: root.activateTab(modelData.key, this)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        ScrollView {
            id: settingsViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: settingsContent
                x: 28
                width: Math.max(0, settingsViewport.availableWidth - 56)
                spacing: 10

                Item { Layout.preferredHeight: 10 }
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
                        numericValue: Number(modelData.value || 0)
                        from: Number(modelData.from || 0)
                        to: Number(modelData.to || 100)
                        stepSize: Number(modelData.step || 1)
                        valueSuffix: modelData.suffix || ""
                        onToggled: Preferences.setValue(modelData.key, value)
                        onValueSelected: function(value) { root.applyChoice(modelData.key, value) }
                        onNumberSelected: function(value) { root.applyNumber(modelData.key, value) }
                        onActionTriggered: root.handleAction(modelData.key)
                    }
                }
                Item { Layout.preferredHeight: 18 }
            }
        }
    }
}

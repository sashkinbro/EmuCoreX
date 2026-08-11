import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EmuCoreX.Native 1.0
import "../components"
import "../theme"

Item {
    id: root
    focus: true
    property bool menuOpen: false
    property bool menuTriggerVisible: false
    property bool launchComplete: Emulator.running
    property bool surfaceRevealed: false
    property int launchStage: Emulator.running ? 3 : 0
    property int stateSlot: 0
    property string currentMenuTab: "session"
    readonly property bool chromeVisible: !root.surfaceRevealed || headerRevealArea.containsMouse
    readonly property var menuTabs: [
        { key: "session", label: I18n.get("emulation_session_tab"), icon: "menu" },
        { key: "controls", label: I18n.get("settings_controls_tab"), icon: "play" },
        { key: "emulation", label: I18n.get("settings_emulation_tab"), icon: "chip" },
        { key: "graphics", label: I18n.get("settings_graphics_tab"), icon: "image" },
        { key: "fixes", label: I18n.get("settings_fixes_tab"), icon: "tune" }
    ]
    readonly property string gameTitle: {
        const parts = Emulator.currentGame.replace(/\\/g, "/").split("/")
        const fileName = parts.length > 0 ? parts[parts.length - 1] : ""
        return fileName.replace(/\.[^.]+$/, "") || I18n.get("emulation_sidebar_title")
    }
    readonly property string launchStatusText: {
        if (root.launchStage <= 0)
            return I18n.get("emulation_status_checking_bios")
        if (root.launchStage === 1)
            return I18n.get("emulation_status_starting_core")
        if (root.launchStage === 2)
            return I18n.get("emulation_status_loading_game")
        return I18n.get("emulation_status_running")
    }

    function t(key) { return I18n.get(key) }
    function option(value, label) { return { value: value, label: label } }
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
            value: Number(Preferences.value(key, fallback)), from: from, to: to, step: step,
            suffix: suffix || "" }
    }
    function action(title, description, icon, key, label) {
        return { title: t(title), desc: t(description), icon: icon, type: "action", key: key,
            action: t(label) }
    }
    function menuRows(tab) {
        if (tab === "controls") {
            return [
                action("settings_gamepad_mapping_title", "settings_gamepad_mapping_disconnected", "play", "controls/mapping", "common_open"),
                toggle("settings_pad_vibration", "settings_help_pad_vibration", "play", "controls/vibration", true),
                range("settings_pad_vibration_strength", "settings_help_pad_vibration", "tune", "controls/vibrationStrength", 100, 0, 150, 5, "%"),
                range("settings_gamepad_stick_deadzone", "settings_help_gamepad_stick_deadzone", "tune", "controls/deadzone", 10, 0, 40, 1, "%"),
                range("settings_gamepad_left_stick_sensitivity", "settings_help_gamepad_left_stick_sensitivity", "tune", "controls/leftSensitivity", 100, 50, 200, 5, "%"),
                range("settings_gamepad_right_stick_sensitivity", "settings_help_gamepad_right_stick_sensitivity", "tune", "controls/rightSensitivity", 100, 50, 200, 5, "%")
            ]
        }
        if (tab === "emulation") {
            return [
                toggle("settings_show_fps", "settings_show_fps_desc", "file", "emulation/showFps", false),
                toggle("settings_frame_limiter", "settings_help_frame_limiter", "refresh", "emulation/frameLimiter", true),
                range("settings_target_fps", "settings_help_target_fps", "refresh", "emulation/targetFps", 60, 30, 240, 1, " FPS"),
                toggle("settings_mtvu", "settings_help_mtvu", "chip", "emulation/mtvu", true),
                toggle("settings_fast_cdvd", "settings_help_fast_cdvd", "play", "emulation/fastCdvd", false),
                toggle("settings_enable_cheats", "settings_help_cheats", "code", "emulation/cheats", true),
                action("cheat_manager_title", "settings_cheats_import_desc", "code", "emulation/cheatManager", "common_open")
            ]
        }
        if (tab === "graphics") {
            return [
                choice("settings_renderer", "settings_help_renderer", "chip", "video/renderer", "auto", [
                    option("auto", t("settings_renderer_auto")), option("vulkan", t("settings_renderer_vulkan")),
                    option("d3d12", t("settings_renderer_d3d12")), option("d3d11", t("settings_renderer_d3d11")),
                    option("opengl", t("settings_renderer_opengl")), option("software", t("settings_renderer_software"))
                ]),
                choice("settings_upscale", "settings_help_upscale", "image", "video/upscale", "3x", [
                    option("native", t("settings_upscale_native")), option("2x", t("settings_upscale_2x")),
                    option("3x", t("settings_upscale_3x")), option("4x", t("settings_upscale_4x")),
                    option("5x", t("settings_upscale_5x")), option("6x", t("settings_upscale_6x"))
                ]),
                choice("settings_aspect_ratio", "settings_help_aspect_ratio", "image", "video/aspect", "auto", [
                    option("auto", t("settings_aspect_ratio_auto")), option("4:3", t("settings_aspect_ratio_43")),
                    option("16:9", t("settings_aspect_ratio_169")), option("10:7", t("settings_aspect_ratio_107"))
                ]),
                choice("settings_anisotropic_filtering", "settings_help_anisotropic_filtering", "tune", "video/aniso", "8x", [
                    option("off", t("settings_dithering_off")), option("2x", "2x"), option("4x", "4x"),
                    option("8x", "8x"), option("16x", "16x")
                ]),
                toggle("settings_fxaa", "settings_help_fxaa", "image", "video/fxaa", false)
            ]
        }
        if (tab === "fixes") {
            return [
                toggle("settings_game_fixes", "settings_game_fixes_desc", "tune", "emulation/gameFixes", true),
                toggle("settings_widescreen_patches", "settings_help_widescreen_patches", "image", "fixes/widescreen", false),
                toggle("settings_no_interlacing_patches", "settings_help_no_interlacing_patches", "image", "fixes/noInterlacing", false),
                toggle("settings_anti_blur", "settings_help_anti_blur", "image", "fixes/antiBlur", true)
            ]
        }
        return []
    }
    function openMenuDestination(route) {
        Emulator.pause(true)
        root.menuOpen = false
        App.navigate(route)
    }
    function handleMenuAction(key) {
        if (key === "controls/mapping") root.openMenuDestination("gamepad-mapping")
        else if (key === "emulation/cheatManager") root.openMenuDestination("cheats")
    }

    function syncSurface() {
        if (renderSurface.nativeHandle !== 0)
            Emulator.setRenderSurface(renderSurface.nativeHandle, renderSurface.surfaceWidth,
                                      renderSurface.surfaceHeight, renderSurface.surfaceScale,
                                      renderSurface.refreshRate)
    }

    Keys.onEscapePressed: {
        root.menuTriggerVisible = false
        root.menuOpen = !root.menuOpen
    }
    Component.onDestruction: Emulator.clearRenderSurface()

    Connections {
        target: Emulator
        function onRunningChanged() {
            root.launchComplete = Emulator.running
            if (Emulator.running) {
                root.launchStage = 3
                stageTimer.stop()
                revealTimer.restart()
            } else {
                root.launchStage = 0
                root.surfaceRevealed = false
                stageTimer.restart()
            }
        }
    }

    Component.onCompleted: {
        if (Emulator.running) {
            root.launchStage = 3
            revealTimer.start()
        } else {
            stageTimer.start()
        }
    }

    Timer {
        id: stageTimer
        interval: 360
        repeat: true
        onTriggered: {
            if (root.launchStage < 2)
                root.launchStage++
            else
                stop()
        }
    }

    Timer {
        id: revealTimer
        interval: 850
        onTriggered: {
            root.surfaceRevealed = true
        }
    }

    Timer {
        id: menuTriggerTimer
        interval: 2400
        onTriggered: root.menuTriggerVisible = false
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.chromeVisible ? 52 : 4
                color: Theme.backgroundRaised
                border.width: 1
                border.color: Theme.border
                clip: true

                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic }
                }

                MouseArea {
                    id: headerRevealArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    opacity: root.chromeVisible ? 1 : 0

                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                    AppLogo { Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: root.gameTitle
                            color: Theme.text
                            font.pixelSize: Theme.sp(13)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Emulator.statusText
                            color: Emulator.running ? Theme.success : Theme.textMuted
                            font.pixelSize: Theme.sp(10)
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: Emulator.running ? Theme.success : Theme.warning
                    }
                    AppButton {
                        text: ""
                        iconName: root.menuOpen ? "close" : "menu"
                        primary: root.menuOpen
                        toolTipText: root.menuOpen ? I18n.get("emulation_close_menu")
                                                   : I18n.get("emulation_show_quick_actions")
                        onClicked: root.menuOpen = !root.menuOpen
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors.fill: parent
                    color: "#000000"
                    clip: true

                    NativeRenderSurface {
                        id: renderSurface
                        anchors.fill: parent
                        surfaceVisible: root.surfaceRevealed
                        onSurfaceChanged: root.syncSurface()
                        onSurfaceDoubleClicked: {
                            if (!root.menuOpen) {
                                root.menuTriggerVisible = true
                                menuTriggerTimer.restart()
                            }
                        }
                        Component.onCompleted: root.syncSurface()
                    }

                    Item {
                        anchors.fill: parent
                        visible: !root.surfaceRevealed

                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.min(340, parent.width - 40)
                            height: 58
                            radius: 18
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.border
                            opacity: root.surfaceRevealed ? 0 : 1
                            scale: root.surfaceRevealed ? 0.96 : 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 17
                                anchors.rightMargin: 18
                                spacing: 12

                                AppIcon {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                    name: root.launchComplete ? "check" : "chip"
                                    color: root.launchComplete ? Theme.success : Theme.accentBright
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.launchStatusText
                                    color: root.launchComplete ? Theme.success : Theme.text
                                    font.pixelSize: Theme.sp(13)
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }

                            Behavior on opacity { NumberAnimation { duration: Theme.duration } }
                            Behavior on scale { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
                        }
                    }
                }

                Popup {
                    id: gameMenuPopup
                    parent: root
                    x: Math.max(0, root.width - width)
                    y: root.chromeVisible ? 52 : 4
                    width: Math.min(680, Math.max(480, root.width * 0.48))
                    height: Math.max(0, root.height - y)
                    visible: root.menuOpen
                    modal: false
                    dim: false
                    focus: true
                    padding: 0
                    closePolicy: Popup.NoAutoClose
                    popupType: Popup.Window
                    onClosed: root.menuOpen = false
                    enter: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
                    }
                    exit: Transition {
                        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
                    }
                    background: Rectangle {
                        color: Theme.backgroundRaised
                        border.width: 1
                        border.color: Theme.border
                    }

                    contentItem: ColumnLayout {
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 22
                            Layout.rightMargin: 14
                            Layout.topMargin: 18
                            Layout.bottomMargin: 14
                            spacing: 12
                            Rectangle {
                                Layout.preferredWidth: 46
                                Layout.preferredHeight: 46
                                radius: 15
                                color: Theme.accentContainer
                                AppLogo { anchors.centerIn: parent; width: 25; height: 25 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: root.gameTitle
                                    color: Theme.text
                                    font.pixelSize: Theme.sp(18)
                                    font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: Emulator.statusText
                                    color: Emulator.running ? Theme.success : Theme.textMuted
                                    font.pixelSize: Theme.sp(11)
                                    elide: Text.ElideRight
                                }
                            }
                            AppButton {
                                text: ""
                                iconName: "close"
                                toolTipText: I18n.get("emulation_close_menu")
                                onClicked: root.menuOpen = false
                            }
                        }

                        Flickable {
                            id: menuTabFlick
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            clip: true
                            contentWidth: menuTabRow.width + 36
                            contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            Row {
                                id: menuTabRow
                                x: 18
                                spacing: 8
                                Repeater {
                                    model: root.menuTabs
                                    AppButton {
                                        text: modelData.label
                                        iconName: modelData.icon
                                        primary: root.currentMenuTab === modelData.key
                                        onClicked: root.currentMenuTab = modelData.key
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                        ScrollView {
                            id: menuViewport
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                            ColumnLayout {
                                x: 18
                                width: Math.max(0, menuViewport.availableWidth - 36)
                                spacing: 10
                                Item { Layout.preferredHeight: 8 }

                                RowLayout {
                                    visible: root.currentMenuTab === "session"
                                    Layout.fillWidth: true
                                    spacing: 10
                                    AppButton {
                                        Layout.fillWidth: true
                                        text: Emulator.paused ? I18n.get("emulation_resume") : I18n.get("emulation_pause")
                                        iconName: Emulator.paused ? "play" : "pause"
                                        primary: true
                                        enabled: Emulator.running
                                        onClicked: Emulator.pause(!Emulator.paused)
                                    }
                                    AppButton {
                                        Layout.fillWidth: true
                                        text: I18n.get("emulation_exit")
                                        iconName: "close"
                                        danger: true
                                        onClicked: { Emulator.shutdown(); App.goBack() }
                                    }
                                }

                                AppCard {
                                    visible: root.currentMenuTab === "session"
                                    Layout.fillWidth: true
                                    implicitHeight: saveStateColumn.implicitHeight + 32
                                    ColumnLayout {
                                        id: saveStateColumn
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 12
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                Layout.fillWidth: true
                                                text: I18n.get("detail_save_states")
                                                color: Theme.text
                                                font.pixelSize: Theme.sp(15)
                                                font.weight: Font.Bold
                                            }
                                            Text {
                                                text: String(root.stateSlot)
                                                color: Theme.accentBright
                                                font.pixelSize: Theme.sp(17)
                                                font.weight: Font.Bold
                                            }
                                            AppButton {
                                                text: ""
                                                iconName: "back"
                                                enabled: root.stateSlot > 0
                                                onClicked: root.stateSlot--
                                            }
                                            AppButton {
                                                text: ""
                                                iconName: "next"
                                                enabled: root.stateSlot < 9
                                                onClicked: root.stateSlot++
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10
                                            AppButton {
                                                Layout.fillWidth: true
                                                text: I18n.get("emulation_quick_save")
                                                iconName: "save"
                                                enabled: Emulator.running
                                                onClicked: Emulator.saveState(root.stateSlot)
                                            }
                                            AppButton {
                                                Layout.fillWidth: true
                                                text: I18n.get("emulation_quick_load")
                                                iconName: "refresh"
                                                enabled: Emulator.running
                                                onClicked: Emulator.loadState(root.stateSlot)
                                            }
                                        }
                                    }
                                }

                                SettingRow {
                                    visible: root.currentMenuTab === "session"
                                    Layout.fillWidth: true
                                    title: I18n.get("emulation_auto_save_on_exit")
                                    description: I18n.get("emulation_auto_save_on_exit_desc")
                                    iconName: "save"
                                    checked: Boolean(Preferences.value("gameMenu/autoSaveOnExit", true))
                                    onToggled: Preferences.setValue("gameMenu/autoSaveOnExit", value)
                                }
                                SettingRow {
                                    visible: root.currentMenuTab === "session"
                                    Layout.fillWidth: true
                                    title: I18n.get("emulation_auto_load_on_start")
                                    description: I18n.get("emulation_auto_load_on_start_desc")
                                    iconName: "refresh"
                                    checked: Boolean(Preferences.value("gameMenu/autoLoadOnStart", false))
                                    onToggled: Preferences.setValue("gameMenu/autoLoadOnStart", value)
                                }

                                AppCard {
                                    visible: root.currentMenuTab === "session"
                                    Layout.fillWidth: true
                                    implicitHeight: statusColumn.implicitHeight + 30
                                    ColumnLayout {
                                        id: statusColumn
                                        anchors.fill: parent
                                        anchors.margins: 15
                                        spacing: 6
                                        Text {
                                            text: I18n.get("emulation_performance_stats")
                                            color: Theme.text
                                            font.pixelSize: Theme.sp(14)
                                            font.weight: Font.Bold
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: Emulator.statusText
                                            color: Emulator.running ? Theme.success : Theme.textMuted
                                            font.pixelSize: Theme.sp(12)
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: Emulator.backendName
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.sp(11)
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }

                                Repeater {
                                    model: root.currentMenuTab === "session" ? [] : root.menuRows(root.currentMenuTab)
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
                                        onValueSelected: function(value) { Preferences.setValue(modelData.key, value) }
                                        onNumberSelected: function(value) { Preferences.setValue(modelData.key, value) }
                                        onActionTriggered: root.handleMenuAction(modelData.key)
                                    }
                                }
                                Item { Layout.preferredHeight: 8 }
                            }
                        }
                    }
                }

                Popup {
                    id: menuTriggerPopup
                    parent: root
                    x: root.width - width - 18
                    y: Math.max(18, (root.height - height) / 2)
                    width: 54
                    height: 54
                    visible: root.menuTriggerVisible && !root.menuOpen
                    modal: false
                    dim: false
                    focus: false
                    padding: 0
                    closePolicy: Popup.NoAutoClose
                    popupType: Popup.Window
                    background: Rectangle {
                        radius: 18
                        color: Theme.accent
                        border.width: 1
                        border.color: Theme.accentBright
                    }
                    contentItem: AppButton {
                        text: ""
                        iconName: "menu"
                        primary: true
                        toolTipText: I18n.get("emulation_show_quick_actions")
                        onClicked: {
                            root.menuTriggerVisible = false
                            menuTriggerTimer.stop()
                            root.menuOpen = true
                        }
                    }
                    enter: Transition {
                        ParallelAnimation {
                            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
                            NumberAnimation { property: "scale"; from: 0.88; to: 1; duration: Theme.durationFast; easing.type: Easing.OutBack }
                        }
                    }
                    exit: Transition {
                        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
                    }
                }
            }
        }
    }
}

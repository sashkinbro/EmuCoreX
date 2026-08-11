import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "theme"

Item {
    id: root
    property bool compact: Preferences.compactSidebar || width < 1120
    property string globalSearch: ""

    Rectangle {
        anchors.fill: parent
        color: Theme.background

        Rectangle {
            id: sidebar
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: root.compact ? Theme.sidebarCompact : Theme.sidebarWide
            color: Theme.sidebar
            border.width: 1
            border.color: Theme.border

            Behavior on width { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 7

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    spacing: 12
                    AppLogo {
                        Layout.preferredWidth: 38; Layout.preferredHeight: 38
                    }
                    ColumnLayout {
                        visible: !root.compact
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: "EmuCoreX"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
                    }
                }

                Item { Layout.preferredHeight: 4 }

                Repeater {
                    model: [
                        { route: "library", icon: "library", label: I18n.get("shell_library") },
                        { route: "catalog", icon: "search", label: I18n.get("shell_catalog_search") },
                        { route: "hub", icon: "hub", label: I18n.get("hub_title") },
                        { route: "achievements", icon: "star", label: I18n.get("settings_achievements_tab") },
                        { route: "profile", icon: "profile", label: I18n.get("profile_title") }
                    ]
                    SidebarItem {
                        Layout.fillWidth: true
                        compact: root.compact
                        iconName: modelData.icon
                        label: modelData.label
                        selected: App.currentRoute === modelData.route
                        onClicked: App.replaceRoute(modelData.route)
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 6; Layout.bottomMargin: 3; color: Theme.border }
                Text {
                    visible: !root.compact
                    text: I18n.get("shell_tools_section").toUpperCase()
                    color: Theme.textDim
                    font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1.1
                    Layout.leftMargin: 12; Layout.bottomMargin: 2
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: parent.width
                        spacing: 5
                        Repeater {
                            model: [
                                { route: "launch-game", icon: "play", label: I18n.get("shell_launch_game") },
                                { route: "launch-bios", icon: "chip", label: I18n.get("shell_launch_bios") },
                                { route: "game-manager", icon: "tune", label: I18n.get("shell_game_settings_manager") },
                                { route: "save-manager", icon: "save", label: I18n.get("shell_save_states") },
                                { route: "memory-cards", icon: "card", label: I18n.get("shell_memory_cards") },
                                { route: "textures", icon: "image", label: I18n.get("shell_texture_manager") },
                                { route: "cheats", icon: "code", label: I18n.get("shell_cheat_manager") }
                            ]
                            SidebarItem {
                                Layout.fillWidth: true
                                compact: root.compact
                                iconName: modelData.icon
                                label: modelData.label
                                selected: App.currentRoute === modelData.route
                                onClicked: {
                                    if (modelData.route === "launch-game") gameFileDialog.open()
                                    else if (modelData.route === "launch-bios") {
                                        if (Emulator.bootBios()) App.navigate("emulation")
                                    } else App.replaceRoute(modelData.route)
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                Repeater {
                    model: [
                        { route: "settings", icon: "settings", label: I18n.get("shell_app_settings") },
                        { route: "formats", icon: "file", label: I18n.get("shell_supported_formats") },
                        { route: "feedback", icon: "chat", label: I18n.get("feedback_title") }
                    ]
                    SidebarItem {
                        Layout.fillWidth: true
                        compact: root.compact
                        iconName: modelData.icon
                        label: modelData.label
                        selected: App.currentRoute === modelData.route
                        onClicked: App.replaceRoute(modelData.route)
                    }
                }
            }
        }

        ColumnLayout {
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.topBarHeight
                color: Theme.backgroundRaised
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    spacing: 14

                    AppButton {
                        visible: App.canGoBack
                        implicitWidth: 42
                        text: ""
                        iconName: "back"
                        onClicked: App.goBack()
                    }
                    AppButton {
                        implicitWidth: 42
                        text: ""
                        iconName: root.compact ? "menu" : "close"
                        onClicked: Preferences.compactSidebar = !Preferences.compactSidebar
                    }

                    Item { Layout.fillWidth: true }
                    Rectangle {
                        Layout.preferredWidth: Math.min(430, root.width * 0.34)
                        Layout.preferredHeight: 42
                        color: Theme.surface
                        radius: 21
                        border.width: 1
                        border.color: searchField.activeFocus ? Theme.accent : Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14; anchors.rightMargin: 14
                            AppIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; name: "search"; color: Theme.textDim }
                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                placeholderText: I18n.get("home_search")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                background: null
                                selectByMouse: true
                                onTextChanged: {
                                    root.globalSearch = text
                                    if (App.currentRoute === "library") GameLibrary.searchQuery = text
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        Layout.preferredWidth: 34; Layout.preferredHeight: 34
                        radius: 17
                        color: Theme.surfaceActive
                        AppIcon { anchors.centerIn: parent; width: 18; height: 18; name: "profile"; color: Theme.accentBright }
                        TapHandler { onTapped: App.replaceRoute("profile") }
                    }
                }
            }

            Loader {
                id: pageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: {
                    switch (App.currentRoute) {
                    case "library": return libraryComponent
                    case "catalog": return catalogComponent
                    case "hub": return hubComponent
                    case "profile": return profileComponent
                    case "settings": return settingsComponent
                    case "emulation": return emulationComponent
                    default: return featureComponent
                    }
                }
                opacity: status === Loader.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
            }
        }
    }

    FileDialog {
        id: gameFileDialog
        title: I18n.get("shell_launch_game")
        nameFilters: ["PlayStation 2 images (*.iso *.bin *.img *.mdf *.nrg *.chd *.cso *.gz *.zso)", "Executables (*.elf)", "All files (*)"]
        onAccepted: {
            if (Emulator.bootGame(selectedFile.toString().replace(/^file:\/\//, "")))
                App.navigate("emulation")
        }
    }

    Component { id: libraryComponent; Loader { source: "screens/LibraryScreen.qml" } }
    Component { id: catalogComponent; Loader { source: "screens/CatalogScreen.qml" } }
    Component { id: hubComponent; Loader { source: "screens/HubScreen.qml" } }
    Component { id: profileComponent; Loader { source: "screens/ProfileScreen.qml" } }
    Component { id: settingsComponent; Loader { source: "screens/SettingsScreen.qml" } }
    Component { id: emulationComponent; Loader { source: "screens/EmulationScreen.qml" } }
    Component {
        id: featureComponent
        Loader {
            source: "screens/UtilityScreen.qml"
            onLoaded: if (item) item.route = Qt.binding(function() { return App.currentRoute })
        }
    }
}

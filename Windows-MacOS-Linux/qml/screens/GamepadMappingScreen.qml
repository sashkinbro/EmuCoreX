import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property int selectedPlayer: 1
    property int mappingRevision: InputBindings.revision

    readonly property var actions: [
        { id: "dpad_up", key: "settings_gamepad_action_dpad_up", icon: "back", rotation: 90 },
        { id: "dpad_down", key: "settings_gamepad_action_dpad_down", icon: "back", rotation: -90 },
        { id: "dpad_left", key: "settings_gamepad_action_dpad_left", icon: "back", rotation: 0 },
        { id: "dpad_right", key: "settings_gamepad_action_dpad_right", icon: "back", rotation: 180 },
        { id: "triangle", key: "settings_gamepad_action_triangle", icon: "play", rotation: -90 },
        { id: "circle", key: "settings_gamepad_action_circle", icon: "circle", rotation: 0 },
        { id: "cross", key: "settings_gamepad_action_cross", icon: "close", rotation: 0 },
        { id: "square", key: "settings_gamepad_action_square", icon: "stop", rotation: 0 },
        { id: "l1", key: "settings_gamepad_action_l1", icon: "button", rotation: 0 },
        { id: "r1", key: "settings_gamepad_action_r1", icon: "button", rotation: 0 },
        { id: "l2", key: "settings_gamepad_action_l2", icon: "button", rotation: 0 },
        { id: "r2", key: "settings_gamepad_action_r2", icon: "button", rotation: 0 },
        { id: "l3", key: "settings_gamepad_action_l3", icon: "stick", rotation: 0 },
        { id: "r3", key: "settings_gamepad_action_r3", icon: "stick", rotation: 0 },
        { id: "select", key: "settings_gamepad_action_select", icon: "menu", rotation: 0 },
        { id: "start", key: "settings_gamepad_action_start", icon: "play", rotation: 0 },
        { id: "left_input_toggle", key: "settings_gamepad_action_left_input_toggle", icon: "tune", rotation: 0 },
        { id: "pressure", key: "settings_gamepad_action_pressure", icon: "chip", rotation: 0 }
    ]

    function bindingText(action) {
        mappingRevision
        const value = InputBindings.bindingDisplay(selectedPlayer, action)
        return value.length > 0 ? value : I18n.get("settings_gamepad_mapping_auto_format")
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            x: Math.max(24, (parent.width - Math.min(Theme.contentMaxWidth, parent.width - 48)) / 2)
            width: Math.min(Theme.contentMaxWidth, parent.width - 48)
            spacing: 16

            Item { Layout.preferredHeight: 18 }
            PageHeader {
                Layout.fillWidth: true
                title: I18n.get("settings_gamepad_mapping_title")
                subtitle: InputBindings.connectedDevices.length > 0
                    ? I18n.format("settings_gamepad_mapping_connected", [InputBindings.connectedDevices[0]])
                    : I18n.get("settings_gamepad_mapping_disconnected")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: [
                        { player: 1, label: I18n.get("settings_gamepad_player_1") },
                        { player: 2, label: I18n.get("settings_gamepad_player_2") }
                    ]
                    AppButton {
                        required property var modelData
                        text: modelData.label
                        primary: root.selectedPlayer === modelData.player
                        onClicked: root.selectedPlayer = modelData.player
                    }
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: I18n.get("settings_gamepad_mapping_reset_title")
                    iconName: "refresh"
                    onClicked: InputBindings.resetPlayer(root.selectedPlayer)
                }
            }

            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 82
                interactive: false

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        radius: 22
                        color: Theme.accentContainer
                        AppIcon {
                            anchors.centerIn: parent
                            width: 21
                            height: 21
                            name: InputBindings.connectedDevices.length > 0 ? "gamepad" : "play"
                            color: Theme.accentBright
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: root.selectedPlayer === 1 ? I18n.get("settings_gamepad_player_1") : I18n.get("settings_gamepad_player_2")
                            color: Theme.text
                            font.pixelSize: Theme.sp(15)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: InputBindings.connectedDevices.length >= root.selectedPlayer
                                ? I18n.format("settings_gamepad_mapping_player_connected", [root.selectedPlayer, InputBindings.connectedDevices[root.selectedPlayer - 1]])
                                : I18n.format("settings_gamepad_mapping_player_disconnected", [root.selectedPlayer])
                            color: Theme.textMuted
                            font.pixelSize: Theme.sp(12)
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width >= 1050 ? 3 : (width >= 670 ? 2 : 1)
                rowSpacing: 12
                columnSpacing: 12

                Repeater {
                    model: root.actions
                    AppCard {
                        id: bindingCard
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        interactive: true
                        selected: InputBindings.listening
                            && InputBindings.listeningPlayer === root.selectedPlayer
                            && InputBindings.listeningAction === modelData.id
                        onClicked: InputBindings.beginListening(root.selectedPlayer, modelData.id)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 11

                            Rectangle {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                radius: 20
                                color: Theme.accentContainer
                                AppIcon {
                                    anchors.centerIn: parent
                                    width: 19
                                    height: 19
                                    name: bindingCard.modelData.icon
                                    rotation: bindingCard.modelData.rotation
                                    color: Theme.accentBright
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: I18n.get(bindingCard.modelData.key)
                                    color: Theme.text
                                    font.pixelSize: Theme.sp(13)
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.bindingText(bindingCard.modelData.id)
                                    color: bindingCard.selected ? Theme.accentBright : Theme.textMuted
                                    font.pixelSize: Theme.sp(12)
                                    font.weight: bindingCard.selected ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                            }
                            AppButton {
                                visible: InputBindings.binding(root.selectedPlayer, bindingCard.modelData.id).length > 0
                                text: ""
                                iconName: "close"
                                toolTipText: I18n.get("settings_gamepad_mapping_clear")
                                onClicked: InputBindings.clearBinding(root.selectedPlayer, bindingCard.modelData.id)
                            }
                        }
                    }
                }
            }
            Item { Layout.preferredHeight: 22 }
        }
    }

    Popup {
        id: listeningPopup
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, root.width - 48)
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: InputBindings.listening
        padding: 0

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
                NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: Theme.duration; easing.type: Easing.OutBack }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
                NumberAnimation { property: "scale"; to: 0.97; duration: Theme.durationFast }
            }
        }

        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.backgroundRaised
            border.width: 1
            border.color: Theme.accent
        }
        contentItem: ColumnLayout {
            spacing: 14
            Item { Layout.preferredHeight: 10 }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 58
                Layout.preferredHeight: 58
                radius: 29
                color: Theme.accentContainer
                AppIcon { anchors.centerIn: parent; width: 28; height: 28; name: "gamepad"; color: Theme.accentBright }
                SequentialAnimation on scale {
                    loops: Animation.Infinite
                    NumberAnimation { to: 1.08; duration: 650; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 650; easing.type: Easing.InOutSine }
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                horizontalAlignment: Text.AlignHCenter
                text: I18n.get("settings_gamepad_mapping_listening_title")
                color: Theme.text
                font.pixelSize: Theme.sp(20)
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: I18n.format("settings_gamepad_mapping_listening_player_desc", [
                    InputBindings.listeningPlayer === 1 ? I18n.get("settings_gamepad_player_1") : I18n.get("settings_gamepad_player_2"),
                    I18n.get("settings_gamepad_action_" + InputBindings.listeningAction)
                ])
                color: Theme.textMuted
                font.pixelSize: Theme.sp(13)
            }
            AppButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 20
                text: I18n.get("common_close")
                onClicked: InputBindings.cancelListening()
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root

    function displayName() {
        const saved = Preferences.value("profile/displayName", "").toString().trim()
        return saved.length > 0 ? saved : I18n.get("profile_player")
    }

    function playTime() {
        const seconds = Number(Preferences.value("profile/playSeconds", 0))
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        return hours.toString().padStart(2, "0") + ":" + minutes.toString().padStart(2, "0")
    }

    Dialog {
        id: editDialog
        anchors.centerIn: parent
        width: Math.min(460, root.width - 48)
        modal: true
        title: I18n.get("profile_edit_name")
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: {
            nameField.text = Preferences.value("profile/displayName", "").toString()
            nameField.forceActiveFocus()
        }
        onAccepted: Preferences.setValue("profile/displayName", nameField.text.trim())
        background: Rectangle { radius: 22; color: Theme.surface; border.width: 1; border.color: Theme.borderStrong }
        contentItem: ColumnLayout {
            spacing: 12
            TextField {
                id: nameField
                Layout.fillWidth: true
                maximumLength: 32
                placeholderText: I18n.get("profile_display_name")
                color: Theme.text
                selectByMouse: true
                background: Rectangle {
                    radius: 14
                    color: Theme.backgroundRaised
                    border.width: 1
                    border.color: nameField.activeFocus ? Theme.accent : Theme.border
                }
            }
            Text {
                Layout.alignment: Qt.AlignRight
                text: I18n.format("profile_name_character_count", [nameField.text.length])
                color: Theme.textDim
                font.pixelSize: Theme.sp(11)
            }
        }
    }

    FileDialog {
        id: avatarDialog
        title: I18n.get("profile_display_name")
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: Preferences.setValue("profile/avatar", selectedFile.toString())
    }

    Flickable {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 7 }

        ColumnLayout {
            id: content
            width: Math.min(parent.width - 56, 1240)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 28
            spacing: 18

            PageHeader {
                Layout.fillWidth: true
                title: I18n.get("profile_title")
                subtitle: I18n.get("profile_stats_empty")
            }

            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 210

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 24

                    Item {
                        Layout.preferredWidth: 116
                        Layout.preferredHeight: 116

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            gradient: Gradient {
                                GradientStop { position: 0; color: Theme.accentBright }
                                GradientStop { position: 1; color: Theme.accent }
                            }
                            AppIcon {
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                name: "profile"
                                color: "white"
                                visible: Preferences.value("profile/avatar", "").toString().length === 0
                            }
                        }
                        RoundedImage {
                            anchors.fill: parent
                            radius: width / 2
                            source: Preferences.value("profile/avatar", "")
                            visible: source.toString().length > 0
                        }
                        RoundButton {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            width: 34
                            height: 34
                            icon.name: "document-open"
                            onClicked: avatarDialog.open()
                            background: Rectangle {
                                radius: 17
                                color: parent.hovered ? Theme.accentBright : Theme.accent
                                border.width: 2
                                border.color: Theme.surface
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: root.displayName()
                            color: Theme.text
                            font.pixelSize: Theme.sp(28)
                            font.weight: Font.Bold
                        }
                        Text {
                            text: I18n.get("profile_stats_lifetime_title")
                            color: Theme.textMuted
                            font.pixelSize: Theme.sp(13)
                        }
                    }

                    AppButton {
                        text: I18n.get("profile_edit_name")
                        iconName: "tune"
                        onClicked: editDialog.open()
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 850 ? 3 : 1
                rowSpacing: 14
                columnSpacing: 14

                Repeater {
                    model: [
                        { value: GameLibrary.count, label: I18n.get("shell_library"), icon: "library" },
                        { value: root.playTime(), label: I18n.get("profile_total_time"), icon: "play" },
                        { value: GameLibrary.favoriteCount, label: I18n.get("hub_tab_favorites"), icon: "star" }
                    ]
                    AppCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 128
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 46
                                Layout.preferredHeight: 46
                                radius: 15
                                color: Theme.surfaceActive
                                AppIcon { anchors.centerIn: parent; width: 22; height: 22; name: modelData.icon; color: Theme.accentBright }
                            }
                            ColumnLayout {
                                Text { text: modelData.value; color: Theme.text; font.pixelSize: Theme.sp(25); font.weight: Font.Bold }
                                Text { text: modelData.label; color: Theme.textMuted; font.pixelSize: Theme.sp(12) }
                            }
                        }
                    }
                }
            }

            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 18
                    AppIcon { Layout.preferredWidth: 36; Layout.preferredHeight: 36; name: "star"; color: Theme.warning }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: "RetroAchievements"; color: Theme.text; font.pixelSize: Theme.sp(17); font.weight: Font.DemiBold }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.get("achievements_login_to_sync")
                            color: Theme.textMuted
                            font.pixelSize: Theme.sp(13)
                            wrapMode: Text.WordWrap
                        }
                    }
                    AppButton {
                        text: I18n.get("profile_sign_in")
                        primary: true
                        onClicked: App.replaceRoute("achievements")
                    }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../theme"

Dialog {
    id: root
    anchors.centerIn: parent
    width: Math.min(720, parent.width - 48)
    height: Math.min(590, parent.height - 48)
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    background: Rectangle {
        radius: 24
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderStrong
    }

    header: Item {
        implicitHeight: 74

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 16
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: I18n.get("settings_game_path")
                    color: Theme.text
                    font.pixelSize: Theme.sp(20)
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: I18n.format("settings_game_folders_count", [GameLibrary.folders.length])
                    color: Theme.textMuted
                    font.pixelSize: Theme.sp(11)
                }
            }

            AppButton {
                text: ""
                iconName: "close"
                toolTipText: I18n.get("common_close")
                Accessible.name: I18n.get("common_close")
                onClicked: root.close()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    contentItem: Item {
        ListView {
            id: folderList
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.topMargin: 12
            anchors.bottomMargin: 12
            clip: true
            spacing: 4
            model: GameLibrary.folders
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

            delegate: Rectangle {
                id: folderRow
                required property int index
                required property string modelData
                width: folderList.width
                height: 72
                radius: 16
                color: rowHover.hovered ? Theme.surfaceHover : "transparent"

                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 13

                    Rectangle {
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        radius: 13
                        color: Theme.surfaceVariant
                        AppIcon {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            name: "folder"
                            color: Theme.accentBright
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: folderRow.modelData
                        color: Theme.text
                        font.pixelSize: Theme.sp(12)
                        maximumLineCount: 2
                        wrapMode: Text.WrapAnywhere
                        elide: Text.ElideMiddle
                    }

                    AppButton {
                        text: ""
                        iconName: "close"
                        danger: true
                        toolTipText: I18n.get("game_folders_remove")
                        Accessible.name: I18n.get("game_folders_remove")
                        onClicked: GameLibrary.removeFolder(folderRow.index)
                    }
                }

                HoverHandler { id: rowHover }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: GameLibrary.folders.length === 0
                spacing: 10
                AppIcon {
                    Layout.alignment: Qt.AlignHCenter
                    width: 48
                    height: 48
                    name: "folder"
                    color: Theme.textDim
                }
                Text {
                    text: I18n.get("home_empty_subtitle")
                    color: Theme.textMuted
                    font.pixelSize: Theme.sp(13)
                }
            }
        }
    }

    footer: Item {
        implicitHeight: 76

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            Item { Layout.fillWidth: true }
            AppButton {
                text: I18n.get("home_add_folder")
                iconName: "folder"
                primary: true
                onClicked: folderPicker.open()
            }
        }
    }

    FolderDialog {
        id: folderPicker
        title: I18n.get("home_add_folder")
        onAccepted: GameLibrary.addFolder(selectedFolder)
    }
}

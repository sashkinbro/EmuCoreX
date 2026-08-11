import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../theme"

Item {
    id: root

    property string category
    property string title
    property string subtitle
    property string iconName: "file"
    property string emptyTitle
    property string emptyBody
    property string importLabel
    property string deleteLabel
    property string backupLabel: ""
    property string restoreLabel: ""
    property var nameFilters: ["All files (*)"]
    property bool folderImport: false
    property bool memoryCards: false
    property var items: []
    property string selectedPath: ""
    property string selectedName: ""
    property string operation: ""
    property string notice: ""

    function refresh() {
        items = DesktopData.entries(category)
    }

    function formatSize(bytes) {
        if (bytes <= 0) return ""
        const units = ["B", "KB", "MB", "GB", "TB"]
        const order = Math.min(units.length - 1, Math.floor(Math.log(bytes) / Math.log(1024)))
        return (bytes / Math.pow(1024, order)).toFixed(order >= 3 ? 2 : 1) + " " + units[order]
    }

    function finish(success, successText) {
        notice = success ? successText : DesktopData.lastError
        refresh()
        noticeTimer.restart()
    }

    Component.onCompleted: refresh()

    Connections {
        target: DesktopData
        function onContentsChanged(changedCategory) {
            if (!changedCategory || changedCategory === root.category)
                root.refresh()
        }
    }

    Timer {
        id: noticeTimer
        interval: 3600
        onTriggered: root.notice = ""
    }

    FileDialog {
        id: importDialog
        title: root.importLabel
        nameFilters: root.nameFilters
        onAccepted: root.finish(DesktopData.importFile(root.category, selectedFile), root.importLabel)
    }

    FolderDialog {
        id: folderImportDialog
        title: root.importLabel
        onAccepted: root.finish(DesktopData.importFile(root.category, selectedFolder), root.importLabel)
    }

    FolderDialog {
        id: backupDialog
        title: root.backupLabel
        onAccepted: root.finish(DesktopData.backupCategory(root.category, selectedFolder), root.backupLabel)
    }

    FolderDialog {
        id: restoreDialog
        title: root.restoreLabel
        onAccepted: root.finish(DesktopData.restoreCategory(root.category, selectedFolder), root.restoreLabel)
    }

    FolderDialog {
        id: exportDialog
        title: I18n.get("memory_card_export_action")
        onAccepted: root.finish(DesktopData.exportEntry(root.selectedPath, selectedFolder), I18n.get("memory_card_export_success"))
    }

    Dialog {
        id: nameDialog
        anchors.centerIn: parent
        width: 430
        modal: true
        title: root.operation === "create" ? I18n.get("memory_card_create_title")
            : root.operation === "rename" ? I18n.get("memory_card_rename_title")
            : I18n.get("memory_card_duplicate_title")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            nameField.text = root.operation === "create" ? "Mcd001" : root.selectedName.replace(/\.ps2$/i, "")
            nameField.forceActiveFocus()
        }
        onAccepted: {
            let success = false
            let message = ""
            if (root.operation === "create") {
                success = DesktopData.createMemoryCard(nameField.text, sizeBox.currentValue, folderCard.checked)
                message = I18n.get("memory_card_create_success")
            } else if (root.operation === "rename") {
                success = DesktopData.renameEntry(root.selectedPath, nameField.text + (root.selectedName.toLowerCase().endsWith(".ps2") ? ".ps2" : ""))
                message = I18n.get("memory_card_rename_success")
            } else {
                success = DesktopData.duplicateEntry(root.selectedPath, nameField.text + (root.selectedName.toLowerCase().endsWith(".ps2") ? ".ps2" : ""))
                message = I18n.get("memory_card_duplicate_success")
            }
            root.finish(success, message)
        }

        background: Rectangle { radius: 22; color: Theme.surface; border.width: 1; border.color: Theme.borderStrong }
        contentItem: ColumnLayout {
            spacing: 14
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: I18n.get("memory_card_name_field")
                color: Theme.text
                selectByMouse: true
                background: Rectangle { radius: 13; color: Theme.backgroundRaised; border.width: 1; border.color: nameField.activeFocus ? Theme.accent : Theme.border }
            }
            CheckBox {
                id: folderCard
                visible: root.operation === "create"
                text: I18n.get("memory_card_type_folder_title")
                checked: true
            }
            ComboBox {
                id: sizeBox
                visible: root.operation === "create" && !folderCard.checked
                Layout.fillWidth: true
                model: [8, 16, 32, 64]
                textRole: ""
                property int currentValue: Number(currentText)
            }
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: 430
        modal: true
        title: root.memoryCards ? I18n.get("memory_card_delete_confirm_title") : root.deleteLabel
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (root.memoryCards) {
                if (Preferences.value("memoryCards/slot1", "") === root.selectedName) Preferences.setValue("memoryCards/slot1", "")
                if (Preferences.value("memoryCards/slot2", "") === root.selectedName) Preferences.setValue("memoryCards/slot2", "")
            }
            root.finish(DesktopData.removeEntry(root.selectedPath), root.memoryCards ? I18n.get("memory_card_delete_success") : root.deleteLabel)
        }
        background: Rectangle { radius: 22; color: Theme.surface; border.width: 1; border.color: Theme.borderStrong }
        contentItem: Text {
            text: root.memoryCards ? I18n.format("memory_card_delete_confirm_body", [root.selectedName]) : root.selectedName
            color: Theme.textMuted
            font.pixelSize: 13
            wrapMode: Text.WordWrap
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
            Layout.bottomMargin: 18
            title: root.title
            subtitle: root.subtitle

            AppButton {
                visible: root.backupLabel.length > 0
                text: root.backupLabel
                iconName: "save"
                onClicked: backupDialog.open()
            }
            AppButton {
                visible: root.restoreLabel.length > 0
                text: root.restoreLabel
                iconName: "refresh"
                onClicked: restoreDialog.open()
            }
            AppButton {
                visible: root.memoryCards
                text: I18n.get("memory_card_create_action")
                iconName: "card"
                onClicked: { root.operation = "create"; nameDialog.open() }
            }
            AppButton {
                primary: true
                text: root.importLabel
                iconName: "folder"
                onClicked: root.folderImport ? folderImportDialog.open() : importDialog.open()
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 520)
                visible: root.items.length === 0
                spacing: 14
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 76
                    radius: 25
                    color: Theme.accentContainer
                    AppIcon { anchors.centerIn: parent; width: 34; height: 34; name: root.iconName; color: Theme.accentBright }
                }
                Text { Layout.fillWidth: true; text: root.emptyTitle; color: Theme.text; font.pixelSize: 22; font.weight: Font.Bold; horizontalAlignment: Text.AlignHCenter }
                Text { Layout.fillWidth: true; text: root.emptyBody; color: Theme.textMuted; font.pixelSize: 13; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
            }

            GridView {
                id: list
                anchors.fill: parent
                visible: root.items.length > 0
                model: root.items
                clip: true
                leftMargin: 28
                rightMargin: 28
                topMargin: 22
                bottomMargin: 22
                readonly property real usableWidth: width - leftMargin - rightMargin
                cellWidth: usableWidth / Math.max(1, Math.floor(usableWidth / 390))
                cellHeight: root.memoryCards ? 184 : 150
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 7 }

                delegate: Item {
                    id: entryDelegate
                    required property var modelData
                    width: list.cellWidth
                    height: list.cellHeight

                    AppCard {
                        anchors.fill: parent
                        anchors.margins: 7
                        interactive: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 9

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Rectangle {
                                    Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                    radius: 14
                                    color: Theme.accentContainer
                                    AppIcon { anchors.centerIn: parent; width: 21; height: 21; name: root.iconName; color: Theme.accentBright }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text { Layout.fillWidth: true; text: entryDelegate.modelData.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
                                    Text { Layout.fillWidth: true; text: [root.formatSize(entryDelegate.modelData.size), entryDelegate.modelData.modified.substring(0, 10)].filter(Boolean).join("  ·  "); color: Theme.textMuted; font.pixelSize: 11 }
                                }
                            }

                            RowLayout {
                                visible: root.memoryCards
                                Layout.fillWidth: true
                                spacing: 8
                                AppButton {
                                    Layout.fillWidth: true
                                    text: I18n.get("memory_card_slot_1")
                                    primary: Preferences.value("memoryCards/slot1", "") === entryDelegate.modelData.name
                                    onClicked: Preferences.setValue("memoryCards/slot1", primary ? "" : entryDelegate.modelData.name)
                                }
                                AppButton {
                                    Layout.fillWidth: true
                                    text: I18n.get("memory_card_slot_2")
                                    primary: Preferences.value("memoryCards/slot2", "") === entryDelegate.modelData.name
                                    onClicked: Preferences.setValue("memoryCards/slot2", primary ? "" : entryDelegate.modelData.name)
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                AppButton {
                                    text: I18n.get("common_open")
                                    iconName: "folder"
                                    onClicked: DesktopData.reveal(entryDelegate.modelData.path)
                                }
                                Item { Layout.fillWidth: true }
                                AppButton {
                                    visible: root.memoryCards
                                    text: ""
                                    iconName: "copy"
                                    toolTipText: I18n.get("memory_card_duplicate_action")
                                    onClicked: {
                                        root.selectedPath = entryDelegate.modelData.path
                                        root.selectedName = entryDelegate.modelData.name
                                        root.operation = "duplicate"
                                        nameDialog.open()
                                    }
                                }
                                AppButton {
                                    visible: root.memoryCards
                                    text: ""
                                    iconName: "tune"
                                    toolTipText: I18n.get("memory_card_rename_action")
                                    onClicked: {
                                        root.selectedPath = entryDelegate.modelData.path
                                        root.selectedName = entryDelegate.modelData.name
                                        root.operation = "rename"
                                        nameDialog.open()
                                    }
                                }
                                AppButton {
                                    visible: root.memoryCards
                                    text: ""
                                    iconName: "export"
                                    toolTipText: I18n.get("memory_card_export_action")
                                    onClicked: {
                                        root.selectedPath = entryDelegate.modelData.path
                                        root.selectedName = entryDelegate.modelData.name
                                        exportDialog.open()
                                    }
                                }
                                AppButton {
                                    text: ""
                                    iconName: "trash"
                                    danger: true
                                    toolTipText: root.deleteLabel
                                    onClicked: {
                                        root.selectedPath = entryDelegate.modelData.path
                                        root.selectedName = entryDelegate.modelData.name
                                        deleteDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 20
                width: noticeText.implicitWidth + 34
                height: 42
                radius: 21
                color: Theme.surfaceActive
                border.width: 1
                border.color: Theme.borderStrong
                visible: root.notice.length > 0
                opacity: visible ? 1 : 0
                Text { id: noticeText; anchors.centerIn: parent; text: root.notice; color: Theme.text; font.pixelSize: 12 }
                Behavior on opacity { NumberAnimation { duration: Theme.duration } }
            }
        }
    }
}

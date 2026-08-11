import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property bool listMode: Preferences.value("library/viewMode", "grid") === "list"

    function coverSources(serial) {
        return CoverArtProvider.urlsForSerial(serial, Preferences.coverArtStyle)
    }

    function formatFileSize(bytes) {
        if (!bytes || bytes <= 0)
            return ""
        const gib = bytes / (1024 * 1024 * 1024)
        if (gib >= 1)
            return Number(gib.toFixed(gib >= 10 ? 1 : 2)) + " GB"
        return Number((bytes / (1024 * 1024)).toFixed(1)) + " MB"
    }

    component LibraryActions: RowLayout {
        property bool compactLabels: gameGrid.availableWidth < 1180
        spacing: 10

        AppButton {
            text: ""
            iconName: "library"
            toolTipText: I18n.get("home_view_grid")
            primary: !root.listMode
            onClicked: {
                root.listMode = false
                Preferences.setValue("library/viewMode", "grid")
            }
        }
        AppButton {
            text: ""
            iconName: "menu"
            toolTipText: I18n.get("home_view_list")
            primary: root.listMode
            onClicked: {
                root.listMode = true
                Preferences.setValue("library/viewMode", "list")
            }
        }
        AppButton {
            text: parent.compactLabels ? "" : I18n.get("home_refresh")
            iconName: "refresh"
            toolTipText: I18n.get("home_refresh")
            enabled: !GameLibrary.scanning
            onClicked: GameLibrary.refresh()
        }
        AppButton {
            text: parent.compactLabels ? "" : I18n.get("settings_game_path")
            iconName: "folder"
            toolTipText: I18n.get("shell_manage_folders")
            primary: true
            onClicked: gameFoldersDialog.open()
        }
    }

    GameFoldersDialog { id: gameFoldersDialog }

    FolderDialog {
        id: folderDialog
        title: I18n.get("home_add_folder")
        onAccepted: GameLibrary.addFolder(selectedFolder)
    }

    GridView {
        id: gameGrid
        anchors.fill: parent
        leftMargin: 22
        rightMargin: 22
        bottomMargin: 18
        clip: true
        model: GameLibrary
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

        readonly property real availableWidth: Math.max(1, width - leftMargin - rightMargin)
        readonly property real desiredWidth: root.listMode ? availableWidth : 190 * Preferences.gridScale
        readonly property int columnCount: Math.max(1, Math.floor(availableWidth / desiredWidth))
        cellWidth: availableWidth / columnCount
        cellHeight: root.listMode ? 82 : 332 * Preferences.gridScale + 18

        header: Item {
            width: gameGrid.availableWidth
            height: headerColumn.implicitHeight + 34

            ColumnLayout {
                id: headerColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 24
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: I18n.get("home_title")
                            color: Theme.text
                            font.pixelSize: Theme.sp(28)
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.get("home_library_desc")
                            color: Theme.textMuted
                            font.pixelSize: Theme.sp(13)
                            elide: Text.ElideRight
                        }
                        Text {
                            text: I18n.format("home_game_count", [GameLibrary.totalCount])
                            color: Theme.textDim
                            font.pixelSize: Theme.sp(11)
                        }
                    }

                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    AppButton {
                        text: I18n.get("gamedb_browser_filter_all")
                        iconName: "library"
                        primary: !GameLibrary.favoritesOnly
                        onClicked: GameLibrary.favoritesOnly = false
                    }
                    AppButton {
                        text: I18n.get("hub_tab_favorites")
                        iconName: "star"
                        primary: GameLibrary.favoritesOnly
                        onClicked: GameLibrary.favoritesOnly = true
                    }
                    Item { Layout.fillWidth: true }
                    LibraryActions { }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.topMargin: 2
                    color: Theme.border
                    opacity: 0.72
                }
            }
        }

        delegate: Item {
            id: delegateRoot
            required property int index
            required property string title
            required property string path
            required property string serial
            required property string region
            required property real fileSize
            required property bool favorite
            width: gameGrid.cellWidth
            height: gameGrid.cellHeight

            AppCard {
                id: gameCard
                anchors.fill: parent
                anchors.margins: root.listMode ? 0 : 7
                interactive: true
                radius: root.listMode ? 12 : Theme.radiusLarge
                color: root.listMode
                    ? (cardHover.hovered ? Theme.surfaceHover : "transparent")
                    : (cardHover.hovered ? Theme.surfaceHover : Theme.surface)
                border.width: root.listMode ? 0 : 1
                border.color: delegateRoot.favorite ? Theme.accent : Theme.border
                scale: cardHover.hovered ? (root.listMode ? 1.003 : 1.018) : 1
                Behavior on scale {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic }
                }

                Loader {
                    anchors.fill: parent
                    anchors.margins: 8
                    sourceComponent: root.listMode ? listContent : gridContent
                }

                Component {
                    id: gridContent
                    ColumnLayout {
                        spacing: 8
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 190 * Preferences.gridScale
                            CoverArt {
                                anchors.fill: parent
                                sources: root.coverSources(delegateRoot.serial)
                                coverStyle: Preferences.coverArtStyle
                                cornerRadius: 16
                            }
                            AppButton {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 8
                                text: ""
                                iconName: delegateRoot.favorite ? "check" : "star"
                                toolTipText: I18n.get("hub_tab_favorites")
                                opacity: cardHover.hovered ? 1 : 0
                                visible: opacity > 0
                                onClicked: GameLibrary.toggleFavorite(delegateRoot.index)
                                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
                            Layout.topMargin: 2
                            text: delegateRoot.title
                            color: Theme.text
                            font.pixelSize: Theme.sp(13)
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
                            Layout.bottomMargin: 6
                            text: [delegateRoot.serial, delegateRoot.region].filter(Boolean).join("  ·  ")
                            color: Theme.textDim
                            font.pixelSize: Theme.sp(10)
                            elide: Text.ElideRight
                        }
                    }
                }

                Component {
                    id: listContent
                    RowLayout {
                        spacing: 13
                        CoverArt {
                            Layout.leftMargin: 7
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 62
                            sources: root.coverSources(delegateRoot.serial)
                            coverStyle: Preferences.coverArtStyle
                            cornerRadius: 10
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: delegateRoot.title
                                color: Theme.text
                                font.pixelSize: Theme.sp(14)
                                font.weight: Font.DemiBold
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: [delegateRoot.serial, delegateRoot.region].filter(Boolean).join("  ·  ")
                                color: Theme.textMuted
                                font.pixelSize: Theme.sp(11)
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            visible: gameGrid.availableWidth >= 760
                            text: root.formatFileSize(delegateRoot.fileSize)
                            color: Theme.textDim
                            font.pixelSize: Theme.sp(11)
                            horizontalAlignment: Text.AlignRight
                        }
                        AppButton {
                            Layout.rightMargin: 7
                            text: ""
                            iconName: delegateRoot.favorite ? "check" : "star"
                            toolTipText: I18n.get("hub_tab_favorites")
                            opacity: cardHover.hovered ? 1 : 0
                            visible: opacity > 0
                            onClicked: GameLibrary.toggleFavorite(delegateRoot.index)
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                        }
                    }
                }

                HoverHandler { id: cardHover }
                onClicked: {
                    if (Emulator.bootGame(delegateRoot.path))
                        App.navigate("emulation")
                }
            }

            Rectangle {
                visible: root.listMode
                anchors.left: parent.left
                anchors.leftMargin: 70
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
                opacity: 0.68
            }
        }

        Item {
            anchors.centerIn: parent
            visible: GameLibrary.count === 0
            width: Math.min(440, parent.width - 48)
            height: emptyColumn.implicitHeight

            ColumnLayout {
                id: emptyColumn
                anchors.fill: parent
                spacing: 12
                AppIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    name: GameLibrary.favoritesOnly ? "star" : "library"
                    color: Theme.accentBright
                }
                Text {
                    Layout.fillWidth: true
                    text: GameLibrary.favoritesOnly || GameLibrary.searchQuery.length > 0
                        ? I18n.get("home_empty_search_title") : I18n.get("home_empty_title")
                    color: Theme.text
                    font.pixelSize: Theme.sp(22)
                    font.weight: Font.Bold
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    Layout.fillWidth: true
                    text: GameLibrary.favoritesOnly || GameLibrary.searchQuery.length > 0
                        ? I18n.get("home_empty_search_subtitle") : I18n.get("home_empty_subtitle")
                    color: Theme.textMuted
                    font.pixelSize: Theme.sp(13)
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
                AppButton {
                    visible: GameLibrary.folders.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    text: I18n.get("home_add_folder")
                    iconName: "folder"
                    primary: true
                    onClicked: folderDialog.open()
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: GameLibrary.scanning
            visible: running
        }
    }
}

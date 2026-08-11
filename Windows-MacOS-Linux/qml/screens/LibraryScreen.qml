import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property int selectedIndex: -1
    property string selectedTitle: ""
    property string selectedPath: ""
    property string selectedSerial: ""
    property string selectedRegion: ""
    property string selectedSummary: ""
    property string selectedGenres: ""
    property string selectedHero: ""
    property int selectedYear: 0
    property real selectedRating: 0
    property real selectedSize: 0

    function selectGame(index, title, path, serial, region, summary, genres, hero, year, rating, size) {
        if (selectedPath === path) {
            clearSelection()
            return
        }
        selectedIndex = index
        selectedTitle = title
        selectedPath = path
        selectedSerial = serial
        selectedRegion = region
        selectedSummary = summary
        selectedGenres = genres
        selectedHero = hero
        selectedYear = year
        selectedRating = rating
        selectedSize = size
    }

    function clearSelection() {
        selectedIndex = -1
        selectedTitle = ""
        selectedPath = ""
        selectedSerial = ""
        selectedRegion = ""
        selectedSummary = ""
        selectedGenres = ""
        selectedHero = ""
        selectedYear = 0
        selectedRating = 0
        selectedSize = 0
    }

    function formatSize(bytes) {
        if (bytes <= 0) return ""
        const units = ["B", "KB", "MB", "GB", "TB"]
        const order = Math.min(units.length - 1, Math.floor(Math.log(bytes) / Math.log(1024)))
        return (bytes / Math.pow(1024, order)).toFixed(order >= 3 ? 2 : 1) + " " + units[order]
    }

    function coverFor(serial) {
        const url = GameCatalog.coverUrlForSerial(serial, Preferences.coverArtStyle)
        return url.length > 0 ? url + "?v=" + GameLibrary.coverRevision : ""
    }

    FolderDialog {
        id: folderDialog
        title: I18n.get("home_add_folder")
        onAccepted: GameLibrary.addFolder(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Image {
            parent: root
            anchors.fill: parent
            source: Preferences.backgroundPath
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            visible: Preferences.backgroundPath.length > 0 && status === Image.Ready
            opacity: 1 - Preferences.backgroundDim / 100
            z: -2
            Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 16
                visible: GameLibrary.count === 0
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 82
                    radius: 28
                    color: Theme.accentContainer
                    AppIcon { anchors.centerIn: parent; width: 38; height: 38; name: "library"; color: Theme.accentBright }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: I18n.get("home_empty_title")
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: I18n.get("home_empty_subtitle")
                    color: Theme.textMuted
                    font.pixelSize: 14
                }
                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    primary: true
                    iconName: "folder"
                    text: I18n.get("home_add_folder")
                    onClicked: folderDialog.open()
                }
            }

            RowLayout {
                anchors.fill: parent
                visible: GameLibrary.count > 0
                spacing: 0

                GridView {
                    id: grid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: GameLibrary
                    clip: true
                    leftMargin: 20
                    rightMargin: 20
                    topMargin: 0
                    bottomMargin: 20
                    readonly property real usableWidth: width - leftMargin - rightMargin
                    cellWidth: usableWidth / Math.max(2, Math.floor(usableWidth / (178 * Preferences.gridScale)))
                    cellHeight: 326 * Preferences.gridScale
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 7 }

                    header: Item {
                        width: grid.width
                        height: 152

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28
                            anchors.topMargin: 22
                            anchors.bottomMargin: 16
                            spacing: 10

                            PageHeader {
                                Layout.fillWidth: true
                                title: I18n.get("home_title")
                                subtitle: I18n.get("home_library_desc")
                                AppButton {
                                    iconName: "refresh"
                                    text: I18n.get("home_refresh")
                                    enabled: !GameLibrary.scanning
                                    onClicked: GameLibrary.refresh()
                                }
                                AppButton {
                                    primary: true
                                    iconName: "folder"
                                    text: I18n.get("home_add_folder")
                                    onClicked: folderDialog.open()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text {
                                    text: I18n.format("home_game_count", [GameLibrary.count])
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }
                                Item { Layout.fillWidth: true }
                                BusyIndicator {
                                    visible: GameLibrary.scanning
                                    running: visible
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                }
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

                    delegate: Item {
                        id: delegateRoot
                        width: grid.cellWidth
                        height: grid.cellHeight
                        required property int index
                        required property string title
                        required property string path
                        required property string serial
                        required property string region
                        required property string summary
                        required property string genres
                        required property string heroUrl
                        required property int year
                        required property real rating
                        required property real fileSize
                        required property bool favorite

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 7
                            radius: 22
                            color: cardHover.hovered || root.selectedPath === delegateRoot.path
                                ? Theme.surfaceHover : Theme.surface
                            border.width: root.selectedPath === delegateRoot.path ? 1.5 : 1
                            border.color: root.selectedPath === delegateRoot.path ? Theme.accent : Theme.border
                            clip: true
                            scale: cardTap.pressed ? 0.975 : 1

                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                            Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.margins: 7
                                    Layout.bottomMargin: 0

                                    CoverArt {
                                        anchors.fill: parent
                                        source: root.coverFor(delegateRoot.serial)
                                        coverStyle: Preferences.coverArtStyle
                                        cornerRadius: 16
                                        fallbackIcon: "play"
                                    }
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 9
                                        width: 32
                                        height: 32
                                        radius: 16
                                        color: Qt.rgba(0.03, 0.02, 0.025, 0.82)
                                        border.width: 1
                                        border.color: delegateRoot.favorite ? Theme.warning : Theme.border
                                        AppIcon {
                                            anchors.centerIn: parent
                                            width: 16
                                            height: 16
                                            name: "star"
                                            color: delegateRoot.favorite ? Theme.warning : Theme.textMuted
                                        }
                                        scale: favoriteTap.pressed ? 0.86 : 1
                                        Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
                                        opacity: cardHover.hovered ? 1 : 0
                                        enabled: opacity > 0.01
                                        Behavior on opacity { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
                                        TapHandler {
                                            id: favoriteTap
                                            onTapped: GameLibrary.toggleFavorite(delegateRoot.index)
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 64
                                    Layout.leftMargin: 13
                                    Layout.rightMargin: 13
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: delegateRoot.title
                                        color: Theme.text
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                        wrapMode: Text.WordWrap
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: [delegateRoot.year > 0 ? delegateRoot.year : "", delegateRoot.region].filter(Boolean).join("  ·  ")
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            HoverHandler { id: cardHover }
                            TapHandler {
                                id: cardTap
                                onTapped: root.selectGame(delegateRoot.index, delegateRoot.title, delegateRoot.path,
                                    delegateRoot.serial, delegateRoot.region, delegateRoot.summary,
                                    delegateRoot.genres, delegateRoot.heroUrl, delegateRoot.year,
                                    delegateRoot.rating, delegateRoot.fileSize)
                                onDoubleTapped: if (Emulator.bootGame(delegateRoot.path)) App.navigate("emulation")
                            }
                        }
                    }
                }

                Rectangle {
                    id: detailPanel
                    readonly property real openWidth: Math.max(360, Math.min(500, root.width * 0.31))
                    visible: Layout.preferredWidth > 1
                    Layout.preferredWidth: root.selectedPath.length > 0 && root.width >= 900 ? openWidth : 0
                    Layout.minimumWidth: 0
                    Layout.fillHeight: true
                    color: Theme.backgroundRaised
                    border.width: 1
                    border.color: Theme.border
                    clip: true
                    opacity: root.selectedPath.length > 0 ? 1 : 0

                    Behavior on Layout.preferredWidth { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic } }
                    Behavior on opacity { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }

                    Image {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 250
                        source: root.selectedHero
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? 0.34 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
                    }
                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 270
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#00101014" }
                            GradientStop { position: 1; color: Theme.backgroundRaised }
                        }
                    }

                    AppButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 16
                        z: 4
                        text: ""
                        iconName: "close"
                        toolTipText: I18n.get("common_close")
                        onClicked: root.clearSelection()
                    }

                    ScrollView {
                        anchors.fill: parent
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            x: 22
                            width: Math.max(0, detailPanel.width - 44)
                            spacing: 14
                            Item { Layout.preferredHeight: 8 }

                            CoverArt {
                                visible: root.selectedPath.length > 0
                                Layout.preferredWidth: 118
                                Layout.preferredHeight: 170
                                Layout.alignment: Qt.AlignHCenter
                                source: root.coverFor(root.selectedSerial)
                                coverStyle: Preferences.coverArtStyle
                                cornerRadius: 18
                            }
                            Text {
                                visible: root.selectedPath.length > 0
                                Layout.fillWidth: true
                                text: root.selectedTitle
                                color: Theme.text
                                font.pixelSize: 22
                                font.weight: Font.Bold
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                visible: root.selectedPath.length > 0
                                Layout.fillWidth: true
                                text: [root.selectedGenres, root.selectedYear > 0 ? root.selectedYear : ""].filter(Boolean).join("  ·  ")
                                color: Theme.textMuted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }

                            AppButton {
                                visible: root.selectedPath.length > 0
                                Layout.fillWidth: true
                                primary: true
                                iconName: "play"
                                text: I18n.get("detail_play")
                                onClicked: if (Emulator.bootGame(root.selectedPath)) App.navigate("emulation")
                            }

                            Rectangle {
                                visible: root.selectedPath.length > 0
                                Layout.fillWidth: true
                                implicitHeight: infoColumn.implicitHeight + 32
                                radius: 20
                                color: Theme.surface
                                border.width: 1
                                border.color: Theme.border
                                ColumnLayout {
                                    id: infoColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 17
                                    anchors.rightMargin: 17
                                    spacing: 10
                                    Text { text: I18n.get("detail_overview"); color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.selectedSummary.length > 0 ? root.selectedSummary : I18n.get("detail_no_data_body")
                                        color: Theme.textMuted
                                        font.pixelSize: 12
                                        lineHeight: 1.25
                                        wrapMode: Text.WordWrap
                                    }
                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: I18n.get("detail_serial"); color: Theme.textMuted; font.pixelSize: 11 }
                                        Item { Layout.fillWidth: true }
                                        Text { text: root.selectedSerial; color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: I18n.get("detail_file_size"); color: Theme.textMuted; font.pixelSize: 11 }
                                        Item { Layout.fillWidth: true }
                                        Text { text: root.formatSize(root.selectedSize); color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    }
                                }
                            }
                            Text {
                                visible: root.selectedPath.length > 0
                                Layout.fillWidth: true
                                text: root.selectedPath
                                color: Theme.textDim
                                font.pixelSize: 10
                                elide: Text.ElideMiddle
                            }
                            Item { Layout.preferredHeight: 10 }
                        }
                    }
                }
            }
        }
    }
}

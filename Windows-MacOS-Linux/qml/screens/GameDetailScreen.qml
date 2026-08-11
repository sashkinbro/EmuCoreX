import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property var details: ({})
    property var localGame: ({})

    function reload() {
        details = GameCatalog.detailsForId(App.selectedCatalogGameId)
        const identitySerials = details.serials || []
        if (identitySerials.length === 0 && details.primarySerial)
            identitySerials.push(details.primarySerial)
        localGame = GameLibrary.gameForSerials(identitySerials)
    }

    function formatSize(bytes) {
        if (!bytes || bytes <= 0) return ""
        const units = ["B", "KB", "MB", "GB", "TB"]
        const order = Math.min(units.length - 1, Math.floor(Math.log(bytes) / Math.log(1024)))
        return (bytes / Math.pow(1024, order)).toFixed(order >= 3 ? 2 : 1) + " " + units[order]
    }

    Component.onCompleted: reload()
    Connections { target: App; function onSelectedGameChanged() { root.reload() } }

    RoundedImage {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.min(430, parent.height * 0.48)
        source: root.details.heroUrl || ""
        radius: 0
        opacity: 0.36
    }
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.min(470, parent.height * 0.54)
        gradient: Gradient {
            GradientStop { position: 0; color: "#00101014" }
            GradientStop { position: 0.78; color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.82) }
            GradientStop { position: 1; color: Theme.background }
        }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentHeight: content.implicitHeight + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 7 }

        ColumnLayout {
            id: content
            width: Math.min(page.width - 64, 1260)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 34
            spacing: 22

            RowLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: 330
                spacing: 30

                CoverArt {
                    Layout.preferredWidth: 220
                    Layout.preferredHeight: 314
                    Layout.alignment: Qt.AlignBottom
                    sources: CoverArtProvider.urlsForSerial(root.localGame.path ? root.localGame.serial : (root.details.primarySerial || ""), Preferences.coverArtStyle)
                    coverStyle: Preferences.coverArtStyle
                    cornerRadius: 24
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: root.details.name || I18n.get("detail_no_data_title")
                        color: Theme.text
                        font.pixelSize: Theme.sp(38)
                        font.weight: Font.Bold
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: [root.details.genres || "", root.details.year || ""].filter(Boolean).join("  ·  ")
                        color: Theme.textMuted
                        font.pixelSize: Theme.sp(14)
                    }

                    RowLayout {
                        spacing: 10
                        Rectangle {
                            visible: Number(root.details.rating || 0) > 0
                            Layout.preferredWidth: ratingRow.implicitWidth + 24
                            Layout.preferredHeight: 38
                            radius: 19
                            color: Theme.surfaceActive
                            Row {
                                id: ratingRow
                                anchors.centerIn: parent
                                spacing: 7
                                AppIcon { width: 16; height: 16; name: "star"; color: Theme.warning }
                                Text { text: Math.round(Number(root.details.rating || 0)) + "%"; color: Theme.text; font.pixelSize: Theme.sp(13); font.weight: Font.DemiBold }
                            }
                        }
                        Repeater {
                            model: root.details.serials || []
                            Rectangle {
                                required property string modelData
                                Layout.preferredWidth: serialLabel.implicitWidth + 22
                                Layout.preferredHeight: 38
                                radius: 19
                                color: Theme.surface
                                border.width: 1
                                border.color: Theme.border
                                Text { id: serialLabel; anchors.centerIn: parent; text: modelData; color: Theme.textMuted; font.pixelSize: Theme.sp(12) }
                            }
                        }
                    }

                    RowLayout {
                        spacing: 10
                        AppButton {
                            visible: Boolean(root.localGame.path)
                            primary: true
                            iconName: "play"
                            text: I18n.get("detail_play")
                            onClicked: if (Emulator.bootGame(root.localGame.path)) App.navigate("emulation")
                        }
                        AppButton {
                            visible: Boolean(root.localGame.path)
                            iconName: "star"
                            text: I18n.get("hub_tab_favorites")
                            onClicked: {
                                GameLibrary.toggleFavoritePath(root.localGame.path)
                                root.reload()
                            }
                        }
                        AppButton {
                            visible: Boolean(root.localGame.path)
                            iconName: "tune"
                            text: I18n.get("home_game_menu_manager")
                            onClicked: App.navigate("game-manager")
                        }
                    }

                    Text {
                        visible: !root.localGame.path
                        Layout.fillWidth: true
                        text: I18n.get("detail_catalog_preview_only")
                        color: Theme.textMuted
                        font.pixelSize: Theme.sp(13)
                        wrapMode: Text.WordWrap
                    }
                }
            }

            AppCard {
                Layout.fillWidth: true
                implicitHeight: overview.implicitHeight + 42
                ColumnLayout {
                    id: overview
                    anchors.fill: parent
                    anchors.margins: 21
                    spacing: 12
                    Text { text: I18n.get("detail_overview"); color: Theme.text; font.pixelSize: Theme.sp(18); font.weight: Font.Bold }
                    Text {
                        Layout.fillWidth: true
                        text: root.details.summary || I18n.get("detail_no_data_body")
                        color: Theme.textMuted
                        font.pixelSize: Theme.sp(14)
                        lineHeight: 1.35
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.details.storyline)
                        Layout.fillWidth: true
                        text: root.details.storyline || ""
                        color: Theme.textDim
                        font.pixelSize: Theme.sp(13)
                        lineHeight: 1.3
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Text {
                visible: (root.details.screenshots || []).length > 0
                text: I18n.get("detail_screenshots")
                color: Theme.text
                font.pixelSize: Theme.sp(20)
                font.weight: Font.Bold
            }
            ListView {
                visible: (root.details.screenshots || []).length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                orientation: ListView.Horizontal
                spacing: 14
                clip: true
                model: root.details.screenshots || []
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                delegate: RoundedImage {
                    required property string modelData
                    width: Math.min(380, page.width * 0.35)
                    height: 214
                    radius: 20
                    source: modelData
                }
            }

            Text {
                visible: (root.details.videos || []).length > 0
                text: I18n.get("detail_videos")
                color: Theme.text
                font.pixelSize: Theme.sp(20)
                font.weight: Font.Bold
            }
            ListView {
                visible: (root.details.videos || []).length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                orientation: ListView.Horizontal
                spacing: 14
                clip: true
                model: root.details.videos || []
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                delegate: Item {
                    required property string modelData
                    width: 320
                    height: 180
                    RoundedImage { anchors.fill: parent; radius: 20; source: "https://i.ytimg.com/vi/" + modelData + "/hqdefault.jpg" }
                    Rectangle { anchors.centerIn: parent; width: 58; height: 58; radius: 29; color: Qt.rgba(0.05, 0.03, 0.035, 0.82); AppIcon { anchors.centerIn: parent; width: 25; height: 25; name: "play"; color: "white" } }
                    HoverHandler { id: videoHover }
                    TapHandler { onTapped: App.openExternalUrl("https://www.youtube.com/watch?v=" + modelData) }
                    scale: videoHover.hovered ? 1.015 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                }
            }

            Text {
                Layout.fillWidth: true
                text: I18n.get("detail_igdb_source_note")
                color: Theme.textDim
                font.pixelSize: Theme.sp(11)
                wrapMode: Text.WordWrap
            }
        }
    }
}

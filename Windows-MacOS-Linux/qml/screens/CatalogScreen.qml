import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property int selectedId: 0
    property string selectedName: ""
    property int selectedYear: 0
    property real selectedRating: 0
    property string selectedSummary: ""
    property string selectedStoryline: ""
    property string selectedCover: ""
    property string selectedHero: ""
    property string selectedGenres: ""
    property string selectedSerial: ""

    function selectGame(id, name, year, rating, summary, storyline, cover, hero, genres, serial) {
        if (selectedId === id) {
            clearSelection()
            return
        }
        selectedId = id
        selectedName = name
        selectedYear = year
        selectedRating = rating
        selectedSummary = summary
        selectedStoryline = storyline
        selectedCover = cover
        selectedHero = hero
        selectedGenres = genres
        selectedSerial = serial
    }

    function clearSelection() {
        selectedId = 0
        selectedName = ""
        selectedYear = 0
        selectedRating = 0
        selectedSummary = ""
        selectedStoryline = ""
        selectedCover = ""
        selectedHero = ""
        selectedGenres = ""
        selectedSerial = ""
    }

    Timer {
        id: searchDelay
        interval: 180
        onTriggered: GameCatalog.searchQuery = searchField.text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 24
            Layout.bottomMargin: 18
            spacing: 16

            PageHeader {
                Layout.fillWidth: true
                title: I18n.get("catalog_search_title")
                subtitle: I18n.format("catalog_search_subtitle", [GameCatalog.count])
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.maximumWidth: 680
                Layout.preferredHeight: 48
                radius: 18
                color: Theme.surface
                border.width: searchField.activeFocus ? 1.5 : 1
                border.color: searchField.activeFocus ? Theme.accent : Theme.border

                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 10
                    spacing: 10
                    AppIcon {
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        name: "search"
                        color: searchField.activeFocus ? Theme.accentBright : Theme.textDim
                    }
                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        placeholderText: I18n.get("catalog_search_hint")
                        selectByMouse: true
                        background: null
                        onTextChanged: searchDelay.restart()
                    }
                    ToolButton {
                        visible: searchField.text.length > 0
                        icon.name: "edit-clear"
                        text: "×"
                        font.pixelSize: 22
                        onClicked: searchField.clear()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textMuted
                            font: parent.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 12
                            color: parent.hovered ? Theme.surfaceHover : "transparent"
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    GridView {
                        id: catalogGrid
                        anchors.fill: parent
                        leftMargin: 20
                        rightMargin: 20
                        topMargin: 20
                        bottomMargin: 20
                        clip: true
                        model: GameCatalog
                        boundsBehavior: Flickable.StopAtBounds
                        readonly property real usableWidth: width - leftMargin - rightMargin
                        cellWidth: usableWidth / Math.max(2, Math.floor(usableWidth / 178))
                        cellHeight: 326
                        highlightMoveDuration: Theme.duration
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            width: 7
                        }

                        delegate: Item {
                            id: delegateRoot
                            width: catalogGrid.cellWidth
                            height: catalogGrid.cellHeight
                            required property int index
                            required property int catalogId
                            required property string catalogName
                            required property int catalogYear
                            required property real catalogRating
                            required property string catalogSummary
                            required property string catalogStoryline
                            required property string catalogCoverUrl
                            required property string catalogHeroUrl
                            required property string catalogGenres
                            required property string catalogPrimarySerial

                            Rectangle {
                                id: gameCard
                                anchors.fill: parent
                                anchors.margins: 7
                                radius: 22
                                color: cardHover.hovered || root.selectedId === delegateRoot.catalogId
                                    ? Theme.surfaceHover : Theme.surface
                                border.width: root.selectedId === delegateRoot.catalogId ? 1.5 : 1
                                border.color: root.selectedId === delegateRoot.catalogId ? Theme.accent : Theme.border
                                clip: true
                                scale: cardTap.pressed ? 0.975 : 1

                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 0

                                    CoverArt {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.margins: 7
                                        Layout.bottomMargin: 0
                                        source: delegateRoot.catalogCoverUrl
                                        coverStyle: 1
                                        cornerRadius: 16
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 66
                                        Layout.leftMargin: 13
                                        Layout.rightMargin: 13
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: delegateRoot.catalogName
                                            color: Theme.text
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            wrapMode: Text.WordWrap
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: [delegateRoot.catalogYear > 0 ? delegateRoot.catalogYear : "",
                                                delegateRoot.catalogGenres.split(" · ")[0]].filter(Boolean).join("  ·  ")
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                HoverHandler { id: cardHover }
                                TapHandler {
                                    id: cardTap
                                    onTapped: root.selectGame(delegateRoot.catalogId, delegateRoot.catalogName,
                                        delegateRoot.catalogYear, delegateRoot.catalogRating,
                                        delegateRoot.catalogSummary, delegateRoot.catalogStoryline,
                                        delegateRoot.catalogCoverUrl, delegateRoot.catalogHeroUrl,
                                        delegateRoot.catalogGenres, delegateRoot.catalogPrimarySerial)
                                    onDoubleTapped: App.openGameDetails(delegateRoot.catalogId)
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: GameCatalog.available && GameCatalog.count === 0
                        spacing: 14
                        AppIcon { Layout.alignment: Qt.AlignHCenter; width: 48; height: 48; name: "search"; color: Theme.textDim }
                        Text { text: I18n.get("detail_no_data_title"); color: Theme.text; font.pixelSize: 20; font.weight: Font.Bold }
                        Text { text: I18n.get("detail_no_data_body"); color: Theme.textMuted; font.pixelSize: 13 }
                    }
                }

                Rectangle {
                    id: detailPanel
                    readonly property real openWidth: Math.max(360, Math.min(500, root.width * 0.31))
                    property real animatedWidth: root.selectedId !== 0 && root.width >= 1120 ? openWidth : 0
                    visible: animatedWidth > 1
                    Layout.preferredWidth: animatedWidth
                    Layout.fillHeight: true
                    color: Theme.backgroundRaised
                    border.width: 1
                    border.color: Theme.border
                    clip: true

                    Behavior on animatedWidth {
                        NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
                    }

                    Image {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 260
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
                        height: 280
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#00101014" }
                            GradientStop { position: 1; color: Theme.backgroundRaised }
                        }
                    }

                    ScrollView {
                        anchors.fill: parent
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            x: 24
                            width: Math.max(0, detailPanel.width - 48)
                            spacing: 16

                            Item { Layout.preferredHeight: 8 }

                            AppButton {
                                Layout.alignment: Qt.AlignRight
                                iconName: "close"
                                iconOnly: true
                                accessibleName: I18n.get("common_close")
                                onClicked: root.clearSelection()
                            }

                            CoverArt {
                                visible: root.selectedId !== 0
                                Layout.preferredWidth: 116
                                Layout.preferredHeight: 168
                                Layout.alignment: Qt.AlignHCenter
                                source: root.selectedCover
                                coverStyle: 1
                                cornerRadius: 18
                            }
                            Text {
                                visible: root.selectedId !== 0
                                Layout.fillWidth: true
                                text: root.selectedName
                                color: Theme.text
                                font.pixelSize: 23
                                font.weight: Font.Bold
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }

                            AppButton {
                                visible: root.selectedId !== 0
                                Layout.fillWidth: true
                                primary: true
                                iconName: "library"
                                text: I18n.get("common_open")
                                onClicked: App.openGameDetails(root.selectedId)
                            }
                            Text {
                                visible: root.selectedId !== 0
                                Layout.fillWidth: true
                                text: [root.selectedGenres, root.selectedYear > 0 ? root.selectedYear : ""].filter(Boolean).join("  ·  ")
                                color: Theme.textMuted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Rectangle {
                                visible: root.selectedId !== 0 && root.selectedRating > 0
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: ratingRow.implicitWidth + 24
                                Layout.preferredHeight: 34
                                radius: 17
                                color: Theme.surfaceVariant
                                Row {
                                    id: ratingRow
                                    anchors.centerIn: parent
                                    spacing: 7
                                    AppIcon { width: 15; height: 15; name: "star"; color: Theme.warning }
                                    Text { text: Math.round(root.selectedRating) + "%"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                                }
                            }

                            Rectangle {
                                visible: root.selectedId !== 0
                                Layout.fillWidth: true
                                implicitHeight: overviewColumn.implicitHeight + 36
                                radius: 20
                                color: Theme.surface
                                border.width: 1
                                border.color: Theme.border
                                ColumnLayout {
                                    id: overviewColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 18
                                    anchors.rightMargin: 18
                                    spacing: 9
                                    Text { text: I18n.get("detail_overview"); color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.selectedSummary.length > 0 ? root.selectedSummary : I18n.get("detail_no_data_body")
                                        color: Theme.textMuted
                                        font.pixelSize: 12
                                        lineHeight: 1.28
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            Text {
                                visible: root.selectedId !== 0
                                Layout.fillWidth: true
                                text: I18n.get("detail_igdb_source_note")
                                color: Theme.textDim
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            Item { Layout.preferredHeight: 12 }
                        }
                    }
                }
            }
        }
    }
}

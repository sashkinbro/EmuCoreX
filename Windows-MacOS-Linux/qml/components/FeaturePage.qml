import QtQuick
import QtQuick.Layouts
import "../theme"

Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string iconName: "settings"
    property var cards: []

    Flickable {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            width: Math.min(parent.width - 48, Theme.contentMaxWidth)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 28
            spacing: 22

            PageHeader { Layout.fillWidth: true; title: root.title; subtitle: root.subtitle }

            GridLayout {
                Layout.fillWidth: true
                columns: width >= 980 ? 3 : (width >= 620 ? 2 : 1)
                rowSpacing: 14
                columnSpacing: 14
                Repeater {
                    model: root.cards
                    AppCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        interactive: true
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                radius: 12
                                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
                                AppIcon { anchors.centerIn: parent; width: 21; height: 21; name: modelData.icon || root.iconName; color: Theme.accentBright }
                            }
                            Text { text: modelData.title; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: modelData.description || ""; color: Theme.textMuted; font.pixelSize: 12; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        }
                        onClicked: if (modelData.route) App.navigate(modelData.route)
                    }
                }
            }
        }
    }
}


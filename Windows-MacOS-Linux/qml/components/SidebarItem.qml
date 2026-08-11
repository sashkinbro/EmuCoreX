import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    property string iconName: "library"
    property string label: ""
    property bool selected: false
    property bool compact: false
    signal clicked()

    implicitHeight: compact ? 44 : Math.max(44, labelText.implicitHeight + 18)
    radius: Theme.radius
    color: selected ? Theme.surfaceActive : (hover.hovered ? Theme.surfaceHover : "transparent")
    border.width: selected ? 1 : 0
    border.color: Theme.borderStrong

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: compact ? 17 : 14
        anchors.rightMargin: 12
        spacing: 13
        AppIcon {
            Layout.preferredWidth: 20; Layout.preferredHeight: 20
            name: root.iconName
            color: root.selected ? Theme.accentBright : Theme.textMuted
        }
        Text {
            id: labelText
            visible: !root.compact
            Layout.fillWidth: true
            text: root.label
            color: root.selected ? Theme.text : Theme.textMuted
            font.pixelSize: 13
            font.weight: root.selected ? Font.DemiBold : Font.Normal
            maximumLineCount: 2
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            lineHeight: 1.08
            verticalAlignment: Text.AlignVCenter
        }
    }
    HoverHandler { id: hover }
    TapHandler { onTapped: root.clicked() }
    ToolTip.visible: compact && hover.hovered
    ToolTip.text: label
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
}

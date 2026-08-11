import QtQuick
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: root
    property string title: ""
    property string subtitle: ""
    property string eyebrow: ""
    default property alias actions: actionsRow.data
    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        Text {
            visible: root.eyebrow.length > 0
            text: root.eyebrow.toUpperCase()
            color: Theme.accentBright
            font.pixelSize: Theme.sp(11)
            font.weight: Font.Bold
            font.letterSpacing: 1.2
        }
        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.sp(28)
            font.weight: Font.Bold
        }
        Text {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textMuted
            font.pixelSize: Theme.sp(14)
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 760
        }
    }
    RowLayout { id: actionsRow; spacing: 10 }
}


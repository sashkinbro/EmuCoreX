import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control
    property string iconName: ""
    property bool primary: false
    property bool danger: false

    implicitHeight: 42
    implicitWidth: Math.max(112, contentRow.implicitWidth + 32)
    hoverEnabled: true
    font.pixelSize: 14
    font.weight: Font.DemiBold

    contentItem: Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 9
        AppIcon {
            visible: control.iconName.length > 0
            width: 18; height: 18
            name: control.iconName
            color: control.primary ? "white" : (control.danger ? Theme.error : Theme.text)
        }
        Text {
            text: control.text
            color: control.primary ? "white" : (control.danger ? Theme.error : Theme.text)
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
            opacity: control.enabled ? 1 : 0.62
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    background: Rectangle {
        radius: 14
        color: control.primary
            ? (control.down ? Qt.darker(Theme.accent, 1.15) : Theme.accent)
            : (control.hovered ? Theme.surfaceHover : Theme.surface)
        border.width: control.primary ? 0 : 1
        border.color: control.danger ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5) : Theme.border
        opacity: control.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    scale: control.down ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
    Behavior on implicitWidth { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
}

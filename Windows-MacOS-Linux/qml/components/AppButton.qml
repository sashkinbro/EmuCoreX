import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: control
    property string iconName: ""
    property bool primary: false
    property bool danger: false
    property string toolTipText: ""
    readonly property bool iconOnly: text.length === 0 && iconName.length > 0

    implicitHeight: Math.max(44, Theme.sp(18) + 26)
    implicitWidth: iconOnly ? implicitHeight : Math.max(104, contentRow.implicitWidth + 34)
    leftPadding: iconOnly ? 0 : 17
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.sp(14)
    font.weight: Font.DemiBold

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: control.implicitHeight

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: control.iconOnly ? 0 : 9

            AppIcon {
                visible: control.iconName.length > 0
                width: 18
                height: 18
                name: control.iconName
                color: control.primary ? "white" : (control.danger ? Theme.error : Theme.text)
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: !control.iconOnly
                text: control.text
                color: control.primary ? "white" : (control.danger ? Theme.error : Theme.text)
                font: control.font
                renderType: Text.NativeRendering
                verticalAlignment: Text.AlignVCenter
                anchors.verticalCenter: parent.verticalCenter
                opacity: control.enabled ? 1 : 0.62

                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            }
        }
    }

    ToolTip {
        visible: control.hovered && control.toolTipText.length > 0
        text: control.toolTipText
        delay: 550
    }

    background: Rectangle {
        radius: control.iconOnly ? 13 : 14
        color: {
            if (control.primary)
                return control.down ? Qt.darker(Theme.accent, 1.12) : (control.hovered ? Theme.accentBright : Theme.accent)
            if (control.down)
                return Theme.surfaceActive
            return control.hovered ? Theme.surfaceHover : Theme.surface
        }
        border.width: control.activeFocus ? 2 : (control.primary ? 0 : 1)
        border.color: control.activeFocus ? Theme.accentBright
            : (control.danger ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.52) : Theme.border)
        opacity: control.enabled ? 1 : 0.45

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }

    scale: control.down ? 0.965 : (control.hovered ? 1.015 : 1)
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
    Behavior on implicitWidth { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
}

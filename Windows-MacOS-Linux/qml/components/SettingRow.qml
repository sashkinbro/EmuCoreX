import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    property string title: ""
    property string description: ""
    property string iconName: "settings"
    property string controlType: "switch"
    property bool checked: false
    property var options: []
    property string currentValue: ""
    property real numericValue: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property string valueSuffix: ""
    signal toggled(bool value)
    signal valueSelected(string value)
    signal numberSelected(real value)
    signal actionTriggered()

    implicitHeight: Math.max(description.length > 0 ? 92 : 72, rowLayout.implicitHeight + 28)
    color: hover.hovered ? Theme.surfaceHover : Theme.surface
    radius: 22
    border.width: 1
    border.color: hover.hovered ? Theme.borderStrong : Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.72)
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 14
        Rectangle {
            Layout.preferredWidth: 42; Layout.preferredHeight: 42
            radius: 21
            color: Theme.accentContainer
            AppIcon { anchors.centerIn: parent; width: 20; height: 20; name: root.iconName; color: Theme.accentBright }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text { text: root.title; color: Theme.text; font.pixelSize: Theme.sp(14); font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
            Text {
                visible: root.description.length > 0
                text: root.description
                color: Theme.textMuted
                font.pixelSize: Theme.sp(12)
                Layout.fillWidth: true
                maximumLineCount: 2
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
            }
        }
        Switch {
            id: toggleControl
            visible: root.controlType === "switch"
            Layout.preferredWidth: 50
            Layout.minimumWidth: 50
            Layout.maximumWidth: 50
            Layout.preferredHeight: 32
            Layout.rightMargin: 4
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            checked: root.checked
            onToggled: root.toggled(checked)
            indicator: Rectangle {
                width: 48
                implicitHeight: 28
                x: 1
                y: (toggleControl.height - height) / 2
                radius: 14
                color: toggleControl.checked ? Theme.accent : Theme.surfaceVariant
                border.width: toggleControl.activeFocus ? 2 : 1
                border.color: toggleControl.activeFocus ? Theme.text : (toggleControl.checked ? Theme.accentBright : Theme.border)
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                Rectangle {
                    width: 20; height: 20; radius: 10
                    y: 4
                    x: toggleControl.checked ? parent.width - width - 4 : 4
                    color: toggleControl.checked ? "white" : Theme.textMuted
                    scale: toggleControl.down ? 0.88 : 1
                    Behavior on x { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack } }
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                }
            }
            contentItem: Item { }
        }
        ComboBox {
            id: comboControl
            visible: root.controlType === "combo"
            model: root.options
            currentIndex: Math.max(0, root.options.indexOf(root.currentValue))
            implicitWidth: 190
            implicitHeight: 44
            onActivated: root.valueSelected(currentText)
            contentItem: Text {
                leftPadding: 15
                rightPadding: 38
                text: comboControl.displayText
                color: Theme.text
                font.pixelSize: Theme.sp(13)
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            indicator: AppIcon {
                x: comboControl.width - width - 14
                y: (comboControl.height - height) / 2
                width: 16; height: 16
                name: comboControl.popup.visible ? "back" : "menu"
                rotation: comboControl.popup.visible ? 90 : 0
                color: Theme.textMuted
                Behavior on rotation { NumberAnimation { duration: Theme.durationFast } }
            }
            background: Rectangle {
                radius: 14
                color: comboControl.hovered ? Theme.surfaceHover : Theme.surfaceVariant
                border.width: 1
                border.color: comboControl.activeFocus ? Theme.accent : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: comboControl.width
                height: 44
                highlighted: comboControl.highlightedIndex === index
                contentItem: Text {
                    text: modelData
                    color: Theme.text
                    font.pixelSize: Theme.sp(13)
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    leftPadding: 10
                    rightPadding: 10
                }
                background: Rectangle {
                    radius: 12
                    color: parent.highlighted ? Theme.accentContainer : (parent.hovered ? Theme.surfaceHover : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
            }
            popup: Popup {
                y: comboControl.height + 6
                width: comboControl.width
                implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 390)
                padding: 6
                topPadding: 6
                bottomPadding: 6
                enter: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
                        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationFast; easing.type: Easing.OutCubic }
                    }
                }
                exit: Transition {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
                }
                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: comboControl.popup.visible ? comboControl.delegateModel : null
                    currentIndex: comboControl.highlightedIndex
                    spacing: 2
                    ScrollIndicator.vertical: ScrollIndicator { }
                }
                background: Rectangle {
                    radius: 18
                    color: Theme.backgroundRaised
                    border.width: 1
                    border.color: Theme.borderStrong
                }
            }
        }
        RowLayout {
            visible: root.controlType === "slider"
            Layout.preferredWidth: Math.min(310, Math.max(210, root.width * 0.3))
            spacing: 10
            Slider {
                id: sliderControl
                Layout.fillWidth: true
                from: root.from
                to: root.to
                stepSize: root.stepSize
                value: root.numericValue
                live: true
                onMoved: root.numberSelected(value)
                background: Rectangle {
                    x: sliderControl.leftPadding
                    y: sliderControl.topPadding + sliderControl.availableHeight / 2 - height / 2
                    width: sliderControl.availableWidth
                    height: 5
                    radius: 3
                    color: Theme.surfaceVariant
                    Rectangle {
                        width: sliderControl.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent
                    }
                }
                handle: Rectangle {
                    x: sliderControl.leftPadding + sliderControl.visualPosition * (sliderControl.availableWidth - width)
                    y: sliderControl.topPadding + sliderControl.availableHeight / 2 - height / 2
                    implicitWidth: 20
                    implicitHeight: 20
                    radius: 10
                    color: sliderControl.pressed ? Theme.accentBright : Theme.text
                    border.width: 3
                    border.color: Theme.accent
                    scale: sliderControl.pressed ? 1.12 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
                }
            }
            Rectangle {
                Layout.preferredWidth: 58
                Layout.preferredHeight: 34
                radius: 12
                color: Theme.surfaceVariant
                border.width: 1
                border.color: Theme.border
                Text {
                    anchors.centerIn: parent
                    text: Math.round(sliderControl.value) + root.valueSuffix
                    color: Theme.text
                    font.pixelSize: Theme.sp(12)
                    font.weight: Font.DemiBold
                }
            }
        }
        Row {
            visible: root.controlType === "colors"
            spacing: 9
            Repeater {
                model: root.options
                Rectangle {
                    required property string modelData
                    width: 30
                    height: 30
                    radius: 15
                    color: modelData
                    border.width: root.currentValue.toUpperCase() === modelData.toUpperCase() ? 3 : 1
                    border.color: root.currentValue.toUpperCase() === modelData.toUpperCase() ? Theme.text : Theme.border
                    scale: colorTap.pressed ? 0.86 : (colorHover.hovered ? 1.08 : 1)
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                    TapHandler { id: colorTap; onTapped: root.valueSelected(modelData) }
                    HoverHandler { id: colorHover }
                }
            }
        }
        AppButton {
            visible: root.controlType === "action"
            text: root.currentValue.length > 0 ? root.currentValue : I18n.get("common_open")
            onClicked: root.actionTriggered()
        }
    }
    HoverHandler { id: hover }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "../theme"

Item {
    id: root
    property url source
    property real radius: 18
    property int fillMode: Image.PreserveAspectCrop
    property string fallbackIcon: "image"
    property color backgroundColor: Theme.backgroundRaised
    property alias status: sourceImage.status

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.backgroundColor
        border.width: 1
        border.color: Theme.border
    }

    Image {
        id: sourceImage
        anchors.fill: parent
        source: root.source
        asynchronous: true
        cache: true
        smooth: true
        mipmap: true
        fillMode: root.fillMode
        visible: false
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: root.radius
        color: "white"
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: sourceImage
        maskEnabled: true
        maskSource: mask
        opacity: sourceImage.status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
    }

    BusyIndicator {
        anchors.centerIn: parent
        width: 30
        height: 30
        running: sourceImage.status === Image.Loading
        visible: running
    }

    AppIcon {
        anchors.centerIn: parent
        width: 38
        height: 38
        name: root.fallbackIcon
        color: Theme.borderStrong
        visible: sourceImage.status === Image.Error || root.source.toString().length === 0
    }
}

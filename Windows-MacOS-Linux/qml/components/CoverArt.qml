import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "../theme"

Item {
    id: root

    property url source
    property var sources: []
    property int sourceIndex: 0
    property int coverStyle: 2
    property real cornerRadius: 16
    property string fallbackIcon: "library"
    property alias status: artwork.status
    readonly property real artworkInset: coverStyle === 2 ? 10 : 5
    readonly property url resolvedSource: sources.length > 0 && sourceIndex < sources.length
        ? sources[sourceIndex] : source

    onSourcesChanged: sourceIndex = 0

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.backgroundRaised
        border.width: 1
        border.color: Theme.border
    }

    Image {
        id: artwork
        anchors.fill: parent
        anchors.margins: root.artworkInset
        source: root.resolvedSource
        asynchronous: true
        cache: true
        mipmap: true
        smooth: true
        fillMode: Image.PreserveAspectFit
        visible: false
    }

    Rectangle {
        id: artworkMask
        anchors.fill: artwork
        radius: Math.max(4, root.cornerRadius - root.artworkInset)
        color: "white"
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: artwork
        source: artwork
        maskEnabled: true
        maskSource: artworkMask
        opacity: artwork.status === Image.Ready ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        width: Math.min(34, parent.width * 0.24)
        height: width
        running: artwork.status === Image.Loading
        visible: running
    }

    AppIcon {
        anchors.centerIn: parent
        width: Math.min(44, parent.width * 0.32)
        height: width
        name: root.fallbackIcon
        color: Theme.borderStrong
        visible: (artwork.status === Image.Error && root.sourceIndex + 1 >= root.sources.length)
            || root.resolvedSource.toString().length === 0
    }

    Connections {
        target: artwork
        function onStatusChanged() {
            if (artwork.status === Image.Error && root.sources.length > 0
                    && root.sourceIndex + 1 < root.sources.length)
                root.sourceIndex += 1
        }
    }
}

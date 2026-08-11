import QtQuick
import "../theme"

Rectangle {
    id: root
    property bool interactive: false
    property bool selected: false
    property alias hovered: hoverHandler.hovered
    signal clicked()

    color: selected ? Theme.surfaceActive : (hovered && interactive ? Theme.surfaceHover : Theme.surface)
    radius: Theme.radiusLarge
    border.width: selected ? 1 : 1
    border.color: selected ? Theme.accent : Theme.border

    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    scale: pressHandler.pressed && interactive ? 0.992 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

    HoverHandler { id: hoverHandler; enabled: root.interactive }
    TapHandler { id: pressHandler; enabled: root.interactive; onTapped: root.clicked() }
}


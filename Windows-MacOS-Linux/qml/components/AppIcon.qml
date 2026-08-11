import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string name: "library"
    property color color: "white"
    property real strokeWidth: 1.8
    implicitWidth: 22
    implicitHeight: 22

    readonly property string iconPath: {
        switch (name) {
        case "library": return "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z"
        case "search": return "M11 4a7 7 0 1 0 0 14a7 7 0 0 0 0-14 M16 16l4 4"
        case "hub": return "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18 M3 12h18 M12 3c3 3 3 15 0 18 M12 3c-3 3-3 15 0 18"
        case "star": return "M12 3l2.8 5.7l6.2.9l-4.5 4.4l1.1 6.2L12 17.3l-5.6 2.9l1.1-6.2L3 9.6l6.2-.9z"
        case "profile": return "M12 12a4 4 0 1 0 0-8a4 4 0 0 0 0 8 M4 21c.7-4.2 3.3-6 8-6s7.3 1.8 8 6"
        case "play": return "M8 5l11 7l-11 7z"
        case "pause": return "M7 5h4v14H7z M13 5h4v14h-4z"
        case "chip": return "M8 3v3 M12 3v3 M16 3v3 M8 18v3 M12 18v3 M16 18v3 M3 8h3 M3 12h3 M3 16h3 M18 8h3 M18 12h3 M18 16h3 M7 7h10v10H7z"
        case "tune": return "M4 6h10 M18 6h2 M4 12h2 M10 12h10 M4 18h8 M16 18h4 M14 4v4 M6 10v4 M12 16v4"
        case "save": return "M5 4h12l2 2v14H5z M8 4v6h8V4 M8 20v-6h8v6"
        case "card": return "M3 6h18v13H3z M3 10h18 M7 15h4"
        case "image": return "M4 4h16v16H4z M7 16l4-4l3 3l2-2l4 4 M8 9h.01"
        case "code": return "M8 7l-5 5l5 5 M16 7l5 5l-5 5 M14 4l-4 16"
        case "settings": return "M12 8a4 4 0 1 0 0 8a4 4 0 0 0 0-8 M12 3v2 M12 19v2 M3 12h2 M19 12h2 M5.6 5.6L7 7 M17 17l1.4 1.4 M18.4 5.6L17 7 M7 17l-1.4 1.4"
        case "file": return "M6 3h8l4 4v14H6z M14 3v5h5 M9 13h6 M9 17h6"
        case "chat": return "M4 5h16v12H9l-5 4z M8 9h8 M8 13h5"
        case "palette": return "M12 3a9 9 0 0 0 0 18h1.5a2 2 0 0 0 0 0-4H12a2 2 0 0 1 0-4a9 9 0 0 0 4-14 M7 10h.01 M10 6.5h.01 M15 7h.01 M17 11h.01"
        case "back": return "M19 12H5 M11 6l-6 6l6 6"
        case "menu": return "M4 7h16 M4 12h16 M4 17h16"
        case "sidebar": return "M4 4h16v16H4z M9 4v16 M6.5 8h0 M6.5 12h0 M6.5 16h0"
        case "refresh": return "M20 7v5h-5 M4 17v-5h5 M18 10a7 7 0 0 0-12-3l-2 5 M6 14a7 7 0 0 0 12 3l2-5"
        case "folder": return "M3 6h7l2 2h9v11H3z"
        case "close": return "M6 6l12 12 M18 6L6 18"
        case "info": return "M12 10v7 M12 7h.01 M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18"
        case "trash": return "M5 7h14 M9 7V4h6v3 M7 7l1 14h8l1-14 M10 11v6 M14 11v6"
        case "copy": return "M8 8h12v12H8z M4 4h12v4 M4 4v12h4"
        case "export": return "M12 15V3 M7 8l5-5l5 5 M5 13v7h14v-7"
        case "check": return "M5 12l4 4L19 6"
        default: return "M5 5h14v14H5z"
        }
    }

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.iconPath }
        }
        transform: Scale {
            origin.x: 12
            origin.y: 12
            xScale: root.width / 24
            yScale: root.height / 24
        }
    }
}

pragma Singleton

import QtQuick

QtObject {
    readonly property bool light: Preferences.themeMode === "light"
        || (Preferences.themeMode === "system" && !App.systemDark)
    readonly property color accent: Preferences.accentColor.length > 0 ? Preferences.accentColor : "#C4203A"
    readonly property color accentBright: Qt.lighter(accent, light ? 1.08 : 1.35)
    readonly property color accentContainer: light ? Qt.rgba(accent.r, accent.g, accent.b, 0.18) : Qt.rgba(accent.r, accent.g, accent.b, 0.24)
    readonly property color onAccentContainer: light ? Qt.darker(accent, 1.35) : Qt.lighter(accent, 1.85)
    readonly property real fontScale: Preferences.fontScale
    readonly property real cornerScale: Preferences.cornerScale
    readonly property real motionScale: Preferences.motionScale
    readonly property color background: light ? "#F6F6FA" : "#050506"
    readonly property color backgroundRaised: light ? "#FFFFFF" : "#08080A"
    readonly property color sidebar: light ? "#EEEEF4" : "#09090C"
    readonly property color surface: light ? "#FFFFFF" : "#101014"
    readonly property color surfaceHover: light ? "#F2F2F7" : "#171316"
    readonly property color surfaceActive: light ? "#E7EDF8" : "#221B1F"
    readonly property color surfaceVariant: light ? "#EEEEF4" : "#1A1518"
    readonly property color border: light ? "#D5D5DE" : "#33262B"
    readonly property color borderStrong: light ? "#A9B9D4" : "#61313A"
    readonly property color text: light ? "#101014" : "#F6F2F3"
    readonly property color textMuted: light ? "#6A6A78" : "#B9A8AE"
    readonly property color textDim: light ? "#858596" : "#88787E"
    readonly property color success: "#50D9A0"
    readonly property color warning: "#C6A15A"
    readonly property color error: "#FF6B7A"

    readonly property int radiusSmall: Math.round(10 * cornerScale)
    readonly property int radius: Math.round(16 * cornerScale)
    readonly property int radiusLarge: Math.round(26 * cornerScale)
    readonly property int radiusXLarge: Math.round(32 * cornerScale)
    readonly property int durationFast: Math.round(140 * motionScale)
    readonly property int duration: Math.round(240 * motionScale)
    readonly property int durationSlow: Math.round(420 * motionScale)
    readonly property int sidebarWide: 246
    readonly property int sidebarCompact: 78
    readonly property int topBarHeight: 72
    readonly property int contentMaxWidth: 1540

    function sp(value) {
        return Math.round(value * fontScale)
    }
}

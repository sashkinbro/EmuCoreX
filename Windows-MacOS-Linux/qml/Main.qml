import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "theme"

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: "EmuCoreX"
    color: Theme.background

    LayoutMirroring.enabled: I18n.rightToLeft
    LayoutMirroring.childrenInherit: true

    Loader {
        id: rootLoader
        anchors.fill: parent
        source: Preferences.onboardingCompleted ? "AppShell.qml" : "screens/OnboardingScreen.qml"
        opacity: status === Loader.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic } }
    }

    Connections {
        target: App
        function onOnboardingFinished() { rootLoader.source = "AppShell.qml" }
    }

    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Back]; onActivated: App.goBack() }
}

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

    // The translations are loaded from Android-compatible XML through a C++
    // invokable. Such calls are not observable QML properties, so existing
    // bindings otherwise keep their old values after a language switch. A
    // lightweight root reload rebuilds those bindings while preserving the
    // AppController route and every persisted setting.
    Connections {
        target: I18n
        function onStringsChanged() {
            rootLoader.active = false
            Qt.callLater(function() { rootLoader.active = true })
        }
    }

    Connections {
        target: App
        function onOnboardingFinished() { rootLoader.source = "AppShell.qml" }
    }

    Shortcut { sequence: StandardKey.Quit; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Back]; onActivated: App.goBack() }
}

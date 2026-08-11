import QtQuick
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    Rectangle {
        anchors.fill: parent
        color: "#030407"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 18
            AppIcon { Layout.alignment: Qt.AlignHCenter; width: 72; height: 72; name: Emulator.running ? "play" : "chip"; color: Theme.accentBright }
            Text { Layout.alignment: Qt.AlignHCenter; text: Emulator.running ? "Emulation session" : "Core adapter"; color: Theme.text; font.pixelSize: Theme.sp(28); font.weight: Font.Bold }
            Text { Layout.alignment: Qt.AlignHCenter; text: Emulator.statusText; color: Theme.textMuted; font.pixelSize: Theme.sp(14) }
            Text { Layout.alignment: Qt.AlignHCenter; text: Emulator.backendName; color: Theme.accentBright; font.pixelSize: Theme.sp(12) }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                AppButton { text: "Save state"; iconName: "save"; enabled: Emulator.running; onClicked: Emulator.saveState(0) }
                AppButton { text: "Load state"; iconName: "refresh"; enabled: Emulator.running; onClicked: Emulator.loadState(0) }
                AppButton { text: "Stop"; danger: true; iconName: "close"; enabled: Emulator.running; onClicked: { Emulator.shutdown(); App.goBack() } }
            }
        }
    }
}


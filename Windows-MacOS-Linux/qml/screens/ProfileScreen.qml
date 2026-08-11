import QtQuick
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    Flickable {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 52
        clip: true

        ColumnLayout {
            id: content
            width: Math.min(parent.width - 52, 1180)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 28
            spacing: 18

            PageHeader { Layout.fillWidth: true; title: I18n.get("profile_title"); subtitle: "Your local player identity, play time and RetroAchievements activity." }

            AppCard {
                Layout.fillWidth: true; Layout.preferredHeight: 210
                RowLayout { anchors.fill: parent; anchors.margins: 24; spacing: 22
                    Rectangle {
                        Layout.preferredWidth: 116; Layout.preferredHeight: 116; radius: 58
                        gradient: Gradient { GradientStop { position: 0; color: Theme.accentBright } GradientStop { position: 1; color: Theme.accent } }
                        AppIcon { anchors.centerIn: parent; width: 48; height: 48; name: "profile"; color: "white" }
                    }
                    ColumnLayout { Layout.fillWidth: true; spacing: 6
                        Text { text: Preferences.value("profile/displayName", "Player"); color: Theme.text; font.pixelSize: 28; font.weight: Font.Bold }
                        Text { text: "Local desktop profile"; color: Theme.textMuted; font.pixelSize: 13 }
                        Text { text: App.platformName + " · " + App.hostArchitecture; color: Theme.accentBright; font.pixelSize: 12; font.weight: Font.DemiBold }
                    }
                    AppButton { text: "Edit profile"; iconName: "tune" }
                }
            }

            GridLayout {
                Layout.fillWidth: true; columns: width > 760 ? 3 : 1; rowSpacing: 14; columnSpacing: 14
                Repeater {
                    model: [
                        { value: GameLibrary.count, label: I18n.get("shell_library"), icon: "library" },
                        { value: "0 h", label: "Play time", icon: "play" },
                        { value: "0", label: I18n.get("settings_achievements_tab"), icon: "star" }
                    ]
                    AppCard {
                        Layout.fillWidth: true; Layout.preferredHeight: 128
                        RowLayout { anchors.fill: parent; anchors.margins: 20; spacing: 16
                            Rectangle { Layout.preferredWidth: 46; Layout.preferredHeight: 46; radius: 13; color: Theme.surfaceActive; AppIcon { anchors.centerIn: parent; width: 22; height: 22; name: modelData.icon; color: Theme.accentBright } }
                            ColumnLayout { Text { text: modelData.value; color: Theme.text; font.pixelSize: 25; font.weight: Font.Bold } Text { text: modelData.label; color: Theme.textMuted; font.pixelSize: 12 } }
                        }
                    }
                }
            }

            AppCard {
                Layout.fillWidth: true; Layout.preferredHeight: 160
                RowLayout { anchors.fill: parent; anchors.margins: 22; spacing: 18
                    AppIcon { Layout.preferredWidth: 36; Layout.preferredHeight: 36; name: "star"; color: Theme.warning }
                    ColumnLayout { Layout.fillWidth: true
                        Text { text: "RetroAchievements"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: "Connect an account to track achievements, mastery progress and recent unlocks."; color: Theme.textMuted; font.pixelSize: 13; wrapMode: Text.WordWrap }
                    }
                    AppButton { text: "Connect"; primary: true }
                }
            }
        }
    }
}


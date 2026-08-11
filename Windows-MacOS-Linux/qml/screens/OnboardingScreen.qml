import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../theme"

Item {
    id: root
    property int page: 0
    readonly property int pageCount: 5
    readonly property bool setupReady: Preferences.biosPath.length > 0 && GameLibrary.folders.length > 0

    function setPage(nextPage) {
        page = Math.max(0, Math.min(pageCount - 1, nextPage))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background

        Rectangle {
            width: Math.min(parent.width * 0.46, 720)
            height: width
            radius: width / 2
            x: -width * 0.62
            y: -height * 0.68
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.055)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 86
                Layout.leftMargin: 34
                Layout.rightMargin: 34
                spacing: 12

                AppLogo {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                }
                Text {
                    text: "EmuCoreX"
                    color: Theme.text
                    font.pixelSize: Theme.sp(18)
                    font.weight: Font.Bold
                }
                Item { Layout.fillWidth: true }
            }

            Item {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Row {
                    id: pages
                    width: viewport.width * root.pageCount
                    height: viewport.height
                    x: -root.page * viewport.width

                    Behavior on x {
                        NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
                    }

                    Repeater {
                        model: root.pageCount
                        Item {
                            required property int index
                            width: viewport.width
                            height: viewport.height
                            opacity: Math.abs(index - root.page) < 0.01 ? 1 : 0.22
                            scale: Math.abs(index - root.page) < 0.01 ? 1 : 0.975
                            Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
                            Behavior on scale { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic } }

                            Loader {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 96, 980)
                                height: Math.min(parent.height - 32, 650)
                                sourceComponent: {
                                    if (index === 0) return welcomePage
                                    if (index === 1) return emulationPage
                                    if (index === 2) return libraryPage
                                    if (index === 3) return profilePage
                                    return setupPage
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                Layout.leftMargin: 34
                Layout.rightMargin: 34
                Layout.bottomMargin: 18
                spacing: 14

                Row {
                    spacing: 8
                    Repeater {
                        model: root.pageCount
                        Rectangle {
                            required property int index
                            width: index === root.page ? 30 : 8
                            height: 8
                            radius: 4
                            color: index === root.page ? Theme.accent : Theme.border
                            Behavior on width { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
                            Behavior on color { ColorAnimation { duration: Theme.duration } }
                            TapHandler { onTapped: root.setPage(index) }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Item {
                    Layout.preferredWidth: 142
                    Layout.preferredHeight: 44
                    AppButton {
                        anchors.fill: parent
                        text: I18n.get("onboarding_back")
                        opacity: root.page > 0 ? 1 : 0
                        scale: root.page > 0 ? 1 : 0.92
                        enabled: root.page > 0
                        onClicked: root.setPage(root.page - 1)
                        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
                        Behavior on scale { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack } }
                    }
                }

                Item {
                    Layout.preferredWidth: 184
                    Layout.preferredHeight: 44

                    AppButton {
                        anchors.fill: parent
                        primary: true
                        text: I18n.get("onboarding_next")
                        opacity: root.page < root.pageCount - 1 ? 1 : 0
                        scale: root.page < root.pageCount - 1 ? 1 : 0.92
                        enabled: root.page < root.pageCount - 1
                        onClicked: root.setPage(root.page + 1)
                        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
                        Behavior on scale { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack } }
                    }

                    AppButton {
                        anchors.fill: parent
                        primary: true
                        text: I18n.get("onboarding_continue")
                        opacity: root.page === root.pageCount - 1 ? 1 : 0
                        scale: root.page === root.pageCount - 1 ? 1 : 0.92
                        enabled: root.page === root.pageCount - 1 && root.setupReady
                        onClicked: App.finishOnboarding()
                        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
                        Behavior on scale { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack } }
                    }
                }
            }
        }
    }

    Component {
        id: welcomePage
        HeroPage {
            iconName: "play"
            title: I18n.get("onboarding_page_1_title")
            subtitle: I18n.get("onboarding_page_1_subtitle")
        }
    }

    Component {
        id: emulationPage
        HeroPage {
            iconName: "chip"
            title: I18n.get("onboarding_page_2_title")
            subtitle: I18n.get("onboarding_page_2_subtitle")
        }
    }

    Component {
        id: libraryPage
        ColumnLayout {
            spacing: 22
            Item { Layout.fillHeight: true }
            PageHeader {
                Layout.fillWidth: true
                title: I18n.get("onboarding_page_3_title")
                subtitle: I18n.get("onboarding_page_3_subtitle")
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 18
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        Layout.preferredWidth: index === 1 ? 166 : 148
                        Layout.preferredHeight: index === 1 ? 236 : 210
                        Layout.alignment: Qt.AlignVCenter
                        radius: 24
                        color: index === 1 ? Theme.surfaceActive : Theme.surface
                        border.width: 1
                        border.color: index === 1 ? Theme.borderStrong : Theme.border
                        scale: 1
                        SequentialAnimation on y {
                            loops: Animation.Infinite
                            running: index === 1
                            NumberAnimation { to: -5; duration: 1500; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 0; duration: 1500; easing.type: Easing.InOutSine }
                        }
                        AppIcon {
                            anchors.centerIn: parent
                            width: 42; height: 42
                            name: index === 1 ? "library" : "play"
                            color: index === 1 ? Theme.accentBright : Theme.textDim
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    Component {
        id: profilePage
        ColumnLayout {
            spacing: 20
            Item { Layout.fillHeight: true }
            PageHeader {
                Layout.fillWidth: true
                title: I18n.get("onboarding_profile_title")
                subtitle: I18n.get("onboarding_profile_subtitle")
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: [
                        { value: 0, title: I18n.get("onboarding_profile_safe_title"), description: I18n.get("onboarding_profile_safe_desc") },
                        { value: 1, title: I18n.get("onboarding_profile_fast_title"), description: I18n.get("onboarding_profile_fast_desc") }
                    ]
                    AppCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 108
                        interactive: true
                        selected: Preferences.performanceProfile === modelData.value
                        onClicked: Preferences.performanceProfile = modelData.value
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 22
                            anchors.rightMargin: 22
                            spacing: 18
                            Rectangle {
                                Layout.preferredWidth: 48; Layout.preferredHeight: 48
                                radius: 24
                                color: Preferences.performanceProfile === modelData.value ? Theme.accentContainer : Theme.surfaceVariant
                                AppIcon { anchors.centerIn: parent; width: 23; height: 23; name: "chip"; color: Preferences.performanceProfile === modelData.value ? Theme.accentBright : Theme.textMuted }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Text { Layout.fillWidth: true; text: modelData.title; color: Theme.text; font.pixelSize: Theme.sp(16); font.weight: Font.Bold }
                                Text { Layout.fillWidth: true; text: modelData.description; color: Theme.textMuted; font.pixelSize: Theme.sp(13); wrapMode: Text.WordWrap }
                            }
                            Rectangle {
                                Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                radius: 11
                                color: Preferences.performanceProfile === modelData.value ? Theme.accent : "transparent"
                                border.width: 2
                                border.color: Preferences.performanceProfile === modelData.value ? Theme.accentBright : Theme.border
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    Component {
        id: setupPage
        ColumnLayout {
            spacing: 16
            Item { Layout.fillHeight: true }
            PageHeader {
                Layout.fillWidth: true
                title: I18n.get("onboarding_title")
                subtitle: I18n.get("onboarding_subtitle")
            }
            SetupRow {
                Layout.fillWidth: true
                iconName: "chip"
                title: I18n.get("onboarding_bios_title")
                description: Preferences.biosPath.length > 0 ? Preferences.biosPath : I18n.get("onboarding_bios_desc")
                ready: Preferences.biosPath.length > 0
                onClicked: biosDialog.open()
            }
            SetupRow {
                Layout.fillWidth: true
                iconName: "folder"
                title: I18n.get("onboarding_games_title")
                description: GameLibrary.folders.length > 0 ? I18n.format("onboarding_games_selected_count", [GameLibrary.folders.length]) : I18n.get("onboarding_games_desc")
                ready: GameLibrary.folders.length > 0
                onClicked: gamesFolderDialog.open()
            }
            Item { Layout.fillHeight: true }
        }
    }

    component HeroPage: ColumnLayout {
        property string iconName
        property string title
        property string subtitle
        spacing: 24
        Item { Layout.fillHeight: true }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 112
            Layout.preferredHeight: 112
            radius: 32
            color: Theme.accentContainer
            AppIcon { anchors.centerIn: parent; width: 54; height: 54; name: parent.parent.iconName; color: Theme.accentBright }
            SequentialAnimation on scale {
                loops: Animation.Infinite
                NumberAnimation { to: 1.035; duration: 1600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 1600; easing.type: Easing.InOutSine }
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 760
            text: parent.title
            color: Theme.text
            font.pixelSize: Theme.sp(38)
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 720
            text: parent.subtitle
            color: Theme.textMuted
            font.pixelSize: Theme.sp(16)
            lineHeight: 1.38
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
    }

    component SetupRow: AppCard {
        property string iconName
        property string title
        property string description
        property bool ready: false
        interactive: true
        Layout.preferredHeight: 104
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 16
            Rectangle {
                Layout.preferredWidth: 48; Layout.preferredHeight: 48
                radius: 24
                color: Theme.accentContainer
                AppIcon { anchors.centerIn: parent; width: 23; height: 23; name: parent.parent.parent.iconName; color: Theme.accentBright }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { Layout.fillWidth: true; text: parent.parent.parent.title; color: Theme.text; font.pixelSize: Theme.sp(16); font.weight: Font.Bold }
                Text { Layout.fillWidth: true; text: parent.parent.parent.description; color: Theme.textMuted; font.pixelSize: Theme.sp(13); elide: Text.ElideMiddle }
            }
            Text {
                text: parent.parent.ready ? I18n.get("onboarding_status_ready") : I18n.get("onboarding_status_required")
                color: parent.parent.ready ? Theme.success : Theme.warning
                font.pixelSize: Theme.sp(12)
                font.weight: Font.Bold
            }
        }
    }

    FileDialog {
        id: biosDialog
        title: I18n.get("onboarding_bios_title")
        nameFilters: ["PlayStation 2 BIOS (*.bin *.rom *.img)", "All files (*)"]
        onAccepted: Preferences.biosPath = decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//, ""))
    }

    FolderDialog {
        id: gamesFolderDialog
        title: I18n.get("onboarding_games_title")
        onAccepted: GameLibrary.addFolder(selectedFolder)
    }
}

import QtQuick
import "../components"

ManagerPage {
    category: "cheats"
    title: I18n.get("cheat_manager_title")
    subtitle: I18n.get("cheat_manager_enable_desc")
    iconName: "code"
    emptyTitle: I18n.get("cheat_manager_installed")
    emptyBody: I18n.get("cheat_manager_no_cheats")
    importLabel: I18n.get("cheat_manager_import_pnach")
    deleteLabel: I18n.get("cheat_manager_delete")
    nameFilters: ["PCSX2 cheats (*.pnach)"]
}

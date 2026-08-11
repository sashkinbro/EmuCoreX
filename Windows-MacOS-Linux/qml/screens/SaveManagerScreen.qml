import QtQuick
import "../components"

ManagerPage {
    category: "sstates"
    title: I18n.get("save_manager_title")
    subtitle: I18n.get("save_manager_empty_body_all")
    iconName: "save"
    emptyTitle: I18n.get("save_manager_empty_title")
    emptyBody: I18n.get("save_manager_empty_body_all")
    importLabel: I18n.get("save_manager_restore_action")
    deleteLabel: I18n.get("save_manager_delete_action")
    backupLabel: I18n.get("save_manager_backup_action")
    nameFilters: ["Save states (*.p2s *.state *.sav)", "All files (*)"]
}

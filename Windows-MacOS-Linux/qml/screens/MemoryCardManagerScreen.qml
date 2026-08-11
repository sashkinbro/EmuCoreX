import QtQuick
import "../components"

ManagerPage {
    category: "memcards"
    title: I18n.get("memory_card_manager_title")
    subtitle: I18n.get("memory_card_manager_subtitle")
    iconName: "card"
    emptyTitle: I18n.get("memory_card_empty_title")
    emptyBody: I18n.get("memory_card_empty_body")
    importLabel: I18n.get("memory_card_restore_action")
    deleteLabel: I18n.get("memory_card_delete_action")
    backupLabel: I18n.get("memory_card_backup_action")
    memoryCards: true
    nameFilters: ["PlayStation 2 memory cards (*.ps2 *.mcd)", "All files (*)"]
}

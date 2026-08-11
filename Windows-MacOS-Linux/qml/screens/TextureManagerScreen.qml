import QtQuick
import "../components"

ManagerPage {
    category: "textures"
    title: I18n.get("texture_manager_title")
    subtitle: I18n.get("texture_manager_enable_replacements_desc")
    iconName: "image"
    emptyTitle: I18n.get("texture_manager_empty_title")
    emptyBody: I18n.get("texture_manager_empty_body")
    importLabel: I18n.get("texture_manager_import_zip")
    deleteLabel: I18n.get("texture_manager_delete_title")
    folderImport: true
    nameFilters: ["Texture packs (*.zip)", "All files (*)"]
}

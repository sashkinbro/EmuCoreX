import QtQuick
import "../components"

FeaturePage {
    id: root
    property string route: "achievements"

    title: {
        switch (route) {
        case "achievements": return I18n.get("settings_achievements_tab")
        case "game-manager": return I18n.get("shell_game_settings_manager")
        case "save-manager": return I18n.get("shell_save_states")
        case "memory-cards": return I18n.get("shell_memory_cards")
        case "textures": return I18n.get("shell_texture_manager")
        case "cheats": return I18n.get("shell_cheat_manager")
        case "formats": return I18n.get("shell_supported_formats")
        case "feedback": return I18n.get("feedback_title")
        default: return "EmuCoreX"
        }
    }
    subtitle: {
        switch (route) {
        case "achievements": return "RetroAchievements status, recent unlocks and game progress."
        case "game-manager": return "Per-game settings, cover overrides, metadata and launch options."
        case "save-manager": return "Browse, load, export and remove save states for every game."
        case "memory-cards": return "Create, mount, duplicate and back up PlayStation 2 memory cards."
        case "textures": return "Installed texture replacements and the online texture catalog."
        case "cheats": return "Per-game patches, widescreen fixes and cheat files."
        case "formats": return "Disc images, compressed formats, executables and dump files supported by EmuCoreX."
        case "feedback": return I18n.get("feedback_subtitle")
        default: return ""
        }
    }
    iconName: route === "textures" ? "image" : route === "cheats" ? "code" : route === "memory-cards" ? "card" : route === "save-manager" ? "save" : route === "formats" ? "file" : route === "feedback" ? "chat" : "star"
    cards: {
        switch (route) {
        case "achievements": return [
            { title: "Account", description: "Connect and manage RetroAchievements credentials.", icon: "profile" },
            { title: "Recent unlocks", description: "Achievements earned during recent sessions.", icon: "star" },
            { title: "Game progress", description: "Completion and mastery across your library.", icon: "library" }
        ]
        case "game-manager": return [
            { title: "Per-game settings", description: "Graphics, emulation and controller overrides.", icon: "tune" },
            { title: "Game database", description: "Titles, serials, regions and compatibility data.", icon: "search" },
            { title: "Cover manager", description: "Download or choose custom cover artwork.", icon: "image" }
        ]
        case "save-manager": return [
            { title: "Save states", description: "All emulator state slots grouped by game.", icon: "save" },
            { title: "Import", description: "Restore save states from a backup.", icon: "folder" },
            { title: "Backup", description: "Export selected states or the complete library.", icon: "file" }
        ]
        case "memory-cards": return [
            { title: "Memory card 1", description: "Create or mount the primary 8 MB card.", icon: "card" },
            { title: "Memory card 2", description: "Configure the secondary memory card slot.", icon: "card" },
            { title: "Backups", description: "Restore and organize memory card backups.", icon: "save" }
        ]
        case "textures": return [
            { title: "Installed packs", description: "Enable, disable and inspect local replacements.", icon: "image" },
            { title: "Online catalog", description: "Find texture packs by game serial.", icon: "hub" },
            { title: "Import archive", description: "Install a local ZIP texture pack.", icon: "folder" }
        ]
        case "cheats": return [
            { title: "Installed cheats", description: "Manage local PNACH files and toggles.", icon: "code" },
            { title: "Online catalog", description: "Search verified patches by game serial.", icon: "hub" },
            { title: "Create patch", description: "Create and validate a custom cheat file.", icon: "file" }
        ]
        case "formats": return [
            { title: "Disc images", description: "ISO, BIN, IMG, MDF and NRG.", icon: "file" },
            { title: "Compressed", description: "CHD, CSO, GZ and ZSO.", icon: "save" },
            { title: "Development", description: "ELF executables, GS dumps and diagnostic files.", icon: "code" }
        ]
        case "feedback": return [
            { title: I18n.get("feedback_category_compatibility"), description: I18n.get("feedback_tip_body"), icon: "play" },
            { title: I18n.get("feedback_category_performance"), description: "Attach logs and describe the affected game.", icon: "chip" },
            { title: I18n.get("feedback_category_ui"), description: "Share interface and usability suggestions.", icon: "chat" }
        ]
        default: return []
        }
    }
}

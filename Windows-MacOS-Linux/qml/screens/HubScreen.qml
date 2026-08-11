import QtQuick
import "../components"

FeaturePage {
    title: I18n.get("hub_title")
    subtitle: "News, compatibility guides, community content and curated downloads for EmuCoreX."
    iconName: "hub"
    cards: [
        { title: "Latest news", description: "Project updates and release notes.", icon: "hub" },
        { title: "Compatibility", description: "Browse PS2 compatibility reports and fixes.", icon: "search" },
        { title: "Texture packs", description: "Discover and install community texture packs.", icon: "image", route: "textures" },
        { title: "Cheat catalog", description: "Search verified per-game cheats and patches.", icon: "code", route: "cheats" },
        { title: "Guides", description: "Recommended graphics and controller setup.", icon: "file" },
        { title: "Community", description: "Open the EmuCoreX community channels.", icon: "chat" }
    ]
}


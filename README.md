<p align="center">
  <img src="public/assets/app-icon.webp" width="132" height="132" alt="EmuCoreX app icon" />
</p>

<h1 align="center">EmuCoreX</h1>

<p align="center">
  A modern PlayStation 2 library and emulator experience built for Android.
</p>

<p align="center">
  <a href="https://play.google.com/store/apps/details?id=com.sbro.emucorex">
    <img src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="72" alt="Get EmuCoreX on Google Play" />
  </a>
</p>

<p align="center">
  <a href="https://emucorex.web.app/"><img alt="Website" src="https://img.shields.io/badge/Website-emucorex.web.app-5ed8ff?style=for-the-badge&logo=firebase&logoColor=white" /></a>
  <a href="https://discord.gg/c5EBeNRpz2"><img alt="Discord" src="https://img.shields.io/badge/Discord-Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" /></a>
  <a href="https://www.patreon.com/c/emucore/membership"><img alt="Patreon" src="https://img.shields.io/badge/Patreon-Support-ff424d?style=for-the-badge&logo=patreon&logoColor=white" /></a>
  <a href="LICENSE"><img alt="GPL v3 or later" src="https://img.shields.io/badge/License-GPLv3%2B-2563eb?style=for-the-badge" /></a>
</p>

> [!WARNING]
> EmuCoreX is under active development. Compatibility and performance depend on the game, device, renderer, cooling, and GPU driver. Keep backups of important saves and memory cards.

## About

EmuCoreX combines a focused Android interface with an Android-adapted emulation core based on **PCSX2 2.7.316**. It is designed to make first-time setup, game browsing, controller use, and day-to-day emulation feel natural on phones, tablets, and handheld-style Android devices.

## Highlights

- Guided BIOS and game-folder setup using Android's Storage Access Framework
- Library with cover art, metadata, recent games, search, and quick launch
- OpenGL and Vulkan renderers with practical device-safe defaults
- Per-game graphics, speedhack, GS hack, aspect ratio, and resolution settings
- Save states, memory cards, cheats, patches, and BIOS boot
- Physical gamepad remapping and gamepad-aware navigation
- RetroAchievements integration and a dedicated achievements screen
- Localized Android interface with ongoing compatibility and stability work

## Device guidance

The current optimization focus is Snapdragon hardware. A Snapdragon 855-class chipset, at least 4 GB of RAM, and active cooling for longer sessions are practical starting points; 6 GB of RAM is recommended.

On MediaTek devices, try **OpenGL** first. If a particular game remains unstable or too slow, try Software rendering as a fallback. These recommendations are starting points, not compatibility guarantees.

## Repository

This repository contains the complete Android application, Jetpack Compose UI, settings and library layers, JNI bridge, website, and bundled native core sources used by EmuCoreX.

| Area | Technology |
| --- | --- |
| Android UI | Kotlin, Jetpack Compose |
| App data | Android DataStore |
| Native integration | JNI, C++ |
| Emulation core | PCSX2 2.7.316-based Android integration |
| Website and services | Firebase Hosting and Firebase services |

Current Android configuration:

- Minimum Android SDK: `29`
- Target Android SDK: `37`
- Application ID: `com.sbro.emucorex`
- Application version: `0.2.7`

## Build locally

### Requirements

- Android Studio with the Android SDK and NDK configured
- A JDK compatible with the repository's Gradle setup
- A physical Android device or emulator for testing

Debug build:

```powershell
.\gradlew :app:assembleDebug
```

Release build:

```powershell
.\gradlew :app:assembleRelease
```

Main directories:

```text
app/                                  Android application module
app/src/main/java/com/sbro/emucorex   Kotlin application code
app/src/main/cpp                      Native bridge and core sources
app/src/main/res                      Resources and translations
public/                               Official website hosted on Firebase
```

## Legal notice

EmuCoreX does not distribute PlayStation 2 BIOS files or commercial game images. You must provide your own legally obtained BIOS and game dumps. EmuCoreX is not affiliated with Sony Interactive Entertainment.

## Credits and license

EmuCoreX builds on the open-source [PCSX2](https://github.com/PCSX2/pcsx2) project and adds its own Android interface, library system, runtime controls, JNI integration, and mobile-focused workflows.

The repository is distributed under the **GNU General Public License v3.0 or later**. See [LICENSE](LICENSE) for details.

## Links

- [Google Play](https://play.google.com/store/apps/details?id=com.sbro.emucorex)
- [Official website](https://emucorex.web.app/)
- [Discord community](https://discord.gg/c5EBeNRpz2)
- [Support on Patreon](https://www.patreon.com/c/emucore/membership)
- [More apps by the author](https://play.google.com/store/apps/dev?id=7136622298887775989)

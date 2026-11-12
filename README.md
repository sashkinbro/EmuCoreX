# EmuCoreX

[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)
[![Get EmuCoreX on Google Play](https://img.shields.io/badge/Google_Play-100K%2B_Downloads-414141?logo=googleplay&logoColor=white)](https://play.google.com/store/apps/details?id=com.sbro.emucorex)
[![Support EmuCoreX on Patreon](https://img.shields.io/badge/Patreon-Support%20EmuCoreX-ff424d?logo=patreon&logoColor=white)](https://www.patreon.com/c/emucore/membership)
[![Join the EmuCoreX Discord](https://img.shields.io/badge/Discord-Join%20our%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/c5EBeNRpz2)

EmuCoreX is a PlayStation 2 library, launcher, and emulator frontend for Android. It pairs a purpose-built interface with a PCSX2-derived emulation core adapted for Android devices. Account, cloud-drive, and Play Billing SDK integrations are not required; backup and restore are user-directed local file operations.

Official website: https://emucorex.web.app/

## Highlights

- PCSX2-based emulation core adapted by EmuCoreX for Android
- Home screen with cover art, game metadata, recent games, and search
- BIOS and game folder setup, with recovery when folders become invalid
- In-game overlay for renderer, aspect ratio, resolution, speedhacks, cheats, FPS, and quick actions
- Save state manager, BIOS boot, and library navigation from the side drawer
- Manual ZIP export/import for settings, memory cards, cheats, and optional save states
- RetroAchievements integration and a dedicated achievements screen
- Cheat management with `.pnach` import, editing, and per-game activation in overlay
- Advanced graphics and GS hack controls, including device-safe defaults for MediaTek
- Physical gamepad remapping and gamepad-aware UI flows

## Recommended Specifications

- **Chipset:** Snapdragon 855 or a comparably powerful MediaTek chipset, such as Dimensity 900 or Dimensity 1080
- **Memory:** at least 4 GB of RAM; 6 GB is recommended for more stable emulation

## Screenshots

In-game captures running on a Snapdragon 8 Elite Gen 5 device with the Vulkan renderer:

| Tekken 5 | Need for Speed: Most Wanted |
| --- | --- |
| ![Tekken 5](Screenshot/tekken-5.jpg?v=2) | ![Need for Speed: Most Wanted](Screenshot/need-for-speed-most-wanted.jpg?v=2) |

| Shadow of the Colossus | SoulCalibur II |
| --- | --- |
| ![Shadow of the Colossus](Screenshot/shadow-of-the-colossus.jpg?v=2) | ![SoulCalibur II](Screenshot/soulcalibur-ii.jpg?v=2) |

| God of War II |
| --- |
| ![God of War II](Screenshot/god-of-war-ii.jpg?v=2) |

## What This Repository Contains

This repository contains the Android app, including its UI, settings, bridge code, and native emulation core integration.

## Tech Stack

- Kotlin + Jetpack Compose
- Android DataStore
- JNI bridge to native C++
- Emulation core derived from PCSX2 and integrated into EmuCoreX's native Android stack
- Local-only settings and save-data backup through Android's document picker

## Current App Scope

The current app build targets Android with:

- `minSdk 29`
- `targetSdk 37`
- package id `com.sbro.emucorex`
- version `0.5.8`

## Building Locally

### Requirements

- Android Studio with Android SDK and NDK configured
- JDK compatible with the Gradle setup in this project
- A device or emulator for Android testing

### Debug Build

```powershell
.\gradlew :app:assembleDebug
```

### Release Build

```powershell
.\gradlew :app:assembleRelease
```

## Project Structure

- `app/` Android application module
- `app/src/main/java/com/sbro/emucorex` Kotlin app code
- `app/src/main/cpp` Native bridge and core sources
- `app/src/main/res` Android resources and translations

## Notes

- BIOS files and game images are not distributed with this project.
- You must use your own legally obtained BIOS files and game dumps.
- Compatibility, performance, and graphics behavior vary by device and renderer.
- Releases marked as "parallel" are identical to the primary build but use an alternate package ID to allow side-by-side installation.

## Credits

EmuCoreX builds on the open-source PCSX2 project together with its own Android interface, library system, runtime controls, and handheld-focused UX. The Android bridge has also been rewritten in Kotlin, and the core has been adapted by EmuCoreX for Android. Further work on stability, integration, and core improvements is planned.

- PCSX2: https://github.com/PCSX2/pcsx2

## Support

If you want to support ongoing development:

- Google Play: https://play.google.com/store/apps/details?id=com.sbro.emucorex (100K+ downloads)
- Website: https://emucorex.web.app/
- Patreon: https://www.patreon.com/c/emucore/membership
- Discord: https://discord.gg/c5EBeNRpz2
- More apps by the author: https://play.google.com/store/apps/dev?id=7136622298887775989

## License

This project includes and derives from GPL-licensed PCSX2 code, so the repository is distributed under the GNU General Public License v3.0 or later.

See [LICENSE](LICENSE) for details.

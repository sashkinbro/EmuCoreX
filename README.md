# EmuCoreX

[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)
[![Get EmuCoreX on Google Play](https://img.shields.io/badge/Google_Play-Get_EmuCoreX-414141?logo=googleplay&logoColor=white)](https://play.google.com/store/apps/details?id=com.sbro.emucorex)
[![Support EmuCoreX on Patreon](https://img.shields.io/badge/Patreon-Support%20EmuCoreX-ff424d?logo=patreon&logoColor=white)](https://www.patreon.com/c/emucore/membership)
[![Join the EmuCoreX Discord](https://img.shields.io/badge/Discord-Join%20our%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/c5EBeNRpz2)

EmuCoreX is a PlayStation 2 library, launcher, and emulator frontend for Android and desktop. It pairs a purpose-built interface with PCSX2-derived emulation cores adapted for each supported architecture.

Official website: https://emucorex.web.app/

![Status](https://img.shields.io/badge/Status-Early%20Development%20%2F%20Unstable-red)

> [!WARNING]
> EmuCoreX is currently in the early stages of development. Expect instability, visual issues, performance drops, random slowdowns, and occasional crashes depending on the game, device, renderer, and driver stack.
>
> The current Android focus is mid-range and high-end phones. Budget devices are not optimized yet.
>
> At this stage, optimization work is mainly focused on Snapdragon devices. MediaTek optimization is still incomplete and may improve later.
>
> If you are using a MediaTek device, try the OpenGL renderer first. If that is still unstable or too slow for a specific game, try Software rendering as a fallback.
>
> Minimum recommended specifications as of July 2026:
> - Chipset: Snapdragon 855 or a similarly powerful MediaTek chipset, such as Dimensity 900 or Dimensity 1080
> - Memory: at least 4 GB of RAM, with 6 GB recommended for more stable emulation
>
> These are practical starting points, not guarantees. Cooling, GPU drivers, RAM bandwidth, renderer choice, and the game itself still matter a lot.
>
> Not all games work correctly yet. Compatibility, fixes, and performance optimization are still in active development.

## Highlights

- PCSX2-based emulation core adapted by EmuCoreX for Android
- Home screen with cover art, game metadata, recent games, and search
- BIOS and game folder setup, with recovery when folders become invalid
- In-game overlay for renderer, aspect ratio, resolution, speedhacks, cheats, FPS, and quick actions
- Save state manager, BIOS boot, and library navigation from the side drawer
- RetroAchievements integration and a dedicated achievements screen
- Cheat management with `.pnach` import, editing, and per-game activation in overlay
- Advanced graphics and GS hack controls, including device-safe defaults for MediaTek
- Physical gamepad remapping and gamepad-aware UI flows
- A shared desktop application for Windows, Linux, and macOS with a native Qt Quick interface

## Desktop Roadmap

Desktop builds are under active development in `Windows-MacOS-Linux/`. EmuCoreX is designed as one application across all three desktop operating systems, with native packaging and platform-appropriate graphics backends.

Planned desktop targets:

| Platform | x64 | ARM64 |
| --- | :---: | :---: |
| Windows | Planned | Planned |
| Linux | Planned | Planned |
| macOS | Planned | Planned |

The first public desktop pre-release alpha is planned for the end of 2026. This is a development target rather than a guaranteed release date; stability and core integration will determine the final timing.

## What This Repository Contains

This repository contains the Android app and the in-development Windows, Linux, and macOS application, together with their UI, settings, bridge code, and native emulation core integrations.

## Tech Stack

- Kotlin + Jetpack Compose
- Android DataStore
- JNI bridge to native C++
- Emulation core derived from PCSX2 and integrated into EmuCoreX's native Android stack
- Firebase services used by the Android app
- C++20, Qt 6, Qt Quick, and QML for the desktop application
- PCSX2 x64 integration and the EmuCoreX ARM64 core path selected per architecture

## Current App Scope

EmuCoreX version `0.3.3` currently targets Android with:

- `minSdk 29`
- `targetSdk 37`
- package id `com.sbro.emucorex`
- version `0.3.3`

Desktop version `0.3.3` is in development for Windows, Linux, and macOS on x64 and ARM64. Public desktop binaries are not available yet.

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
- `Windows-MacOS-Linux/` shared desktop application, native core adapters, packaging scripts, and desktop translations

## Notes

- BIOS files and game images are not distributed with this project.
- You must use your own legally obtained BIOS files and game dumps.
- Compatibility, performance, and graphics behavior vary by device and renderer.

## Credits

EmuCoreX builds on the open-source PCSX2 project together with its own Android interface, library system, runtime controls, and handheld-focused UX. The Android bridge has also been rewritten in Kotlin, and the core has been adapted by EmuCoreX for Android. Further work on stability, integration, and core improvements is planned.

- PCSX2: https://github.com/PCSX2/pcsx2

## Support

If you want to support ongoing development:

- Google Play: https://play.google.com/store/apps/details?id=com.sbro.emucorex
- Website: https://emucorex.web.app/
- Patreon: https://www.patreon.com/c/emucore/membership
- Discord: https://discord.gg/c5EBeNRpz2
- More apps by the author: https://play.google.com/store/apps/dev?id=7136622298887775989

## License

This project includes and derives from GPL-licensed PCSX2 code, so the repository is distributed under the GNU General Public License v3.0 or later.

See [LICENSE](LICENSE) for details.

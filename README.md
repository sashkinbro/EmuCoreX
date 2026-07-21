# EmuCoreX

[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)
[![Get EmuCoreX on Google Play](https://img.shields.io/badge/Google_Play-Get_EmuCoreX-414141?logo=googleplay&logoColor=white)](https://play.google.com/store/apps/details?id=com.sbro.emucorex)
[![Support EmuCoreX on Patreon](https://img.shields.io/badge/Patreon-Support%20EmuCoreX-ff424d?logo=patreon&logoColor=white)](https://www.patreon.com/c/emucore/membership)
[![Join the EmuCoreX Discord](https://img.shields.io/badge/Discord-Join%20our%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/c5EBeNRpz2)

EmuCoreX is a PlayStation 2 library and launcher for Android. It pairs a custom Android interface with a PCSX2-based emulation core adapted by EmuCoreX for Android.

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

## What This Repository Contains

This repository contains the Android app, UI, settings, bridge code, and bundled native core sources used by EmuCoreX.

## Tech Stack

- Kotlin + Jetpack Compose
- Android DataStore
- JNI bridge to native C++
- Emulation core derived from PCSX2 and integrated into EmuCoreX's native Android stack
- Firebase services used by the Android app

## Current App Scope

EmuCoreX currently targets Android with:

- `minSdk 29`
- `targetSdk 37`
- package id `com.sbro.emucorex`
- version `0.2.9`

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

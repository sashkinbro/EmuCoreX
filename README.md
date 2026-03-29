# EmuCoreX

[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)
[![Support the project](https://img.shields.io/badge/Donate-Support%20EmuCoreX-ff5f45.svg)](https://send.monobank.ua/jar/9ZocYsprhJ)

EmuCoreX is a PlayStation 2 library and launcher for Android built around a PCSX2-based emulation core. The project focuses on a clean handheld-friendly UI, quick setup, practical in-game controls, and deeper runtime tuning than a typical mobile frontend.

## Highlights

- PCSX2-based native emulation core integrated into an Android-first experience
- Library-first home screen with cover art, game metadata, recent games, and search
- Guided onboarding for BIOS and game folders with recovery when folders become invalid
- In-game overlay for renderer, aspect ratio, resolution, speedhacks, cheats, FPS, and quick actions
- Save state manager, BIOS boot, and drawer-based library navigation
- RetroAchievements integration and dedicated achievements screen
- Cheat management with `.pnach` import, editing, and per-game activation in overlay
- Advanced graphics and GS hack controls, including device-safe defaults for MediaTek
- Physical gamepad remapping and better controller-aware UI flows

## What This Repository Contains

This repository contains the Android app, UI, settings flows, bridge code, and the bundled native emulation core sources used by EmuCoreX.

Some internal or private project assets are intentionally excluded from version control, including:

- crash and local diagnostic logs
- private website files and support pages
- IGDB tooling and generated data
- local native helper tooling

## Tech Stack

- Kotlin + Jetpack Compose
- Android DataStore
- JNI bridge to native C++
- PCSX2-derived core and supporting native libraries
- Firebase services used by the Android app

## Current App Scope

EmuCoreX currently targets Android with:

- `minSdk 29`
- `targetSdk 36`
- package id `com.sbro.emucorex`
- version `0.0.3`

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

EmuCoreX builds on the open-source PCSX2 project and combines it with a custom Android interface, library system, runtime controls, and handheld-oriented UX work.

- PCSX2: https://github.com/PCSX2/pcsx2

## Support

If you want to support ongoing development:

- Donate: https://send.monobank.ua/jar/9ZocYsprhJ
- More apps by the author: https://play.google.com/store/apps/dev?id=7136622298887775989

## License

This project includes and derives from GPL-licensed PCSX2 code, so the repository is distributed under the GNU General Public License v3.0 or later.

See [LICENSE](LICENSE) for details.

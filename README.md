# EmuCoreX

[![License: GPL v3+](https://img.shields.io/badge/License-GPLv3%2B-blue.svg)](LICENSE)
[![Support the project](https://img.shields.io/badge/Donate-Support%20EmuCoreX-ff5f45.svg)](https://send.monobank.ua/jar/9ZocYsprhJ)

EmuCoreX is a PlayStation 2 library and launcher for Android. It combines a custom Android interface with an emulation core based on ARMSX2 and PCSX2 work, with a focus on clean navigation, quick setup, and practical in-game controls.

Official website: https://emucorex.web.app/

## Highlights

- Native emulation core based on the ARMSX2 app core
- Home screen with cover art, game metadata, recent games, and search
- Guided onboarding for BIOS and game folders with recovery when folders become invalid
- In-game overlay for renderer, aspect ratio, resolution, speedhacks, cheats, FPS, and quick actions
- Save state manager, BIOS boot, and library navigation from the side drawer
- RetroAchievements integration and dedicated achievements screen
- Cheat management with `.pnach` import, editing, and per-game activation in overlay
- Advanced graphics and GS hack controls, including device-safe defaults for MediaTek
- Physical gamepad remapping and controller-aware UI flows

## What This Repository Contains

This repository contains the Android app, UI, settings flows, bridge code, and the bundled native emulation core sources used by EmuCoreX.

## Tech Stack

- Kotlin + Jetpack Compose
- Android DataStore
- JNI bridge to native C++
- Emulation core derived from PCSX2, with native libraries and app-core groundwork coming from ARMSX2
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

EmuCoreX builds on the open-source PCSX2 project and on the ARMSX2 app core foundation, combining them with its own Android interface, library system, runtime controls, and handheld-focused UX. The Android bridge has also been rewritten in Kotlin. Further work on stability, integration, and core improvements is planned as development continues.

- PCSX2: https://github.com/PCSX2/pcsx2
- Thanks to the ARMSX2 team for the app-core groundwork that helped form the base of EmuCoreX.

## Support

If you want to support ongoing development:

- Website: https://emucorex.web.app/
- Donate: https://send.monobank.ua/jar/9ZocYsprhJ
- More apps by the author: https://play.google.com/store/apps/dev?id=7136622298887775989

## License

This project includes and derives from GPL-licensed PCSX2 code, so the repository is distributed under the GNU General Public License v3.0 or later.

See [LICENSE](LICENSE) for details.

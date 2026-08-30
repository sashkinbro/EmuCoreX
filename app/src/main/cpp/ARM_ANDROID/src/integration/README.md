# Android RetroAchievements integration

This directory contains the complete Android-specific RetroAchievements integration shipped by EmuCoreX.

## Source layout

- `pcsx2_achievements_android.cpp` is the Android overlay for PCSX2 `v2.8.0` (`c10a5c9ad951c517dc48b722b5b84eb9bf7fdb42`). CMake removes the upstream `Achievements.cpp` translation unit and builds this tracked overlay instead.
- `retro_achievements_android.cpp` owns the JNI bridge, current-state notifications, stable EmuCoreX user-agent, and Android Hardcore policy helpers.
- `include/emucorex/retro_achievements_android.h` is the shared boundary used by the Android runtime and the PCSX2 host callbacks.

The local `app/src/main/cpp/PCSX2` checkout is not the source of EmuCoreX-specific RetroAchievements behavior. This keeps the integration reviewable in the main repository and prevents a local vendor checkout from silently changing the shipped client identity or Hardcore behavior.

## Hardcore invariants

- Casual to Hardcore never happens in the middle of a running session. Enabling the preference becomes active only after a full VM reset or a new boot.
- Hardcore to Casual may happen immediately.
- Loading or resuming a save state is blocked using the authoritative native `rc_client` Hardcore state. Creating save states remains allowed.
- Cheats, memory editing, input playback/TAS, frame advance, rewind, and slow motion are unavailable while Hardcore is active.
- Rich Presence and leaderboards remain enabled; notification popups may be hidden independently without disabling evaluation.
- The RetroAchievements server sees the unique stable client identity `EmuCoreX/v<app-version> (Android) pcsx2/v2.8.0`.

When updating PCSX2, rebase the overlay against the new upstream commit and update both the CMake version constants and this file in the same change.

The full code matrix, manual verification pass, and application disclosures are
tracked beside this file in `COMPLIANCE.md`.

# EmuCoreX-Mali — Project TODO

This roadmap tracks the remaining work for the Android PS2 emulator fork. Items are not considered complete until they are implemented and verified on-device or by the relevant automated tests.

## Recently completed

- [x] Remove the ANGLE renderer option and its Android/native runtime configuration.
- [x] Remove obsolete ANGLE preference and UI string resources.
- [x] Use GPU-family-aware renderer defaults: Vulkan for Adreno, OpenGL for other detected GPU families; keep OpenGL, Vulkan, and Software selectable.
- [x] Keep English as the app's built-in language; user-provided dictionaries are the planned localisation model, not bundled translations.

## Priority 1 — Stability and renderer validation

- [ ] Build debug and release variants after the ANGLE removal; fix any Kotlin, JNI, native-linking, or packaging regressions.
- [ ] Scan the full source tree and packaged outputs for leftover ANGLE libraries, hints, settings, and user-facing references.
- [ ] Verify OpenGL and Vulkan startup, gameplay, suspend/resume, surface recreation, and recovery on Mali devices.
- [ ] Benchmark both renderers on the same games and scenes; confirm the GPU-family defaults are sensible and retain a safe manual override.
- [ ] Investigate OpenGL visual glitches and Vulkan performance regressions on Mali without assuming one backend is universally faster.
- [ ] Run the existing renderer lifecycle, Vulkan startup guard, shader preparation, and gameplay profiling tests.

## Priority 2 — Performance and low-memory target

- [ ] Add reliable performance instrumentation for frame pacing, CPU/GPU time or wait indicators where available, queue backlog, thermal state, and memory use.
- [ ] Implement a conservative rendering watchdog with hysteresis. It may adjust safe host-side quality controls (internal resolution, filtering, post-processing, cache budgets); it must not skip emulated GS commands or alter game simulation.
- [ ] Prototype Phantom LOD only where draw/resource semantics can be proven safe; use per-game profiles or verified bounds rather than generic scene assumptions.
- [ ] Profile asset/texture cache budgets and safe eviction before attempting broader asset hollowing or memory reclamation.
- [ ] Measure peak and sustained app memory (PSS/RSS, native heap, and graphics allocations) across representative games and devices.
- [ ] Work toward a 2 GB RAM minimum using measured results; do not advertise 2 GB support until a representative compatibility and stability matrix passes.

## Priority 3 — UI and settings polish

- [ ] Finish applying the refreshed UI to the game library and navigation drawer.
- [ ] Add free basic UI personalization (at minimum an accent/color picker); keep premium theme packs and extra theme slots as optional extras.
- [ ] Complete the settings audit: remove redundant or ineffective toggles while preserving useful compatibility, graphics, audio, controls, and per-game options.
- [ ] Verify every settings control persists correctly and has a clear description; test reset, backup, and restore flows.
- [ ] Review the in-game performance overlay for accurate, useful metrics and low overhead.

## Priority 4 — User dictionary localization

- [ ] Keep English strings as the canonical built-in source; do not ship a large set of official translations.
- [ ] Define a versioned JSON dictionary format using stable string IDs, language metadata, and translated values.
- [ ] Add dictionary import, validation, preview, apply, export, and remove flows.
- [ ] Fall back to English for missing keys and safely handle unknown keys, malformed files, plurals, and formatting placeholders.
- [ ] Persist the selected dictionary and apply it consistently across app screens without changing Android's system language.
- [ ] Provide a sample dictionary and a short guide so the community can create language packs.

## Priority 5 — Compatibility, release, and documentation

- [ ] Run a representative game compatibility and performance test matrix, recording game, device/GPU, renderer, resolution, frame pacing, glitches, and crashes.
- [ ] Test on Mali first, then collect reports from Adreno and other supported GPU families.
- [ ] Verify ARM64 packaging, 4 KB / 16 KB native library variants, install/upgrade behavior, and app startup on supported Android versions.
- [ ] Keep README version, minimum specifications, renderer guidance, and build instructions aligned with the actual release.
- [ ] Prepare a release checklist covering clean install, existing-user upgrade, BIOS/game folder recovery, saves, controller input, audio, and graphics.

## Working rules

- Prefer measured improvements over speculative toggles or claims.
- Preserve emulation correctness: never trade game logic, timing-sensitive GS behavior, or compatibility for a generic performance shortcut.
- Mark an item complete only after the relevant test or device verification has been recorded.

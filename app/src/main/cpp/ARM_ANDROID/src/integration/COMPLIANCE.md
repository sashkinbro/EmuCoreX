# RetroAchievements Hardcore compliance audit

Audit baseline: RetroAchievements Hardcore Compliance Requirements and the
`rc_client` integration guide, reviewed on 2026-07-13.

## Integration provenance

- Android RetroAchievements behavior is tracked in this directory.
- The overlay is based on PCSX2 `v2.8.0`, commit
  `c10a5c9ad951c517dc48b722b5b84eb9bf7fdb42`.
- CMake removes the upstream `Achievements.cpp` translation unit and builds
  `pcsx2_achievements_android.cpp` instead.
- SHA-256 guards fail configuration if the pinned upstream
  `Achievements.cpp`, `Achievements.h`, or `Host.cpp` is changed without
  rebasing and reviewing the Android overlay.
- RetroAchievements HTTP requests use the EmuCoreX identity
  `EmuCoreX/v<version> (Android) pcsx2/v2.8.0`.

## Code compliance matrix

| Requirement | Implementation/evidence | Status |
| --- | --- | --- |
| Achievement list is accessible | Main Achievements screen and in-game Achievements tab use active `rc_client` data | Implemented |
| Measured and Trigger states are visible | `measuredProgress`, `measuredPercent`, and active-challenge bucket are exported to Android; PCSX2 also renders live progress/challenge overlays | Implemented |
| Rich Presence and leaderboards evaluate | Upstream `rc_client` frame processing remains enabled; only notification visibility is configurable | Implemented |
| Offline unlock retry | Provided by the pinned `rc_client` request queue; disconnect/reconnect state is surfaced to Android | Implemented |
| Save-state hit storage | PCSX2 serializes/deserializes `rc_client` progress in save states | Implemented |
| Cheats are blocked in Hardcore | Sanitized before launch, disabled by PCSX2 Hardcore enforcement, blocked again when patches reload, and blocked in Android UI | Implemented |
| Rewind is blocked | EmuCoreX exposes no rewind path | Implemented |
| Slow motion and frame advance are blocked | PCSX2 Hardcore checks reject both; Android exposes neither while Hardcore is active | Implemented |
| Save-state loading is always blocked | Android runtime checks the authoritative active client state and PCSX2 independently rejects the load | Implemented |
| Save-state creation remains available | Save operations are not blocked by Hardcore | Implemented |
| Quick resume cannot retain Hardcore | No direct quick-resume boot path is exposed; any state load is rejected while Hardcore is active | Implemented |
| Casual to Hardcore requires reset | The setting only records the request. `VMManager` activates it during a full boot/reset | Implemented |
| Hardcore to Casual is immediate | Android calls `DisableHardcoreMode()` immediately | Implemented |
| Memory editor/debugger/TAS playback unavailable | No memory editor, debugger, scripting, or recorded-input playback is exposed in the Android product | Implemented |
| Hardcore mode is indicated | Game summary notification includes Hardcore mode; current native state is also exposed to Android UI | Implemented |
| Client runs every emulated frame and idles while paused | Preserved from the pinned PCSX2 `rc_client` integration | Implemented |
| Reset event handling | Hardcore activation occurs inside the VM reset flow and the `rc_client` runtime is reset with it | Implemented |

Fast-forward is allowed by RetroAchievements. The pinned PCSX2 core is
currently stricter and returns to nominal speed while Hardcore is active; this
does not create a Hardcore bypass.

## Required manual test pass before reapplying

Use a real RetroAchievements account and at least one supported PS2 set with a
Measured achievement, a Trigger/Primed achievement, Rich Presence, and a
leaderboard.

1. Start in Casual, enable Hardcore during gameplay, and confirm it remains
   Casual until a full reset.
2. Reset and confirm the startup summary says Hardcore and the RA website sees
   the EmuCoreX user agent/version.
3. Attempt every save-state load entry point: quick load, slot picker,
   autosave load, and boot/resume. Every load must fail while Hardcore remains
   active.
4. Create a save state in Hardcore, switch to Casual, load it, and confirm the
   achievement hit counts restore correctly.
5. Try enabling built-in cheats and individual cheat blocks before boot and
   during play. Confirm they remain off and patches reload without cheats.
6. Verify no slowdown/frame-advance/rewind/debug/TAS action is reachable.
7. Trigger a Measured achievement and a Primed condition. Confirm progress is
   visible in the achievement list and the live overlay is visible in game.
8. Start, fail, and submit a leaderboard. Confirm tracker and result UI, Rich
   Presence, and server submission.
9. Disconnect networking before an unlock, reconnect without closing the
   emulator, and confirm the pending unlock is submitted.
10. Repeat the matrix on both debug and signed release builds and capture a
    short screen recording for the application.

## Non-code application items

These cannot be proven by the native build and must be completed or explicitly
disclosed in the new application:

- Disclose that earlier EmuCoreX development builds inherited PCSX2's user
  agent before the dedicated EmuCoreX identity was added.
- Provide the first public release date and evidence that the emulator (or its
  parent) satisfies the six-month eligibility timeline.
- Publish an exact Free-versus-Pro feature matrix. Paid features must not alter
  or weaken Hardcore behavior.
- Provide a public third-party software/license page with PCSX2 and every
  shipped dependency, license, and upstream link.
- Recheck the published privacy policy for concrete retention periods, server
  countries/locations, Firebase/Google processing, analytics/social login,
  and GDPR handling. Placeholders or contradictions are an auto-fail risk.

### Current publication blockers found on 2026-07-13

These must be corrected before the application is submitted:

- The published privacy policy claims that EmuCoreX ships Firebase Crashlytics,
  Google AdMob, cloud-synced save states, BIOS configuration, and controller
  profiles. Those services/features are not present in the current Android
  dependency and implementation audit. This contradicts the shipped product.
- The policy does not give concrete retention periods (apart from a 30-day
  deletion-request target), Firebase/Google server countries, international
  transfer details, or a complete GDPR basis. RetroAchievements processing and
  Google Play Billing are also not described.
- The public website still describes the core as PCSX2 `2.7.217`, while this
  pinned integration and client identity use PCSX2 `2.8.0`.
- A public Free-versus-Pro matrix and complete shipped-dependency/license page
  were not found on the public website.

Do not reapply until these public pages match the release being submitted.

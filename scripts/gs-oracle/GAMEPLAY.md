# Gameplay profiling on Lenovo

Use actual controllable gameplay to choose optimization targets. Startup, loading,
menus and movies remain navigation evidence. This opt-in harness lives in the
Android test APK; its native freshness getter is compiled only with the Debug GS
oracle. App Debug uses optimized native code (`-O2 -g1`). Experimental IR stays
disabled in Debug and Release; direct recompilers remain active.

Build and install `:app:assembleDebug` and `:app:assembleDebugAndroidTest`. Always
select the authorized Lenovo explicitly with ADB `-s`. Supply an accessible local
game file and BIOS directory. On scoped-storage devices, a shell-readable game
under `/sdcard` may still be inaccessible to the app. Test copies can be placed
in the debuggable app's private `files/oracle-games` and `files/oracle-bios`
directories using `run-as`; keep existing user files untouched.

Start the instrumentation in one terminal (use a fresh run ID):

```powershell
adb -s YOUR_LENOVO_SERIAL shell am instrument -w -r `
  -e class com.sbro.emucorex.core.GameplayProfilingDeviceTest `
  -e gamePath /data/user/0/com.sbro.emucorex/files/oracle-games/game.iso `
  -e biosPath /data/user/0/com.sbro.emucorex/files/oracle-bios `
  -e renderer 14 -e runId race-vk-01 -e runSeconds 900 `
  -e unlimited true -e gpuTiming false `
  com.sbro.emucorex.test/androidx.test.runner.AndroidJUnitRunner
```

Renderer 14 is Vulkan; 12 is OpenGL. The test saves/restores BIOS, game-directory
and onboarding preferences, shuts down the VM on completion/failure and releases
held buttons. It does not overwrite an existing save-state slot. Use dedicated
test app data; normal guest gameplay can still write its memory card.

In another terminal, control the run:

```powershell
$control = 'scripts/gs-oracle/gameplay.py'
$adbGS = 'C:/path/to/adb.exe'
$serialGS = 'YOUR_LENOVO_SERIAL'
python $control --adb $adbGS --serial $serialGS --run race-vk-01 status
python $control --adb $adbGS --serial $serialGS --run race-vk-01 press --button 108
python $control --adb $adbGS --serial $serialGS --run race-vk-01 screenshot --output ./screen-01.png
```

Pad keys: Cross 96, Circle 97, Square 99, Triangle 100, Start 108,
Select 109, Up/Down/Left/Right 19/20/21/22, L1/R1 102/103, L2/R2 104/105.
`press` accepts `--duration-ms` (1–60000) and `--pressure` (1–255); expiry releases
the button. Separate presses can overlap. `release` releases all held buttons.

Navigate into a race or playable scene, inspect a screenshot, then explicitly
mark the window. A phase marker also records a screenshot:

```powershell
python $control --adb $adbGS --serial $serialGS --run race-vk-01 phase --value gameplay
python $control --adb $adbGS --serial $serialGS --run race-vk-01 press --button 96 --duration-ms 20000
```

For NFS Most Wanted, Cross accelerates. Confirm motion and scene progress from
screenshots; commanded input alone is not proof of movement. The test requires
at least 20 fresh nonzero-FPS gameplay samples. Native publication sequences
prevent one cached FPS value from passing a stalled run. Timing is sampled every
500 ms with detailed counters. Unlimited mode is the default: the native limiter
is disabled after startup and after loading a state, and each fresh gameplay
sample must report `Target: Max`. GPU timing defaults off to reduce measurement
overhead. A separate `-e gpuTiming true` profile can request GPU timers;
unsupported timers remain unavailable. Use `-e unlimited false` only for an
explicitly capped diagnostic. The prior limiter setting is restored at shutdown.

Diagnostics end the measured phase. Screenshots, save/load and dump requests
switch back to navigation; a new gameplay marker and a two-second settling
period are required before samples count again:

```powershell
python $control --adb $adbGS --serial $serialGS --run race-vk-01 save --slot 7
python $control --adb $adbGS --serial $serialGS --run race-vk-01 dump --frames 3
python $control --adb $adbGS --serial $serialGS --run race-vk-01 stop
python $control --adb $adbGS --serial $serialGS --run race-vk-01 pull --output ./race-vk-01 `
  --apk app/build/outputs/apk/debug/app-debug.apk `
  --native-build-directory app/.cxx/Debug/YOUR_CONFIG/arm64-v8a
python scripts/gs-oracle/gameplay_report.py ./race-vk-01 --active-button 96 --require-unlimited
```

Reports distinguish confirmed gameplay samples from samples overlapping active
input, discard stale/navigation/settling/diagnostic intervals and retain the
command log and screenshots. A test ending before confirmed gameplay fails.
`--require-unlimited` rejects missing/capped limiter metadata, samples without
`Target: Max`, or enabled GPU timing. It does not establish identical workloads
or a causal speedup by itself. Repeat the same playable state and input, keep
resolution/settings/build configuration fixed, and compare comparable thermal
conditions. Timed input presses alone can cover different emulated distances
at different speeds; use deterministic GS replay for isolated renderer changes.

The paired APK/build options verify the installed APK hash, matching packaged
and unstripped core ELF build IDs and the native compiler configuration before
attaching `host-metadata.json`. A local build that differs from the installed
app is rejected. Git state in that file is explicitly the state observed at
pull time. Keep the exact APK and native binaries with the recorded profile.

GS dumps are written to the app's configured snapshots directory, normally
`/sdcard/Android/data/com.sbro.emucorex/files/snaps`. Wait for recording to finish,
pull the complete file and pass it to `Run-GSOracle.ps1 -InputDump ...` for strict
Software/Vulkan/OpenGL comparison, isolated replay benchmarks and profiles.
That oracle fixes native resolution and maximum blending accuracy, so its replay
timings must not be equated with game FPS or different game settings.

For CPU sampling, the NDK's `simpleperf/app_profiler.py` supports attaching to
the already-running debuggable app. Set `ANDROID_SERIAL` to the authorized Lenovo
and use `--disable_adb_root`; omit activity/launch options to preserve gameplay.
For example, record `-e task-clock:u -f 500 --duration 15 -g`. Mark this separate
diagnostic interval as navigation in the harness to exclude profiler overhead
from ordinary timing samples. Retain `perf.data`, sample-loss counts and the
matching native binaries/build IDs for symbol attribution.

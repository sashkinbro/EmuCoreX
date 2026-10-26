# Debug GS oracle

Deterministic Software-reference tests for the actual EmuCoreX Vulkan and
OpenGL renderers, plus separate benchmark and profiling passes. Runs in isolated
ADB shell processes: no Activity, BIOS, APK installation, application settings
changes, package stops, or logcat clearing. Always select an authorized device
explicitly with `-Serial`.

## Build and run

Gradle's **debug** variant enables `EMUCOREX_ENABLE_GS_ORACLE=ON` together with
native self-tests. Alternatively configure a separate Android CMake directory
with `CMAKE_BUILD_TYPE=Debug`, `EMUCOREX_ENABLE_NATIVE_SELF_TESTS=ON`, and
`EMUCOREX_ENABLE_GS_ORACLE=ON`. Reuse the project's Android toolchain, ABI,
page-size, dependency and output-directory options. Do not change a shared
Release build directory to Debug. Both library and runner output directories
must be set in the CMake cache, as in Gradle's generated configuration.

```powershell
./scripts/gs-oracle/Run-GSOracle.ps1 `
  -NativeBuildDirectory app/.cxx/GsOracle/arm64-v8a `
  -Serial 'YOUR_LENOVO_SERIAL'
```

The runner builds `emucore` and `emucorex_gs_oracle`, deploys the core, runner,
resources and librashader to a unique temporary directory, executes all three
backends, and pulls evidence. `-SkipBuild` uses an already completed build.
Python 3 (standard library) and PowerShell are required. `-Adb` and `-Python`
accept explicit executable paths.

Defaults: **44 named scenes + 256 generated state combinations**, seed 1,
two complete warmup replays, seven measured samples, ten replays per sample.
Use `-Cases 4096 -Seed 42` for a larger deterministic campaign, or `-Cases 0`
for the named cases. Seeds range from 1 to 2147483647. Every generated GIF
stream is retained as `capture/corpus.bin` and hashed independently per backend.
`-Backends sw,vk` is valid when invoking from PowerShell; Software plus at least
one hardware backend is required. Each process has a 900-second watchdog.

**A renderer mismatch fails the command intentionally.** Crashes, malformed
input, absent artifacts, incompatible settings, missing frames, invalid timing
samples and missing completion markers also fail. Other backends still run
after a renderer failure so their logs survive. A transport failure during
deployment stops the run. Device temporary directories are retained for diagnosis.

## Coverage and reference contract

Each synthetic scene resets the GS, initializes CT32 framebuffer and Z32 depth
storage at disjoint addresses, sends real GIF packets, flushes drawing, and reads
both buffers through the synchronized GS FIFO. Checks include all RGBA bits
(including alpha) and raw depth/VRAM bytes. Software output additionally passes
independently calculated background, rectangle coverage and depth anchors.

The 44 named cases cover sprites, points, lines/strips, triangles/strips/fans,
Gouraud colors, fog, context 2, scissor, frame masks, alpha/depth/destination-alpha
tests, FBA, alpha and fixed-alpha blending, wrapping, texture uploads, nearest
and bilinear sampling, repeat/clamp/region modes, all four TFX functions,
RGB/RGBA texture selection, CLUT4/8 with CSM1/2, CLUT8H/4HL/4HH, CT16/16S
dithering, CT24, Z16/Z24, local copies/overlap, TEXA and perspective STQ.

The generated cases combine all eight alpha-test functions and four alpha-fail
actions, depth-test functions/write masking, DATE/DATM, FBA, PABE, random bit
masks, clipping, format reinterpretation and blending selectors with seeded
geometry/colors. The first 256 cases exercise all 81 legal ALPHA selector
tuples across the corpus. More cases and seeds expand interactions; they do not
constitute exhaustive GS coverage. Mipmapping, AA1, field history, texture
feedback and game-specific sequences need recorded dumps and further fixtures.

Configuration is fixed and reported as `native-exact-v2-interlace-auto`: 1x,
maximum blending accuracy, synchronous guest downloads, PS2 texture filtering,
automatic interlacing, one synchronous Software rasterizer, no postprocessing,
texture replacement or game-specific overrides. Software is a useful reference;
a mismatch still needs investigation rather than automatically proving a hardware
backend bug.

## Dumps and reproductions

For a sustained coverage campaign, run multiple seeds and optionally every dump
in a directory (including subdirectories):

```powershell
./scripts/gs-oracle/Run-GSCampaign.ps1 `
  -NativeBuildDirectory app/.cxx/GsOracle/arm64-v8a `
  -Serial 'YOUR_LENOVO_SERIAL' -Seeds 1,7,42 -Cases 512 `
  -DumpDirectory ./gs-corpus
```

Omit `-DumpDirectory` to run only synthetic inputs. The default three seeds and
512 generated combinations execute **1,668 scenes and 3,336 color/depth
checkpoints per backend**. `-Cases 4096` increases this to 12,420 scenes across
three seeds. Each run retains its full report; `campaign-report.json` groups
identical reference/candidate snapshots while retaining seed, frame, packet and
source report. These groups help triage; their count is not an independent bug
count. `campaign.json` records progress after every run, so interrupted work
retains a precise ledger. Interrupted or failed executions cannot become a
campaign PASS. Campaigns continue after mismatches and retain execution errors,
then return failure if either occurred. The native build is reused after the
first completed run. Device runs are sequential on the explicitly selected serial.

```powershell
./scripts/gs-oracle/Run-GSOracle.ps1 `
  -NativeBuildDirectory app/.cxx/GsOracle/arm64-v8a `
  -Serial 'YOUR_LENOVO_SERIAL' -InputDump ./capture.gs
```

`.gs`, `.gs.xz` and `.gs.zst` use PCSX2's existing reader and freeze format.
All GIF paths, privileged-register updates, field values and guest FIFO reads
are replayed. Strict parsing rejects a truncated final packet; normal emulator
dump playback retains its previous permissive behavior. Incompatible savestate
versions fail. Displayed frames are downloaded directly without screenshot
aspect correction or color/alpha filtering. FIFO results are compared exactly.
An unrenderable/absent output frame is an error, never an implicitly matching
black image. All-blank or startup-only recordings may need a later capture.

The first twelve synthetic scenes export `sw/capture/repro-<name>.gs` with an
initial state and original drawing/FIFO commands. `-ExportCase N` additionally
exports a selected scene (zero-based frame index shown in the report), including
generated cases. Re-run with the same `-Cases` and `-Seed`, then use its `.gs`
as input. A backend state leak may require the complete builtin sequence rather
than the isolated scene; the saved seed and stream preserve that distinction.

The Android debug app already supports launch extras
`com.sbro.emucorex.extra.GS_DUMP_FRAMES` and
`com.sbro.emucorex.extra.GS_DUMP_DELAY_MS`; they are ignored by release builds.
Capture a problematic game sequence with these existing controls, retrieve the
GS dump from the app's screenshot directory, then replay it with this runner.

## Reports and optimization

Each timestamped result directory contains:

- `metadata.json`: commit/dirty state, binary SHA256, device serial/model/hardware/
  OS fingerprint, input hash and replay parameters.
- `report.md`, `report.json`, **`report.html`**: readable summary, structured data
  and a local expandable image gallery.
- `diff-vk/`, `diff-gl/`: exact changed pixel count, pixels outside explicit
  tolerance, first differing coordinate/channel, error bounds, mean/max byte
  error, reference/candidate/difference PNGs. Alpha differences remain visible.
- `<backend>/capture/`: original binary snapshots, input evidence and manifest.
- `<backend>/benchmark/`: warm synchronized replay samples, no comparison
  downloads or profiling-counter collection.
- `<backend>/profile/`: per-checkpoint submission/wait time and GS counters for
  primitives, draws, GPU draw calls, readbacks, barriers, passes, copies/uploads
  and ROV work. `copies_or_fillrate` and `uploads_or_syncpoints` follow the GS's
  hardware/Software counter aliases. These diagnostic timings include explicit
  synchronization and cold work; use benchmark timings for regression decisions.
- `<backend>/*.log` and `*/driver.txt`: native diagnostics and GPU identity.

Benchmark GPU work is explicitly submitted and completed before the timer stops
(Vulkan command-buffer fence or OpenGL `glFinish`). Software uses the synchronous
rasterizer. Timing includes command replay, GS resets/defrost and guest-requested
FIFO reads, but excludes diagnostic snapshots, file writes, library/device startup
and teardown. These are replay timings, **not game FPS or isolated GPU timestamps**.
Ten complete, individually drained replays form one sample. Median/p95/min/max
are reported; thermal state, background work and debug build settings affect them.

```powershell
./scripts/gs-oracle/Run-GSOracle.ps1 `
  -NativeBuildDirectory app/.cxx/GsOracle/arm64-v8a `
  -Serial 'YOUR_LENOVO_SERIAL' -Baseline ./previous-result-directory
```

Baseline comparison requires identical input, device/OS, GPU driver, backend,
settings, frame count and warmup. Positive time change means slower. Changed
binary hashes are preserved, allowing before/after code comparisons. Use the same
compiler configuration and comparable device temperature when assessing changes.
`-Tolerance 1` explicitly allows up to one byte of RGBA error while retaining
the exact difference count; **depth and FIFO never use pixel tolerance**.

## Verification and release isolation

```powershell
python scripts/gs-oracle/test_compare.py
./scripts/gs-oracle/Verify-ReleaseIsolation.ps1 `
  -DebugBuildDirectory app/.cxx/GsOracle/arm64-v8a `
  -ReleaseBuildDirectory app/.cxx/GsOracle/ReleaseCheck
```

The second command checks built ELF exports and the Release Ninja graph.
CMake also rejects `EMUCOREX_ENABLE_GS_ORACLE=ON` outside Debug, even when the
older CPU-native-self-test option is enabled. Release contains neither GS oracle
source, entry point nor runner target. The OpenGL surfaceless-context fix and
general optional strict dump-reader API are renderer/parser support, not runtime
comparison hooks.

To exercise the production OpenGL clear method on a connected ARM64 Android
device (requires the Android NDK and GLES 3.2):

```powershell
python scripts/gs-oracle/run_clear_test.py `
  --ndk C:/path/to/android-ndk --adb C:/path/to/adb.exe `
  --serial YOUR_LENOVO_SERIAL --output ./clear-test-results
```

Use a new output directory for each run. The harness compiles the current
`CommitClear` method body with minimal texture/state adapters and checks 18
normalized-color cases: plain, feedback and shader-write targets, explicit
scratch-FBO selection, MRT attachment preservation, full-surface clearing and
restored state. It retains the extracted body, binary, hashes and device log.
This focused regression does not cover depth/integer clears or replace full
renderer replay; its adapters do not exercise production state-cache helpers.

For controllable in-game captures, uncapped performance observations and CPU
sampling, see [GAMEPLAY.md](GAMEPLAY.md). The gameplay report distinguishes fresh
samples during active input from the rest of the marked scene and records
limiter/GPU-timing eligibility explicitly.

New GS runs record a schema-2 build-configuration fingerprint covering native
compiler/NDK identity, ABI/API, flags and settings for all native targets,
including PCSX2/PCSX2_LTO and common. Timing baselines require matching complete
fingerprints; missing or older provenance remains usable for correctness only.
Binary hashes may differ to compare code changes. This prevents an optimization
flag change such as `-O0` to `-O2` from being presented as a source-code speedup.

```powershell
python scripts/gs-oracle/triage.py ./result-directory
```

The inventory accounts for every strict failure, groups generated selector
families, identifies changed byte channels and summarizes numeric depth deltas
and agreement between hardware backends. These groups guide investigation;
agreement between Vulkan and OpenGL does not establish correctness.

# GS oracle validation — 2026-10-26

Device used: Lenovo TB710FU, Adreno 750, Android 16. Other connected devices
were not used. Tests ran in isolated ADB shell processes.

Verified:

- Debug core and GS executable build successfully.
- Release core builds successfully. ELF export inspection and Ninja graph checks
  confirm that GS oracle sources, executable target, and entry point are absent.
  An explicit attempt to enable the GS oracle in Release is rejected by CMake.
- 12 host tests pass, covering exact/alpha/depth/FIFO comparisons, explicit
  tolerance, absent/truncated captures, incompatible inputs, malformed frame
  accounting, benchmark compatibility, profiles, complete reports, and campaign
  provenance/error handling. New frame-accounting and campaign validation tests
  were observed failing before the corresponding fixes.
- A three-seed campaign (1, 7, 42), 512 generated cases plus 44 named scenes per
  seed, completed all backends: 1,668 scenes and 3,336 color/depth checkpoints per
  backend, with separate benchmark and profile passes. No execution errors.
- A final campaign with the stricter reviewed campaign contract completed five
  runs: seeds 1 and 42 with 256 generated cases each, plus identical sprite
  recordings in uncompressed GS, XZ, and Zstandard formats. All three recorded
  inputs compare exactly across Software, Vulkan, and OpenGL. No execution errors.
- A deliberately truncated GS packet is rejected by native replay and the host
  runner. The failure cannot become a comparison PASS.
- Before/after benchmark reporting was exercised with matching input/settings.
- Independent code review findings were fixed: dump profiles now include VSync's
  merge/deinterlace work; benchmark frame accounting and campaign workload/input/
  backend provenance are checked. Final review has no remaining findings.

The oracle intentionally reports existing renderer discrepancies. The final
default-size seed-1 run found 77 Vulkan and 308 OpenGL differing checkpoints out
of 600 per hardware backend; seed 42 found 387 combined differing checkpoints.
These counts are neither independent bug counts nor a claim that every mismatch
has already been assigned to the backend. Software reference anchors pass;
triangle/texture-edge and OpenGL alpha/depth differences remain visible for
further investigation. The isolated scissor recording also reproduces an OpenGL
difference while Vulkan matches Software.

An Android OpenGL context-selection issue encountered while bringing up the
runner was fixed: surfaceless Android startup now selects the existing Android
EGL implementation and prefers GLES. The real OpenGL backend subsequently
completed all coverage/replay passes.

Local evidence (ignored by Git):

- `results/campaign-20261026-101143-621-8e238e40/`: larger three-seed campaign.
- `results/campaign-20261026-101504-212-90a31fed/`: final reviewed five-run campaign.
- Each campaign retains its manifest, aggregate report, per-run HTML/JSON/MD,
  native logs, raw snapshots, generated inputs, reproductions and timing samples.

Timing values are synchronized Debug replay measurements, not game FPS. No
performance improvement is claimed by this instrumentation commit. See README
for the fixed settings, workload coverage, and remaining coverage limitations.

## OpenGL clear correction (2026-10-26)

The oracle exposed a clear operation on a feedback render target that bound an
empty scratch framebuffer: `GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT`,
followed by `GL_INVALID_FRAMEBUFFER_OPERATION`. Texture usage is a bitmask;
enum equality omitted render/depth attachments for combined usages outside the
existing Haunting Ground special case. The correction uses the existing usage
predicates for all titles and routes clears through the scratch framebuffer
whenever a depth-as-color MRT is active.

- Actual production-method regression on Lenovo: old body 9/18 passing; corrected
  body 18/18 passing. The final harness also verifies that the other MRT texture
  retains its pixels.
- Full seed-1 run, 300 scenes / 600 checkpoints per backend: OpenGL mismatches
  decreased from 308 to 75; Vulkan remains at 77. No previously passing
  checkpoint became a mismatch in this run. These are checkpoint counts, not
  independent bug counts. Evidence: `results/20261026-103652-975-8a91c468/`.
- Debug and Release native builds succeed; compiled Release isolation passes.
  Host comparator suite: 12 tests pass. Independent review found no important
  correctness issues in the production fix.
- CPU oracle guards pass for classifier, JIT cache, VU cache, VIF MPG, VU divide
  by zero, scalar clamp, semaphore and VU suites. SPU2 volume has one performance
  assertion failure on both the previous and corrected core in the unoptimized
  Debug build; all 16,777,216 volume-state transitions pass. This baseline
  performance failure remains visible and is not treated as a passing suite.
- OpenGL timing changed by -0.60% median and +1.92% p95 in this paired run.
  No performance improvement is claimed from these measurements.

## Direct recompilers and actual gameplay

- Gradle Debug APK build initially reproduced a configuration failure: the
  default Release native override conflicted with the Debug-only GS option.
  Variant-specific configuration now builds and installs optimized Debug
  (`-O2 -g1`) APKs. Experimental IR is explicitly OFF for both app variants.
- Runtime-gate tests cover 36 actual production build/property/override
  combinations; 86 dispatcher cases verify explicit IR-suite unavailability
  and separate direct selectors.
- On Lenovo, optimized native guards completed 4,769 checks with zero failures:
  direct EE stores 726, direct EE 3,132, direct IOP 379, direct VU 441, SPU2 volume
  9, classifier 53, JIT cache 11, VU cache 18. Output reports compiled IR runtime
  capability 0. The earlier SPU2 timing gate passes with optimized Debug.
- Direct IOP cached-base addresses now fold signed offsets within ±4095 into
  ARM64 ADD/SUB immediates. The real emitter passes 972 host code-generation
  cases (378 failures before the change). The expanded direct IOP oracle passes
  2,380 checks, including 687 RAM/mirror/alias/zero-register/MMIO fixtures.
  All eight direct/runtime guard suites pass 6,770 checks on Lenovo with IR
  runtime capability 0: `tests/results/jit-oracle/20261026-121415-312/`.
  This reduces emitted host instructions; no game-FPS improvement is claimed.
- NFS Most Wanted gameplay reached a Sprint on Seaside & Power Station in the
  Golf GTI. Saved state 7 permits starting both hardware backends from the same
  playable state. Screenshots show the car moving and race progress; startup
  and menus remain navigation evidence. Both capped diagnostic runs passed the
  gameplay capture gate, but are not uncapped performance baselines.
- A separate 15-second driving CPU sample recorded 14,061 samples with zero
  loss. Hot symbols include GSRendererHW::Draw, the fused STQ/RGBA/XYZF handler,
  VIF/MTVU, time stretching and SPU2 mixing. Exact data/binaries are retained in
  `.git/mw-simpleperf-vk1/`; sampling intervals are excluded from ordinary timing.
- The harness now defaults to unlimited mode and GPU timing off. Native
  `Target: Max` is required in fresh marked gameplay samples. Save/load and
  screenshot/dump intervals end the measured phase. Cached samples cannot meet
  the gameplay gate; active-input statistics exclude time after button expiry.
- Both uncapped runs from state 7 passed instrumentation and the strict unlimited
  report check. Each has 46 fresh samples overlapping active Cross input:
  Vulkan median reported speed 242.962%, OpenGL 193.719%. Native snapshots report
  `Target: Max`; GPU timing is disabled. Their installed APK hashes and core ELF
  build IDs match the local artifacts, with schema-2 configuration attached in
  `host-metadata.json`. These short independent observations cover different
  emulated distances under wall-clock input and are not a causal speedup or a
  controlled backend ranking. Evidence: `.git/gameplay-mw-vk-unlimited-01-final/`
  and `.git/gameplay-mw-gl-unlimited-01-final/`.
- A maximal synthetic run completed 4,140 scenes / 8,280 checkpoints per backend
  with zero execution errors. Strict differences remain: Vulkan 670 and OpenGL
  629. The full 1,299-failure inventory is retained in
  `results/20261026-112740-077-0c9e88fb/triage.json` and `triage.md`.
- The six-field driving GS dump completed all replay/capture/profile passes.
  Both hardware backends produce identical bytes but differ from Software on
  all six captures. The first field has a large alpha/history difference;
  later fields have many small color differences and some edge outliers.
  They remain strict failures requiring investigation. Evidence:
  `results/20261026-112117-804-dd5bd74b/`.
- A confirmed format-transition defect has an independent GS-address-table
  anchor: `matrix-0194` must preserve untouched raw word 0 as `0x80102030`,
  while both hardware backends return the repacked value `0x88868886`.
  Z16 layout cases use exact depths 0–3, so their errors cannot be dismissed as
  floating-point depth precision. These fixes are still in progress.

Older captures use schema-1 compiler provenance and remain correctness evidence.
They are deliberately refused as timing baselines after the all-native-target
schema-2 correction. New before/after measurements must use schema 2 with equal
compiler configuration. No emulator FPS improvement is claimed from instruction
count reduction, capped gameplay, or mismatched compilation settings.

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

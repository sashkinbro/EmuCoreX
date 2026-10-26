# Direct recompilers and experimental IR

Normal app Debug and Release builds explicitly configure
`EMUCOREX_ENABLE_EXPERIMENTAL_IR=OFF`. EE, IOP, VU0, VU1 and EE macro VU dispatch
ignore old IR Android properties and oracle overrides unless a separate research
build enables this capability. Direct recompilers remain the gameplay paths.
There are no shared-IR settings in the app UI.

Debug native code uses `-O2 -g1`, retains self-tests and the GS oracle, and can
produce an installable profiling APK. Release disables the native self-tests and
GS oracle. `scripts/gs-oracle/Verify-ReleaseIsolation.ps1` checks the compiled GS
and gameplay-profiling exports and requires experimental IR OFF in both caches.

Host regression checks execute extracted production gate/dispatcher code and
real Oaknut instruction generation:

```powershell
python build-support/test_shared_ir_runtime_gates.py
python build-support/test_jit_oracle_ir_policy.py
python build-support/test_ee_store_address.py --compiler C:/path/to/g++.exe
```

The native JIT oracle reports the compiled experimental-IR capability. Use
`ee-direct`, `ee-direct-stores`, `iop-direct` and `vu-direct` for interpreter/direct
comparisons with IR disabled. `ee-direct-stores` checks dynamic cached-base stores
at displacement boundaries, source/base aliases, all scalar widths, SQ alignment,
SWC1, fastmem/VTLB parity, full architectural state and exact cycles.

Original mixed/IR integration selectors such as `ee`, `iop`, `vu` and `ee-stores`
return `UNAVAILABLE` and exit 2 when IR is disabled. They must not silently test
the direct fallback and claim IR coverage. Pure lifter/lowerer diagnostics remain
available. A research build must explicitly opt in to experimental IR as well
as native self-tests; app variants never opt in.

The EE store-address optimization folds a small signed displacement directly
from a cached base, eliminating the preceding MOV. Host instruction tests prove
reduced emission; they do not establish an emulator FPS improvement. Native
oracle and actual gameplay profiling are required in addition.

# IOP execution ownership

The channel, worker, bridge, mode and placement implementation is adapted from
ARMSX2 revision aa52a1fdc7cd03fb02bd404ebaa9940944e7fc60, under GPL-3.0+.
Original SPDX copyright and license notices are retained in each source file.

EmuCoreX adaptations preserve its inline Off execution/cycle accounting and
32-bit CPU/savestate layout. CycleClock32 unwraps EE stamps on the owning thread.
The worker uses the portable fence sleep handshake by default, explicitly
sets/restores FPCR, and services IOP-to-EE work during EE waits. Shared compiler
allocator/instruction state is thread-local; shared SBUS and input words use
atomic accesses without changing storage layout.

Native settings: EmuCore/Speedhacks iopThread (default false),
iopThreadLockstep (development reference, false), iopThreadLag (24576 EE cycles,
clamped to 5120..196608 (the local event interval is 4096)). Eligibility reports requested/effective modes and
fallback conditions in the log. Debug native-test builds additionally accept
Android property debug.emucorex.iop_mode=off|lockstep|threaded; clear the property
to use saved settings. The override never bypasses hardware/VM eligibility.

VM pause/reset/settings/save/load and shutdown hold the IOP before accessing its
state. Stop joins before devices are destroyed. EE-to-IOP RAM/MMIO access and
combined SIF pumps use holds; IOP-started pumps rendezvous on the EE. Host
callbacks that cannot block are queued to their owner.

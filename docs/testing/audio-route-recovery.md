# Android audio route recovery

Reported on Retroid Pocket 5: connecting wired or Bluetooth headphones silences
audio, and disconnecting does not restore it. Pausing/resuming or restarting the
game restores playback.

The AAudio error callback previously marked the stream disconnected but recovery
ran only in `SetPaused(false)`. A disconnected stream cannot recover by itself.
The backend now signals an owned worker to reopen it outside audio callbacks.
Lifecycle operations serialize with pause and teardown. Generation-tagged
callbacks cannot silence replacement streams; failed opens/starts retry after
250 ms, paused games remain silent, and teardown interrupts and joins the worker.
Mixer buffers are initialized once and survive output-stream replacements.

## Automated checks

```powershell
python build-support/test_android_aaudio_recovery.py --compiler C:/msys64/ucrt64/bin/g++.exe
python build-support/test_android_aaudio_recovery.py --export-directory app/build/aaudio-recovery-oracle
```

The test compiles the actual backend against fake AAudio/mixer seams. It covers
automatic recovery without pause, callback restrictions, stale errors, errors
during start, open/start failures, retry rate, pause races and shutdown. The old
backend reproduced the missing automatic recovery before implementation.

The exported directory contains an immutable production-source snapshot, source
hashes, and NDK/ADB instructions for running the same fault injection on ARM64.
Host and Lenovo TB710FU Android ARM64 runs pass **76 checks, zero failures**.
Debug and Release native builds pass; IR remains disabled and comparison tools
remain absent from Release. No app test hook or new exported symbol is required.

These tests inject API errors; they do not simulate physical Bluetooth routing.
Retroid Pocket 5 jack/Bluetooth confirmation remains pending. On that device,
test repeated connect/disconnect during actual gameplay and while paused, rapid
pause/resume, screen off/on, and game exit during a route change. Playback should
resume automatically when running and stay silent while paused. Native logs
include `AAudio stream recovered after disconnect`; repeated failures log their
retry count without flooding every attempt.

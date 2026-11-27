# Native GameShare lifecycle tests

Run from the repository root:

```sh
python3 libraries/gameshare/tests/native/run.py
```

The runner compiles the current production
`libraries/gameshare/src/host.cpp` under `__SWITCH2__`, with host-only
Switch IPC, applet, and guest-input mocks. It runs the actual asynchronous
worker and snapshot API; there is no second implementation of the lifecycle.
A C++20 compiler with AddressSanitizer and UndefinedBehaviorSanitizer is required.
Set `CXX` to select a compiler.

The cases cover:

- Start, native-ready input publication, immediate stop neutralization, and complete cleanup.
- Initial zero connection masks followed by admission during the two-second grace period.
- Input expiration while a connection-status IPC call blocks the worker.
- Guest disconnection and cleanup after a native buffer-registration failure.
- Matchmaking cancellation, including stop while the applet is still open.
- Owned player-selector success, cancellation, and cancellation while open.
- Locked title with a validated launch profile, reuse across sessions,
  the launcher's profile cache, and missing or malformed launch data.
- Audio-manager fallback and temporary service cleanup.
- Disabling application audio mirroring or guest-input mapping independently.
- Invalid host configurations rejected before opening native resources.
- A second host rejected without touching the active host; it can start after the first stops.
- Separate-display mode passed through the native configuration; render-target
  open precedes Initialize, and target retirement follows session closure and
  HID unmapping but precedes native Finalize. Display close follows successful
  Finalize and precedes transfer-handle closure and work-memory release.
- Missing render targets and unknown video modes rejected before native work.
- Partial target-open failures and provider exceptions cleaned up; failed target
  retirement or post-Finalize close retains the complete owner and provider,
  including after Host destruction. A failed Finalize prevents display close.
- Failed native finalization: transfer handles and work remain owned, input is unmapped,
  and both the same instance and a new instance after destruction refuse another start.
- `canUnload()` distinguishes a new or joined clean host from an active,
  finished-but-unjoined, or quarantined host.

Mocks reject unexpected commands, incorrect selected ABI inputs, double closes,
closure of the borrowed self-controller, and transfer-handle closure before native
finalization. Tests inspect the actual work allocation through ASan to distinguish
freeing it after successful finalization from retaining it after failure. Each
scenario runs in its own process, with bounded waits. Leak reporting is disabled
only for the scenarios that deliberately retain native-owned resources until
process exit.

These tests establish application lifecycle and ownership behavior against modeled
IPC responses. They do not qualify firmware IPC contracts, screen/audio mirroring,
radio behavior, actual applet behavior, or physical controller reception. No test
uses a console, network connection, keys, or proprietary binary.

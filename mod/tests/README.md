# Automated compatibility and runtime regressions

With `BUILD_TESTING=ON` (the CMake default), the normal build produces two
test executables. These tests do not open an audio endpoint or play music:

```powershell
ctest --test-dir mod/build -C Release --output-on-failure
```

`am_exclusive_compatibility_tests` checks the 1.6/1.7 COM interface paths,
unknown-interface rejection, recorded image guards, and (when Apple metadata
is present) the generated player IIDs. It also accepts CoreMedia.dll paths to
verify actual files without loading Apple's DLLs. The installed package's
CoreMedia file is included in CTest when available.

`am_exclusive_runtime_tests` drives the production runtime through five
scenarios: legacy selection after observer loss, canonical pause precedence
and stale revisions, rebind without selection, expired selection, and queue
overflow with worker retirement and subsequent re-enable. No handoff callback
is supplied, so these scenarios cannot acquire a physical audio device.

# WASAPI drain hardware regression probe

Build explicitly (the target is excluded from normal builds):

```powershell
cmake --build <build-directory> --config Release --target am_exclusive_drain_probe
<build-directory>\Release\am_exclusive_drain_probe.exe --run-on-default-device
```

This acquires the current default render endpoint in exclusive mode and sends
only silence. Stop other playback first. The device must support stereo packed
24-bit and 16-bit PCM at 192, 96 and 44.1 kHz. Unsupported formats are reported
as failures, not skipped. It is deliberately not registered as an unattended
CTest.

The 54 cases cover empty, partial and full terminal packets, source starvation,
pause/resume during drain, initially empty sources, and producer notifications
as well as polling. The source rejects any request after its terminal packet.
Each case requires clock-certified completion without underruns or errors.

Motivation: on a C-Media-based USB endpoint (Fosi Audio DA2120C), end-of-stream
drains that followed a source starvation wait timed out intermittently (3-4 of
the 36 cases per run, always `wait=1` cases). The device loses its event/clock
cycle when a packet is released into a stopped, previously run exclusive
stream: IAudioClock parks below the committed end-of-stream position and the
drain can only fault. Restarting the preserved endpoint queue before releasing
the next packet (with a readiness check first, so the restarted stream is
filled on the same wake) made all 36 cases pass repeatedly on the same
hardware.

These are transport/control tests. They do not establish audible correctness
or Apple Music lifecycle behavior.

An optional sample rate argument selects its 18-case subset, for example:

```powershell
mod/build/Release/am_exclusive_drain_probe.exe --run-on-default-device 44100
```

## Pause cadence hardware probe

Build `am_exclusive_pause_probe` explicitly. It also outputs only silence and
requires `--run-on-default-device`, optionally followed by 44100, 96000 or
192000. It measures a running baseline and two one-second pause/resume cycles.
Each running interval must remain within 5% of the nominal sample rate. A
keep-alive pause must continue submitting silent device packets without
restarting the client or reading more than one in-flight media packet.

On the local MOONDROP MM3A, the original 44.1-kHz/16-bit, 20-ms Stop/Start
pause reproduced an approximately 80% resumed cadence with no underrun/error.
The keep-alive path avoids that physical restart while preserving the source.

The probe also injects a one-second source-delivery gap and a resume-blocked
control interval. It checks that source data stays unconsumed while device
silence continues, that no extra client Start occurs, and that cadence returns
to nominal afterward. Supplying `--legacy-wait` after the optional sample rate
selects Stop/Start on that gap for an A/B reproduction of the old fault:

```powershell
mod/build/Release/am_exclusive_pause_probe.exe --run-on-default-device 44100 --legacy-wait
mod/build/Release/am_exclusive_pause_probe.exe --run-on-default-device 44100
```

The opt-in device tests model the delivery interruption observed after display
wake; a real power transition still needs a separate listening check.

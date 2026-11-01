# Local Apple Music 1.7 compatibility revision

Base: upstream `1.7`, commit `91f589d87b6eed62b94c1cde2fd92711bb9303ac`.
Local PE product version: `1.7.3.0`. This is not an upstream release.

## Recorded builds

| AppleMusic.exe | Store package | Architecture |
| --- | --- | --- |
| 1.6.4.90 | 1.1540.23042.0 | x64 |
| 1.7.0.155 | 1.1792.39682.0 | x64 |

Apple's binaries/metadata are inputs for local inspection and are not included
in this repository or its distributable installer.

## Read-only player ABI

The installed `AMP.Services.winmd` was compared with the metadata from the
1.1540.23042.0 MSIX bundle. The consumed method names, ordering and signatures
are unchanged. New trailing methods change two interface IIDs:

| Interface | 1.6.4.90 | 1.7.0.155 |
| --- | --- | --- |
| IPlayer | 958DA6F6-2E93-5BA7-B604-28DD57FEB8E0 | F807B158-B2E7-5CF7-A512-8B0BE767DF1B |
| INowPlayingItem | E376ACB7-0569-56C5-8443-A288ACD362EA | 2DC5F65C-28F6-58AA-87CF-E4A7DF9B9A4B |

`IPlayerStatics` and `IPlayerState` retain their previous IIDs and consumed
slots. The adapter queries the known current IID, then the legacy IID; an
unknown IID still fails instead of calling a guessed vtable. Existing property
notification subscriptions remain unchanged.

## CoreMedia filter implementation

| Build | PE timestamp | SizeOfImage | Simple Apply RVA | Tree Apply RVA |
| --- | --- | --- | --- | --- |
| 1.6.4.90 | 69B9C1B1 | 01498000 | 006CA8E0 | 006E2F10 |
| 1.7.0.155 | 6AC9C301 | 016B7000 | 007DC3A0 | 007F4A60 |

The corresponding method-table references were checked in both images. The
consumed simple/tree/context field accesses are unchanged. Normalized
disassembly of the 1,072-byte Simple Apply region (277 instructions) and
2,117-byte Tree Apply function (570 instructions) differs only at three
diagnostic line-number immediates in each region, after normalizing relative
call/branch/RIP targets. Both 32-byte prologues are byte-identical.

Runtime admission requires the exact timestamp, image size, and both recorded
prologues. There is no scan-and-patch fallback for unknown future images.
The existing constructor callback/priority and object-layout checks remain.

## Agent local-file seek classification

| Build | Agent timestamp / image size | First return RVA | Second return RVA | Third return RVAs |
| --- | --- | --- | --- | --- |
| 1.6.4.90 | 69C4EC34 / 01262000 | 0040B9FA | 0082F6BC | 00B1E8AD, 00B1E972 |
| 1.7.0.155 | 6AD9AC61 / 01296000 | 00435FE6 | 008669AB | 00B5BCA1, 00B5BD64 |

The call chain was followed from both outer seek branches, through the decoder
and `audc` helper, to the imported `AudioConverterReset` thunk. Each return
address directly follows an E8 CALL. Classification now checks the Agent image
identity as well as all three frames. These are observations, not code patches.

## Runtime fixes

- A replacement graph bound within a current row-selection window can clear a
  stale legacy pause only when the canonical player observer is unavailable.
  A known canonical pause, a rebind without selection, and expired selection
  windows do not clear pause. Converter lifetime/disposal rules are unchanged.
- The worker retires a generation when either the endpoint or the producer
  gate is faulted. A queue overflow reports `ERROR_BUFFER_OVERFLOW` instead of
  leaving an unusable generation holding the exclusive endpoint/native latch.
  A later enable can create a fresh generation.
- Capturing/Probing now emits periodic telemetry, including pause intent and
  player-observer availability, rather than logging only Active playback.
- AME enables `keepRunningOnPause` for its sink. A short user pause retains
  the physical client/render worker, sends only silent packets and does not
  call the media source. Resume switches back to the same source at a packet
  boundary without another physical Stop/Start. Native graph teardown still
  closes the client normally; no converter is retained across disposal. Pause
  latency is bounded by the device packets already submitted (normally tens
  of milliseconds). Standalone sinks retain the traditional pause by default.

## Validation

```powershell
ctest --test-dir mod/build -C Release --output-on-failure
mod/build/Release/am_exclusive_compatibility_tests.exe <old-CoreMedia.dll> <new-CoreMedia.dll>
mod/build/Release/AMExclusive-Setup.exe --extract-test
```

Release x64 compilation, the three CTest entries, both actual CoreMedia
fixtures, and verification of the installer's 11 embedded resources pass.
The runtime target covers five control/producer scenarios without opening a
device. Compatibility tests also check the generated installed-metadata IIDs.

These automated checks do not establish audible correctness or cover system
sleep, every Apple Music control, every sample rate, or every DAC. Live startup
and playback results must be recorded separately; future Store versions require
another metadata/binary review.

Initial live check on Windows 11 25H2, Apple Music 1.7.0.155 and a MOONDROP MM3A:
the installed compatibility modules load, the new quality profile is admitted, canonical
Playing/Paused snapshots reach the runtime, and repeated same-item pause/resume
returns to 16-bit/44.1-kHz exclusive output. The observed telemetry has zero
drops, underruns and sample-mapping failures. This is log evidence; it does not
by itself certify the audible output or untested controls/formats.

The first listening test exposed a persistent short-pause/resume stutter even
with those clean counters. A standalone silence source reproduced it without
Apple Music: at 44.1 kHz/16-bit and a 20-ms period, initial output measured
44,203 frames/s; two Stop/Start resumes measured 35,437 and 35,547 frames/s.
The device clock advanced at the same reduced cadence. Keeping the physical
stream running through the pause restored both resumed intervals to roughly
44,000 frames/s in the same probe. This is a targeted device workaround, not a
claim that every driver exhibits the fault.

Final hardware checks with 1.7.2 on that endpoint:

| Check | Result |
| --- | --- |
| 44.1-kHz/16-bit cadence, repeated pause, two resumes | PASS; resumed 43,924 / 43,932 frames/s |
| 96-kHz/16-bit cadence, repeated pause, two resumes | PASS; resumed 96,252 / 96,162 frames/s |
| Source frames consumed during each one-second keep-alive pause | 0 in all four intervals |
| Physical client starts across each baseline and two resumes | 1 |
| 44.1-kHz drain/wait/terminal-packet matrix | 18/18 PASS |
| 96-kHz drain/wait/terminal-packet matrix | 18/18 PASS |

The full 54-case probe rejected all 18 192-kHz cases at format admission on
this device; they are not counted as passes or claimed as verified. The same
36 supported cases pass when selected with the optional sample-rate argument.
After installation, the user confirmed that both 1-2-second pauses and pauses
longer than 8 seconds resume without the persistent stutter in Apple Music
1.7.0.155. Sleep/wake and other devices remain outside this listening check.

## 1.7.3: display/Modern Standby wake delivery gap

After the 1.7.2 listening check, continuous playback during display-off exposed
another persistent stutter after display-on. Two Kernel-Power resume events
correlated with one source-wait episode in each affected live
generation, followed by about 80% nominal submission cadence. Seeking reopened
the generation and restored normal cadence. The old source-wait branch still
called Stop/Start even though the user-pause branch kept the client running.

AME now also opts into `keepRunningOnStarvation`. After a physical stream has
started, a temporary source gap submits explicitly counted silence on real
device events, leaving the source/queue/staging intact. Data arrival resumes
media at a packet boundary without another client Start. Producer hints never
grant GetBuffer; resume-blocked control sections continue silence without
consuming source data. Before the first Start, the original wait/prime/start
ordering and sealed-empty behavior remain unchanged. EOS is still certified
by the device clock. The default standalone configuration remains opt-in.

An A/B silence probe uses one binary with `--legacy-wait` selecting the old
wait behavior. At 44.1 kHz the old behavior reproduced 35,417 frames/s after
one source gap, with two physical Starts. The fixed path measured 44,004
frames/s with one Start; the 96-kHz case measured 95,531 frames/s. Waiting
and resume-blocked intervals consumed zero source frames in the measured
cases. Both fixed cases, the 36 supported drain cases, three CTest entries
and installer resource verification pass. This simulated input gap is not a
substitute for a new live display-off/on listening check.

1.7.3 was installed and its payload hashes verified against the Release build.
The user subsequently completed the live display-off/on listening check and
confirmed normal playback after display-on without replaying or seeking.
This validates the reported scenario on the local Windows 11/MM3A setup;
full suspend/hibernate and other devices have not been established by this
check. The previously tested 1.7.2 installer and its pre-update installed
payload were retained for rollback.

## Reference hashes (SHA-256)

| File | 1.6.4.90 | 1.7.0.155 |
| --- | --- | --- |
| CoreMedia.dll | 505ad3a5a804c1a00a2f58a8237292f000efacb0ee57850e5b6b3716adf155ce | 40736bc27ae049408e3a54ec40d7279df002e69282dd729c09c725be34e0ed4a |
| AMP.Services.winmd | 3933de962400ca1e67b4ffaba2151d93017ad8ed98f259b365ae5231eec60efd | 0c5310e631cf9fca8e07dd21aefb14697baf9fbe800eb4f6a7bdbc10a4f37846 |

# 01.13 workbench — not a completed 60 FPS release

The user subsequently requested testing on the physical Vita. The tested 01.13
menu-correction package has now replaced the existing 01.12 app files over FTP,
with backups and read-back verification. Saves, firmware and plugins remain
unchanged. Hardware execution is pending: remote-launch plugins are disabled
in the boot-stable configuration. This is not a completed or published release.

## Hardware evidence retrieved without requesting another run

The existing Classic V1 `performance.csv` now contains 95 rows from **01.12**.
SHA-256: `44f583878a715fc6fa7bcee38ea0b0a506958a521d181e0c68a3d9d909de6cc5`.

- First 540 guest frames: 23.837 seconds, 22.65 presentations/second,
  **39.55 ms core work/frame**, 0.164 ms rendering/presentation, maximum core
  interval 113.646 ms, and 374 producer-observed empty audio queues.
- Entire trace: 127.833 seconds, 5,752 guest frames, 1,559 observed audio
  underruns and 740 dropped audio frames. These are not counts of missing
  hardware audio samples. The intervening workload is not identified, so it
  must not be described as a controlled idle or Labyrinth benchmark.
- First autosave snapshot took only 4.786 ms, but its worker took 4,739.174 ms.
  The corresponding reporting window took 4,928.685 ms despite only 619.369 ms
  of core execution and 12.980 ms of rendering. The prior main-thread
  `fprintf`/`fflush` to the same volume could still block behind the worker's
  storage operations. This is a concrete competing-I/O path, not proof that
  every unexplained millisecond was caused by the log.

## Changes under test

- Guest CPU execution now runs on a persistent worker, separate from UI event
  polling and presentation. There is only one in-flight guest frame. A
  semaphore publishes its keyboard snapshot, and an atomic completion flag
  publishes its LCD/audio output and timing. SDL texture and audio operations
  remain on the UI thread; no renderer is accessed from the worker.
  Input hold counters advance at guest dispatch, not on UI presentations;
  short taps arriving during a slow guest frame survive until the next sample.
  Autosave capture waits for an idle guest boundary without blocking the UI;
  explicit save/suspend and shutdown wait for completion before touching the
  emulator. The guest thread is joined before model teardown.
  Presentation and guest scheduling have independent deadlines. This removes
  synchronous CPU execution from presentation, but does **not** accelerate
  the interpreter, generate intermediate animation images, or cure audio
  starvation when the guest cannot run in real time.
- Live 4 KiB RAM/ROM mappings bypass repeated address decoding. Partial pages,
  mirror/page boundaries and peripheral accesses retain the slow router.
  Mappings alias backing memory, so self-modifying code remains visible.
  They add 32 KiB of mapping pointers on the Vita. Rebuild mappings after any
  change to backing allocations or access permissions.
- Classic executes a timer-bounded batch within the interpreter rather than
  calling its full entry point for every instruction. I/O, interrupt deferral,
  halt, speaker position and timer/completion ordering remain unchanged.
- Packed flag updates and 32-bit arithmetic avoid unnecessary 64-bit work.
  The guest clock, instruction budget and timer rates were not reduced.
- Unsigned byte multiplication now reads the low byte of its **word**
  destination, including the upper register bank. This follows
  [MAME's H8 instruction definition](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.lst).
  It is not yet an established fix for Labyrinth's misplaced gun.
- CRC-32 uses a thread-safe constant 64-byte nibble table; byte-for-byte
  compatibility is checked against the prior bitwise calculation. Save capture
  no longer zeroes the entire RAM buffer immediately before overwriting it.
- Performance rows accumulate in a bounded 256 KiB memory buffer. Immutable
  copies are written by the save worker; explicit save/suspend/exit flushes
  occur outside normal ongoing playback. There is no periodic log file write
  or flush on the normal game-loop path. Logs on storage can lag the live
  session until autosave or explicit save/exit; always check their version.
- Logs also record maximum presentation-call spacing and intervals exceeding
  125% of the target period. This measures application submission timing, not
  physical panel scanout, and does not imply motion interpolation.

## Motion interpolation and menu-artifact investigation

The workbench now generates spatially warped intermediate LCD images at the
480×300 display resolution. Estimation runs on the guest worker; SDL texture
upload remains on the UI thread. A bounded 16-pair history uses 200 ms of LCD
lookahead to cover uneven source animation intervals. This adds visible input
response latency; keyboard sampling and audio are not deliberately delayed.
Scene cuts and long idle gaps discard the history. Exact endpoints retain the
original pixels and palette; the app layout and controls are unchanged.

The reported artifacts occurred in **Windows Vita3K**. A deterministic 1,600
guest-frame Classic V1 navigation replay also contains overlapping icon redraws
before interpolation, with intact settled menus. An experimental complete-VRAM
transfer latch did not eliminate those overlaps and was removed; no LCD/core
behavior change is being claimed as their fix.

Interpolation added another problem: one dominant motion vector was applied to
independently moving regions. In recorded source frames 960→970, the carousel
moves −19 pixels, the title −15, and the clock stays still. Horizontal estimation
now separates bands at uniform background rows and validates their motion
individually. Stable/blinking status rows remain at native positions. Repeated
glyph strokes that also match the verified translation are no longer incorrectly
pinned as stationary tiles. Full-resolution refinement no longer skips alternate
pixels, which could mistake a 19-pixel displacement for 18 pixels.

New regression tests cover independent scrolling bands, unaligned/blinking HUD
glyphs, odd-pixel motion, exact endpoints, scaled subpixel motion, cuts, history
bounds, and SDL texture readback. Local/non-rigid interpolation remains an
approximation, not a guarantee of artifact-free output in every application.

`CYBIKO_REPLAY_VIDEO=/path/frames.gray` records native 160×100 gray8 guest frames.
`cybiko-motion-replay input.gray output.gray metrics.csv 3` exercises the actual
presenter at 480×300. The revised navigation replay generates 195 non-endpoint frames
from 78 source changes. These are **host replay** results, not Vita measurements.
Firmware-derived raw recordings stay local and are not bundled.

## Automated validation

With Python available, the current core-plus-motion suite has 15 groups; the frontend-enabled suite passes 16/16.
The frontend-enabled AddressSanitizer/UndefinedBehaviorSanitizer build also
passes 16/16 (the local SDL2 compatibility runtime requires the Homebrew SDL3
library directory in `LD_LIBRARY_PATH` for its intercepted dynamic load).
600-frame real-firmware scheduler comparisons pass for all three models, and
the saved Classic V1 full-charge/navigation regression passes.

Tests cover every mapped page and boundary, all 256 byte values under all 256
initial CCR values, 24,576 long-arithmetic cases, every byte-multiply register
pair, CRC equivalence, and immutable asynchronous save/log snapshots for all
three profiles. A gated worker test deliberately stalls guest execution while
the UI renders and receives a complete Right-key tap, then verifies all eight
Classic hold frames, snapshot capture at an idle boundary, worker recreation,
and shutdown with a frame in flight. It is a concurrency/ownership regression,
not a physical-Vita frame-rate measurement. The full-charge Classic V1
navigation fixture and real-firmware
scheduler comparisons remain required after core changes.

`cybiko-replay` adds deterministic RTC progression, optional scripted keyboard
matrices, per-frame CPU timings and CPU/RAM/LCD/audio fingerprints. Proprietary
firmware and saved sessions are supplied locally, never included in the repo.

Example (use absolute local paths for executables/fixtures):

```sh
CYBIKO_SMOKE_RAM=/path/to/ram.raw \
CYBIKO_SMOKE_CLOCK=/path/to/clock.raw \
python3 tools/compare_replays.py \
  --baseline /path/to/baseline/cybiko-replay \
  --candidate /path/to/candidate/cybiko-replay \
  --output /path/to/new-results-directory --runs 5 -- \
  --classic-v1 /path/to/cyrom112.bin - /path/to/save.flash 600
```

`CYBIKO_REPLAY_KEYS` optionally names a text file of zero-based
`frame,column,hex_mask` records in increasing frame order. Masks stay held
until changed. The standard smoke Enter-pulse option still works without a
keyboard script. CSV fingerprints exclude timing; they are regression
fingerprints, not cryptographic proofs of complete machine equivalence.

In five alternating 600-frame runs per build/profile before the separate
unsigned-multiply correction, all fingerprints matched 01.12. Host median
core times were:

| Profile | 01.12 | Optimization candidate | Reduction |
| --- | ---: | ---: | ---: |
| Classic V1 | 1,272.653 ms | 1,202.035 ms | 5.55% |
| Classic V2 | 639.974 ms | 592.040 ms | 7.49% |
| Xtreme | 4,251.282 ms | 4,059.564 ms | 4.51% |

Runs were noisy (including overlapping individual results). These modest host
numbers do **not** establish a useful physical-Vita speedup or meet the goal.

After the separate unsigned-multiply correction, the V1 comparison intentionally
diverges from 01.12's RAM fingerprint at frame 198. Its 600 LCD/audio frame
fingerprints remain identical. The old erroneous arithmetic is not the golden
reference for this correction; its register-pair tests and the corrected
single-step/batched state comparison are the relevant checks.

## Windows Vita3K observations

Both 01.12 and an intermediate 01.13 ARM build booted the supplied Classic V1
session to the desktop. Direct window captures showed the version and desktop.
An [intermediate 01.13 window capture](vita3k-0113-workbench.png) records that
limited result, not smoothness or final-build performance.
The capture helper now refuses unrelated foreground desktop captures; its
`-BackgroundCapture` mode captures only the selected emulator window.
Synthetic background keyboard messages did not establish navigation, so that
attempt is not counted as a successful controller test.

The 60 FPS Vita3K overlay did not mean smooth guest animation. Both captured
startup logs exceeded the frame budget and contained underruns. Other host
work and JIT startup confounded a fair timing comparison; neither run establishes
physical-Vita performance. The intermediate candidate predates the memory-only
diagnostic log fix and final instruction-fetch path.

The Windows app/data directories were restored byte-for-byte to their pre-test
backups after closing Vita3K. Trial app/data and logs were retained separately.

A subsequent 60-second run of the motion-enabled ARM build booted Classic V1
to the [desktop](vita3k-0113-motion-workbench.png). Explicit `--boot-model
classic-v1 --run-seconds 60` arguments selected the profile and exited normally;
`tools/run_vita3k_validation.ps1` wraps this without synthetic background keys.
The closed trace records 3,480 presentations, 3,375 guest frames, 609 intermediate
submissions, 182 producer-observed audio underruns, and 23 late presentations
over 59.022 logged seconds. Startup still misses the budget. Intermediate
submission counts do not prove every image is unique or physical panel scanout.
The final screenshot proves boot/rendering only, not artifact-free navigation.
Original Windows app/data/config were restored byte-for-byte afterward; trial
data was retained separately. No physical-Vita files were changed.

WSL reads of an open Windows log returned stale contents during this session;
reading/copying through Windows showed the current version. Validate version
and collect a closed/new copy before interpreting these logs.

## Release gates still open

1. Native Vita startup and game execution must fit the frame/audio budget;
   the latest available hardware trace clearly fails this gate.
2. Decoupled presentation and genuine motion interpolation are implemented and
   tested, but require physical-target timing and broader visual validation.
   Source gaps longer than the lookahead can still produce held frames, and
   interpolation cannot repair incorrect or partially drawn guest source images.
3. Controlled game/animation and audio runs, input/persistence checks, and
   Labyrinth rendering validation must pass before a completion claim.
4. The inherited one-instruction-per-clock model is not full H8 bus/instruction
   timing. [MAME's H8 access timing](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.cpp)
   accounts for additional memory accesses/internal work and itself documents
   external wait-state limitations. Reducing the instruction budget arbitrarily
   is not an acceptable optimization or a substitute for a timing model.

Do not publish this workbench as “fixed,” “fully optimized,” or “60 FPS smooth.”

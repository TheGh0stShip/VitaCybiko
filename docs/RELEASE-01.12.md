# VitaCybiko 01.12 — interrupt timing, full battery, audio and autosave

This preview keeps the existing device appearance, input mappings and disabled
rear touch. It does not claim 60 FPS interpolation or complete hardware accuracy.

## Changes

- **CPU interrupt timing:** CCR-control instructions now defer IRQ acceptance
  through the following instruction. CyOS enables interrupts immediately before
  loading a task's stack pointer; the previous interpreter could interrupt
  between those instructions. A regression test checks that the IRQ frame lands
  on the new stack, not the old one. Timing cross-check:
  [MAME H8 instruction definitions](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.lst)
  and its `prefetch_done_noirq` path.
- **Classic battery:** ADC samples 768/610 now reach charge-complete rather than
  the permanently flashing 25%/C state. Both current and older saved Classic V1
  host sessions show 100% and respond to Right, moving You & Me to E-Mail. The
  full-charge navigation failure from the earlier experiment no longer occurs
  with corrected interrupt timing. Physical-Vita verification remains pending.
- **Timer hot paths:** replace the repeated 256-step timer deadline scan with a
  direct calculation. Timer8/16 synchronization skips deadline calculation and
  division when not even one prescaler tick can occur. No guest instruction
  budget, clock rate or animation-speed multiplier was changed.
- **Audio:** prebuffer actual sound at boot/resume; stop inserting silence ahead
  of fresh sound at every low-water event; preserve scheduled sound on queue
  overflow. Limit extra catch-up work when enough audio is queued. Sustained
  slow emulation can still underrun; counters expose this explicitly.
- **Autosave:** capture immutable flash/RAM/RTC copies, then checksum and write
  them on one background worker. The worker never reads the running emulator
  or mutable model paths. Explicit save/suspend/exit joins any outstanding write
  before saving the latest state. Existing formats and validation are retained.
  Multi-file checkpoints are still not power-loss-transactional.
- **Arithmetic:** correct signed multiplication destination-byte selection,
  signed division register selection/flags and the host division-overflow edge.
  These are not yet a verified fix for Labyrinth's gun placement.
- **Diagnostics/version:** 01.12 on launch and LiveArea; performance logs include
  audio/LCD callback costs, audio queue starvation/drop counts, and main-thread
  snapshot versus background save times, for up to 600 rows.

## Validation

- Core suite 13/13; SDL frontend suite 14/14, including async snapshots for all
  three models, independent lifetimes/paths, checksums, and worker failure.
- Timer deadline comparison covers 65,536 counter/comparator combinations.
- Optional real-firmware scheduler comparisons cover CPU state, RAM, timers,
  ADC, LCD and speaker, against single-cycle scheduling.
- Saved Classic V1 full-charge navigation is now an opt-in automated regression
  (`CYBIKO_BATTERY_DESKTOP_FIXTURE`, using local firmware only).
- Vita build succeeds. These host results do not establish Vita frame rate.

The preceding device log measured startup core work at 46.3 ms per guest frame
on average, with a 235.6 ms maximum. Rendering averaged 0.16 ms. It also showed
an approximately 4.7-second excess stall near the first autosave deadline.
The new build separates save capture/write time to check the improvement on
hardware. Details and original measurements: [investigation](INVESTIGATION-01.12.md).

## Still open

Physical startup/game performance and sound need a run of this version.
Lost in Labyrinth Basic Edition's gun placement remains unverified. Motion
interpolation is not implemented. No firmware, games or personal saves are
included in this package.

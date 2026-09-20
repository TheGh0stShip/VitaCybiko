# VitaCybiko 01.11 — battery sampling and Classic frame scheduling

**Subsequent hardware feedback:** this release still displays a flashing 25%/C
battery and has choppy audio/animation. The battery-level interpretation below
was incomplete; see [the follow-up investigation](INVESTIGATION-01.12.md).
It should not be described as resolving those issues.

This candidate addresses the battery faults and the presentation stalls reported
on physical Vita with 01.10. Physical-Vita smoothness is not yet verified.

- Restore ADC completion interrupt 28. Previously CyOS never entered its sample
  handler, even though the converter claimed completion. Single conversions and
  channel scans now take their modeled clock duration; cancellation and SLEEP
  wake-up deadlines are handled alongside DMA/serial events.
- Correct ADC word reads to match the left-aligned byte registers. Classic now
  uses channel samples 768/584: the charging differential is positive and
  `2 * ch2 - ch1 - 341` gives a bounded healthy level of 59. The old 768/256
  pair gave -597. Arbitrarily maximizing the samples is not a valid fix.
- Batch Classic peripheral clocks between observable events. I/O accesses
  synchronize pending clocks before reading or changing registers. CPU
  instruction budgets, timer rates, and input mapping are unchanged.
- Permit catch-up only when measured CPU/render cost leaves room in the frame
  budget. Slow frames are presented without forcing two more guest frames first.
- Cache LCD color conversion and skip identical texture uploads, preserving
  the existing colors and device layout.
- Include 01.11 in package metadata, launch UI, and LiveArea artwork.

## Validation and limitations

The release host suite passed 13/13; the SDL frontend suite passed 14/14.
Tests cover ADC interrupt enable, conversion/scan timing, cancellation, sample
filtering, word/byte consistency, LCD colors and duplicate uploads, frame budgets,
input and persistence.

An additional comparison ran 600 real-firmware frames per model against the
cycle-by-cycle reference, checking CPU state, RAM, interrupts, timers, LCD and
speaker transitions every frame. All three model profiles passed. Classic V1/V2
used locally copied Vita saves; the Xtreme comparison used its supplied firmware.
Test clocks were held deterministic. No proprietary fixtures are published.

In one host comparison Classic V1 used 1.626 s versus 1.845 s CPU time, and V2
used 0.698 s versus 0.748 s. These are host measurements, not Vita FPS guarantees.
Xtreme retains the original execution path because batching regressed its workload.

A saved-state Classic V1 input run moved from You & Me to E-Mail. A separate
60-second accelerated-RTC run stayed on the desktop without a low-battery dialog.
These were headless host runs with inspected LCD captures, not physical-Vita or
Vita3K captures. Physical input feel, audio and transition smoothness still need
confirmation with the installed build.

## Physical-Vita timing report

Each model boot writes `ux0:data/VitaCybiko/<model>/performance.csv`, with up to
120 rows, one per 60 presentations. The next boot of that model replaces it.
Rows contain version/model, presentations, guest frames, LCD uploads, elapsed
time, total core time, total render time, maximum core work per presentation,
and the guest PC. All times are milliseconds. `1000 * presents / elapsed_ms`
gives presented FPS; core totals include audio generation and LCD conversion.
The log is intended to distinguish interpreter load, rendering cost, and low
guest LCD update rates without changing the screen layout.

## Reproducing the optional firmware comparison

Build the host tests, then run `test_scheduler` with these environment variables:

- `CYBIKO_SCHEDULER_MODEL`: 0 = Xtreme, 1 = Classic V1, 2 = Classic V2.
- `CYBIKO_SCHEDULER_BOOT`: model-matching boot ROM.
- `CYBIKO_SCHEDULER_FLASH`: parallel flash, for Xtreme and V2.
- `CYBIKO_SCHEDULER_DATAFLASH`: serial flash, for Classic.
- `CYBIKO_SCHEDULER_RAM`: optional raw RAM without the Vita checkpoint header.

The smoke executable also accepts optional `CYBIKO_SMOKE_RAM` and
`CYBIKO_SMOKE_CLOCK` raw checkpoints and `CYBIKO_SMOKE_ENTER_FRAME` for one
12-frame Enter press. It reports CPU time and changed LCD frames. LCD activity
alone remains insufficient to prove a completed boot.

ADC references: [MAME H8S2245 memory map and interrupt wiring](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8s2245.cpp),
[MAME ADC timing and completion](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8_adc.cpp),
and [upstream Classic battery calculation analysis](https://github.com/daberkow/cybiko-java-emulator/blob/main/emulator/src/main/java/com/github/daberkow/AddressBus.java).

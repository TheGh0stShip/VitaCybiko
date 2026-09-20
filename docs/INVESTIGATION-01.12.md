# Post-01.11 investigation

**Follow-up:** [01.12](RELEASE-01.12.md) corrects CCR interrupt deferral. The
previous full-charge navigation stall no longer reproduces in the current or
older V1 desktop snapshots; 768/610 samples are now enabled. It also moves
periodic saves to an immutable-snapshot worker and removes timer deadline scan
overhead. The initial findings and rejected experiments below are historical.

User hardware feedback supersedes the earlier smoke-test conclusions: the
battery flashes 25%/C, startup and game audio stutter, animation remains slow,
and Labyrinth may draw its gun incorrectly. This is **not a finished fix or a
60 FPS release**. Device appearance and input mappings are unchanged.

## Implemented locally

- Removed silence insertion on every low audio queue. Boot/resume now collects
  two frames of actual sound before starting playback. Subsequent underruns
  queue fresh sound immediately without another artificial silence prefix.
- Queue overflow preserves scheduled sound instead of clearing the entire queue.
  Emergency overflow still drops the incoming frame and is counted. Extra
  emulation catch-up frames are suppressed when sufficient audio is queued.
  This cannot make sustained below-realtime emulation sound continuous.
- Replaced signed-left-shift PCM conversion with defined multiplication.
- Corrected MULXS.B destination addressing (low byte of the selected word,
  including E0–E7), DIVXS.B upper-word destinations, and DIVXS Z/N flags.
  Guarded signed word division against host INT32_MIN/-1 undefined behavior
  without adding a 64-bit software divide to ordinary Vita execution.
  These are instruction-level fixes, **not a verified Labyrinth fix**.
- Added audio/LCD callback time, queue starvation/drop counters, and queue bytes
  to performance.csv. These callback times are included in core_ms; subtract
  them before estimating interpreter/peripheral time. Starvation is observed
  when the producer finds an empty queue, not an exact count of missing samples.
  The log now retains up to 600 rows (about ten minutes at 60 presentations/s),
  covering navigation into games as well as startup.

Audio semantics: [SDL_QueueAudio documentation](https://wiki.libsdl.org/SDL2/SDL_QueueAudio).
Arithmetic cross-check: [MAME H8 instruction definitions](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.lst)
(`01c05000`, `01d05100`, `01d05300`). No MAME implementation was copied.

## Battery: identified state machine, rejected regression

Classic V1 RAM code at 0x21DD5A implements the charging state machine. With
`ch1 - ch2 > 15`, it computes `raw = 2*avg2 - avg1`, subtracts **349**, clamps
the display level to **31..78**, and disables charging only for **raw > 450**.
Other differential branches subtract 341. The earlier blanket offset of 341
was incorrect. The averaging routine at 0x21DED6 seeds from the first valid
sample and subsequently uses `(7*old + sample + 3) >> 3`.

The battery object is referenced at 0x227FD0 (not the ADC driver at 0x227FCC):

| Offset | Observed meaning |
| --- | --- |
| +0x14 | Display level |
| +0x16 | Charging flag |
| +0x18/+0x1A | Previous channel samples |
| +0x1C/+0x1E | Filtered channel samples |

The 01.11 768/584 ADC values yield raw=400, display=51 and charging=1. This
explains the user's report; the old unit test name claiming a charged battery
was misleading and has been corrected.

An experimental 768/610 pair produced raw=452, display=78, charging=0 and a
100% icon without C. **It was reverted:** the saved V1 session then stopped
responding to navigation. The stall also reproduced with the original CPU
arithmetic implementation, so the new signed-arithmetic changes are not its
cause. Advancing the RTC by an hour did not resolve it. No guest save was edited.

In the failing run, the CPU repeatedly passed through the scheduler/time helper
at 0x20461C/0x207452/0x21EE2E; IER stayed zero. The timer-dispatch re-entry flag
at 0x21F06E remained set, unlike the responding baseline. Timer ticks continued.
This is the next local investigation target; forcing the icon or enabling IRQs
against the firmware's mask would hide the underlying problem.

## Acceptance gates still open

1. Split the measured core cost into interpreter/peripheral, LCD upload and audio
   costs, and capture a known gameplay interval. The 01.11 device log has now
   been retrieved (see below); it does not contain the new callback counters.
2. Verify the corrected full-charge state and navigation on physical Vita;
   the saved-session host regression now passes with CCR interrupt deferral.
3. Verify the audio changes on hardware, including several minutes of gameplay.
4. Identify the exact Labyrinth package/profile and reproduce the gun placement
   against reference behavior. Arithmetic tests alone cannot establish this.
5. Implement and evaluate motion interpolation separately from emulation speed.
   It is still absent; neither 60 presentations/s nor blending alone proves
   smooth, correctly interpolated motion.

## Local validation

- Core host suite: 13/13 passed.
- SDL dummy-device frontend suite: 14/14 passed, including real-sample startup,
  resume, starvation and overflow queue tests.
- Signed byte multiplication checks all 256 source/destination register pairs;
  division tests cover upper-word destinations, zero quotient/divisor, and the
  signed host-overflow edge case.
- Vita executable cross-compilation/link succeeded. No new VPK release was made.
- With the rejected ADC experiment removed, a saved Classic V1 host session
  moved from You & Me to E-Mail after a Right press. This is a local input
  regression check, not physical-Vita gameplay verification.
- The full-charge V1 experiment also matched the original per-instruction
  scheduler over 600 frames, including CPU/peripheral state; its stall is not
  explained by the 01.11 batching optimization alone.

Deployment status is recorded separately from these host results.

## Physical Vita log retrieved after FTP was restored

Read-only retrieval confirmed a 01.11 Classic V1 run. Classic V2 and Xtreme had
no performance.csv. Current V1 flash/RAM/RTC were copied locally for reproduction;
no files on the device were changed. The flash contains `labyrinth.app` and
the title **Lost in Labyrinth Basic Edition**, including game save records.
This identifies the installed edition, not a proven cause of its gun placement.

| Interval | Measured result |
| --- | --- |
| First 9 rows (startup) | 549 guest frames / 540 presentations in 27.972 s |
| Startup average core cost | 46.265 ms per guest frame |
| Startup average rendering cost | 0.159 ms per presentation |
| Slowest 60-presentation startup row | 5.625 s, about 10.67 presentations/s |
| Largest recorded core frame | 235.596 ms |
| Idle rows 10–40 | 60.001 presentations/s; 2.447 ms core per guest frame |
| Idle LCD changes in those rows | 61 changes across 31 seconds |

The startup bottleneck is inside `advance_emulated_frame`, not `render_frame`.
In 01.11 the core measurement includes emulation, LCD conversion/upload and
the audio callback, so it does not yet isolate interpreter time alone. A
60 Hz idle presentation rate does not establish 60 Hz guest animation.

There is also a distinct stall around the first 60-second autosave deadline:
row 42 spans 5.734 s but contains only 146.877 ms of core work and 11.388 ms
of rendering. About 4.73 s is excess over the surrounding one-second rows.
The synchronous autosave is a strong suspect, **not proven by this log**, because
save time is outside both measured sections. This needs separate timing before
changing persistence behavior; the user's saves must remain intact.

The log ends after 120 rows / 144.450 wall-clock seconds. Most post-startup rows
show the idle guest PC. Do not treat this capture as a timed Labyrinth benchmark.

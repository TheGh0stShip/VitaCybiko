# Live Windows Vita3K menu-artifact observations

Observed 2026-09-19 during the user's navigation, Classic V1, VitaCybiko 01.13.
The installed executable SHA-256 was
`4c2d06c21b55d1a39a5c73f814d7b391d24ea3d872159f801ccb46c269ad1b94`.

## Capture method and limits

100 passive `PrintWindow` captures of the VitaCybiko client window span 14.43
seconds. Capture-start intervals are approximately 140–150 ms: this is a sampled
sequence, **not** a 60 FPS recording. No input was injected during this sequence,
and the application was neither restarted nor changed during navigation.

These images contain Vita3K's final displayed output, including interpolation.
They are **not** live guest VRAM dumps. The capture cannot establish which source
frame, interpolation phase, or guest LCD transfer produced an individual tear.
The 60/61 FPS overlay is not evidence of correct icon rendering.

## Confirmed visible defects

- [Capture 34](vita3k-menu-tearing-0034.png): the E-Mail tile has a detached
  horizontal patch at its left edge; its top border and adjacent tile borders
  are broken or offset. The caption area is blank during this sampled transition.
- [Capture 35](vita3k-menu-tearing-0035.png), approximately 5.12 seconds into the
  capture: the Chat tile contains discontinuous phone graphics, while displaced
  horizontal strips run across the tops of the E-Mail and People tiles. The
  caption reads Chat while neighboring tiles remain visibly fragmented.
- [Capture 36](vita3k-menu-tearing-0036.png): a wide vertical background-colored
  strip erases part of a phone graphic, and adjacent icon boundaries have
  displaced/duplicated vertical edges. The caption is absent in this sample.

The outer application keyboard and shell remain intact in these examples;
the visible corruption is concentrated inside the emulated LCD image. It is
not merely a low animation cadence. Mixed old/new icon fragments, holes and
broken borders remain in the current motion-enabled build despite earlier
title/clock and odd-pixel matching corrections.

## Interpretation

Earlier deterministic raw guest-frame replays also contained intermediate
overlapping icon redraws. That establishes that some overlaps can predate
interpolation, but does not attribute these particular live captures to the same
cause. Local block warping, occlusion handling, and presentation of partially
redrawn source images remain candidates—not proven diagnoses.

To isolate the cause, a subsequent instrumented run should retain paired raw
guest LCD images, the chosen motion vectors/phase, and the synthesized result
for the same presentation. Do not classify the artifacts as resolved from a
clean settled-menu screenshot or a presentation-rate counter.

Full local evidence: `D:\Vita3k\vitacybiko-menu-observation-0000.png` through
`0099.png`, with capture timing in `vitacybiko-menu-observation.png.csv`.
A frozen Linux copy is in `/tmp/vitacybiko-live-menu-evidence/`.

## Paired source/output investigation and candidate corrections

The subsequent candidate fixes two reproducible interpolation defects:

- Local inverse warping could use coordinates from a different vector than its
  final match, or accept an invalid source block. It now requires convergence
  and a reverse match returning within one source pixel. Pairs with fewer than
  75% round-trip-consistent moving patches use exact native frames, preventing
  an unreliable patchwork from being presented as coherent motion.
- A periodic horizontal row could be identical at both endpoints but belong to
  a scrolling icon. It was incorrectly frozen through the moving image. Rows
  exactly explained by their verified band's translation now move with it.

Both defects have regression coverage at native and 3x output sizes. These
changes retain interpolation for verified scrolling; they do **not** interpolate
every non-rigid transition. The 1,600-frame host navigation recording now
produces 195 non-endpoint images from 78 source changes (previously 281 with
the less conservative matching). This reduction is intentional, not evidence
of faster or smoother guest execution.

An opt-in bounded ARM run (`--boot-model classic-v1 --run-seconds 60
--validate-menu`) records 64 paired source/output images into
`classic-v1/motion-trace-v1.bin`. The diagnostic uses the normal input mailbox
to hold Right at guest frames 950–957 and Left at 1100–1107. Half of the trace
capacity is reserved for each direction. It allocates about 11.3 MB only when
explicitly enabled, and writes the trace after execution stops. Normal launches
neither allocate this trace nor inject navigation.

Decode using `tools/inspect_motion_trace.py TRACE NEW_OUTPUT_DIRECTORY`; verify
ARM/host synthesis using `cybiko-check-motion-trace TRACE`. Invalid or empty
traces fail validation. Record outputs are the CPU images supplied to SDL,
**not GPU readbacks**. Desktop captures remain necessary to assess presentation.

The first isolated Windows run captured 64 records during the rightward
transition: zero ARM/host output or motion-metadata mismatches, and all rejected
flow records retained the exact native image. Inspection of paired records 20
and 40 confirms generated scrolling positions between source endpoints. This
establishes target execution of the corrected interpolation, not elimination
of every artifact or physical-Vita performance. No firmware-derived trace is
distributed in the repository.

The second isolated 60-second run, in
`D:\Vita3k-validation-bidirectional.ejonRb`, saved all 64 records spanning both
scripted directions: 21 outputs differ from both endpoints, zero rejected-flow
output mismatches, zero host/ARM pixel mismatches, and zero motion-metadata
mismatches. Its 340 passive desktop captures cover the actual rendered menu.
[Capture 92](vita3k-menu-corrected-scroll.png) matches generated trace record 33
pixel-for-pixel throughout the 479×299 LCD interior after applying the app's
integer RGB palette. Across the full 480×300 rectangle, 779 pixels differ,
confined to the bottom row and right column where the displayed border overlaps;
they are not interior icon fragments. This sampled correspondence checks the
display path as well as CPU synthesis, but is not a synchronized every-frame
GPU audit or a guarantee that all source animations are smooth.

In total, 107 sampled window interiors match a recorded output exactly; three
are generated rather than native endpoints: captures 75, 92 and 95 correspond
to trace records 5, 33 and 59. Record 59 has positive horizontal displacement,
covering the reverse direction. The many matching settled/native images must
not be counted as additional unique interpolated frames.

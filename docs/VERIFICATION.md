# Verification — 2026-09-19

## v0.1.1-preview follow-up

Vita package metadata is now `01.01`. VPK SHA-256:

```text
a1da5712c3a856e1eb5df9fe4039a616be4401722c47d3fc682e598cb824d236
```

- Classic tap duration: a headless firmware run ignored a three-frame Esc press
  and recognized eight frames. The frontend now guarantees eight emulated
  frames for short Classic presses. In Windows Vita3K, 40 ms taps activate F4,
  open Pinball's quit dialog and confirm a return to Games.
- Text Editor: created `test.txt` with `vita 123`, accepted the guest save prompt,
  exited, saved, closed Vita3K, launched the updated build and reopened it intact.
- Clock: added the previously missing battery-backed Classic SRAM. A host run
  preserving both SRAM and RTC booted with the expected 2:00 AM time. Windows
  Vita3K showed 12:02 before a full process restart and 12:04 afterward, instead
  of resetting its displayed clock. No guest firmware bytes were patched.
- RAM sidecars validate model, size, header and payload CRCs and matching flash.
  Host tests cover atomic replacement, corrupt contents, wrong flash and wrong
  model, preserving original files and live RAM when validation fails.
- All 13 ASan/UBSan suites and seven local Python tests pass after these changes.

![Pinball quit dialog after a short Esc tap](vita3k-pinball-quit.png)

![Text Editor document reopened after restart](vita3k-note-reopened.png)

![Guest clock before restart](vita3k-clock-before.png)

![Guest clock after restart](vita3k-clock-after.png)

These follow-up captures are actual Windows Vita3K client-window captures.
The older baseline below is retained as history; its clock-reset, short-tap and
Text Editor acceptance gaps are superseded by the results above. Alarm modes,
all-app coverage, physical Vita testing and full-speed gameplay remain open.

## v0.1.0-preview baseline artifact

Release preview `v0.1.0-preview`, title ID `VCYB00001` (Vita package metadata 01.00).

VPK SHA-256:

```text
24cd78ad5fb71d43f55588ec64c2fb9d40763c5215e21fc2bdc56b9008ea7c28
```

All nine ZIP entries passed integrity checks. Firmware and apps are not inside
the package; licenses and the literal-device LiveArea artwork are included.

Published at [v0.1.0-preview](https://github.com/TheGh0stShip/VitaCybiko/releases/tag/v0.1.0-preview).
The uploaded VPK was downloaded again, checked against the published SHA-256,
and passed ZIP integrity verification. The installed Windows Vita3K executable
matches the executable inside that package. Public
[release-commit CI](https://github.com/TheGh0stShip/VitaCybiko/actions/runs/35459876479)
passed on a clean Ubuntu runner.

## Real Windows Vita3K testing

Vita3K 0.2.1, build 4095-84184a36, OpenGL, 960×544. Tests were performed by
launching the actual VPK and operating its controls, not by substituting mock
screens. The emulator configuration was left unchanged.

| Test | Observed result |
| --- | --- |
| Three-model selector | Displays V1, V2 and Xtreme; selected model survives process restart |
| Classic V2 first boot | Welcome → date setup → synthetic nickname → Main Desktop |
| Subsequent boot | Returns to desktop without repeating first-run setup |
| Games / Pinball Pro | Game starts and renders gameplay; observed roughly 32–46 FPS in the optimized build |
| Applications / Calculator | Numeric input and directional button selection compute 2 + 3 = 5 |
| Save/model return | Start + Select saves and returns to the model menu |
| RTC storage | Versioned clock file is written; host round-trip checks pass, but guest desktop time discrepancies remain |
| Classic V1 / Xtreme | Missing matching firmware; guest boot not verified |

Desktop and Calculator generally showed about 60 FPS. The overlay is Vita3K's
reported presentation rate, **not** a measured cycle-accuracy or hardware-speed
benchmark. Pinball is not consistently full speed.

![Model selector](vita3k-model-selector.png)

![Classic V2 desktop](vita3k-classic-desktop.png)

![Pinball Pro gameplay](vita3k-pinball.png)

![Calculator showing the result of 2 + 3](vita3k-calculator.png)

Desktop/selector/Pinball captures are from the final packaged build. Calculator
was tested on the preceding build (VPK SHA-256
`225772871e3ade2d4fd00eba50a60ac2dc3045465fb04731954661f05814a2a8`);
the final build also isolates Classic V2's Esc sense line and adds licenses.
Capture helper:
`tools/capture_vita3k.ps1` (explicit process ID and output path required).

## Automated checks

- 13 C test suites pass with AddressSanitizer, leak detection and UBSan.
- Actual SDL frontend tests cover input-source isolation, focus/background
  handling, rendering, model selection, storage failures, save preservation,
  clock corruption handling, preferences and layout switching.
- Core regressions cover the packed CCR/PC interrupt frame, RTE/TRAPA,
  long-displacement stores, instruction-memory mapping including self-modifying
  code, serial TX interrupts, RTC I2C/calendar, Classic profiles and DataFlash.
- A 1,200-frame run of the supplied Classic V2 firmware plus the test save passed
  ASan/UBSan; LCD activity was observed in 1,195 frames. This headless metric
  alone is not treated as boot proof.
- Seven Python tests pass locally. Three CD integration tests require private
  source media and skip in a clean public checkout; the other four use synthetic
  fixtures. Proprietary fixtures are deliberately not distributed.
- Firmware staging identifies all three supplied V2 images, reports missing
  V1/Xtreme images and leaves saves untouched.

## Open acceptance items

Physical Vita/PSTV testing; V1/Xtreme boots; all original launch-bundle apps;
user-created Notes/Organizer data save-and-reopen; reliable guest Esc/exit
behavior across apps; audible fidelity; guest clock correctness; sustained
full-speed games; wireless/accessory interoperability.

The release is a working **Classic V2 preview**, not completion of these items.
See [compatibility](COMPATIBILITY.md) and [release goals](RELEASE-PLAN.md).

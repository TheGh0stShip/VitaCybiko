# Verification — 2026-09-19

## v0.1.3-preview candidate

Vita package metadata is `01.03`. Candidate VPK SHA-256:

```text
794c25200c699fac3fa38a5bbff8325006f6f95cf4b2397ed66979f9481d2819
```

Executable payload SHA-256:

```text
cae04e9dee6e5cded235f371a1d23427b9d26e124905cba94cbf769f9c194459
```

- LiveArea PNGs were converted to Vita-safe indexed PNGs. The Vita build now
  runs `tools/repack_vpk_store.py` after `vita-pack-vpk`, so every VPK member is
  stored instead of deflated. The resulting archive passed `zipfile.testzip()`.
- Startup data-folder creation now uses recursive, stat-first directory
  creation and only prepares `ux0:data/VitaCybiko` before the model selector.
  Model-specific `roms/` and `apps/` folders are created after selection.
- The frontend-enabled host suite passes 13/13, including
  `runtime_dir_creation_on_prepared_storage`, using SDL's dummy video/audio
  drivers. The regular host suite passes 12/12 and Python tests pass 7/7.
- The fixed VPK was uploaded to the physical Vita at
  `ux0:/VPK/VitaCybiko.vpk` and size-verified over FTP. The extracted app
  folder was also updated at `ux0:/app/VCYB00001/` with 9/9 files verified, so
  an existing bubble should launch the fixed executable. Runtime data in
  `ux0:/data/VitaCybiko/` was rechecked with 28/28 files verified.
- Archive.org direct member links for all seven recognized firmware files were
  added to `docs/MODELS.md`; no firmware or save data is tracked in Git.

Physical Vita launch behavior after the FTP update still requires user
confirmation. This release fixes the reported install/data-folder blockers; it
does not claim completed physical-device compatibility.

## v0.1.2-preview candidate

Vita package metadata is `01.02`. Candidate VPK SHA-256:

```text
f375f58afa85b0ce39a70bb0b1ea0b346b6690ad9ce96f7b58570ed5dabc39aa
```

Executable payload SHA-256:

```text
45fd818b706635556385e20b23d509ae0921f88d361f52c24fdd7ec860345df2
```

- Timer scheduling was corrected so timers started by guest firmware tick within
  the same frame. A synthetic boot-ROM regression covers 8-bit and 16-bit timer
  starts for Classic V1, Classic V2 and Xtreme.
- The frontend now requests a smaller SDL audio device buffer, caps queued audio
  to four emulated frames and drops stale queued sound. A bounded catch-up path
  can advance up to two extra emulated frames before presenting when rendering
  falls behind, improving audio freshness during slow gameplay. This is not a
  full-speed guarantee.
- User-supplied MAME-reference firmware was staged only into local Vita3K data
  storage, not into the repository or VPK. The four newly staged images matched
  the reference CRC32/SHA-1 values for `cyrom112.bin`, `flash_v1246.bin`,
  `cyrom150.bin` and `cyos_v1508.bin`.
- Host smoke boots now reach the Classic V1 desktop and Xtreme setup with LCD
  activity and 480,000 generated audio samples over 600 frames:
  Classic V1 `pc=21EE36`, Xtreme `pc=4A3C40`.
- Windows Vita3K runtime captures from the actual installed 01.02 build show:
  Classic V1 stock desktop, Classic V2 desktop, Classic V2 Pinball gameplay and
  Xtreme first-run setup. Classic V1 and Xtreme are boot-verified, not yet
  app-by-app verified. Xtreme and V1 are noticeably slower than Classic V2.
- All 13 ASan/UBSan C suites and seven local Python tests passed after the
  audio/timer changes. The long ASan firmware smoke was cancelled because it was
  only a performance burden; the release host smoke was used for firmware boot
  screen evidence.

![Classic V1 desktop in Windows Vita3K](vita3k-classic-v1-desktop.png)

![Classic V2 desktop in Windows Vita3K](vita3k-classic-desktop.png)

![Classic V2 Pinball gameplay after audio queue changes](vita3k-pinball.png)

![Xtreme first-run setup in Windows Vita3K](vita3k-xtreme-setup.png)

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
- [Release-commit GitHub CI](https://github.com/TheGh0stShip/VitaCybiko/actions/runs/35461016270)
  passes on a clean Ubuntu runner. The published VPK is available in the
  [v0.1.1-preview release](https://github.com/TheGh0stShip/VitaCybiko/releases/tag/v0.1.1-preview).
  Downloading that asset again reproduced the recorded SHA-256 and all nine ZIP
  entries passed integrity verification. Its executable matches the installed
  Windows Vita3K executable byte-for-byte.

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
| Games / Pinball Pro | Game starts and renders gameplay; observed roughly 32–46 FPS in the optimized build before the v0.1.2 timing work; the v0.1.2 capture during play showed 17 FPS presentation in Vita3K |
| Applications / Calculator | Numeric input and directional button selection compute 2 + 3 = 5 |
| Save/model return | Start + Select saves and returns to the model menu |
| RTC storage | Versioned clock file is written; host round-trip checks pass, but guest desktop time discrepancies remain |
| Classic V1 / Xtreme | Superseded by v0.1.2: matching firmware staged and boot verified, but app coverage/performance remain open |

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
- Firmware staging identifies all supplied model images by size/SHA-1, stages
  recognized V1/V2/Xtreme ROMs without overwriting existing saves and reports
  each missing profile individually.

## Open acceptance items

Physical Vita/PSTV testing; V1/Xtreme app coverage and performance; all
original launch-bundle apps; user-created Notes/Organizer data save-and-reopen;
reliable guest Esc/exit behavior across apps; audible fidelity; guest clock
correctness; sustained full-speed games; wireless/accessory interoperability.

The release remains a preview with the deepest app verification on Classic V2,
not completion of these items. See [compatibility](COMPATIBILITY.md) and
[release goals](RELEASE-PLAN.md).

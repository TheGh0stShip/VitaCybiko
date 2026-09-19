# Release goals and current result

A model-selection menu is not proof of emulation. The overall 1:1 dual-family
completion goal remains open.

| Goal | Status |
| --- | --- |
| Diagnose Classic V2 boot stall and reach real desktop | Achieved: CPU stack/store corrections and SCI TX interrupt implementation |
| Preserve Classic V1 and Xtreme selection | Implemented; real boots blocked on matching firmware |
| Usable frontend, input and safe model-separated storage | Host regressions pass; V2 setup, desktop, Pinball, Calculator, text save/reopen and clock continuity exercised |
| Real screenshots from Windows Vita3K | Captured; published in verification record |
| Reproducible source, tests, VPK and documentation | Prepared, with proprietary images and saves excluded |
| Public repository/release | Published as [v0.1.1-preview](https://github.com/TheGh0stShip/VitaCybiko/releases/tag/v0.1.1-preview), with unresolved acceptance items explicit |
| 1:1 Cybiko hardware and entire original launch set | Not achieved; see remaining requirements below |

## Completed implementation work

- Packed 4-byte CCR/PC interrupt frames, RTE/TRAPA consistency and corrected
  long-displacement MOV.L store operands.
- SCI0/SCI2 TX completion interrupts; the missing interrupts previously filled
  the CyOS console buffer and blocked startup tasks.
- Bounded RTC I2C protocol, proper Port F direction behavior and calendar tests.
- Upright landscape UI with full touch keyboard, retained portrait view, saved
  model/layout/skin preferences and literal-device LiveArea artwork.
- Separate model saves, atomic replacement, invalid-save preservation and RTC
  sidecars. Instruction-memory mapping cache and link-time optimization.
- Firmware-identification/copy helper and public host-test CI.

No guest firmware bytes, task states or return addresses are patched to force boot.

## Remaining requirements

1. Legally supplied Classic V1 boot/DataFlash and Xtreme boot/CyOS images.
2. A verifiable original Classic retail launch-bundle manifest and matching apps.
3. Broader app coverage, remaining CPU/peripheral modes and stronger performance.
4. Physical Vita/PSTV runs, audible fidelity and suspend/resume acceptance.
5. Radio, PC synchronization and accessory implementations plus interoperability
   evidence before any 1:1 claim.

A preview publication preserves and makes the verified work available. It does
not close the overall completion goal or turn untested profiles into supported
hardware.

## Follow-up: input and battery-backed state

- Reproduced Pinball ignoring a three-frame Esc tap but recognizing eight frames.
  Classic now has model-specific press timing, with immediate dedicated numbers;
  Xtreme's Fn sequencing is unchanged. Short 40 ms Vita3K taps were exercised.
- Pinball's quit dialog and return to Games now verified. Calculator's documented
  Esc-once-clear / Esc-twice-exit behavior is distinguished from input faults.
- Created `test.txt` containing `vita 123`, saved through the stock Text Editor,
  closed/restarted Vita3K and reopened the unchanged document.
- Preserved Classic SRAM with model/size/checksum validation and flash binding.
  Windows Vita3K clock advanced from 12:02 before restart to 12:04 afterward.
  Host corruption/mismatch tests preserve files and existing RAM on failure.

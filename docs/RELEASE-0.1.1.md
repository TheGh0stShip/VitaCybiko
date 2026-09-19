# VitaCybiko v0.1.1-preview

Fixes two Classic usability faults and adds real save/reopen evidence.

- Short taps now survive Classic CyOS's scan/debounce window. A 40 ms touch
  activates F4 and opens Pinball's quit confirmation in Windows Vita3K.
- Classic battery-backed SRAM is preserved alongside flash and RTC. Clock
  continuity now survives a full Vita3K restart.
- RAM checkpoints have model/size/checksum validation and are bound to the
  matching flash image; failures preserve existing files instead of resetting.
- The stock Text Editor saved `test.txt` containing `vita 123`; after closing
  and restarting Vita3K, the same document reopened unchanged.
- Thirteen sanitizer test suites and seven local Python tests pass, with new
  timing and RAM corruption/mismatch regressions. Real screenshots are in the
  [verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md).

Install the VPK over the previous version. Title ID remains `VCYB00001`; Vita
package metadata is `01.01`. **Back up the entire model folder first.** Keep
`save.flash`, `ram.dat` and `clock.dat` together. An old installation without RAM
state cold-boots once before its first checkpoint; that first boot may reset the
displayed time. See [storage details](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/MODELS.md).

This remains a preview: V1/Xtreme firmware boots, the exact original launch
bundle, all-app coverage, physical Vita, full-speed games and wireless/accessory
support are not fully verified. Firmware and commercial applications are not
distributed. The previous release remains available.

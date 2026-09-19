# VitaCybiko v0.1.2-preview

Corrects timer scheduling, reduces prescaler overhead and lowers audio latency
during slow gameplay.

- Timers started by guest code now tick within the same frame. Previously,
  frame-start caching could defer a newly started timer until the next frame.
- Small inline prescaler checks avoid calling full timer handlers on every
  emulated cycle. Model clock rates and instruction budgets are unchanged.
- An 8-bit timer's compare-triggered clear no longer sets a false overflow flag.
- SDL queued audio is now bounded to roughly four emulated frames instead of
  allowing about half a second of stale audio to build up. The frontend also
  requests a smaller audio device buffer and can run a bounded emulation catch-up
  before presenting a slow frame.
- A synthetic boot ROM checks start/stop timing for both timer types on all
  three model profiles. It fails against the previous frame loop and passes
  against this one. Thirteen ASan/UBSan suites and seven local Python tests pass.
- Matching Classic V1 and Xtreme firmware were staged in local Vita3K storage
  after size/CRC32/SHA-1 verification. Classic V1 reaches the stock desktop in
  Windows Vita3K; Xtreme reaches first-run setup. They are boot-verified, not
  yet app-by-app verified.

Title ID remains `VCYB00001`; Vita package metadata is `01.02`. Install over the
previous VPK after backing up the complete model data folder. Storage format is
unchanged from v0.1.1-preview. Firmware, commercial applications and saves are
not distributed.

This is still a preview, not completed 1:1 emulation. The exact original Classic
launch bundle, all-app compatibility, V1/Xtreme performance, physical Vita
testing, sustained full-speed gameplay and wireless/accessory support remain
open. See the
[verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
and [compatibility limits](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/COMPATIBILITY.md).

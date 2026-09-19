# VitaCybiko v0.1.3-preview

Physical-Vita install/setup fix for PlayStation Vita/PSTV.

- Vita-safe indexed LiveArea PNGs address VitaShell install error
  `0x8010113d`.
- Startup now tolerates FTP-prepared storage and creates model-specific folders
  after model selection, avoiding the false “cannot create VitaCybiko data
  folders” error.
- Direct Archive.org links and hashes for all recognized Classic V1, Classic V2
  and Xtreme firmware images are documented in
  [model setup](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/MODELS.md).
- Keeps v0.1.2's timer scheduling, audio queue and Classic V1/Xtreme boot
  verification improvements.

Install `VitaCybiko.vpk`, then follow model setup. No firmware, commercial
applications or user saves are distributed. The source archive contains the
vendored core and tests; no Git submodule checkout is needed.

**Not a completed 1:1 emulator.** Classic V2 is the deepest-tested path.
Classic V1 and Xtreme boot with matching firmware, but broad app coverage,
physical-device performance, wireless/accessory support and exact hardware
equivalence remain open. Consult the
[verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
before treating a profile or app as supported.

Vita title ID: `VCYB00001`. Package metadata version: `01.03`.
Back up `ux0:data/VitaCybiko` before upgrading.

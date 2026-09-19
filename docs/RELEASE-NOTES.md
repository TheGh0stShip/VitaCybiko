# VitaCybiko v0.1.4-preview

Physical-Vita audio pacing candidate for PlayStation Vita/PSTV.

- Prefers 48 kHz signed 16-bit stereo output on Vita and converts the emulator's
  internal 8-bit mono speaker stream before queueing.
- Keeps a one-frame audio prebuffer to reduce underrun crackle/choppiness while
  still dropping stale backlog after four emulated frames.
- Keeps v0.1.3's VitaShell-safe package images, stored VPK entries and
  FTP-prepared storage fix.
- Direct Archive.org links and hashes for all recognized Classic V1, Classic V2
  and Xtreme firmware images remain documented in
  [model setup](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/MODELS.md).

Install `VitaCybiko.vpk`, then follow model setup. No firmware, commercial
applications or user saves are distributed. The source archive contains the
vendored core and tests; no Git submodule checkout is needed.

**Not a completed 1:1 emulator.** Classic V2 is the deepest-tested path.
Classic V1 and Xtreme boot with matching firmware, but broad app coverage,
physical-device performance/audio confirmation, wireless/accessory support and
exact hardware equivalence remain open. Consult the
[verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
before treating a profile or app as supported.

Vita title ID: `VCYB00001`. Package metadata version: `01.04`.
Back up `ux0:data/VitaCybiko` before upgrading.

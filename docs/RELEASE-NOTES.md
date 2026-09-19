# VitaCybiko v0.1.0-preview

Classic V2 preview for PlayStation Vita/PSTV, tested in Windows Vita3K.

- Boots the supplied Classic V2 / CyOS 1.3.58 firmware to its genuine desktop.
- Pinball Pro gameplay and Calculator arithmetic exercised in Vita3K.
- Selectable Classic V1, Classic V2 and Xtreme profiles, with isolated saves.
- Landscape/portrait touch keyboard, skin preferences and literal-device icon.
- CPU interrupt, SCI transmit, RTC I2C and input fixes; sanitizer-tested core
  and actual frontend, plus firmware-identification and app-preparation tools.
- Actual runtime screenshots and setup/build/compatibility documentation.

Install `VitaCybiko.vpk`, then follow
[model setup](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/MODELS.md).
No firmware, commercial applications or user saves are distributed. The source
archive contains the vendored core and tests; no Git submodule checkout is needed.

**Not a completed 1:1 emulator.** Classic V1 and Xtreme boots remain unverified
without matching firmware. The exact original Classic launch bundle, physical
Vita testing, guest clock accuracy, app-exit behavior, full-speed gameplay and
wireless/accessory support remain open. Consult the
[verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
before treating a profile or app as supported.

Vita title ID: `VCYB00001`. Package metadata version: `01.00`.
Back up `ux0:data/VitaCybiko` before upgrading.

# VitaCybiko v0.1.3-preview

Physical-Vita install and setup fix release.

- Converts LiveArea package images to Vita-safe indexed PNGs. This addresses
  VitaShell install failures reported as `0x8010113d`.
- Startup storage creation now creates only `ux0:data/VitaCybiko` before the
  model selector, then creates the selected model's `roms/` and `apps/`
  folders afterward. This avoids a false fatal error on Vita when the data tree
  has already been staged over FTP.
- Package metadata is bumped to `01.03`.
- Setup documentation now includes direct Archive.org links for all recognized
  Classic V1, Classic V2 and Xtreme firmware files from the MAME 0.221 merged
  `cybikov1.zip`, plus sizes, CRC32 values and SHA-1 hashes.

No Cybiko firmware, commercial apps or user saves are distributed in the VPK or
source tree. Use only files you are legally allowed to use and follow
[model setup](MODELS.md).

This remains a preview. Classic V2 is still the deepest-tested path. Classic V1
and Xtreme boot with matching firmware, but broad app coverage, physical-device
performance, wireless/accessory behavior and exact 1:1 hardware equivalence
remain open.

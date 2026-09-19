# Device selection and firmware

Choose Classic V1, Classic V2, or Xtreme at startup. Classic V2 boots to the
desktop with CyOS 1.3.58 and has the deepest app-level Vita3K verification.
Classic V1 reaches the stock desktop, and Xtreme reaches first-run setup when
the matching images below are supplied. V1 and Xtreme remain experimental:
selecting a profile is not proof that every app or workflow works.

## Independent storage

```text
ux0:data/VitaCybiko/
  classic-v1/
    roms/boot.bin       # cyrom112.bin, 32768 bytes
    roms/dataflash.bin  # flash_v1246.bin, 540672 bytes; OS + bundled apps
    save.flash          # created on save; never overwrites roms/dataflash.bin
    ram.dat             # checksummed battery-backed RAM, bound to save.flash
  classic-v2/
    roms/boot.bin       # cyrom117.bin / C4PC emu_rom.bin, 32768 bytes
    roms/flash.bin      # cyos_v1358.bin / C4PC emu_cyos.bin, 262144 bytes
    roms/dataflash.bin  # flash_v1358.bin / C4PC emu_flash.bin, 540672 bytes
    save.flash          # this model's writable serial flash
    ram.dat             # stock 256 KiB SRAM plus checkpoint header
  xtreme/
    roms/boot.bin       # cyrom150.bin, 32768 bytes
    roms/flash.bin      # cyos_v1508.bin, 524288 bytes
    apps/              # optional Xtreme .app/.dl files and Libraries/
    save.nvram         # Xtreme's 2 MiB RAM image
```

V1 has no memory-mapped `flash.bin`: its OS is in the serial DataFlash.
C4PC's `emu_rom.bin`, `emu_cyos.bin` and `emu_flash.bin` are V2, not V1 or
Xtreme. Rename copies according to the mapping above. No proprietary images
are bundled in the VPK. Never rename a different model's ROM to bypass checks.

## Archive.org reference links

VitaCybiko does not download or distribute Cybiko firmware, commercial apps, or
user saves. Use only files you are legally allowed to use. For preservation and
hash verification, the Internet Archive MAME 0.221 merged set exposes one
`cybikov1.zip` archive that contains every firmware image currently recognized
by `tools/prepare_firmware.py`:

- Complete archive:
  <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip>
- Browse archive contents:
  <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/>

Direct member links:

| VitaCybiko profile | Archive member | Direct link | Install as |
| --- | --- | --- | --- |
| Classic V1 | `cyrom112.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/cyrom112.bin> | `classic-v1/roms/boot.bin` |
| Classic V1 | `flash_v1246.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/flash_v1246.bin> | `classic-v1/roms/dataflash.bin` |
| Classic V2 | `cybikov2/cyrom117.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/cybikov2%2Fcyrom117.bin> | `classic-v2/roms/boot.bin` |
| Classic V2 | `cybikov2/cyos_v1358.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/cybikov2%2Fcyos_v1358.bin> | `classic-v2/roms/flash.bin` |
| Classic V2 | `cybikov2/flash_v1358.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/cybikov2%2Fflash_v1358.bin> | `classic-v2/roms/dataflash.bin` |
| Xtreme | `cybikoxt/cyrom150.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/cybikoxt%2Fcyrom150.bin> | `xtreme/roms/boot.bin` |
| Xtreme | `cybikoxt/cyos_v1508.bin` | <https://archive.org/download/mame-0.221-roms-merged/cybikov1.zip/cybikoxt%2Fcyos_v1508.bin> | `xtreme/roms/flash.bin` |

The same MAME material is also available in split collections, including:

- MAME 0.236 split directory:
  <https://archive.org/download/mame-0.236-roms-split/MAME%200.236%20ROMs%20%28split%29/>
- MAME 0.172 directory:
  <https://archive.org/download/Mame172arcdcmplt>

After extracting an archive, stage recognized files without hand-renaming:

```sh
python3 tools/prepare_firmware.py /path/to/extracted-cybiko-files /path/to/ux0/data/VitaCybiko --copy
```

The Classic app bundle is read from the supplied flash image. The current
Xtreme app injector is **not** used on Classic's different CFS format; files
placed in a Classic `apps/` directory are not imported yet. The v1.3.58 image
is a later Classic software set, not verified as the exact launch bundle.

Existing Xtreme installs can keep the old `roms/`, `apps/`, and `save.nvram`
directly under `VitaCybiko/`. Only the Xtreme profile falls back to that layout,
and only when `xtreme/roms/boot.bin` is absent. To migrate, copy all three parts
together while the application is closed; retain a backup of the old save.

Each Classic save must be exactly 540672 bytes. An unreadable or wrong-size
existing save stops loading; it does not silently reset to the factory image.
Content/checksum validation of modified Classic CFS pages is not implemented.
Saves use a temporary file and rename. Back up saves before testing an
experimental core: guest firmware can modify the emulated flash.

## Accepted image identifiers

To identify an extracted local collection without changing it, use:

```sh
python3 tools/prepare_firmware.py /path/to/extracted-firmware /path/to/ux0/data/VitaCybiko
```

Add `--copy` to copy recognized images. The helper matches size and SHA-1,
refuses conflicting destinations, and never replaces saves. Missing profiles
are reported individually. ZIP/CD extraction must be done separately.

| Selection | Source filename | Size | CRC32 | SHA-1 |
| --- | --- | ---: | --- | --- |
| Classic V1 | `cyrom112.bin` | 32768 | `9e1f1a0f` | `6fc08de6b2c67d884ec78f748e4a4bad27ee8045` |
| Classic V1 | `flash_v1246.bin` | 540672 | `3816d0ab` | `19be4fed8d95112568adf93219afe9406d7baecf` |
| Classic V2, CyOS v1.3.58 | `cyrom117.bin` | 32768 | `268da7bf` | `135eaf9e3905e69582aabd9b06bc4de0a66780d5` |
| Classic V2, CyOS v1.3.58 | `cyos_v1358.bin` | 262144 | `05ca4ece` | `eee329e8541e1e36c22acb1317378ce23ccd1e12` |
| Classic V2, CyOS v1.3.58 | `flash_v1358.bin` | 540672 | `e485880f` | `e414d6d2f876c7c811946bcdfcb6212999412381` |
| Xtreme, CyOS v1.5.08 | `cyrom150.bin` | 32768 | `18b9b21f` | `28868d6174eb198a6cec6c3c70b6e494517229b9` |
| Xtreme, CyOS v1.5.08 | `cyos_v1508.bin` | 524288 | `f79400ba` | `537a88e238746b3944b0cdfd4b0a9396460b2977` |

Reference: [MAME Cybiko hardware definitions](https://github.com/mamedev/mame/blob/master/src/mame/cybiko/cybiko.cpp).
Hashes identify the supported revisions; they do not establish completed boot.

## Implementation and limits

Profiles select clock, RAM size/mirrors, boot/flash/LCD maps, on-chip RAM base,
timer count, RTC pins, and keyboard matrix. Classic uses a bounded AT45DB041
SPI implementation with status/read/compare/program commands and a limited
SCI1/DTC path. The code is adapted from the MIT-licensed
[upstream Java emulator](https://github.com/daberkow/cybiko-java-emulator), whose
copyright notice is preserved in `third_party/cybiko-c-emulator/LICENSE`.
The original C core's default API still creates an Xtreme machine.

V2 uses the stock 256 KiB RAM configuration. No firmware-specific task flags,
OS code, or CPU return addresses are patched to force startup. General DTC,
complete serial/radio hardware, full CPU timing and Classic app installation
remain unfinished. Classic keys have their own mapping, including dedicated
numbers and punctuation; number presses no longer synthesize Xtreme Fn chords.

Observed V2 run: first-run setup, Main Desktop and Pinball Pro gameplay in
Windows Vita3K. Setup survives restart. The previous startup stall was fixed
by implementing SCI transmit interrupts; PC `11ED98` is a normal idle loop,
not sufficient evidence of a stall. With matching MAME-reference firmware
staged, V1 reaches its stock desktop and Xtreme reaches first-run setup in
Windows Vita3K. V1/Xtreme performance and app coverage remain incomplete.

Each model also stores `clock.dat`, a versioned/checksummed RTC sidecar. It
advances time while the app is closed. A corrupt clock file stops startup and
is preserved; move it aside only if you intentionally want a clock reset.
`preferences.dat` in the shared root remembers model, layout and shell color.
Back up the entire model folder, not just its save file.

Starting with v0.1.1-preview, Classic also restores its battery-backed SRAM.
CyOS needs saved RAM calendar state as well as the RTC to preserve its displayed
time; saving only flash and RTC is insufficient. `ram.dat` records the model, RAM length,
header/payload CRCs and the matching flash CRC. A corrupt or mismatched pair
stops loading instead of booting inconsistent cached data. Each file is replaced
atomically, but the entire set is not one filesystem transaction: keep all three
files together when backing up/restoring. After an interrupted multi-file save,
restore a matching backup, or move `ram.dat` aside for an intentional cold boot
that keeps flash documents but may reset the clock. An older installation without
`ram.dat` likewise cold-boots once before creating its first RAM checkpoint.

Reproduce the read-only V2 diagnostic without touching source firmware:

```bash
./build-host/cybiko-smoke --classic-v2 emu_rom.bin emu_cyos.bin emu_flash.bin 3600 classic.pgm
```

For V1 use `--classic-v1 cyrom112.bin - flash_v1246.bin`. For Xtreme omit the
Classic flag and pass `cyrom150.bin cyos_v1508.bin`. The smoke tool reports LCD
activity, not successful OS startup. Inspect its image and the guest UI.

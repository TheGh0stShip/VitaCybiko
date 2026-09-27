# VitaCybiko 01.19 preview

Classic checkpoint-integrity preview for PlayStation Vita/PSTV.

- Package metadata, runtime UI, LiveArea revision and startup artwork now agree
  on `01.19`.
- Validates Classic serial-flash page checksums, allocation chains, file headers,
  duplicate names, orphaned/duplicate parts and declared sizes before loading a
  save. The known legacy V1 factory page is accepted only while byte-identical
  to the recognized factory image.
- Writes `session.dat` last to bind the exact Classic `save.flash`, `ram.dat`
  and `clock.dat` files. Mixed or interrupted checkpoints are rejected without
  overwriting the original files.
- Makes the host smoke/replay tools consume complete Vita `ram.dat` and
  `clock.dat` sidecars directly, with model, size, CRC and flash-binding checks.
- Adds unit coverage for valid sidecars and corruption/model/flash mismatch
  rejection.
- Deterministic physical-checkpoint replay reaches both Classic desktops,
  Pinball Pro gameplay and a playable Reversi 3 board.
- Retains the 01.18 H8S branch correction, defined-arithmetic fixes, bounded
  audio queue, background saves and performance telemetry. Xtreme continues to
  pass its existing boot/smoke gates; its performance work remains deferred.
- First-party code and package notices remain GPL-3.0-or-later; vendored
  dependencies retain their upstream licenses.

Install `VitaCybiko.vpk`, then follow model setup. No firmware, commercial
applications or user saves are distributed. Back up the complete model folders
under `ux0:data/VitaCybiko` before upgrading.

Release asset SHA-256:

```text
a0ac44f78f1e3bdce768ccbb4230c7f68f7a408aa2f01d0330f36858afb26e93  VitaCybiko.vpk
```

The release also includes `SHA256SUMS`; after downloading both assets into the
same directory, run `sha256sum -c SHA256SUMS`.

**Not a completed 1:1 emulator.** Physical 01.19 input/audio/suspend testing,
subjective performance qualification, broader Classic V1 app coverage,
wireless/accessory support and exact hardware equivalence remain open. Consult
the [verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
before treating a profile or app as supported.

Vita title ID: `VCYB00001`. Package metadata version: `01.19`.

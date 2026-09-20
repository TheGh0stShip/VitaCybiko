# VitaCybiko 01.18 preview

Performance-instrumented preview for PlayStation Vita/PSTV.

- Package metadata, LiveArea title, and runtime UI now report `01.18`.
- Fixes H8S semantic fast-path BHI/BLS branch conditions so the optional ROM
  block executor matches the interpreter. This removes the Xtreme host-smoke
  failure that previously fell into unmapped `0xF00000` execution.
- Retunes the cached semantic-reject backoff to 256 cycles after the Xtreme
  branch fix. The local three-model smoke gate passes for Classic V1, Classic
  V2 and Xtreme.
- Keeps the sanitizer-detected signed-overflow fixes in H8S long INC/DEC and
  NEG semantics. Public CI includes both a clean Release host gate and a
  sanitizer/frontend host gate.
- Adds H8S semantic fast-path telemetry to Vita `performance.csv`:
  accepted blocks, accepted cycles, guarded rejects, cached rejects, and
  backoff skips.
- Local Xtreme 600-frame smoke passes with 600 active frames and PC `4A3C40`.
- Keeps the 48 kHz signed 16-bit stereo Vita audio output path, bounded audio
  queueing, VitaShell-safe package images, stored VPK entries and model-specific
  storage layout.
- Direct Archive.org links and hashes for all recognized Classic V1, Classic V2
  and Xtreme firmware images remain documented in
  [model setup](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/MODELS.md).

Install `VitaCybiko.vpk`, then follow model setup. No firmware, commercial
applications or user saves are distributed. The source archive contains the
vendored core and tests; no Git submodule checkout is needed.

Release asset SHA-256:

```text
2befe6984c4eca573cf5ce2d33b70c89c977f86b227ea1d338d44a5819f8c392  VitaCybiko.vpk
```

The release also includes `SHA256SUMS`; after downloading both assets into the
same directory, run `sha256sum -c SHA256SUMS`.

**Not a completed 1:1 emulator.** Classic V2 is the deepest-tested path and is
the deepest app-verified path. Classic V1 and Xtreme pass the current
matching-firmware host smoke gate, but broad app coverage, physical-device
performance/audio confirmation, wireless/accessory support and exact hardware
equivalence remain open. Consult the
[verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
before treating a profile or app as supported.

Vita title ID: `VCYB00001`. Package metadata version: `01.18`.
Back up `ux0:data/VitaCybiko` before upgrading.

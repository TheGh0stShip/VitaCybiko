# VitaCybiko 01.17 preview

Performance-instrumented preview for PlayStation Vita/PSTV.

- Package metadata, LiveArea title, and runtime UI now report `01.17`.
- Fixes sanitizer-detected signed-overflow undefined behavior in H8S long
  INC/DEC and NEG semantics. Public CI now includes both a clean Release host
  gate and a sanitizer/frontend host gate.
- Adds H8S semantic fast-path telemetry to Vita `performance.csv`:
  accepted blocks, accepted cycles, guarded rejects, cached rejects, and
  backoff skips.
- Keeps the measured fixed 128-cycle semantic reject backoff. Local Classic V2
  600-frame smoke passes with 599 active frames and reports more than 11 million
  skipped optional semantic probes.
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
dccacfd58ae84abd1dba5c236d6710bff2339aeeaa665b972c689dd586b1676b  VitaCybiko.vpk
```

The release also includes `SHA256SUMS`; after downloading both assets into the
same directory, run `sha256sum -c SHA256SUMS`.

**Not a completed 1:1 emulator.** Classic V2 is the deepest-tested path and is
the only profile with the latest local smoke-performance gate. Classic V1 and
Xtreme boot with matching firmware in prior verification, but broad app
coverage, physical-device performance/audio confirmation, wireless/accessory
support and exact hardware equivalence remain open. Consult the
[verification record](https://github.com/TheGh0stShip/VitaCybiko/blob/main/docs/VERIFICATION.md)
before treating a profile or app as supported.

Vita title ID: `VCYB00001`. Package metadata version: `01.17`.
Back up `ux0:data/VitaCybiko` before upgrading.

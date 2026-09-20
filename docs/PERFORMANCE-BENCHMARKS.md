# Cross-model performance benchmark

The repository includes `tools/benchmark_models.sh`, a repeatable host smoke
benchmark for the three supported firmware profiles. It runs 600 guest frames
with the same core and reports wall time plus the firmware/model reached.

Using the locally supplied firmware fixtures on 2026-09-20 after the H8S
semantic BHI/BLS branch-condition fix and the 256-cycle cached-reject backoff:

| Profile | Wall time | Equivalent guest rate |
| --- | ---: | ---: |
| Classic V1 | 1.02 s | 588.2 frames/s |
| Classic V2 | 0.39 s | 1538.5 frames/s |
| Xtreme | 2.25 s | 266.7 frames/s |

The host smoke run is a core regression/performance gate, not a physical Vita
frame-rate claim. The previous Xtreme smoke with matching MAME-reference
firmware fell into unmapped `0xF00000` execution and failed with no active LCD
frames. Correcting the semantic block resolver's BHI/BLS conditions makes the
same Xtreme smoke pass at PC `0x4A3C40` with 600 active frames. The current
backoff setting is a host-smoke tuning gate; physical-Vita captures remain the
authoritative hardware measure.

Run it with operator-supplied firmware paths (the files are intentionally not
redistributed):

```sh
tools/benchmark_models.sh \
  /path/to/cybiko-smoke \
  /path/to/cyrom112.bin /path/to/flash_v1246.bin \
  /path/to/classic-v2/boot.bin /path/to/classic-v2/flash.bin /path/to/classic-v2/save.flash \
  /path/to/xtreme/boot.bin /path/to/xtreme/flash.bin
```

Any future core optimization must preserve the smoke `PASS` lines and compare
the resulting times against this gate before it is described as an improvement.

## Current smoke gate after timer8 deadline caching

After `814c2a0` and the follow-up fixture pull from the physical Vita's
operator-owned `ux0:/data/VitaCybiko` tree, all three firmware profiles passed
the 600-frame host smoke gate:

| Profile | CPU seconds | Active frames | Final PC |
| --- | ---: | ---: | --- |
| Classic V1 | 1.308243 s | 586 / 600 | `0x219C8E` |
| Classic V2 | 0.605239 s | 596 / 600 | `0x11ED98` |
| Xtreme | 1.959807 s | 600 / 600 | `0x4A3C40` |

This is still a host core smoke benchmark, not a physical Vita smoothness
claim. It is useful because it proves the latest core changes still run all
three model profiles with matching firmware, including Classic V2.

If the Vita has already been staged with user-owned firmware, the local fixture
tree can be refreshed without committing proprietary data:

```sh
tools/pull_vita_fixtures.sh \
  ftp://10.0.0.202:1337/ux0:/data/VitaCybiko

tools/benchmark_models.sh \
  build-ci-release-host/cybiko-smoke \
  vita_runtime_pull/current/classic-v1/roms/boot.bin \
  vita_runtime_pull/current/classic-v1/roms/dataflash.bin \
  vita_runtime_pull/current/classic-v2/roms/boot.bin \
  vita_runtime_pull/current/classic-v2/roms/flash.bin \
  vita_runtime_pull/current/classic-v2/roms/dataflash.bin \
  vita_runtime_pull/current/xtreme/roms/boot.bin \
  vita_runtime_pull/current/xtreme/roms/flash.bin
```

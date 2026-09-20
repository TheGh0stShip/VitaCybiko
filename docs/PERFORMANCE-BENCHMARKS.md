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

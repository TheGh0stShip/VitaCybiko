# Cross-model performance benchmark

The repository includes `tools/benchmark_models.sh`, a repeatable host smoke
benchmark for the three supported firmware profiles. It runs 600 guest frames
with the same core and reports wall time plus the firmware/model reached.

Using the locally supplied firmware fixtures on 2026-09-20:

| Profile | Wall time | Equivalent guest rate |
| --- | ---: | ---: |
| Classic V1 | 0.88 s | 681.8 frames/s |
| Classic V2 | 1.10 s | 545.5 frames/s |
| Xtreme | 18.95 s | 31.7 frames/s |

The host smoke run is a core regression/performance gate, not a physical Vita
frame-rate claim. The Xtreme result is nevertheless useful: it reproduces the
model-specific interpreter cost outside SDL, so audio or presentation changes
alone cannot make Xtreme real-time. Physical-Vita captures remain the
authoritative hardware measure and currently show the same Xtreme bottleneck.

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

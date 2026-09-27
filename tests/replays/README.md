# Optional Classic application replays

These input files drive `cybiko-replay` through real Classic applications
without embedding firmware or saves. They use the guest's already-translated
Classic matrix (`frame,column,hex_mask`), not Vita button identities.

They are checkpoint-specific acceptance probes:

| Input | Starting checkpoint selection | Frames | Expected final screen |
| --- | --- | ---: | --- |
| `classic-v1-labyrinth.keys` | Main Desktop, You & Me | 4,200 | Lost in Labyrinth active gameplay |
| `classic-v2-pinball.keys` | Main Desktop, Games | 1,800 | Pinball Pro Level 1 gameplay |
| `classic-v2-reversi.keys` | Main Desktop, Games | 4,200 | Reversi 3 playable board |

Use matching, legally supplied firmware plus the complete `save.flash`,
`ram.dat` and `clock.dat` pulled from the device. For example:

```sh
CYBIKO_SMOKE_RAM=/private/classic-v1/ram.dat \
CYBIKO_SMOKE_CLOCK=/private/classic-v1/clock.dat \
CYBIKO_REPLAY_KEYS=tests/replays/classic-v1-labyrinth.keys \
CYBIKO_REPLAY_LOG=/tmp/classic-v1-labyrinth.csv \
build-host/cybiko-replay --classic-v1 /private/classic-v1/boot.bin - \
  /private/classic-v1/save.flash 4200 /tmp/classic-v1-labyrinth.pgm
```

Inspect the final PGM; process exit success and nonblank LCD activity alone do
not establish that the expected app screen was reached. A different desktop
selection or application-set ordering requires a different input sequence.
These host replays check deterministic execution and input response, not
physical speed or audible fidelity.

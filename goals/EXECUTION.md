# VitaCybiko execution goals

This file is the anti-loop checklist. If work resumes, start here before
touching code. Do not keep polishing the frontend while the measured bottleneck
is still the H8S core.

## Current release state

- Latest published preview: `v0.1.17-preview`.
- Version shown by package/runtime: `01.17`.
- Classic V2 is the deepest-tested path.
- Classic V1 and Xtreme are supported as selectable profiles, but broad app
  coverage and physical smoothness are not complete.
- Firmware and commercial Cybiko data are not redistributed.

## Non-negotiable finish criteria

The project is not “finished” until all of these are true:

1. Classic V1, Classic V2 and Xtreme boot from clean model folders with matching
   operator-supplied firmware.
2. Physical Vita performance is real-time enough that guest audio and animation
   are smooth during CyOS startup, desktop navigation and at least one app/game
   per model.
3. Presentation interpolation only smooths already-correct guest frames; it is
   not used to hide CPU starvation or frame dropping.
4. Battery readings do not trigger low/critical battery UI in Classic or Xtreme
   under normal emulated AC/full-charge state.
5. LiveArea title/art and runtime launch screen show the same package version.
6. The VPK, docs, release notes, checksums and verification evidence match the
   exact published build.

## Current blocker

The repeated failures point at core throughput, especially Xtreme. The latest
research and local measurements show this is not primarily an SDL audio queue,
LiveArea, rear-touch, or cosmetic interpolation issue.

The next engineering work must therefore be one of:

- branch-aware cached interpretation for immutable ROM blocks;
- an ARMv7-A translation tier for a tightly bounded, correctness-tested subset;
- timer/peripheral event scheduling reductions with equivalence tests;
- profiling that proves a different bottleneck with numbers.

## Stop repeating

Do not spend another iteration on these unless new measurements contradict the
current evidence:

- larger audio buffers as the main fix;
- frame dropping as a smoothness fix;
- tiny opcode-by-opcode interpreter tweaks without an executed-op profile;
- global adaptive semantic-probe backoff, which already regressed Classic V2;
- frontend-only interpolation changes while Xtreme core FPS is below real time.

## Required gates for the next optimization

Every performance change must include:

1. Host tests passing.
2. Three-model smoke or a clear explanation of which firmware fixture is absent.
3. Before/after timing for Classic V1, Classic V2 and Xtreme when fixtures exist.
4. A note in `goals/OPTIMIZATION.md` saying whether the result improved,
   regressed, or was rejected.
5. No claim of physical Vita smoothness without physical Vita evidence.


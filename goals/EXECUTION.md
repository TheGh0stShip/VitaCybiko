# VitaCybiko execution goals

This file is the anti-loop checklist. If work resumes, start here before
touching code. Do not keep polishing the frontend while the measured bottleneck
is still the H8S core.

## Current release state

- Latest published preview: `v0.1.18-preview`.
- Version shown by package/runtime: `01.18`.
- Classic V2 is the deepest-tested path.
- Classic V1 and Xtreme are supported as selectable profiles, but broad app
  coverage and physical smoothness are not complete.
- Firmware and commercial Cybiko data are not redistributed.

## Active milestone: playable Classic

Engineering priority is now Classic V1 and Classic V2. Xtreme must continue to
boot and pass existing regression/smoke tests, but Xtreme throughput and broad
application coverage are deferred until both Classic profiles satisfy these
playability gates:

1. Both Classic models reach their desktops from clean model folders.
2. Pinball, Calculator, Text Editor and representative bundled games work.
3. Controls and audible output behave correctly on physical hardware.
4. Flash, SRAM and clock state survive restart and suspend/resume without
   accepting corrupt or mismatched checkpoints.
5. Physical Vita/PSTV performance is acceptable in startup, desktop navigation
   and the tested applications.
6. No firmware bytes, task states or return addresses are patched to force boot.

Current physical evidence is uneven: the captured 01.18 Classic V1 workload
reports roughly 44–48 guest frames/sec, while Classic V2 only has an older 01.14
capture around 16–18 guest frames/sec. A fresh Classic V2 physical run is the
next required performance measurement; host smoke and Vita3K cannot replace it.

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

For the active Classic milestone, the immediate evidence gap is a current
physical Classic V2 performance/application run. Xtreme remains substantially
slower, but that optimization no longer blocks the Classic milestone. Low guest
throughput is not primarily an SDL audio queue, LiveArea, rear-touch, or
cosmetic interpolation issue.

When a Classic workload is CPU-bound, the next engineering work must be one of:

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

1. `scripts/release_gate.sh` passing locally before push.
2. The pushed GitHub Actions run completing green before the VPK is uploaded,
   installed or described as publishable.
3. Three-model smoke or a clear explanation of which firmware fixture is absent.
4. Before/after timing for Classic V1, Classic V2 and Xtreme when fixtures exist.
5. A note in `goals/OPTIMIZATION.md` saying whether the result improved,
   regressed, or was rejected.
6. No claim of physical Vita smoothness without physical Vita evidence.

The recent failed GitHub runs were caused by sanitizer/leak gates that had not
been run before pushing. Do not bypass the release gate to ship a “quick fix.”

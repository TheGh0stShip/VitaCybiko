# VitaCybiko optimization goals

These are the remaining engineering goals derived from physical performance
logs and upstream research. They are measurable and do not substitute frame
drops for emulation speed.

## Goal A — ROM prefetch hardening

Status: implemented in `27aeb68`.

Gate: all 16 tests, scheduler equivalence, and all three firmware smoke
profiles remain green. Host Xtreme smoke improved from about 19 s to 15.29 s
per 600 frames.

## Goal B — Immutable-ROM decoded blocks

Status: partial — raw immutable-ROM fetch blocks landed in `495aa2a`; semantic
decoded blocks remain open.

Build a bounded cache keyed by ROM PC. A block ends before branches, interrupts,
I/O, event deadlines, or any instruction whose operands leave immutable ROM.
The cache must not include RAM or on-chip code. Each block records its source
range and exits to the existing interpreter at every observable boundary.

Gates:

- byte-for-byte CPU/RAM/LCD/audio fingerprints against the interpreter;
- event-scheduler equivalence for all three models;
- invalidation tests for reset, IRQ, branch, and ROM/RAM boundary transitions;
- no regression in Classic V1/V2 boot smoke.

## Goal C — ARMv7 translation backend

Status: research/design only.

If Goal B demonstrates that decode dispatch is still dominant, add a small
ARMv7-A backend for straight-line ROM blocks. Generated code must use explicit
block exits for timer/DMA/IRQ deadlines and invalidate safely if a block ever
references mutable memory. The first translation tier remains immutable-ROM
only.

## Goal D — Peripheral-event cost reduction

Status: partial — disabled timers now skip event-query calls in `5216028`;
empty bus completion scans are bypassed, and timer8/timer16 deadline helpers
now inline their hot internal paths in the 01.15 worktree. Deadline caching for
active timers remains open.

The latest host profile attributes material time to
`timer16_cycles_until_event`, `cycles_until_next_peripheral_event`, and
`sync_peripherals`. Add cached next-event deadlines only after proving timer
register writes, compare matches, DMA completion, and interrupt ordering remain
equivalent.

Latest gate: all 16 host tests pass. The Xtreme 600-frame smoke moved from the
previous optimized 14.87 s run to 13.50 s on the same host gate after the
completion-bypass and timer helper cleanup; Classic V1/V2 remained smoke-pass.

## Goal E — Presentation budget

Status: separate from CPU optimization.

Measure texture upload, interpolation synthesis, and `SDL_RenderPresent` waits
independently. Preserve VSYNC and native endpoint correctness; do not hide guest
starvation with frame dropping.

## Research references

- [MAME CPU core concepts](https://wiki.mamedev.org/index.php/Core_Concepts)
- [MAME H8 core](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.h)
- [ARM cache-coherency guidance](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-and-self-modifying-code)
- [Mupen64Plus ARM dynarec notes](https://github.com/mupen64plus/mupen64plus-core/blob/master/doc/new_dynarec.mediawiki)

# Optimization goals

These are the remaining engineering goals derived from the latest physical
logs and upstream research. They are intentionally measurable and do not
substitute frame dropping for emulation speed.

## Goal A — ROM prefetch hardening

Status: implemented in `27aeb68`.

Gate: all 16 tests, scheduler equivalence, and all three firmware smoke
profiles remain green. Host Xtreme smoke improved from about 19 s to 15.29 s
per 600 frames.

## Goal B — Immutable-ROM decoded blocks

Status: next implementation target.

Build a bounded cache keyed by ROM PC. A block ends before branches, interrupts,
I/O, event deadlines, or any instruction whose operands leave immutable ROM.
The cache must not include RAM or on-chip code. Each block records its source
range and exits to the existing interpreter at every observable boundary.

Gates:

* byte-for-byte CPU/RAM/LCD/audio fingerprints against the interpreter;
* event-scheduler equivalence for all three models;
* invalidation tests for reset, IRQ, branch, and ROM/RAM boundary transitions;
* no regression in Classic V1/V2 boot smoke.

## Goal C — ARMv7 translation backend

Status: research/design only.

If Goal B demonstrates that decode dispatch is still dominant, add a small
ARMv7-A backend for straight-line ROM blocks. Generated code must use explicit
block exits for timer/DMA/IRQ deadlines and must invalidate safely if a block
ever references mutable memory. ARM documentation warns that generated code
and data caches are not automatically coherent for self-modifying code, so
cache maintenance and a strict ROM-only first tier are required.

## Goal D — peripheral-event cost reduction

Status: profiling target.

The latest host profile attributes material time to
`timer16_cycles_until_event`, `cycles_until_next_peripheral_event`, and
`sync_peripherals`. Add cached next-event deadlines only after proving that
timer register writes, compare matches, DMA completion, and interrupt ordering
remain equivalent.

## Goal E — presentation budget

Status: separate from CPU optimization.

Measure texture upload, interpolation synthesis, and `SDL_RenderPresent`
waits independently. Preserve VSYNC and native endpoint correctness; do not
hide guest starvation with frame dropping.

## Research basis

MAME documents interpreter overhead and block translation/code caching in its
[CPU core concepts](https://wiki.mamedev.org/index.php/Core_Concepts), and its
[H8 core](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.h)
keeps explicit prefetch state. ARM's guidance on [self-modifying code and
cache coherence](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-and-self-modifying-code)
supports keeping the first translation tier immutable-ROM only. The
[Mupen64Plus ARM dynarec notes](https://github.com/mupen64plus/mupen64plus-core/blob/master/doc/new_dynarec.mediawiki)
provide a concrete precedent for page invalidation and block exits.

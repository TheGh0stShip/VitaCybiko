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

Status: partial — raw immutable-ROM fetch blocks landed in `495aa2a`; a
conservative straight-line ROM block analyzer, block scanner, bounded PC-keyed
block cache, and tier-one semantic eligibility classifier have landed; semantic
decoded block execution remains open.

Build a bounded cache keyed by ROM PC. A block ends before branches, interrupts,
I/O, event deadlines, or any instruction whose operands leave immutable ROM.
The cache must not include RAM or on-chip code. Each block records its source
range and exits to the existing interpreter at every observable boundary.

Gates:

- byte-for-byte CPU/RAM/LCD/audio fingerprints against the interpreter;
- event-scheduler equivalence for all three models;
- invalidation tests for reset, IRQ, branch, and ROM/RAM boundary transitions;
- no regression in Classic V1/V2 boot smoke.

Current block-discovery gate:

- stops before Bcc/RTS/RTE/TRAPA/JMP/JSR/SLEEP control transfers;
- decodes implemented prefix instruction lengths while keeping control-flow
  prefixes as conservative boundaries;
- validates fixed and immediate instruction lengths before any runtime cache
  can use the ranges;
- all 17 host test groups pass with `h8s_block` included;
- `cybiko-block-scan` can scan operator-supplied ROM files without storing
  proprietary bytes in the repository;
- `h8s_block_cache_t` now provides a 256-entry direct-mapped cache keyed by ROM
  PC, with tests for hit/miss accounting, collision eviction, clearing, and
  invalid starts;
- tier-one executable-block classification now identifies straight-line blocks
  containing only register/immediate/non-memory instructions, while recording
  the executable prefix before memory/I/O/control-sensitive forms;
- cached blocks now store bounded decoded instruction words and byte lengths,
  capped by `H8S_BLOCK_MAX_INSTRUCTIONS`, so the next semantic executor can
  consume cache entries without repeating length decode;
- block-cache invalidation now uses an epoch/generation instead of clearing all
  decoded entries, so future runtime integration can invalidate cheaply;
- decoded block entries now retain immediate operands for 4/6-byte ROM
  instructions;
- an isolated semantic block executor now handles non-memory register/immediate
  forms (`ADDS`/`SUBS` ER forms, `MOV.L` register, byte immediate
  ADD/ADDX/CMP/SUBX/OR/XOR/AND/MOV, and word/long immediate
  MOV/ADD/CMP/SUB/OR/XOR/AND, byte/word register
  ADD/MOV/ADDX/SUB/CMP/SUBX, long register ADD/SUB/CMP, and byte register
  OR/XOR/AND) against a small standalone CPU state. It is deliberately not wired
  into `h8s_cpu_run` yet; tests prove the semantics before runtime integration.

Local block-scan coverage, max 32 instructions per candidate start:

| Image | Avg insns | Tier1 blocks | Tier1 prefix insns | Stop branch | Stop prefix | Stop unsupported |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic V1 boot | 13.19 | 4,881 | 125,482 | 11,634 | 0 | 0 |
| Classic V1 flash | 9.29 | 38,325 | 412,395 | 250,552 | 0 | 0 |
| Classic V2 boot | 11.91 | 4,119 | 96,277 | 12,384 | 0 | 0 |
| Classic V2 flash | 5.65 | 16,584 | 59,818 | 123,488 | 0 | 0 |
| Xtreme boot | 8.11 | 2,643 | 36,270 | 14,591 | 0 | 0 |
| Xtreme flash | 8.68 | 46,533 | 556,157 | 246,250 | 0 | 0 |

The scan shows branch boundaries now dominate; unsupported and prefix length
decoding are no longer the blocker. The next decoded-block step should begin
executing cache entries that contain only the classified straight-line forms.
This is deliberately tracked as an optimization goal, not as a runtime speed
claim: the Vita frontend still runs the interpreter until the semantic
execution tier is wired into `h8s_cpu_run`.

Executed opcode profile gate:

- `CYBIKO_OPCODE_PROFILE=ON` now builds a host-only profiler that dumps H8S
  opcode high-byte counts at process exit; normal release/Vita builds leave it
  disabled.
- 600-frame smoke profiles show that byte-immediate opcodes are not the right
  first runtime tier. The hottest groups are prefix `0x01`, `0x0f` MOV.L
  register, branches (`0x40`-`0x4f`), shifts/rotates (`0x10`/`0x11`), ADDS/SUBS
  (`0x0b`/`0x1b`), memory moves (`0x68`/`0x6f`), and immediate long forms
  (`0x7a`).

Top executed high-byte opcodes over 600 smoke frames:

| Image | #1 | #2 | #3 | #4 | #5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic V1 | 01 14.14% | 11 9.09% | 0f 8.16% | 0b 4.95% | 46 4.38% |
| Classic V2 | 01 8.76% | 0f 8.55% | 47 5.84% | 11 5.53% | 0b 5.22% |
| Xtreme | 01 9.69% | 0f 9.63% | 47 5.95% | 0b 5.59% | 11 4.93% |

Rejected runtime experiment on 2026-09-20:

- A direct `h8s_cpu_run` fast path for immutable-ROM byte-immediate opcodes
  passed all 17 host tests but slowed the 600-frame Xtreme smoke to 16.17 s
  after guard tightening, so it was removed. The next tier must cover the hot
  prefix/register/branch/memory groups as cached blocks rather than probing
  one narrow immediate class per instruction.
- Forcing `always_inline` on the hottest decode wrappers
  (`decode01`, `decode0f`, ADDS/SUBS, shifts/rotates, and immediate helpers)
  passed all 17 host tests but slowed the 600-frame Xtreme smoke to 18.92 s,
  so it was removed. The compiler's existing size/speed tradeoff is better
  than broad manual inlining here; the next optimization should reduce dispatch
  count with cached blocks instead of inflating the interpreter.
- Inlining `evaluate_condition` and caching CCR flag booleans for the hot Bcc
  path passed all 17 host tests but slowed the 600-frame Xtreme smoke to
  18.21 s, so it was removed. Branch micro-tuning is not the remaining path;
  reducing total interpreter dispatches is.
- Embedding the decoded block cache directly in `h8s_cpu_t` and clearing it on
  instruction-memory remaps passed all 17 host tests but slowed the 600-frame
  Xtreme smoke to 16.96-19.82 s depending on invalidation strategy, so CPU
  runtime wiring was removed for now. Keep decoded block data structures out of
  the hot CPU state until the executor can actually use them to offset the
  footprint/invalidation cost.

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

Rejected follow-up experiments on 2026-09-20:

- Observable-only timer deadlines preserved scheduler tests but slowed Xtreme
  smoke to 13.90-15.50 s, so raw timer deadlines remain the faster gate.
- Direct 32-bit bus big-endian loads/stores preserved tests but measured
  13.71-14.17 s on Xtreme, slower than the 13.50 s gate.
- Hoisting bus/speaker/sync pointers in `h8s_cpu_run` preserved tests but
  measured 14.71 s on Xtreme.
- Increasing immutable ROM fetch blocks from 16 to 64 words preserved tests but
  measured 15.91 s on Xtreme.

Do not retry these as-is; the next performance step needs semantic decoded
blocks or an ARMv7 translation tier rather than more scalar hot-path nibbling.

## Goal E — Presentation budget

Status: separate from CPU optimization.

Measure texture upload, interpolation synthesis, and `SDL_RenderPresent` waits
independently. Preserve VSYNC and native endpoint correctness; do not hide guest
starvation with frame dropping.

## Research references

- [MAME CPU core concepts](https://wiki.mamedev.org/index.php/Core_Concepts)
- [MAME H8 core](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.h)
- [QEMU TCG translation blocks](https://www.qemu.org/docs/master/devel/tcg.html)
- [Cached interpreter overview](https://emudev.org/2021/01/31/cached-interpreter.html)
- [melonDS JIT/cached-interpreter notes](https://melonds.kuribo64.net/comments.php?id=138)
- [ARM cache-coherency guidance](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-and-self-modifying-code)
- [Mupen64Plus ARM dynarec notes](https://github.com/mupen64plus/mupen64plus-core/blob/master/doc/new_dynarec.mediawiki)

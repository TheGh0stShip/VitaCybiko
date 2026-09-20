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
  OR/XOR/AND, plus unary NOT/EXTU/EXTS/NEG register forms) against a small
  standalone CPU state. It is deliberately not wired into `h8s_cpu_run` yet;
  tests prove the semantics before runtime integration.
- `cybiko-block-scan` now reports blocks fully supported by the semantic
  executor, separately from broader tier-one classification.
- `cybiko-block-scan` now also reports ranked opcode high-byte families that
  make a tier-one block miss semantic-executor coverage. This turns the next
  executor expansion into a measured backlog rather than another broad
  interpreter tweak.
- `cybiko-block-scan` now reports exact unsupported opcode words inside
  tier-one blocks, because high-byte families are too coarse for the remaining
  coverage work.
- The semantic executor now covers valid `0x10`-`0x13` shift/rotate register
  forms (byte/word/long, one/two-bit, logical/arithmetic, and with/without
  carry), with standalone tests for flags, carry flow, register width, and PC
  advancement.
- The semantic executor now covers the remaining valid non-memory
  INC/DEC/ADDS/SUBS families in `0x0a`/`0x0b`/`0x1a`/`0x1b`, including
  byte/word/long flag-setting forms and no-flag ER add/sub-short forms.
- The semantic executor now covers direct register-only bit and word-logic
  families in `0x60`-`0x66` and `0x70`-`0x77`. Memory and compound bit
  operations remain excluded from this tier.
- Tier-one classification now rejects unsupported register subforms instead of
  counting them as executable semantic gaps. On local Classic V2 candidate
  blobs, every tier-one executable block is now covered by the isolated
  semantic executor.
- A first interpreter-equivalence harness now compares isolated semantic block
  execution against `h8s_cpu_step` for representative supported forms across
  multiple register/CCR edge states. This is the required safety gate before
  runtime integration.
- The equivalence harness now includes a generated two-byte opcode-family
  sweep for supported tier-one forms, covering byte immediates and valid
  register/immediate subforms across multiple register/CCR states.
- The equivalence harness now includes generated 4-byte and 6-byte immediate
  sweeps for `0x79`/`0x7a` word/long MOV/ADD/CMP/SUB/OR/XOR/AND forms,
  covering all destination registers and edge immediate values across multiple
  register/CCR states.
- The equivalence harness now includes mixed multi-instruction block checks
  that compare semantic block execution with repeated `h8s_cpu_step` execution
  across flag dependency, register dependency, logic/shift/bit, and immediate
  logic chains.
- Decoded blocks now retain branch-exit metadata for control-transfer
  terminators without executing them: branch kind, opcode, byte length,
  condition, fall-through, and static targets for Bcc/BSR d:8/d:16 and
  absolute JMP/JSR. Indirect, return, trap, and sleep exits are explicitly
  classified. This is the first data-structure step toward a branch-aware
  cached-block or native translation tier.
- A branch resolver now maps decoded static branch exits plus CCR to the next
  PC for Bcc d:8/d:16, BSR d:8/d:16, absolute JMP, and absolute JSR. Dynamic
  exits (return, indirect, trap, sleep) deliberately reject resolution so a
  future dispatcher cannot accidentally chain through state-dependent exits.
- A standalone branch-edge cache now stores resolved static branch exits by
  branch PC and CCR-dependent condition bits. It uses cheap generation
  invalidation, deliberately ignores CCR bits irrelevant to the branch
  decision, accounts hits/misses/evictions, and rejects dynamic exits. This is
  still not wired into runtime dispatch; it is the next safety component for a
  future branch-aware cached-block tier.
- A chain-target helper now combines static branch-edge resolution with the
  decoded block cache and semantic-support gate. It only returns a fast-path
  target for branch/jump exits when the resolved PC is ROM-valid, even-aligned,
  analyzable, cached, and semantic-supported; calls, returns, traps, sleeps,
  indirect exits, and unsupported targets fall back to the safe interpreter
  path.
- A transactional semantic execute-and-exit helper now executes a supported
  straight-line block first, then resolves Bcc/JMP exits using the post-block
  CCR. This prevents the future runtime tier from resolving conditional
  branches with stale flags. Rejected exits leave the caller's state unchanged.
- The CPU now exposes a tested immutable fetch-window helper for boot ROM and
  flash. Future runtime block-cache integration can ask the CPU for a
  const-backed ROM window instead of duplicating machine-specific address
  mapping, and RAM/I/O PCs are rejected before any decoded-block lookup.
- A guarded CPU-level semantic ROM block helper now executes a narrow Bcc-only
  cached block from immutable boot/flash when there is enough cycle budget and
  no pending unmasked interrupt. It updates registers, CCR, PC, and cycle count
  as one bounded operation.
- `h8s_cpu_run` now attempts that guarded Bcc-only semantic ROM block fast path
  before falling back to the interpreter. Timer/completion debts are charged for
  the whole accepted block, and the old one-instruction path remains unchanged
  whenever any guard rejects the cached block. The fast path is also suppressed
  when it could cross the existing end-of-run peripheral synchronization
  boundary.
- The guarded runtime fast path now also accepts static absolute JMP exits.
  Bcc exits remain ROM-window-relative, while JMP @aa:24 exits are treated as
  absolute machine addresses and must still land in immutable boot ROM or flash.
- The CPU now records semantic fast-path accepted blocks, accepted cycles, and
  guarded rejects. These counters make future Vita/host smoke runs measurable
  instead of guessing whether decoded-block execution is being used.
- The emulator API and `cybiko-smoke` now expose those counters, so firmware
  smoke/performance runs print semantic fast-path block, cycle, and reject
  counts alongside CPU time and frame activity.
- The CPU now keeps a tiny negative cache for immutable-ROM PCs that are known
  not to be eligible for the guarded semantic fast path. This prevents the
  runtime from repeatedly paying decoded-block support checks at hot unsupported
  PCs. On the locally staged Classic V2 candidate smoke command
  (`emu_rom.bin`, `emu_cyos.bin`, `emu_flash.bin`, 600 frames), CPU time moved
  from 3.986120 s to a corrected 3.779400 s with conditional-branch safety
  tightened. The smoke counters were `blocks=3460`, `cycles=11721`,
  `rejects=110559225`, and `cached_rejects=110559017`, showing that nearly
  every semantic fast-path rejection is a repeated static reject. The speedup
  comes from making repeated static rejects cheaper. This is a narrow
  dispatch-cost fix, not proof that the current semantic tier is broad enough
  for Vita smoothness.
- The negative cache deliberately does not remember state-dependent conditional
  branch target failures. A regression test covers the same PC rejecting when
  CCR takes an out-of-window Bcc target, then succeeding when CCR falls through
  inside immutable ROM. Only state-independent unsupported blocks/exits are
  cached.
- After a cached state-independent reject, `h8s_cpu_run` now backs off semantic
  fast-path probing for a 128-cycle interpreter window. This does not
  change guest semantics because the fast path is optional and skipped probes
  execute through the normal interpreter. On the same locally staged Classic V2
  candidate smoke, 16/32/64-cycle backoff windows all passed but still spent
  5.71/5.58/5.50 CPU seconds respectively. A 128-cycle window passed with
  `active=599`, `changed_frames=5`, `blocks=220057`, `cycles=556359`,
  `rejects=229188`, and `cached_rejects=91655` over 600 frames while reducing
  host CPU time to 0.561577 s. A real 256-cycle window also passed after
  widening the counter type, but measured 0.572431 s with fewer active frames
  (`active=596`) in the same smoke, so 128 is the current measured gate. The
  next optimization must use this telemetry to either broaden safe semantic
  block execution or make the backoff adaptive, not return to every-cycle
  semantic probing.
- The emulator API and `cybiko-smoke` now also report
  `semantic_fast_backoff_skips`, the number of optional semantic probes skipped
  while the cached-reject backoff was active. The Classic V2 600-frame smoke at
  the fixed 128-cycle gate reported `semantic_fast_backoff_skips=11731784`,
  proving that avoided failed probes are a material part of the current speedup
  and must remain visible in Vita/host logs.
- The Vita `performance.csv` now logs per-window semantic fast-path deltas:
  accepted blocks, accepted cycles, guarded rejects, cached rejects, and
  backoff skips. This is required for physical Vita/Vita3K diagnosis because
  Xtreme or Classic slowdowns can now be separated into CPU fast-path coverage,
  repeated reject/probe overhead, audio queue starvation, render/present stalls,
  and interpolation work from the same runtime trace.

Rejected follow-up experiment on 2026-09-20:

- An adaptive cached-reject backoff that ramped from 16 to 128 cycles and reset
  on semantic-block success preserved the `h8s_cpu` focused test and Classic V2
  smoke activity, but regressed the same 600-frame smoke to 4.467410 CPU
  seconds and produced repeated unmapped reads around PC `0x12831A`/`0x128322`.
  Keep the measured fixed 128-cycle window until a broader block executor or a
  PC-local policy can be validated. A global adaptive streak is too sensitive
  to phase changes in CyOS boot.
- `cybiko-block-scan` now reports branch-exit distributions and top static
  branch targets. It also reports chainable static edges and how many of those
  edges land on semantic-supported decoded blocks. This turns branch-aware
  cached-block work into a measurable target instead of guessing from aggregate
  stop counts.

Local block-scan coverage, max 32 instructions per candidate start:

| Image | Avg insns | Tier1 blocks | Tier1 prefix insns | Semantic blocks | Semantic insns | Stop branch | Stop prefix | Stop unsupported |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic V1 boot | 13.19 | 5,451 | 128,763 | 5,077 | 120,583 | 11,634 | 0 | 0 |
| Classic V1 flash | 9.29 | 47,638 | 490,464 | 32,340 | 120,014 | 250,552 | 0 | 0 |
| Classic V2 boot | 11.91 | 4,835 | 100,540 | 4,275 | 90,118 | 12,384 | 0 | 0 |
| Classic V2 flash | 5.65 | 21,645 | 75,613 | 19,756 | 33,738 | 123,488 | 0 | 0 |
| Xtreme boot | 8.11 | 3,425 | 40,759 | 2,810 | 29,043 | 14,591 | 0 | 0 |
| Xtreme flash | 8.68 | 55,432 | 631,234 | 39,631 | 276,777 | 246,250 | 0 | 0 |

The scan shows branch boundaries now dominate; unsupported and prefix length
decoding are no longer the blocker. The next decoded-block step should begin
executing cache entries that contain only the classified straight-line forms.
This is deliberately tracked as an optimization goal, not as a runtime speed
claim: the Vita frontend still runs the interpreter until the semantic
execution tier is wired into `h8s_cpu_run`.

Semantic executor gap scan, max 32 instructions per candidate start, using the
locally available Classic V2 candidate blobs:

| Image | Semantic gap blocks | Unsupported insns in tier-one blocks | Top gap families |
| --- | ---: | ---: | --- |
| Classic V2 boot (`emu_rom.bin`) | 560 | 896 | `0x11` 21.54%, `0x10` 16.18%, `0x12` 13.50%, `0x1a` 9.93%, `0x64` 9.93% |
| Classic V2 flash (`emu_flash.bin`) | 15,418 | 19,525 | `0x0b` 7.73%, `0x13` 6.20%, `0x10` 5.47%, `0x11` 5.45%, `0x60` 5.19% |
| Classic V2 flash 512K (`emu_flash_512k.bin`) | 14,980 | 18,772 | `0x0b` 6.02%, `0x10` 5.69%, `0x11` 5.67%, `0x13` 5.44%, `0x60` 5.40% |
| Classic V2 CyOS (`emu_cyos.bin`) | 1,889 | 2,362 | `0x73` 19.81%, `0x10` 19.22%, `0x11` 13.63%, `0x64` 9.48%, `0x72` 7.62% |

Next semantic-executor targets, in order:

1. Use the expanded equivalence harness to guide a larger branch-aware cached
   block or ARMv7 translation tier rather than adding more one-opcode hot-path
   probes or reintroducing the rejected semantic-runtime hook.
2. If a future runtime block tier is attempted, it must include the branch/hot
   prefix groups that dominate the executed opcode profile; straight-line
   semantic-only immutable blocks are too narrow to repay their lookup cost.
3. Extend host scanning/profiling to rank branch-exit kinds and static target
   reuse, then use that data to decide whether a C cached-interpreter tier or
   Vita ARMv7 translation backend has the better payoff.

Current Classic V2 branch-exit scan, max 32 instructions per candidate start:

| Image | Branch stops | Conditional | Static targets | In-ROM even targets | Top branch kinds |
| --- | ---: | ---: | ---: | ---: | --- |
| Boot (`emu_rom.bin`) | 12,384 | 8,553 | 10,439 | 9,826 | Bcc8 7,313; JSR abs24 1,605; return 1,295 |
| CyOS (`emu_cyos.bin`) | 123,488 | 54,773 | 101,342 | 58,176 | Bcc8 46,553; JSR abs24 44,143; return 13,462 |
| DataFlash (`emu_flash.bin`) | 250,935 | 168,977 | 201,555 | 109,266 | Bcc8 158,926; indirect 25,866; return 16,084 |

The branch data shows a pure straight-line semantic tier is structurally too
narrow: even the boot/CyOS images repeatedly exit on branch-heavy control flow.
The next implementation target should use the static metadata to design a
branch-aware block tier with explicit exits for conditional fall-through/target,
call/return/indirect exits, and event deadlines.

Branch resolver gate:

- `h8s_block_resolve_static_branch` resolves every Bcc condition from CCR to
  fall-through or target and resolves static BSR/JMP/JSR exits to their target.
- Unit tests cover unconditional/never, equality, sign/overflow, signed
  greater/less, d:8/d:16 forms, absolute jump/call targets, and rejection of
  RTS/RTE/TRAPA/indirect/SLEEP exits.

Branch edge-cache gate:

- `h8s_branch_edge_cache_t` is a 512-entry direct-mapped cache for resolved
  static branch edges. It is intentionally separate from the runtime CPU loop
  until the block-dispatch integration has equivalence coverage.
- The CCR cache key includes only C/V/Z/N, so unrelated H-bit changes do not
  create avoidable misses.
- Unit tests cover miss-to-hit reuse, CCR-key reuse/difference, generation
  clear, collision eviction, and rejection of dynamic branch exits.
- `h8s_block_cache_get_chain_target` is the first explicit branch-aware
  dispatcher primitive. Unit tests cover returning a cached semantic target,
  edge-cache reuse, block-cache reuse, and rejecting unsupported fallback
  targets without executing them. Static BSR/JSR call targets are deliberately
  rejected until the runtime block tier models call-side effects and stack/link
  ordering.
- `h8s_execute_semantic_block_exit` is the first execute+exit primitive for the
  branch-aware tier. Unit tests prove that conditional exits use CCR after
  semantic block execution, not stale entry flags, and that rejected static
  calls do not partially mutate the supplied state.
- `h8s_cpu_get_immutable_fetch_window` is the runtime mapping gate. Unit tests
  cover mirrored boot ROM, model-profile flash, and rejection of mutable
  on-chip RAM plus I/O space.
- `h8s_cpu_try_execute_semantic_rom_block` is the first CPU-facing runtime
  experiment for the decoded block tier. Unit tests cover successful Bcc
  execution from boot ROM plus rejection of mutable RAM, insufficient cycle
  budget, and pending unmasked IRQs.
- The first frame-runner integration test compares `h8s_cpu_run` using the
  semantic ROM fast path against repeated `h8s_cpu_step` for the same ROM block,
  including PC, registers, CCR, cycle count, I/O flag, and timer/completion
  debt.
- A synchronization-boundary regression test proves that the fast path does not
  skip the existing `sync_peripherals` callback at the end of a bounded run.
- A JMP @aa:24 frame-runner equivalence test covers the absolute-target path
  and caught the distinction between window-relative branch offsets and absolute
  H8S jump addresses.
- Runtime fast-path counter tests cover accepted block/cycle accounting and
  rejection accounting for mutable RAM, insufficient cycle budget, and pending
  unmasked IRQ guards.
- Emulator-level tests cover public fast-path stats retrieval, and the host
  smoke binary builds with fast-path counter reporting enabled.

Current Classic V2 static chain-edge scan:

| Image | Static chain edges | Conditional edges | Unconditional edges | In-ROM even edges | Semantic target edges |
| --- | ---: | ---: | ---: | ---: | ---: |
| Boot (`emu_rom.bin`) | 18,992 | 17,106 | 1,886 | 18,379 | 3,810 |
| CyOS (`emu_cyos.bin`) | 156,115 | 109,546 | 46,569 | 112,949 | 29,700 |
| DataFlash (`emu_flash.bin`) | 370,532 | 337,954 | 32,578 | 278,241 | 44,684 |

The static chain-edge scan shows that a branch-aware tier must keep a cheap
dispatcher fallback for non-semantic target blocks; a naive always-chain
semantic path would leave most static edges uncovered. This favors a small C
cached-interpreter dispatcher/edge cache before attempting ARMv7 native code.

Do not make semantic block execution the default Vita runtime path until the
guarded runtime experiment proves an actual speedup without breaking
cross-firmware smoke/equivalence gates.

After adding shift/rotate semantics, local Classic V2 coverage moved to:

| Image | Semantic blocks | Semantic insns | Gap blocks | Unsupported insns in tier-one blocks | New top gap families |
| --- | ---: | ---: | ---: | ---: | --- |
| Classic V2 boot (`emu_rom.bin`) | 4,522 | 90,835 | 313 | 424 | `0x1a` 20.99%, `0x64` 20.99%, `0x1b` 12.03%, `0x73` 8.49%, `0x0b` 6.13% |
| Classic V2 flash (`emu_flash.bin`) | 34,099 | 81,455 | 13,436 | 16,277 | `0x0b` 9.27%, `0x60` 6.23%, `0x62` 5.30%, `0x0a` 5.25%, `0x74` 4.75% |
| Classic V2 flash 512K (`emu_flash_512k.bin`) | 33,973 | 81,329 | 12,998 | 15,713 | `0x0b` 7.20%, `0x60` 6.45%, `0x62` 5.49%, `0x74` 4.92%, `0x65` 4.87% |
| Classic V2 CyOS (`emu_cyos.bin`) | 20,310 | 35,860 | 1,335 | 1,471 | `0x73` 31.82%, `0x64` 15.23%, `0x72` 12.24%, `0x74` 5.57%, `0x70` 5.44% |

After adding the non-memory INC/DEC/ADDS/SUBS forms, local Classic V2 coverage
moved again to:

| Image | Semantic blocks | Semantic insns | Gap blocks | Unsupported insns in tier-one blocks | New top gap families |
| --- | ---: | ---: | ---: | ---: | --- |
| Classic V2 boot (`emu_rom.bin`) | 4,642 | 91,141 | 193 | 274 | `0x64` 32.48%, `0x73` 13.14%, `0x1a` 7.30%, `0x1f` 7.30%, `0x17` 5.84% |
| Classic V2 flash (`emu_flash.bin`) | 34,636 | 83,251 | 12,899 | 15,576 | `0x0b` 8.31%, `0x60` 6.51%, `0x62` 5.53%, `0x74` 4.96%, `0x65` 4.92% |
| Classic V2 flash 512K (`emu_flash_512k.bin`) | 34,510 | 83,125 | 12,461 | 15,012 | `0x60` 6.75%, `0x0b` 6.10%, `0x62` 5.74%, `0x74` 5.15%, `0x65` 5.10% |
| Classic V2 CyOS (`emu_cyos.bin`) | 20,339 | 35,899 | 1,306 | 1,432 | `0x73` 32.68%, `0x64` 15.64%, `0x72` 12.57%, `0x74` 5.73%, `0x70` 5.59% |

After adding direct register bit/word logic, local Classic V2 coverage moved to:

| Image | Semantic blocks | Semantic insns | Gap blocks | Unsupported insns in tier-one blocks | New top gap families |
| --- | ---: | ---: | ---: | ---: | --- |
| Classic V2 boot (`emu_rom.bin`) | 4,787 | 91,710 | 48 | 95 | `0x1a` 21.05%, `0x1f` 21.05%, `0x17` 16.84%, `0x0b` 12.63%, `0x13` 10.53% |
| Classic V2 flash (`emu_flash.bin`) | 43,124 | 115,681 | 4,411 | 4,785 | `0x0b` 27.04%, `0x0a` 13.08%, `0x0f` 9.49%, `0x17` 8.28%, `0x1b` 8.17% |
| Classic V2 flash 512K (`emu_flash_512k.bin`) | 42,998 | 115,555 | 3,973 | 4,221 | `0x0b` 21.70%, `0x0f` 10.76%, `0x0a` 10.42%, `0x17` 9.38%, `0x1b` 9.26% |
| Classic V2 CyOS (`emu_cyos.bin`) | 21,552 | 38,101 | 93 | 93 | `0x0b` 44.09%, `0x0a` 17.20%, `0x10` 13.98%, `0x0f` 7.53%, `0x1b` 6.45% |

Exact unsupported-opcode scan after the register bit/word-logic expansion:

| Image | Top exact unsupported opcodes |
| --- | --- |
| Classic V2 boot (`emu_rom.bin`) | `0x1a40` 18.95%, `0x1f40` 18.95%, `0x0b40` 12.63%, `0x17c0` 12.63%, `0x0a46` 6.32% |
| Classic V2 flash (`emu_flash.bin`) | `0x0b2b` 9.13%, `0x0a61` 6.83%, `0x0bb1` 4.83%, `0x0f00` 1.13%, `0x0a43` 0.59% |
| Classic V2 flash 512K (`emu_flash_512k.bin`) | `0x0b2b` 4.38%, `0x0a61` 3.34%, `0x0bb1` 2.49%, `0x0f00` 1.28%, `0x0a43` 0.66% |
| Classic V2 CyOS (`emu_cyos.bin`) | `0x1020` 10.75%, `0x0a6e` 5.38%, `0x0b64` 4.30%, `0x0bb4` 4.30%, `0x0bb8` 4.30% |

The exact scan suggests the next action is classification tightening, not blind
semantic expansion: many top forms do not match the interpreter's supported
subop masks for INC/DEC/ADDS/SUBS or shift/rotate. Treat them as block
boundaries unless verified against the H8S manual and interpreter.

After tightening tier-one classification, local Classic V2 candidate scans show
the semantic executor covers every executable tier-one block:

| Image | Tier-one blocks | Tier-one prefix insns | Semantic blocks | Semantic insns | Gap blocks | Unsupported insns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic V2 boot (`emu_rom.bin`) | 4,787 | 99,353 | 4,787 | 91,710 | 0 | 0 |
| Classic V2 flash (`emu_flash.bin`) | 43,124 | 391,383 | 43,124 | 115,681 | 0 | 0 |
| Classic V2 flash 512K (`emu_flash_512k.bin`) | 42,998 | 386,154 | 42,998 | 115,555 | 0 | 0 |
| Classic V2 CyOS (`emu_cyos.bin`) | 21,552 | 75,025 | 21,552 | 38,101 | 0 | 0 |

This does not mean runtime speed is fixed. It means the isolated executor's
eligibility predicate is now honest enough to move to equivalence testing and
careful runtime integration experiments.

Initial interpreter-equivalence gate:

- Added a test harness that writes a single instruction to on-chip RAM, executes
  it through the existing `h8s_cpu_step` interpreter, executes the same bytes as
  a one-instruction semantic block, and compares all ER registers, CCR, and PC
  delta.
- Current matrix covers 15 representative forms across three edge-value
  register banks and four CCR states (180 comparisons total):
  byte/word/long register ALU, MOV.L, INC/DEC, NEG, shift/rotate, register bit
  ops, immediate bit ops, byte immediate, word immediate, and long immediate.
- Added a generated sweep over supported two-byte opcode families. It covers
  representative low-byte combinations for register ALU/MOV/CMP, MOV.L/CMP.L,
  INC/DEC/ADDS/SUBS, unary, shift/rotate, register bit ops, immediate bit ops,
  and every byte-immediate high byte with edge immediate values. The generated
  portion currently checks 724 opcode encodings across twelve register/CCR
  state combinations (8,688 interpreter-vs-semantic comparisons).
- Added generated sweeps for supported `0x79` word-immediate and `0x7a`
  long-immediate forms. They cover MOV/ADD/CMP/SUB/OR/XOR/AND, all eight
  destination registers, and zero/one/sign-boundary/all-ones immediates across
  twelve register/CCR state combinations (6,720 additional
  interpreter-vs-semantic comparisons).
- Added mixed multi-instruction semantic block equivalence tests over four
  straight-line dependency chains, each run across three register banks and
  four CCR states. These now exercise PC advancement, CCR carry-over, and
  inter-instruction register dependencies at the decoded-block level.
- Focused `h8s_block` test passes with this matrix. This is now enough to
  justify a guarded runtime semantic-block experiment, but not enough to claim
  a Vita performance win until measured against the interpreter.
- Release-mode host testing exposed a signed-overflow bug in the isolated
  semantic `DEC.L` path. The path now uses unsigned arithmetic and preserves
  the H8S overflow flag condition for `0x80000000 -> 0x7fffffff`.

Executed opcode profile gate:

- `CYBIKO_OPCODE_PROFILE=ON` now builds a host-only profiler that dumps H8S
  opcode high-byte counts at process exit; normal release/Vita builds leave it
  disabled. It now also reports dynamic static-branch execution/taken counts
  and a direct-mapped top target sample for Bcc/BSR/JMP/JSR, return,
  indirect, trap, and sleep exits.
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

Dynamic branch profile on the locally staged Classic V2 600-frame smoke:

| Branch kind | Executed | Taken | Notes |
| --- | ---: | ---: | --- |
| Bcc d:8 | 5,393,726 | 3,942,735 | Main branch-aware tier target; ~73% taken |
| Bcc d:16 | 86,653 | 47,022 | Secondary conditional path |
| BSR d:8 | 33,452 | 33,452 | Direct call edge |
| BSR d:16 | 19 | 19 | Rare |
| JMP abs24 | 423 | 423 | Rare static exit |
| JSR abs24 | 584,470 | 584,470 | Important static call edge |
| Indirect JMP/JSR | 42,163 | 42,163 | Needs exit-to-dispatch, not static chaining |
| Return/RTE | 654,985 | 654,985 | Hot enough to require a return/stack-aware exit plan |
| Sleep | 1,297 | 1,297 | Event scheduler boundary |

Top sampled dynamic branch targets included `0x001598` (562,841),
`0x0061e0` (481,591), `0x002418` (278,288), and `0x0061ea`
(198,809). This is much stronger evidence than the static scan: the next
runtime experiment must prioritize Bcc d:8 taken/fall-through dispatch and hot
JSR/static-target linking, while treating returns, indirect jumps/calls, sleep,
and event deadlines as explicit exits back to the dispatcher. A branch-aware
tier that ignores returns would miss a larger dynamic exit class than direct
JSR abs24.

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
- A compile-time guarded immutable-ROM semantic block runtime hook passed the
  opt-in Release host suite but did not improve the locally available Classic
  V2 600-frame smoke. Baseline Release measured about 0.309 CPU seconds; the
  semantic hook measured about 0.330 CPU seconds on the same workload. The hook
  was removed. Do not retry this exact straight-line semantic-only runtime
  probe; the next tier needs branch-aware cached blocks or native ARMv7
  translation with materially larger hot-opcode coverage.

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
- [MAME H8 execution core](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.cpp)
- [MAME H8S/2000 device wrapper](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8s2000.cpp)
- [Renesas H8/300H software manual](https://www.renesas.com/)
- [H8/300 programming manual mirror](https://docs.alexrp.com/h8300/)
- [QEMU TCG translation blocks](https://www.qemu.org/docs/master/devel/tcg.html)
- [Cached interpreter overview](https://emudev.org/2021/01/31/cached-interpreter.html)
- [QEMU translator internals](https://www.qemu.org/docs/master/devel/tcg.html)
- [Differential emulator-instruction testing example](https://arxiv.org/abs/2105.14273)
- [CPU emulator testing methodology](https://rpaleari.github.io/)
- [Interpreter-guided differential JIT compiler unit testing](https://hal.science/)
- [melonDS JIT/cached-interpreter notes](https://melonds.kuribo64.net/comments.php?id=138)
- [ARM cache-coherency guidance](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-and-self-modifying-code)
- [Mupen64Plus ARM dynarec notes](https://github.com/mupen64plus/mupen64plus-core/blob/master/doc/new_dynarec.mediawiki)

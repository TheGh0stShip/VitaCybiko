# VitaCybiko optimization goals

These are the remaining engineering goals derived from physical performance
logs and upstream research. They are measurable and do not substitute frame
drops for emulation speed.

Start new work from [`EXECUTION.md`](EXECUTION.md). This file is the detailed
optimization ledger; it is not permission to repeat rejected experiments.

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
- The semantic executor now covers the register-only prefixed long logic forms
  `0x01f0 0x64xx`/`0x65xx`/`0x66xx` (`OR.L`/`XOR.L`/`AND.L`). The analyzer
  still rejects `0x0100` long memory moves for semantic execution because those
  can touch RAM/I/O and must remain explicit memory exits until a memory-aware
  block tier exists.
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
- The semantic branch resolver now matches the interpreter for BHI/BLS
  conditions: BHI requires both C and Z clear, while BLS is true when C or Z is
  set. The previous resolver ignored Z for these two conditions, which could
  send the optional semantic ROM fast path down an interpreter-inconsistent
  branch. With MAME-reference local fixtures, the same 600-frame Xtreme smoke
  that previously fell into unmapped `0xF00000` execution now passes with
  `active=600`, `pc=4A3C40`, `semantic_fast_blocks=874500`, and
  `semantic_fast_cycles=2205420`. This is a correctness fix that also removes
  the largest current Xtreme host-smoke blocker.
- After the BHI/BLS fix, the fixed cached-reject backoff was retested at 128,
  256, 384, and 512 cycles. A 256-cycle window preserved the three-model smoke
  gates and reduced repeated cached-reject probe overhead on Xtreme. The final
  Release host gate measured Classic V1 1.02 s, Classic V2 0.38 s, and Xtreme
  2.25 s for 600 frames. Host wall times have visible run-to-run variance, so
  this is a smoke benchmark gate, not a physical-Vita smoothness claim.

Rejected follow-up experiment on 2026-09-20:

- Branch-chaining multiple semantic ROM blocks inside one `h8s_cpu_run`
  fast-path attempt was prototyped with transactional rollback tests. It did
  not materially improve the three-model host smoke after the BHI/BLS fix and
  added extra correctness risk around rejected absolute-jump targets, so it was
  removed. Keep future branch-aware work at the decoded-block/JIT design level
  with full equivalence gates rather than adding ad-hoc chaining to the current
  optional fast path.
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
| Classic V1 boot | 13.19 | 5,435 | 128,529 | 5,435 | 121,705 | 11,634 | 0 | 0 |
| Classic V1 flash | 9.29 | 44,599 | 461,587 | 44,599 | 168,823 | 250,552 | 0 | 0 |
| Classic V2 boot | 11.91 | 4,800 | 99,413 | 4,800 | 91,774 | 12,384 | 0 | 0 |
| Classic V2 flash | 9.10 | 43,124 | 391,383 | 43,124 | 115,681 | 250,935 | 0 | 0 |
| Xtreme boot | 8.11 | 3,386 | 39,512 | 3,386 | 30,847 | 14,591 | 0 | 0 |
| Xtreme flash | 8.68 | 51,692 | 599,073 | 51,692 | 321,850 | 246,250 | 0 | 0 |

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
  opcode high-byte counts and the top exact 16-bit opcodes at process exit;
  normal release/Vita builds leave it disabled. It now also reports dynamic
  static-branch execution/taken counts and a direct-mapped top target sample
  for Bcc/BSR/JMP/JSR, return, indirect, trap, and sleep exits. The profiler
  now also ranks the second word of hot `0x0100` and `0x01f0` prefixed
  instructions so memory-form work can target measured addressing modes rather
  than broad prefix families.
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

Top exact 16-bit opcodes over the same 600-frame smoke runs:

| Image | #1 | #2 | #3 | #4 | #5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic V1 | 0100 8,369,121 | 1173 4,630,836 | 0b03 1,436,545 | 1072 1,402,748 | 0fc2 1,295,891 |
| Classic V2 | 0100 1,832,567 | 47fa 730,287 | 735a 728,733 | 0b03 706,174 | 1175 706,120 |
| Xtreme | 0100 5,283,437 | 0b03 2,060,564 | 0f80 1,951,666 | 470c 1,941,254 | 01f0 1,683,705 |

Exact opcode profiling changes the next optimization target: any branch-aware
cached-interpreter or ARMv7 translation tier that still exits on the hot
`0x01xx` prefix forms will leave the largest executed class in the interpreter.
The first useful design checkpoint is therefore memory-aware handling for
`0x0100` long MOV plus the already-hot branch/call/return exits. The
register-only `0x01f0` long logic subset is now covered by the isolated
semantic executor, but this does not by itself improve runtime speed until a
broader branch-aware block dispatcher uses the coverage.

Top `0x0100` second words from the same 600-frame smoke runs:

| Image | #1 | #2 | #3 | #4 | #5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic V1 | 7820 1,153,481 | 6903 1,085,519 | 6982 1,077,587 | 6f75 662,810 | 6ff5 458,592 |
| Classic V2 | 69c2 187,036 | 7820 177,872 | 6945 176,538 | 6b00 134,989 | 6f75 94,241 |
| Xtreme | 6f75 985,484 | 6ff5 606,280 | 6f42 427,808 | 6f73 421,133 | 6ff3 393,799 |

For Xtreme, the hot `0x0100` forms are overwhelmingly long memory moves with
16-bit displacement (`0x6fxx`), followed by absolute (`0x6b00`) and register
indirect (`0x69xx`) forms. A safe memory-aware block tier should therefore
start with guarded 32-bit reads/writes where the effective address resolves to
plain RAM or immutable ROM/flash, and must exit before RAM writes that could
affect code, memory-mapped I/O, DMA-visible regions, or peripheral
synchronization. This matches QEMU-style translated memory fast/slow paths:
fast only when the address-space guard proves ordinary memory, otherwise fall
back to the existing interpreter/bus path.

Effective-address profiling for `0x0100` shows the immediate bus-routing
opportunity before a full memory-aware block tier:

| Image | Read fast page | Read external RAM | Read on-chip RAM | Write fast page | Write external RAM | Write on-chip RAM |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Classic V1 | 3,989,093 | 3,988,381 | 1,562,437 | 2,804,625 | 2,804,625 | 12,959 |
| Classic V2 | 1,281,383 | 956,919 | 317,992 | 512,773 | 512,723 | 5,810 |
| Xtreme | 2,970,498 | 2,482,715 | 1,000,551 | 1,136,542 | 1,136,467 | 559,602 |

On-chip RAM below `0xFFFC00` is plain storage, but pages adjacent to the I/O
window cannot be fully mapped in the 4 KiB page table. The bus now has an
inline guarded fast path for reads/writes wholly inside that plain on-chip RAM
subrange while keeping `0xFFFC00` and above on the slow peripheral router. Host
gates passed after the change: `test_h8s_cpu`, `test_h8s_block`,
`test_emulator`, full `ctest` 17/17, and three-model 600-frame smoke measured
Classic V1 0.99 s, Classic V2 0.37 s, Xtreme 3.64 s. The new `address_bus`
test explicitly covers the `0xFFFC00` boundary so plain on-chip RAM stays on
the fast path while accesses that reach peripheral space still synchronize via
the slow router. Repeated Xtreme-only runs were 3.66/3.62/4.21 s, so this is a
safe hot-path cleanup with noisy host timing, not proof of physical Vita
smoothness.

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
- A frame-local countdown cache for the next timer/DMA deadline preserved the
  host unit suite and scheduler equivalence test, but did not improve the
  three-model smoke gate. Classic V1/V2 stayed around 1.02 s/0.38 s, while
  Xtreme measured 3.73 s on the first three-model run and 3.74/3.92/3.01 s on
  repeated Xtreme-only runs. The patch was removed. Do not retry simple
  `cybiko_run_frame` deadline memoization without proving the CPU batches are
  actually long enough for it to amortize the extra branch/state tracking.
- Header-only timer8/timer16 deadline helpers for the frame scheduler preserved
  timer tests, scheduler equivalence, full `ctest`, and three-model smoke, but
  did not demonstrate a stable speedup. The first Xtreme smoke measured 3.59 s,
  while repeated Xtreme-only runs measured 3.65/3.93/4.37 s. The patch was
  removed. Do not duplicate timer deadline logic in headers without a stronger
  benchmark signal or a structural reduction in deadline queries.

Do not retry these as-is; the next performance step needs semantic decoded
blocks or an ARMv7 translation tier rather than more scalar hot-path nibbling.

Semantic fast-path reject telemetry was added after the timer-helper rejection
so future work can stop guessing from the aggregate reject counter. The host
smoke binary and Vita `performance.csv` now report rejects split by guard, IRQ,
immutable-window miss, cached static reject, unsupported block, unsupported
exit, cycle budget, branch resolution, and target-window failures. The focused
CPU tests cover these counters.

First 600-frame direct host smoke after adding reason counters:

| Model | CPU seconds | Blocks | Cycles | Rejects | Cached | Backoff skips | Dominant reasons |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Classic V2 | 0.436124 | 141,315 | 371,070 | 322,589 | 100,648 | 25,765,783 | window 107,933; cached 100,648; unsupported block 97,699; unsupported exit 15,222 |
| Xtreme | 4.964638 | 489,324 | 1,243,043 | 121,315 | 87,832 | 22,484,992 | cached 87,832; window 32,149; unsupported block 798; unsupported exit 122 |

Implication: Xtreme should not receive another broad isolated semantic-opcode
expansion first; unsupported semantic coverage is now a tiny fraction of its
fast-path rejects. Xtreme needs a structurally different dispatcher/translation
path that avoids repeated cached-reject probes and/or makes immutable-window
transitions cheaper. Classic V2 still shows enough unsupported block/exit
rejects that a branch-aware, memory-aware cached-interpreter tier can pay off
there, but it must be measured separately from the Xtreme path.

The first reject-reason-driven optimization applies the existing fixed
semantic-probe backoff immediately after state-independent misses instead of
waiting for a second cached reject. The change is semantics-neutral because the
semantic ROM fast path is optional; skipped probes fall back to the normal
interpreter. Focused CPU tests now assert first-miss backoff for immutable
window, unsupported block, unsupported exit, and target-window rejects.

Gate after this change:

- full host suite: 17/17 passed;
- three-model smoke passed: Classic V1 0.96 s, Classic V2 0.39 s, Xtreme
  5.20 s on the wrapper run;
- direct repeat smokes: Classic V2 0.468719/0.364147/0.362801 s; Xtreme
  4.991744/4.673404/4.944293 s.

The reason counters show the intended effect. Classic V2 total fast rejects
fell from 322,589 to about 102,150, with window rejects down from 107,933 to
about 445 and unsupported block/exit down from 97,699/15,222 to about
11,722/1,333. Xtreme total fast rejects fell from 121,315 to 87,966, with
window rejects down from 32,149 to 133. Cached rejects remain the dominant
Xtreme reason, so the next structural target is still a broader
dispatcher/translation tier rather than another scalar timer tweak.

The next measured cached-reject step uses a longer 1024-cycle backoff only
after a cached static reject. First-time static misses remain at the accepted
256-cycle window above. This follows the cached-interpreter/QEMU-style rule
that a proven cache miss should not repeatedly pay the full lookup path while
hot code is still in the same phase.

Gate after the cached-reject-specific backoff:

- focused CPU tests passed;
- full host suite: 17/17 passed;
- three-model smoke passed: Classic V1 1.13 s, Classic V2 0.53 s, Xtreme
  5.17 s on the wrapper run;
- direct repeat smokes: Classic V2 0.463759/0.356248/0.353124 s; Xtreme
  4.863801/4.470016/4.670583 s.

The reason counters show the intended reduction. Classic V2 total fast rejects
fell from about 102,150 to about 31,500, with cached rejects down from about
88,180 to about 23,830. Xtreme total fast rejects fell from 87,966 to 23,275,
with cached rejects down from 87,603 to 22,961. Accepted semantic fast blocks
also fell substantially, so this is a probe-overhead reduction, not a substitute
for the broader branch-aware/translation tier still needed for Vita smoothness.

A final bounded cached-reject backoff check moved the cached-reject-specific
window from 1024 to 2048 cycles. This further reduces repeated cached-reject
probe overhead but also cuts accepted semantic fast blocks again, so this should
be treated as the end of backoff tuning unless physical Vita logs contradict
the host trend.

Gate after the 2048-cycle cached-reject backoff:

- focused CPU tests passed;
- full host suite: 17/17 passed;
- three-model smoke passed: Classic V1 0.96 s, Classic V2 0.38 s, Xtreme
  5.11 s on the wrapper run;
- direct repeat smokes: Classic V2 0.465761/0.351684 s; Xtreme
  5.082200/4.411509 s.

The reason counters show Classic V2 total fast rejects around 17.5k and Xtreme
around 11.8k. Xtreme cached rejects fell to 11,538, but accepted semantic fast
blocks also fell to 51,191 from 93,637 at the 1024 gate. Do not keep increasing
the backoff as a substitute for real coverage; the next meaningful optimization
must make more hot PCs executable through a branch-aware cached interpreter or
ARMv7 translation tier.

To make that next tier concrete, `CYBIKO_OPCODE_PROFILE=ON` now also reports
the hottest semantic fast-path reject PCs by reason. This is host-only
instrumentation; normal release/Vita builds keep the profiler compiled out.

600-frame profile evidence with the 2048 cached-reject gate:

| Model | Top semantic reject PCs |
| --- | --- |
| Xtreme | `0x0076c2` cached 5,046; `0x0076c0` cached 1,166; `0x004a50` cached 781; `0x0076b8` cached 579; `0x004a58` cached 424; `0x005920` cached 419 |
| Classic V2 | `0x0061f0` cached 1,516; `0x001598` cached 851; `0x118612` cached 750; `0x0015a0` cached 620; `0x0061ee` cached 473; `0x118616` cached 259 |

The Xtreme hot reject PCs cluster in boot ROM, while Classic V2 spans boot ROM
and flash. The next implementation should disassemble/analyze these exact
blocks and add branch-aware or memory-aware cached execution for their concrete
exit patterns. Do not broaden the semantic executor blindly; use the hot reject
PC list to decide which unsupported exit or memory form will repay its
correctness risk.

`cybiko-block-scan` now has an `--inspect` mode for those exact PCs. Inspecting
the Xtreme hot rejects showed several branch-only static blocks that were
previously rejected solely because the semantic block predicate required at
least one straight-line instruction:

- `0x0076c0`: branch-only `BRA/Bcc8` to `0x0076b2` or fall-through
  `0x0076c2`;
- `0x0076b8`: branch-only `BCC/BHS d:8`;
- `0x0061ee` on Classic V2: branch-only `BRA/Bcc8`;
- `0x0015a0` on Classic V2: branch-only loop branch.

The semantic fast path now accepts zero-instruction static Bcc/JMP blocks while
still rejecting returns, calls, traps, sleeps, and indirect exits. One-cycle
semantic block exits are allowed when the caller supplies enough cycle budget.
Tests cover branch-only Bcc execution, return rejection, CPU-level branch-only
fast-path execution, and the adjusted cycle-budget reject.

Gate after branch-only static exit support:

- focused `h8s_block` and `h8s_cpu` tests passed;
- full host suite: 17/17 passed;
- three-model smoke passed: Classic V1 0.93 s, Classic V2 0.40 s, Xtreme
  4.28 s on the wrapper run;
- direct Xtreme smoke: 4.385596 s with accepted semantic blocks rising from
  51,191 to 71,141.

The profile confirms the branch-only Bcc PCs fell out of the Xtreme top reject
list, but `0x0076c2` became the dominant remaining cached reject because it is a
return boundary. Do not fake returns; the next branch-aware tier must model
call/return stack/link effects explicitly or keep returns as interpreter exits.

Rejected follow-up experiment: a guarded CPU-only semantic RTS path was tested
for blocks ending in `RTS`, with the stack read limited to plain RAM/on-chip RAM
below I/O space and focused equivalence tests against `h8s_cpu_step`. It
preserved the focused CPU tests, but Xtreme 600-frame smoke regressed/noised to
5.865057/4.873302/5.423801 s while only raising accepted semantic blocks from
71,141 to 72,373. The patch was removed. Keep returns as interpreter exits
until a broader call/return-aware tier can amortize stack-visible control flow;
do not reintroduce isolated RTS handling as a standalone fast path.

The `--inspect` path now annotates common memory-form instructions so the next
memory tier can distinguish plain RAM/ROM candidates from MMIO hazards. Current
Xtreme hot-block inspection shows:

- `0x005920`: unsupported `0x2a84` is `MOV.B @0xffff84,R2L`, an on-chip
  I/O/MMIO byte read. This must remain an explicit interpreter/peripheral
  boundary unless the tier models sync and side effects.
- `0x004a50`: unsupported `0x0100 0x6b00 ...` is a prefixed absolute long
  memory form before a static `JSR @0x0076b0`. This is a better candidate for a
  guarded memory-aware block tier, but only when the effective address is proven
  to map to ordinary RAM/ROM and not MMIO.

This reinforces the next implementation order: add guarded memory-form support
for plain RAM/ROM only, keep MMIO forms as exits, and keep returns/calls as
interpreter exits until a proper call/return-aware tier exists.

The bus now exposes tested `bus_is_plain_read_range` and
`bus_is_plain_write_range` helpers. They accept complete accesses that map to
ordinary read/write memory pages or plain on-chip RAM below `0xfffc00`, reject
MMIO such as `0xffff84`, reject page-crossing accesses, and reject writes to
ROM. This is the guard future memory-aware block execution must use before it
directly reads/writes memory. It mirrors the QEMU-style RAM/ROM fast path vs
MMIO slow path split: safe ordinary memory can be fast, but device-visible
addresses remain interpreter exits.

The bus now also exposes tested `bus_plain_read_ptr` and
`bus_plain_write_ptr` helpers. They return live backing-storage pointers only
after the plain-range guards pass, return read-only pointers for ROM, return
writable pointers for RAM/on-chip RAM, and return `NULL` for MMIO or
page-crossing slow paths. Future memory-aware semantic blocks should use these
pointers for direct big-endian loads/stores only after resolving an effective
address and proving the access remains ordinary memory.

The first test-only memory-form semantic helper now covers the `0x0100/0x6b`
`MOV.L` absolute d:16 form through `bus_plain_read_ptr` /
`bus_plain_write_ptr`. It performs 4-byte big-endian loads/stores, updates
`ERn`/CCR like the interpreter, rejects MMIO and ROM writes, and has a focused
interpreter-equivalence test. It is deliberately not wired into runtime yet:
the next implementation must prove block-level PC/branch/call behavior before
executing memory forms inside cached blocks. Validation for this checkpoint:
focused `h8s_block` passed, full host suite 17/17 passed, and three-model smoke
passed with Classic V1 1.25 s, Classic V2 0.81 s, Xtreme 5.84 s. Result:
accepted as a correctness primitive only; not a runtime performance win yet.

Rejected follow-up experiment: runtime-wiring the plain-memory helper as a
prefix path before `JSR_ABS24` was tested in two forms: first for a single
`MOV.L` instruction, then for the inspected `0x004a50` shape
(`MOV.L` plus semantic register/immediate instructions before `JSR`). Focused
CPU equivalence passed after constructing one-instruction semantic temporaries,
but the Xtreme 600-frame counters did not change
(`semantic_fast_blocks=71141`, `semantic_fast_cycles=172273`), while wrapper
smoke showed noisy/worse timings up to 6.37 s. The runtime wiring was removed.
The evidence says the current 600-frame Xtreme path is dominated by cached
reject/backoff behavior and return/call boundaries, not by this isolated
prefix. Do not reintroduce this prefix path unless profiling proves the target
PC is reached in the measured window and accepted-block counters increase.

Rejected follow-up experiment: folding the hot `MOV.L` flag update
(`set_nz_l` plus clear-V) into a single helper was tested because the opcode
profile shows millions of `0x0100` long-move forms and most addresses are
ordinary RAM/on-chip RAM fast pages. CPU CCR tests and the full host suite
passed, but three-model smoke worsened/noised to Classic V1 1.67 s, Classic V2
0.66 s, Xtreme 7.61 s, and direct Xtreme was 6.545663 s with unchanged semantic
counters. The patch was removed. Treat broad MOV.L micro-cleanups as
insufficient until a profiler shows instruction helper overhead, not
reject/backoff/call-return structure, is the limiting cost.

Rejected follow-up experiment: a guarded whole-call fast path for semantic
`JSR_ABS24` callers was prototyped after upstream research on direct block
chaining/call-return-aware traces. The prototype delayed the JSR stack write
until success, simulated only immutable-ROM semantic blocks, required the
callee to reach `RTS` with the expected stack pointer, then committed the return
address and CPU state. A focused `ADD; JSR; semantic callee; RTS` equivalence
test passed, but the three-model smoke worsened/noised to Classic V1 1.90 s,
Classic V2 1.06 s, Xtreme 7.73 s, and direct Xtreme still showed unchanged
accepted counters (`semantic_fast_blocks=71141`,
`semantic_fast_cycles=172273`). The patch was removed. The likely issue is
that the real hot Xtreme callee loop exceeds the safe per-event cycle window,
so this trace shape rejects in practice. Future call/return work needs an event
deadline-aware trace scheduler or a lower-overhead translated loop body, not a
single bounded call wrapper.

## Xtreme-specific diagnosis

Public/upstream research did not reveal a known "Cybiko Xtreme is slow in
emulators" consensus. MAME's Cybiko driver documents the family as historically
undertested and lacking software-loading facilities, while public hardware
summaries describe Xtreme as the faster second-generation model: H8S/2323 at
18 MHz, more RAM, larger ROM, improved audio and OS. Therefore the current
VitaCybiko Xtreme lag should be treated as a local core/scheduler mismatch, not
as an unavoidable property of Xtreme emulation.

Host-only scheduler profiling (`CYBIKO_OPCODE_PROFILE=ON`) now records CPU-run
chunk histograms and next-event deadline histograms. A 600-frame Xtreme run
showed the critical difference:

- Xtreme: `scheduler_run_calls=14251994`, `io_breaks=14251642`.
  The scheduler was forced into tiny chunks for almost the whole boot window:
  `scheduler_chunk_1=531756`, `scheduler_chunk_2=336183`,
  `scheduler_chunk_3_4=12997902`, and only `scheduler_chunk_129_plus=385737`.
- Classic V2: `scheduler_run_calls=1655462`, `io_breaks=1654165`.
  Classic V2 mostly runs in large chunks:
  `scheduler_chunk_129_plus=1614488`, with only a few thousand sub-16-cycle
  chunks.

That makes Xtreme about 8.6x more fragmented at the scheduler boundary during
the same 600-frame smoke. The next-event histogram mirrors the chunk histogram,
so the fragmentation is driven by timer/DMA/peripheral deadlines, not merely by
arbitrary frontend frame slicing.

The Xtreme instruction/branch profile explains why small semantic-block
extensions keep failing:

- `0x0100` prefixed long-memory forms dominate (`5,283,436` executions), with
  hot forms such as `0x6f75`, `0x6ff5`, `0x6f42`, `0x6f73`, `0x6ff3`,
  `0x6b00`, `0x6901`, and `0x6981`.
- Most `0x0100` accesses are ordinary memory, not device I/O:
  read fast pages `2,970,498`, external RAM `2,482,715`, on-chip RAM
  `1,000,551`; write fast pages `1,136,542`, external RAM `1,136,467`.
- Branch traffic is also extreme: `branch_bcc8=14,404,968`,
  `branch_jsr_abs24=1,335,730`, and `branch_return=1,412,802`.
- Hot branch targets cluster around the same ROM call/return loop:
  `0x004a50`, `0x004a5e`, `0x00591c`, `0x0076b0`, `0x0076b2`,
  `0x0076bc`, `0x0076c2`.

Conclusion: Xtreme probably needs a bespoke Xtreme-aware execution tier, but not
a separate emulator fork. The target should be a model-aware high-throughput
tier for Xtreme's hot ROM/event pattern:

1. keep shared Classic/Xtreme correctness infrastructure;
2. add an Xtreme-targeted event-deadline-aware trace scheduler that can execute
   repeated 1-4 cycle deadlines without re-entering the full outer scheduler
   millions of times;
3. add a trace/JIT/cached-interpreter body for the hot H8S loop families
   (`0x0100` MOV.L variants, Bcc loops, JSR/RTS pairs) while preserving explicit
   exits for MMIO, IRQ, DMA/timer compare deadlines, and LCD/audio side effects;
4. treat frontend frame interpolation as presentation polish only after this
   Xtreme core path is real-time.

Do not keep nibbling at single opcodes unless the Xtreme scheduler histogram
changes or a profiler shows a specific helper dominating inside those tiny
deadline windows.

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

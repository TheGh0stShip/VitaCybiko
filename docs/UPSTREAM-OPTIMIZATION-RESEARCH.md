# Upstream optimization research

Research performed 2026-09-20 against the current MAME H8 core, the upstream
`daberkow/cybiko-c-emulator` C port, and the Cybiko Java emulator.

## Findings

* The upstream C port is still a switch-based H8S interpreter. It has no
  decoded basic-block cache or native translation layer; this Vita fork adds
  memory-region fetch caching and event-bounded batches, but retains the same
  per-instruction decode model.
* MAME's H8 implementation keeps a prefetched instruction word (`PIR`) and
  explicit previous/next PC state, and separates full and partial execution
  paths. That is a useful model for reducing sequential fetch overhead, but it
  cannot be copied directly: VitaCybiko has different peripheral callbacks,
  self-modifying RAM requirements, and a cycle/event scheduler that is covered
  by equivalence tests.
* MAME's CPU documentation explains why this matters: interpreter fetch and
  decode overhead becomes dominant as guest clocks rise, while dynamic
  recompilation removes repeated decode work by translating and caching basic
  blocks. The H8S/2323 Xtreme's 18.432 MHz clock is exactly the case where the
  current interpreter becomes too expensive on a Vita Cortex-A9.
* The current physical logs support that diagnosis: Xtreme reached 5.88 guest
  FPS and Classic V2 18.06 FPS in the latest run, with zero producer-observed
  audio underruns. This is not primarily an SDL audio starvation problem.

## Optimization order

1. Add a correctness-gated one-word instruction prefetch for immutable ROM
   regions. Invalidate it on branches, interrupts, reset, and any write to a
   mapped executable RAM page. Keep the existing slow path for peripherals and
   self-modifying RAM.
2. Add a decoded basic-block cache for immutable firmware ROM. Blocks must end
   at branches, interrupts, I/O accesses, and scheduler event boundaries;
   mutable RAM remains interpreted. Compare CPU/RAM/LCD/audio fingerprints and
   event-scheduler state against the existing interpreter after every change.
3. Only after the block cache is proven, evaluate an ARMv7-A native backend for
   the small subset of straight-line H8S operations used by CyOS. A full JIT is
   platform-specific and substantially larger than the current project; it
   must not be presented as a drop-in “optimization” without invalidation and
   interrupt handling.
4. Separately profile SDL texture upload and present waits. Do not trade away
   VSYNC or frame correctness to hide a CPU deficit with frame dropping.

## Sources

* [MAME H8 core](https://github.com/mamedev/mame/blob/master/src/devices/cpu/h8/h8.h)
  (prefetch state and full/partial execution declarations).
* [MAME CPU core concepts](https://wiki.mamedev.org/index.php/Core_Concepts)
  (interpreter overhead, block translation and code-cache tradeoffs).
* [Common dynarec optimizations](https://emudev.org/2021/02/01/Dynarec)
  (flat block lookup, invalidation and synchronization cautions).
* [Upstream Cybiko Java emulator](https://github.com/daberkow/cybiko-java-emulator)
  (model coverage and known CyOS V2 firmware behavior).

The roadmap is deliberately staged: a block cache/JIT is the route capable of
the order-of-magnitude Xtreme improvement the logs require, while prefetch is
the next small, independently verifiable step.

# PS Vita architecture requirements

Treat the PS Vita target architecture as a prerequisite for every target-side
change. Vita is little-endian ARMv7-A (Cortex-A9), 32-bit ARM/Thumb with an
ILP32 ABI: `int`, `long`, pointers and `size_t` are 32-bit. It is not AArch64.

Check the installed toolchain and linked ELF attributes for floating-point ABI
consistency; never infer the ABI from CPU capability. Vita artifacts must pass
`scripts/verify_vita_abi.sh`.

Development hosts are distinct targets. Linux/WSL x86-64 normally uses LP64;
Windows x64 uses LLP64. Host probes must preserve original 32-bit disk, wire,
checksum and pointer-token semantics with explicit-width types, endian-safe
access and layout assertions. Do not globally pack structs or truncate real
host pointers. Check ARM alignment and calling conventions before trusting a
host test, and label host-only defects as such. Host or emulator success never
establishes physical Vita correctness.

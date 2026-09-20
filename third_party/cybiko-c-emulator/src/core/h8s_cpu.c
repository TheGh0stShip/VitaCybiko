/*
 * Hitachi H8S/2323 CPU emulator core.
 * Ported from H8SCpu.java.
 */
#include "core/h8s_cpu.h"
#include "core/address_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define CPU_INLINE static inline __attribute__((always_inline))
#define CPU_LIKELY(x)   __builtin_expect(!!(x), 1)
#define CPU_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define CPU_INLINE static inline
#define CPU_LIKELY(x)   (x)
#define CPU_UNLIKELY(x) (x)
#endif

typedef enum {
    SEM_REJECT_GUARD = 1,
    SEM_REJECT_IRQ,
    SEM_REJECT_WINDOW,
    SEM_REJECT_CACHED,
    SEM_REJECT_UNSUPPORTED_BLOCK,
    SEM_REJECT_UNSUPPORTED_EXIT,
    SEM_REJECT_CYCLE_BUDGET,
    SEM_REJECT_BRANCH_RESOLVE,
    SEM_REJECT_TARGET
} semantic_reject_profile_reason_t;

#ifdef CYBIKO_OPCODE_PROFILE
#define BRANCH_PROFILE_TARGET_SLOTS 4096
#define SEMANTIC_REJECT_PROFILE_SLOTS 4096
#define WINDOW_PROFILE_SLOTS 4096
#define MUTABLE_NONPLAIN_PROFILE_SLOTS 4096
static uint64_t opcode_profile_hi[256];
static uint64_t opcode_profile_exact[65536];
static uint64_t prefix0100_profile_op2[65536];
static uint64_t prefix01f0_profile_op2[65536];
static uint64_t prefix0100_addr_region[2][8];
static uint64_t prefix0100_addr_fast[2];
static uint64_t branch_profile_kind[12];
static uint64_t branch_profile_taken_kind[12];
static uint32_t branch_profile_target_tag[BRANCH_PROFILE_TARGET_SLOTS];
static uint64_t branch_profile_target_count[BRANCH_PROFILE_TARGET_SLOTS];
static uint32_t semantic_reject_profile_tag[SEMANTIC_REJECT_PROFILE_SLOTS];
static uint8_t semantic_reject_profile_reason[SEMANTIC_REJECT_PROFILE_SLOTS];
static uint64_t semantic_reject_profile_count[SEMANTIC_REJECT_PROFILE_SLOTS];
static uint32_t window_profile_tag[WINDOW_PROFILE_SLOTS];
static uint16_t window_profile_op0[WINDOW_PROFILE_SLOTS];
static uint16_t window_profile_op1[WINDOW_PROFILE_SLOTS];
static uint64_t window_profile_count[WINDOW_PROFILE_SLOTS];
static uint32_t mutable_nonplain_pc[MUTABLE_NONPLAIN_PROFILE_SLOTS];
static uint32_t mutable_nonplain_address[MUTABLE_NONPLAIN_PROFILE_SLOTS];
static uint16_t mutable_nonplain_op[MUTABLE_NONPLAIN_PROFILE_SLOTS];
static uint8_t mutable_nonplain_write[MUTABLE_NONPLAIN_PROFILE_SLOTS];
static uint64_t mutable_nonplain_count[MUTABLE_NONPLAIN_PROFILE_SLOTS];
static bool opcode_profile_registered;

static void opcode_profile_dump(void) {
    for (unsigned i = 0; i < 256; ++i) {
        if (opcode_profile_hi[i])
            fprintf(stderr, "opcode_hi_%02x=%llu\n", i,
                    (unsigned long long)opcode_profile_hi[i]);
    }
    bool opcode_printed[65536] = {0};
    for (unsigned rank = 0; rank < 24; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < 65536; ++i) {
            if (!opcode_printed[i] && opcode_profile_exact[i] > best_count) {
                best_count = opcode_profile_exact[i];
                best = i;
            }
        }
        if (!best_count) break;
        opcode_printed[best] = true;
        fprintf(stderr, "opcode_exact_top%02u_op=0x%04x count=%llu\n",
                rank + 1, best, (unsigned long long)best_count);
    }
    bool prefix0100_printed[65536] = {0};
    for (unsigned rank = 0; rank < 16; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < 65536; ++i) {
            if (!prefix0100_printed[i] && prefix0100_profile_op2[i] > best_count) {
                best_count = prefix0100_profile_op2[i];
                best = i;
            }
        }
        if (!best_count) break;
        prefix0100_printed[best] = true;
        fprintf(stderr, "prefix0100_op2_top%02u_op=0x%04x count=%llu\n",
                rank + 1, best, (unsigned long long)best_count);
    }
    bool prefix01f0_printed[65536] = {0};
    for (unsigned rank = 0; rank < 16; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < 65536; ++i) {
            if (!prefix01f0_printed[i] && prefix01f0_profile_op2[i] > best_count) {
                best_count = prefix01f0_profile_op2[i];
                best = i;
            }
        }
        if (!best_count) break;
        prefix01f0_printed[best] = true;
        fprintf(stderr, "prefix01f0_op2_top%02u_op=0x%04x count=%llu\n",
                rank + 1, best, (unsigned long long)best_count);
    }
    static const char *access_names[] = {"read", "write"};
    static const char *region_names[] = {
        "boot", "flash", "external_ram", "on_chip_ram",
        "lcd", "io_or_unmapped", "page_cross", "unknown"
    };
    for (unsigned access = 0; access < 2; ++access) {
        if (prefix0100_addr_fast[access])
            fprintf(stderr, "prefix0100_addr_%s_fast_page=%llu\n",
                    access_names[access],
                    (unsigned long long)prefix0100_addr_fast[access]);
        for (unsigned region = 0; region < 8; ++region) {
            if (prefix0100_addr_region[access][region])
                fprintf(stderr, "prefix0100_addr_%s_%s=%llu\n",
                        access_names[access], region_names[region],
                        (unsigned long long)prefix0100_addr_region[access][region]);
        }
    }
    static const char *names[] = {
        "none", "bcc8", "bcc16", "bsr8", "bsr16", "jmp_abs24",
        "jsr_abs24", "indirect", "return", "trap", "sleep", "unknown"
    };
    for (unsigned i = 0; i < sizeof(branch_profile_kind) / sizeof(branch_profile_kind[0]); ++i) {
        if (branch_profile_kind[i])
            fprintf(stderr, "branch_%s=%llu taken=%llu\n", names[i],
                    (unsigned long long)branch_profile_kind[i],
                    (unsigned long long)branch_profile_taken_kind[i]);
    }
    bool printed[BRANCH_PROFILE_TARGET_SLOTS] = {0};
    for (unsigned rank = 0; rank < 12; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < BRANCH_PROFILE_TARGET_SLOTS; ++i) {
            if (!printed[i] && branch_profile_target_count[i] > best_count) {
                best_count = branch_profile_target_count[i];
                best = i;
            }
        }
        if (!best_count) break;
        printed[best] = true;
        fprintf(stderr, "branch_target_top%02u_pc=0x%06x count=%llu\n",
                rank + 1, branch_profile_target_tag[best],
                (unsigned long long)best_count);
    }
    bool reject_printed[SEMANTIC_REJECT_PROFILE_SLOTS] = {0};
    static const char *reject_names[] = {
        "none", "guard", "irq", "window", "cached", "unsupported_block",
        "unsupported_exit", "cycle_budget", "branch_resolve", "target"
    };
    for (unsigned rank = 0; rank < 16; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < SEMANTIC_REJECT_PROFILE_SLOTS; ++i) {
            if (!reject_printed[i] && semantic_reject_profile_count[i] > best_count) {
                best_count = semantic_reject_profile_count[i];
                best = i;
            }
        }
        if (!best_count) break;
        reject_printed[best] = true;
        uint8_t reason = semantic_reject_profile_reason[best];
        const char *name = reason < sizeof(reject_names) / sizeof(reject_names[0]) ?
            reject_names[reason] : "unknown";
        fprintf(stderr, "semantic_reject_top%02u_pc=0x%06x reason=%s count=%llu\n",
                rank + 1, semantic_reject_profile_tag[best], name,
                (unsigned long long)best_count);
    }
    bool window_printed[WINDOW_PROFILE_SLOTS] = {0};
    for (unsigned rank = 0; rank < 16; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < WINDOW_PROFILE_SLOTS; ++i) {
            if (!window_printed[i] && window_profile_count[i] > best_count) {
                best_count = window_profile_count[i];
                best = i;
            }
        }
        if (!best_count) break;
        window_printed[best] = true;
        fprintf(stderr, "window_reject_top%02u_pc=0x%06x op0=0x%04x op1=0x%04x count=%llu\n",
                rank + 1, window_profile_tag[best],
                window_profile_op0[best], window_profile_op1[best],
                (unsigned long long)best_count);
    }
    bool nonplain_printed[MUTABLE_NONPLAIN_PROFILE_SLOTS] = {0};
    for (unsigned rank = 0; rank < 16; ++rank) {
        unsigned best = 0;
        uint64_t best_count = 0;
        for (unsigned i = 0; i < MUTABLE_NONPLAIN_PROFILE_SLOTS; ++i) {
            if (!nonplain_printed[i] && mutable_nonplain_count[i] > best_count) {
                best_count = mutable_nonplain_count[i];
                best = i;
            }
        }
        if (!best_count) break;
        nonplain_printed[best] = true;
        fprintf(stderr, "mutable_nonplain_top%02u_pc=0x%06x op=0x%04x address=0x%06x access=%s count=%llu\n",
                rank + 1, mutable_nonplain_pc[best], mutable_nonplain_op[best],
                mutable_nonplain_address[best],
                mutable_nonplain_write[best] ? "write" : "read",
                (unsigned long long)best_count);
    }
}

CPU_INLINE void opcode_profile_record(uint16_t op) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    opcode_profile_hi[op >> 8]++;
    opcode_profile_exact[op]++;
}

CPU_INLINE void prefix01_profile_record(uint8_t lo, uint16_t op2) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    if (lo == 0x00)
        prefix0100_profile_op2[op2]++;
    else if (lo == 0xf0)
        prefix01f0_profile_op2[op2]++;
}

CPU_INLINE void prefix0100_addr_profile_record(const address_bus_t *bus, bool write,
                                               uint32_t address) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    address &= 0xffffff;
    unsigned access = write ? 1u : 0u;
    unsigned offset = address & 4095u;
    if (offset > 4092u) {
        prefix0100_addr_region[access][6]++;
        return;
    }
    if (write) {
        if (bus->write_pages[address >> 12])
            prefix0100_addr_fast[access]++;
    } else if (bus->read_pages[address >> 12]) {
        prefix0100_addr_fast[access]++;
    }

    const cybiko_machine_t *m = bus->machine;
    unsigned region = 7;
    if (m) {
        if (address <= m->boot_end) {
            region = 0;
        } else if (m->flash_size && address >= m->flash_base && address <= m->flash_end) {
            region = 1;
        } else if (address >= m->ram_base && address <= m->ram_end) {
            region = 2;
        } else if (address >= m->on_chip_base && address < m->on_chip_base + 0x2400u) {
            region = 3;
        } else if (address >= m->lcd_base && address <= m->lcd_end) {
            region = 4;
        } else {
            region = 5;
        }
    }
    prefix0100_addr_region[access][region]++;
}

CPU_INLINE void branch_profile_record(unsigned kind, bool taken, uint32_t target) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    if (kind >= sizeof(branch_profile_kind) / sizeof(branch_profile_kind[0]))
        kind = sizeof(branch_profile_kind) / sizeof(branch_profile_kind[0]) - 1;
    branch_profile_kind[kind]++;
    if (!taken) return;
    branch_profile_taken_kind[kind]++;
    unsigned index = ((target >> 1) ^ (target >> 13)) & (BRANCH_PROFILE_TARGET_SLOTS - 1);
    if (branch_profile_target_count[index] == 0 || branch_profile_target_tag[index] == target) {
        branch_profile_target_tag[index] = target;
        branch_profile_target_count[index]++;
    }
}
CPU_INLINE void semantic_reject_profile_record(uint32_t pc, unsigned reason) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    pc &= 0xffffff;
    unsigned index = ((pc >> 1) ^ (pc >> 12) ^ (reason * 131u)) &
                     (SEMANTIC_REJECT_PROFILE_SLOTS - 1);
    if (semantic_reject_profile_count[index] == 0 ||
        (semantic_reject_profile_tag[index] == pc &&
         semantic_reject_profile_reason[index] == reason)) {
        semantic_reject_profile_tag[index] = pc;
        semantic_reject_profile_reason[index] = (uint8_t)reason;
        semantic_reject_profile_count[index]++;
    }
}

CPU_INLINE void semantic_window_profile_record(h8s_cpu_t *cpu, uint32_t pc) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    if (!cpu || !cpu->fetch_data || pc < cpu->fetch_base || pc + 3 >= cpu->fetch_end)
        return;
    const uint8_t *p = cpu->fetch_data + (pc - cpu->fetch_base);
    uint16_t op0 = (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
    uint16_t op1 = (uint16_t)(((uint16_t)p[2] << 8) | p[3]);
    unsigned index = ((pc >> 1) ^ (pc >> 12) ^ op0 ^ (op1 << 1)) &
                     (WINDOW_PROFILE_SLOTS - 1);
    if (window_profile_count[index] == 0 ||
        (window_profile_tag[index] == (pc & 0xffffff) &&
         window_profile_op0[index] == op0 &&
         window_profile_op1[index] == op1)) {
        window_profile_tag[index] = pc & 0xffffff;
        window_profile_op0[index] = op0;
        window_profile_op1[index] = op1;
        window_profile_count[index]++;
    }
}

CPU_INLINE void mutable_nonplain_profile_record(uint32_t pc, uint16_t op,
                                                uint32_t address, bool write) {
    if (!opcode_profile_registered) {
        atexit(opcode_profile_dump);
        opcode_profile_registered = true;
    }
    pc &= 0xffffff;
    address &= 0xffffff;
    unsigned index = ((pc >> 1) ^ (pc >> 12) ^ (address >> 1) ^
                      (address >> 13) ^ op ^ (write ? 0x155u : 0x2aau)) &
                     (MUTABLE_NONPLAIN_PROFILE_SLOTS - 1);
    if (mutable_nonplain_count[index] == 0 ||
        (mutable_nonplain_pc[index] == pc &&
         mutable_nonplain_op[index] == op &&
         mutable_nonplain_address[index] == address &&
         mutable_nonplain_write[index] == (write ? 1u : 0u))) {
        mutable_nonplain_pc[index] = pc;
        mutable_nonplain_op[index] = op;
        mutable_nonplain_address[index] = address;
        mutable_nonplain_write[index] = write ? 1u : 0u;
        mutable_nonplain_count[index]++;
    }
}
#else
CPU_INLINE void opcode_profile_record(uint16_t op) {
    (void)op;
}
CPU_INLINE void prefix01_profile_record(uint8_t lo, uint16_t op2) {
    (void)lo; (void)op2;
}
CPU_INLINE void prefix0100_addr_profile_record(const address_bus_t *bus, bool write,
                                               uint32_t address) {
    (void)bus; (void)write; (void)address;
}
CPU_INLINE void branch_profile_record(unsigned kind, bool taken, uint32_t target) {
    (void)kind; (void)taken; (void)target;
}
CPU_INLINE void semantic_reject_profile_record(uint32_t pc, unsigned reason) {
    (void)pc; (void)reason;
}
CPU_INLINE void semantic_window_profile_record(h8s_cpu_t *cpu, uint32_t pc) {
    (void)cpu; (void)pc;
}
CPU_INLINE void mutable_nonplain_profile_record(uint32_t pc, uint16_t op,
                                                uint32_t address, bool write) {
    (void)pc; (void)op; (void)address; (void)write;
}
#endif

CPU_INLINE bool semantic_fast_reject(h8s_cpu_t *cpu, uint64_t *counter,
                                     unsigned profile_reason, uint32_t pc)
{
    cpu->semantic_fast_rejects++;
    if (counter) (*counter)++;
    semantic_reject_profile_record(pc, profile_reason);
    return false;
}

/* ---- CCR bit positions (for Java-style getFlag/setFlag using bit index) ---- */
#define BIT_C  0
#define BIT_V  1
#define BIT_Z  2
#define BIT_N  3
#define BIT_U  4
#define BIT_H  5
#define BIT_UI 6
#define BIT_I  7

/* ---- Trace macro ---- */
#ifdef CYBIKO_TRACE
#define TRACE(cpu, fmt, ...) do { if ((cpu)->tracing) fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while(0)
#else
#define TRACE(cpu, fmt, ...) ((void)0)
#endif

/* ---- Register access helpers ---- */
static inline uint32_t get_er(const h8s_cpu_t *cpu, int n) { return cpu->er[n]; }
static inline void set_er(h8s_cpu_t *cpu, int n, uint32_t v) { cpu->er[n] = v; }

static inline uint16_t get_e(const h8s_cpu_t *cpu, int n) { return (uint16_t)((cpu->er[n] >> 16) & 0xFFFF); }
static inline void set_e(h8s_cpu_t *cpu, int n, uint16_t v) {
    cpu->er[n] = (cpu->er[n] & 0x0000FFFF) | ((uint32_t)v << 16);
}

static inline uint16_t get_r(const h8s_cpu_t *cpu, int n) {
    if (n < 8) return (uint16_t)(cpu->er[n] & 0xFFFF);
    return (uint16_t)((cpu->er[n - 8] >> 16) & 0xFFFF);
}
static inline void set_r(h8s_cpu_t *cpu, int n, uint16_t v) {
    if (n < 8) cpu->er[n] = (cpu->er[n] & 0xFFFF0000u) | v;
    else { int i = n - 8; cpu->er[i] = (cpu->er[i] & 0x0000FFFFu) | ((uint32_t)v << 16); }
}

static inline uint8_t get_rh(const h8s_cpu_t *cpu, int n) { return (uint8_t)((cpu->er[n] >> 8) & 0xFF); }
static inline void set_rh(h8s_cpu_t *cpu, int n, uint8_t v) {
    cpu->er[n] = (cpu->er[n] & 0xFFFF00FFu) | ((uint32_t)v << 8);
}

static inline uint8_t get_rl(const h8s_cpu_t *cpu, int n) { return (uint8_t)(cpu->er[n] & 0xFF); }
static inline void set_rl(h8s_cpu_t *cpu, int n, uint8_t v) {
    cpu->er[n] = (cpu->er[n] & 0xFFFFFF00u) | v;
}

static inline uint8_t get_reg_b(const h8s_cpu_t *cpu, int n) {
    if (n < 8) return get_rh(cpu, n);
    return get_rl(cpu, n - 8);
}
static inline void set_reg_b(h8s_cpu_t *cpu, int n, uint8_t v) {
    if (n < 8) set_rh(cpu, n, v);
    else set_rl(cpu, n - 8, v);
}

/* ---- Flag helpers ---- */
static inline bool get_flag(const h8s_cpu_t *cpu, int bit) { return (cpu->ccr & (1 << bit)) != 0; }
static inline void set_flag(h8s_cpu_t *cpu, int bit, bool v) {
    if (v) cpu->ccr |= (uint8_t)(1 << bit);
    else cpu->ccr &= (uint8_t)~(1 << bit);
}

static inline void set_nz_b(h8s_cpu_t *cpu, int result) {
    uint32_t value = (uint8_t)result;
    cpu->ccr = (uint8_t)((cpu->ccr & ~(CCR_N | CCR_Z)) |
                        ((value >> 4) & CCR_N) | ((value == 0) << BIT_Z));
}
static inline void set_nz_w(h8s_cpu_t *cpu, int result) {
    uint32_t value = (uint16_t)result;
    cpu->ccr = (uint8_t)((cpu->ccr & ~(CCR_N | CCR_Z)) |
                        ((value >> 12) & CCR_N) | ((value == 0) << BIT_Z));
}
static inline void set_nz_l(h8s_cpu_t *cpu, int32_t result) {
    cpu->ccr = (uint8_t)((cpu->ccr & ~(CCR_N | CCR_Z)) |
                        (((uint32_t)result >> 28) & CCR_N) | ((result == 0) << BIT_Z));
}

/* Carry/borrow are 32-bit comparisons, not a 64-bit emulation operation.
 * Build the arithmetic flags once; preserve I/UI/U exactly. */
static inline void set_arithmetic_l(h8s_cpu_t *cpu, uint32_t d, uint32_t s,
                                    uint32_t result, bool subtract) {
    uint32_t overflow = subtract ? ((d ^ s) & (d ^ result))
                                 : ((d ^ result) & (s ^ result));
    cpu->ccr = (uint8_t)((cpu->ccr & (CCR_I | CCR_UI | CCR_U)) |
        ((result >> 28) & CCR_N) | (result == 0 ? CCR_Z : 0) |
        ((overflow >> 30) & CCR_V) |
        (((d ^ s ^ result) >> 23) & CCR_H) |
        (subtract ? d < s : result < d));
}

/* ---- Fetch helpers ---- */
static void cache_instruction_memory(h8s_cpu_t *cpu, uint32_t pc) {
    address_bus_t *b = cpu->bus;
    const cybiko_machine_t *m = b->machine;
    const memory_t *memory = NULL;
    uint32_t base = 0;
    if (pc <= m->boot_end) {
        memory = &b->boot_rom;
        base = pc & ~0x7fffu;
    } else if (pc >= m->ram_base && pc <= m->ram_end) {
        memory = &b->external_ram;
        base = m->ram_base + ((pc - m->ram_base) & ~(m->ram_size - 1));
    } else if (m->flash_size && pc >= m->flash_base && pc <= m->flash_end) {
        memory = &b->flash_rom;
        base = m->flash_base + ((pc - m->flash_base) & ~(m->flash_size - 1));
    } else if (pc >= m->on_chip_base && pc < 0xFFFC00) {
        memory = &b->on_chip_ram;
        base = m->on_chip_base;
    }
    cpu->fetch_data = memory ? memory->data : NULL;
    cpu->fetch_base = base;
    cpu->fetch_end = memory ? base + (uint32_t)memory->size : 0;
    cpu->fetch_immutable = memory == &b->boot_rom || memory == &b->flash_rom;
    cpu->prefetch_valid = false;
    cpu->rom_block_valid = false;
    if (memory == &b->on_chip_ram && cpu->fetch_end > 0xFFFC00)
        cpu->fetch_end = 0xFFFC00;
}

static inline uint16_t fetch16(h8s_cpu_t *cpu) {
    uint32_t pc = cpu->pc & 0xffffff;
    if (CPU_LIKELY(cpu->fetch_immutable && cpu->rom_block_valid &&
                   pc >= cpu->rom_block_base)) {
        uint32_t delta = pc - cpu->rom_block_base;
        unsigned index = delta >> 1;
        if ((delta & 1u) == 0 && index < cpu->rom_block_count) {
            cpu->pc = (pc + 2) & 0xffffff;
            return cpu->rom_block_words[index];
        }
    }
    if (CPU_LIKELY(cpu->prefetch_valid && cpu->prefetch_pc == pc)) {
        uint16_t val = cpu->prefetch_word;
        cpu->pc = (pc + 2) & 0xffffff;
        cpu->prefetch_valid = false;
        return val;
    }
    /* Firmware executes in long contiguous runs from ROM/RAM. Prefer the
     * validated region cache before looking up a 4 KiB bus page; peripheral
     * addresses never enter this range and still use the bus path below. */
    if (CPU_LIKELY(cpu->fetch_data && pc >= cpu->fetch_base && pc + 1 < cpu->fetch_end)) {
        const uint8_t *p = cpu->fetch_data + (pc - cpu->fetch_base);
        cpu->pc = (pc + 2) & 0xffffff;
        uint16_t val = (uint16_t)((p[0] << 8) | p[1]);
        if (cpu->fetch_immutable) {
            uint32_t base = pc & ~(uint32_t)(H8S_ROM_FETCH_BLOCK_WORDS * 2 - 1);
            if (base >= cpu->fetch_base && base + 1 < cpu->fetch_end) {
                unsigned count = (cpu->fetch_end - base) / 2;
                if (count > H8S_ROM_FETCH_BLOCK_WORDS) count = H8S_ROM_FETCH_BLOCK_WORDS;
                const uint8_t *block = cpu->fetch_data + (base - cpu->fetch_base);
                for (unsigned i = 0; i < count; ++i)
                    cpu->rom_block_words[i] = (uint16_t)((block[i * 2] << 8) | block[i * 2 + 1]);
                cpu->rom_block_base = base;
                cpu->rom_block_count = (uint8_t)count;
                cpu->rom_block_valid = true;
            }
        }
        if (cpu->fetch_immutable && cpu->pc + 3 < cpu->fetch_end) {
            cpu->prefetch_pc = cpu->pc;
            cpu->prefetch_word = (uint16_t)((p[2] << 8) | p[3]);
            cpu->prefetch_valid = true;
        }
        return val;
    }
    const uint8_t *page = cpu->bus->read_pages[pc >> 12];
    unsigned offset = pc & 4095;
    if (page && offset <= 4094) {
        cpu->pc = (pc + 2) & 0xffffff;
        return (uint16_t)((page[offset] << 8) | page[offset + 1]);
    }
    if (pc < cpu->fetch_base || pc + 1 >= cpu->fetch_end)
        cache_instruction_memory(cpu, pc);
    uint16_t val;
    if (cpu->fetch_data && pc >= cpu->fetch_base && pc + 1 < cpu->fetch_end) {
        const uint8_t *p = cpu->fetch_data + (pc - cpu->fetch_base);
        val = (uint16_t)((p[0] << 8) | p[1]);
    } else val = bus_read16(cpu->bus, pc);
    cpu->pc = (cpu->pc + 2) & 0xFFFFFF;
    return val;
}
static inline uint32_t fetch32(h8s_cpu_t *cpu) {
    uint16_t hi = fetch16(cpu);
    uint16_t lo = fetch16(cpu);
    return ((uint32_t)hi << 16) | lo;
}

CPU_INLINE bool execute_hot_plain_memory_2b(h8s_cpu_t *cpu, uint16_t op)
{
    uint8_t hi = (uint8_t)(op >> 8);
    if (hi != 0x68 && hi != 0x69 && hi != 0x6c && hi != 0x6d)
        return false;

    uint8_t lo = (uint8_t)op;
    bool write = (lo & 0x80u) != 0;
    unsigned reg = lo & 0xfu;
    unsigned address_reg = (lo >> 4) & 0x7u;
    unsigned bytes = (hi == 0x68 || hi == 0x6c) ? 1u : 2u;
    bool auto_adjust = hi == 0x6c || hi == 0x6d;
    uint32_t address = cpu->er[address_reg] & 0xffffffu;
    if (auto_adjust && write)
        address = (address - bytes) & 0xffffffu;

    if (write) {
        if (!bus_is_plain_write_range(cpu->bus, address, bytes))
            return false;
        uint32_t value = bytes == 1 ? get_reg_b(cpu, (int)reg) :
                                      get_r(cpu, (int)reg);
        if (auto_adjust)
            cpu->er[address_reg] = (cpu->er[address_reg] - bytes) & 0xffffffffu;
        if (bytes == 1) {
            bus_write8(cpu->bus, address, (uint8_t)value);
            set_nz_b(cpu, (int)value);
        } else {
            bus_write16(cpu->bus, address, (uint16_t)value);
            set_nz_w(cpu, (int)value);
        }
    } else {
        const uint8_t *ptr = bus_plain_read_ptr(cpu->bus, address, bytes);
        if (!ptr)
            return false;
        if (auto_adjust)
            cpu->er[address_reg] = (cpu->er[address_reg] + bytes) & 0xffffffffu;
        if (bytes == 1) {
            uint8_t value = ptr[0];
            set_reg_b(cpu, (int)reg, value);
            set_nz_b(cpu, value);
        } else {
            uint16_t value = (uint16_t)((ptr[0] << 8) | ptr[1]);
            set_r(cpu, (int)reg, value);
            set_nz_w(cpu, value);
        }
    }
    set_flag(cpu, BIT_V, false);
    cpu->hot_plain_memory_2b_instructions++;
    return true;
}

CPU_INLINE bool execute_hot_plain_memory_4b(h8s_cpu_t *cpu, uint16_t op)
{
    uint8_t hi = (uint8_t)(op >> 8);
    if (hi != 0x6e && hi != 0x6f)
        return false;

    uint32_t saved_pc = cpu->pc;
    uint32_t saved_prefetch_pc = cpu->prefetch_pc;
    uint16_t saved_prefetch_word = cpu->prefetch_word;
    bool saved_prefetch_valid = cpu->prefetch_valid;

    uint8_t lo = (uint8_t)op;
    uint16_t disp = fetch16(cpu);
    bool write = (lo & 0x80u) != 0;
    unsigned reg = lo & 0xfu;
    unsigned address_reg = (lo >> 4) & 0x7u;
    unsigned bytes = hi == 0x6e ? 1u : 2u;
    uint32_t address =
        (cpu->er[address_reg] + (uint32_t)(int32_t)(int16_t)disp) & 0xffffffu;

    if (write) {
        if (!bus_is_plain_write_range(cpu->bus, address, bytes))
            goto restore_and_fallback;
        uint32_t value = bytes == 1 ? get_reg_b(cpu, (int)reg) :
                                      get_r(cpu, (int)reg);
        if (bytes == 1) {
            bus_write8(cpu->bus, address, (uint8_t)value);
            set_nz_b(cpu, (int)value);
        } else {
            bus_write16(cpu->bus, address, (uint16_t)value);
            set_nz_w(cpu, (int)value);
        }
    } else {
        const uint8_t *ptr = bus_plain_read_ptr(cpu->bus, address, bytes);
        if (!ptr)
            goto restore_and_fallback;
        if (bytes == 1) {
            uint8_t value = ptr[0];
            set_reg_b(cpu, (int)reg, value);
            set_nz_b(cpu, value);
        } else {
            uint16_t value = (uint16_t)((ptr[0] << 8) | ptr[1]);
            set_r(cpu, (int)reg, value);
            set_nz_w(cpu, value);
        }
    }
    set_flag(cpu, BIT_V, false);
    cpu->hot_plain_memory_4b_instructions++;
    return true;

restore_and_fallback:
    cpu->pc = saved_pc;
    cpu->prefetch_pc = saved_prefetch_pc;
    cpu->prefetch_word = saved_prefetch_word;
    cpu->prefetch_valid = saved_prefetch_valid;
    return false;
}

CPU_INLINE bool execute_hot_prefix0100_plain_memory(h8s_cpu_t *cpu, uint16_t op)
{
    if (op != 0x0100)
        return false;

    uint32_t saved_pc = cpu->pc;
    uint32_t saved_prefetch_pc = cpu->prefetch_pc;
    uint16_t saved_prefetch_word = cpu->prefetch_word;
    bool saved_prefetch_valid = cpu->prefetch_valid;

    uint16_t op2 = fetch16(cpu);
    uint8_t hi2 = (uint8_t)(op2 >> 8);
    uint8_t lo2 = (uint8_t)op2;
    bool write = (lo2 & 0x80u) != 0;
    unsigned source_reg = (lo2 >> 4) & 0x7u;
    unsigned reg = lo2 & 0x7u;
    uint32_t address = 0;

    switch (hi2) {
    case 0x69:
        address = cpu->er[source_reg] & 0xffffffu;
        break;
    case 0x6d:
        if (write)
            address = (cpu->er[source_reg] - 4u) & 0xffffffu;
        else
            address = cpu->er[source_reg] & 0xffffffu;
        break;
    case 0x6b:
        if (lo2 & 0x20u)
            address = fetch32(cpu) & 0xffffffu;
        else
            address = (uint32_t)(int32_t)(int16_t)fetch16(cpu);
        break;
    case 0x6f: {
        int16_t disp = (int16_t)fetch16(cpu);
        address = (cpu->er[source_reg] + (uint32_t)(int32_t)disp) & 0xffffffu;
        break;
    }
    default:
        goto restore_and_fallback;
    }

    address &= 0xffffffu;
    prefix0100_addr_profile_record(cpu->bus, write, address);
    if (write) {
        if (!bus_is_plain_write_range(cpu->bus, address, 4))
            goto restore_and_fallback;
        if (hi2 == 0x6d)
            cpu->er[source_reg] = (cpu->er[source_reg] - 4u) & 0xffffffffu;
        uint32_t value = cpu->er[reg];
        bus_write32(cpu->bus, address, value);
        set_nz_l(cpu, (int32_t)value);
    } else {
        const uint8_t *ptr = bus_plain_read_ptr(cpu->bus, address, 4);
        if (!ptr)
            goto restore_and_fallback;
        if (hi2 == 0x6d)
            cpu->er[source_reg] = (cpu->er[source_reg] + 4u) & 0xffffffffu;
        uint32_t value = ((uint32_t)ptr[0] << 24) |
                         ((uint32_t)ptr[1] << 16) |
                         ((uint32_t)ptr[2] << 8) |
                         ptr[3];
        cpu->er[reg] = value;
        set_nz_l(cpu, (int32_t)value);
    }
    set_flag(cpu, BIT_V, false);
    cpu->hot_prefix0100_plain_memory_instructions++;
    return true;

restore_and_fallback:
    cpu->pc = saved_pc;
    cpu->prefetch_pc = saved_prefetch_pc;
    cpu->prefetch_word = saved_prefetch_word;
    cpu->prefetch_valid = saved_prefetch_valid;
    return false;
}

/* ---- Branch condition evaluator ---- */
static bool evaluate_condition(const h8s_cpu_t *cpu, int cond) {
    bool c = get_flag(cpu, BIT_C);
    bool z = get_flag(cpu, BIT_Z);
    bool n = get_flag(cpu, BIT_N);
    bool v = get_flag(cpu, BIT_V);
    switch (cond) {
        case 0x0: return true;
        case 0x1: return false;
        case 0x2: return !c && !z;
        case 0x3: return c || z;
        case 0x4: return !c;
        case 0x5: return c;
        case 0x6: return !z;
        case 0x7: return z;
        case 0x8: return !v;
        case 0x9: return v;
        case 0xA: return !n;
        case 0xB: return n;
        case 0xC: return !(n ^ v);
        case 0xD: return n ^ v;
        case 0xE: return !(z || (n ^ v));
        case 0xF: return z || (n ^ v);
        default: return false;
    }
}

/* ---- Unimplemented opcode handler ---- */
static void unimplemented(h8s_cpu_t *cpu, int op, uint32_t addr) {
    fprintf(stderr, "Unimplemented opcode 0x%04X at PC=0x%06X\n", op, addr);
    (void)cpu;
}

/* ---- Shift helpers ---- */
static void shift_b(h8s_cpu_t *cpu, int rd, bool left, bool logical, int count) {
    int val = get_reg_b(cpu, rd);
    for (int i = 0; i < count; i++) {
        if (left) {
            set_flag(cpu, BIT_C, (val & 0x80) != 0);
            int old = val;
            val = (val << 1) & 0xFF;
            if (!logical) set_flag(cpu, BIT_V, ((old ^ val) & 0x80) != 0);
        } else {
            set_flag(cpu, BIT_C, (val & 1) != 0);
            if (logical) val = (val >> 1) & 0x7F;
            else val = ((int8_t)val >> 1) & 0xFF;
        }
    }
    set_reg_b(cpu, rd, (uint8_t)val);
    set_nz_b(cpu, val);
    if (left && logical) set_flag(cpu, BIT_V, false);
    if (!left) set_flag(cpu, BIT_V, false);
}

static void shift_w(h8s_cpu_t *cpu, int rd, bool left, bool logical, int count) {
    int val = get_r(cpu, rd);
    for (int i = 0; i < count; i++) {
        if (left) {
            set_flag(cpu, BIT_C, (val & 0x8000) != 0);
            int old = val;
            val = (val << 1) & 0xFFFF;
            if (!logical) set_flag(cpu, BIT_V, ((old ^ val) & 0x8000) != 0);
        } else {
            set_flag(cpu, BIT_C, (val & 1) != 0);
            if (logical) val = (val >> 1) & 0x7FFF;
            else val = ((int16_t)val >> 1) & 0xFFFF;
        }
    }
    set_r(cpu, rd, (uint16_t)val);
    set_nz_w(cpu, val);
    if (left && logical) set_flag(cpu, BIT_V, false);
    if (!left) set_flag(cpu, BIT_V, false);
}

static void shift_l(h8s_cpu_t *cpu, int erd, bool left, bool logical, int count) {
    uint32_t val = cpu->er[erd];
    for (int i = 0; i < count; i++) {
        if (left) {
            set_flag(cpu, BIT_C, (val & 0x80000000u) != 0);
            uint32_t old = val;
            val = val << 1;
            if (!logical) set_flag(cpu, BIT_V, ((old ^ val) & 0x80000000u) != 0);
        } else {
            set_flag(cpu, BIT_C, (val & 1) != 0);
            if (logical) val >>= 1;
            else val = (val >> 1) | (val & 0x80000000u);
        }
    }
    cpu->er[erd] = (uint32_t)val;
    set_nz_l(cpu, val);
    if (left && logical) set_flag(cpu, BIT_V, false);
    if (!left) set_flag(cpu, BIT_V, false);
}

/* ---- Rotate helpers ---- */
static void rotate_b(h8s_cpu_t *cpu, int rd, bool left, bool through_carry, int count) {
    int val = get_reg_b(cpu, rd);
    for (int i = 0; i < count; i++) {
        if (left) {
            int msb = (val >> 7) & 1;
            if (through_carry) {
                int oldC = get_flag(cpu, BIT_C) ? 1 : 0;
                set_flag(cpu, BIT_C, msb != 0);
                val = ((val << 1) | oldC) & 0xFF;
            } else {
                set_flag(cpu, BIT_C, msb != 0);
                val = ((val << 1) | msb) & 0xFF;
            }
        } else {
            int lsb = val & 1;
            if (through_carry) {
                int oldC = get_flag(cpu, BIT_C) ? 0x80 : 0;
                set_flag(cpu, BIT_C, lsb != 0);
                val = ((val >> 1) | oldC) & 0xFF;
            } else {
                set_flag(cpu, BIT_C, lsb != 0);
                val = ((val >> 1) | (lsb << 7)) & 0xFF;
            }
        }
    }
    set_reg_b(cpu, rd, (uint8_t)val);
    set_nz_b(cpu, val);
    set_flag(cpu, BIT_V, false);
}

static void rotate_w(h8s_cpu_t *cpu, int rd, bool left, bool through_carry, int count) {
    int val = get_r(cpu, rd);
    for (int i = 0; i < count; i++) {
        if (left) {
            int msb = (val >> 15) & 1;
            if (through_carry) {
                int oldC = get_flag(cpu, BIT_C) ? 1 : 0;
                set_flag(cpu, BIT_C, msb != 0);
                val = ((val << 1) | oldC) & 0xFFFF;
            } else {
                set_flag(cpu, BIT_C, msb != 0);
                val = ((val << 1) | msb) & 0xFFFF;
            }
        } else {
            int lsb = val & 1;
            if (through_carry) {
                int oldC = get_flag(cpu, BIT_C) ? 0x8000 : 0;
                set_flag(cpu, BIT_C, lsb != 0);
                val = ((val >> 1) | oldC) & 0xFFFF;
            } else {
                set_flag(cpu, BIT_C, lsb != 0);
                val = ((val >> 1) | (lsb << 15)) & 0xFFFF;
            }
        }
    }
    set_r(cpu, rd, (uint16_t)val);
    set_nz_w(cpu, val);
    set_flag(cpu, BIT_V, false);
}

static void rotate_l(h8s_cpu_t *cpu, int erd, bool left, bool through_carry, int count) {
    uint32_t val = cpu->er[erd];
    for (int i = 0; i < count; i++) {
        if (left) {
            uint32_t msb = (val >> 31) & 1;
            if (through_carry) {
                uint32_t oldC = get_flag(cpu, BIT_C) ? 1u : 0u;
                set_flag(cpu, BIT_C, msb != 0);
                val = (val << 1) | oldC;
            } else {
                set_flag(cpu, BIT_C, msb != 0);
                val = (val << 1) | msb;
            }
        } else {
            uint32_t lsb = val & 1;
            if (through_carry) {
                uint32_t oldC = get_flag(cpu, BIT_C) ? 0x80000000u : 0u;
                set_flag(cpu, BIT_C, lsb != 0);
                val = (val >> 1) | oldC;
            } else {
                set_flag(cpu, BIT_C, lsb != 0);
                val = (val >> 1) | (lsb << 31);
            }
        }
    }
    cpu->er[erd] = val;
    set_nz_l(cpu, (int32_t)val);
    set_flag(cpu, BIT_V, false);
}

/* ---- Bit operation at memory address ---- */
static void execute_bit_op_at_address(h8s_cpu_t *cpu, uint32_t addr, uint16_t bit_op) {
    int op_type = (bit_op >> 8) & 0xFF;
    int bit_num = (bit_op >> 4) & 0x07;
    int val = bus_read8(cpu->bus, addr);

    switch (op_type) {
        case 0x70: bus_write8(cpu->bus, addr, (uint8_t)(val | (1 << bit_num))); break;
        case 0x71: bus_write8(cpu->bus, addr, (uint8_t)(val ^ (1 << bit_num))); break;
        case 0x72: bus_write8(cpu->bus, addr, (uint8_t)(val & ~(1 << bit_num))); break;
        case 0x73: set_flag(cpu, BIT_Z, (val & (1 << bit_num)) == 0); break;
        case 0x74: if ((val & (1 << bit_num)) != 0) set_flag(cpu, BIT_C, true); break;
        case 0x75: if ((val & (1 << bit_num)) != 0) set_flag(cpu, BIT_C, !get_flag(cpu, BIT_C)); break;
        case 0x76: if ((val & (1 << bit_num)) == 0) set_flag(cpu, BIT_C, false); break;
        case 0x77: set_flag(cpu, BIT_C, (val & (1 << bit_num)) != 0); break;
        case 0x67: {
            if (get_flag(cpu, BIT_C)) val |= (1 << bit_num);
            else val &= ~(1 << bit_num);
            bus_write8(cpu->bus, addr, (uint8_t)val);
            break;
        }
        case 0x60: {
            int rn = (bit_op >> 4) & 0xF;
            int bit = get_reg_b(cpu, rn) & 0x7;
            bus_write8(cpu->bus, addr, (uint8_t)(val | (1 << bit)));
            break;
        }
        case 0x61: {
            int rn = (bit_op >> 4) & 0xF;
            int bit = get_reg_b(cpu, rn) & 0x7;
            bus_write8(cpu->bus, addr, (uint8_t)(val ^ (1 << bit)));
            break;
        }
        case 0x62: {
            int rn = (bit_op >> 4) & 0xF;
            int bit = get_reg_b(cpu, rn) & 0x7;
            bus_write8(cpu->bus, addr, (uint8_t)(val & ~(1 << bit)));
            break;
        }
        case 0x63: {
            int rn = (bit_op >> 4) & 0xF;
            int bit = get_reg_b(cpu, rn) & 0x7;
            set_flag(cpu, BIT_Z, (val & (1 << bit)) == 0);
            break;
        }
        default: unimplemented(cpu, bit_op, cpu->pc - 2); break;
    }
}

/* ======== Forward declarations for decode sub-functions ======== */
CPU_INLINE void decode(h8s_cpu_t *cpu, uint16_t op);

/* ---- MOV.L with absolute address (6B_32) ---- */
static void decode6B_32(h8s_cpu_t *cpu, uint16_t op2, bool profile0100) {
    int lo2 = op2 & 0xFF;
    bool write = (lo2 & 0x80) != 0;
    bool addr24 = (lo2 & 0x20) != 0;
    int rd = lo2 & 0x7;
    uint32_t addr;
    if (addr24) addr = fetch32(cpu) & 0xFFFFFF;
    else { addr = (uint32_t)(int32_t)(int16_t)fetch16(cpu); addr &= 0xFFFFFF; }
    if (profile0100) prefix0100_addr_profile_record(cpu->bus, write, addr);

    if (!write) {
        cpu->er[rd] = bus_read32(cpu->bus, addr);
        set_nz_l(cpu, (int32_t)cpu->er[rd]);
        set_flag(cpu, BIT_V, false);
    } else {
        bus_write32(cpu->bus, addr, cpu->er[rd]);
        set_nz_l(cpu, (int32_t)cpu->er[rd]);
        set_flag(cpu, BIT_V, false);
    }
}

/* ---- MULXS ---- */
static void decode_mulxs(h8s_cpu_t *cpu, int hi2, uint16_t op2) {
    int rs = (op2 >> 4) & 0xF;
    int rd = op2 & 0xF;
    if (hi2 == 0x50) {
        int result = (int8_t)get_reg_b(cpu, rs) * (int8_t)get_r(cpu, rd);
        set_r(cpu, rd, (uint16_t)(result & 0xFFFF));
        set_flag(cpu, BIT_N, (result & 0x8000) != 0);
        set_flag(cpu, BIT_Z, (result & 0xFFFF) == 0);
    } else {
        int32_t result = (int16_t)get_r(cpu, rs) * (int16_t)get_r(cpu, rd);
        cpu->er[rd & 0x7] = (uint32_t)result;
        set_flag(cpu, BIT_N, result < 0);
        set_flag(cpu, BIT_Z, result == 0);
    }
}

/* ---- DIVXS ---- */
static void decode_divxs(h8s_cpu_t *cpu, int hi2, uint16_t op2) {
    int rs = (op2 >> 4) & 0xF;
    int rd = op2 & 0xF;
    if (hi2 == 0x51) {
        int16_t dividend = (int16_t)get_r(cpu, rd);
        int8_t divisor = (int8_t)get_reg_b(cpu, rs);
        set_flag(cpu, BIT_N, false);
        set_flag(cpu, BIT_Z, divisor == 0);
        if (divisor == 0) { /* Result registers are unchanged. */ }
        else {
            int quotient = dividend / divisor;
            int remainder = dividend % divisor;
            set_r(cpu, rd, (uint16_t)(((remainder & 0xFF) << 8) | (quotient & 0xFF)));
            set_flag(cpu, BIT_N, quotient < 0);
        }
    } else {
        rd &= 7;
        int32_t dividend = (int32_t)cpu->er[rd];
        int16_t divisor = (int16_t)get_r(cpu, rs);
        set_flag(cpu, BIT_N, false);
        set_flag(cpu, BIT_Z, divisor == 0);
        if (divisor == 0) { /* Result registers are unchanged. */ }
        else if (dividend == INT32_MIN && divisor == -1) {
            /* Truncated quotient and remainder are both zero. Handle this
             * overflow explicitly, avoiding host UB and a costly 64-bit
             * software divide on Vita for every ordinary DIVXS.W. */
            cpu->er[rd] = 0;
        }
        else {
            int32_t quotient = dividend / divisor;
            int32_t remainder = dividend % divisor;
            cpu->er[rd] = ((uint32_t)(remainder & 0xFFFF) << 16) |
                         (uint32_t)(quotient & 0xFFFF);
            set_flag(cpu, BIT_N, quotient < 0);
        }
    }
}

/* ---- decode0141: EXR operations ---- */
static void decode0141(h8s_cpu_t *cpu, uint16_t op2) {
    int hi2 = (op2 >> 8) & 0xFF;
    if (hi2 >= 4 && hi2 <= 7) cpu->irq_deferred = true;
    switch (hi2) {
        case 0x04: cpu->exr |= (op2 & 0xFF); break;
        case 0x05: cpu->exr ^= (op2 & 0xFF); break;
        case 0x06: cpu->exr &= (op2 & 0xFF); break;
        case 0x07: cpu->exr = (uint8_t)(op2 & 0xFF); break;
        default: unimplemented(cpu, 0x0141, cpu->pc - 2); break;
    }
}

/* ---- decode0140: LDC/STC CCR word-width ---- */
static void decode0140(h8s_cpu_t *cpu, uint16_t op2) {
    int hi2 = (op2 >> 8) & 0xFF;
    if (!(op2 & 0x80)) cpu->irq_deferred = true;
    switch (hi2) {
        case 0x69: {
            int r = (op2 >> 4) & 0x7;
            if ((op2 & 0x80) == 0) cpu->ccr = (uint8_t)(bus_read16(cpu->bus, cpu->er[r]) & 0xFF);
            else bus_write16(cpu->bus, cpu->er[r], cpu->ccr);
            break;
        }
        case 0x6B: {
            bool write = (op2 & 0x80) != 0;
            bool addr24 = (op2 & 0x20) != 0;
            uint32_t addr;
            if (addr24) addr = fetch32(cpu) & 0xFFFFFF;
            else { addr = (uint32_t)(int32_t)(int16_t)fetch16(cpu); addr &= 0xFFFFFF; }
            if (!write) cpu->ccr = (uint8_t)(bus_read16(cpu->bus, addr) & 0xFF);
            else bus_write16(cpu->bus, addr, cpu->ccr);
            break;
        }
        case 0x6D: {
            int r = (op2 >> 4) & 0x7;
            if ((op2 & 0x80) == 0) {
                cpu->ccr = (uint8_t)(bus_read16(cpu->bus, cpu->er[r]) & 0xFF);
                cpu->er[r] += 2;
            } else {
                cpu->er[r] -= 2;
                bus_write16(cpu->bus, cpu->er[r], cpu->ccr);
            }
            break;
        }
        case 0x6F: {
            int r = (op2 >> 4) & 0x7;
            int16_t disp = (int16_t)fetch16(cpu);
            if ((op2 & 0x80) == 0) cpu->ccr = (uint8_t)(bus_read16(cpu->bus, cpu->er[r] + disp) & 0xFF);
            else bus_write16(cpu->bus, cpu->er[r] + disp, cpu->ccr);
            break;
        }
        default: unimplemented(cpu, 0x0140, cpu->pc - 2); break;
    }
}

/* ---- decode0100: MOV.L variants ---- */
static void decode0100(h8s_cpu_t *cpu, uint16_t op2) {
    int hi2 = (op2 >> 8) & 0xFF;
    int lo2 = op2 & 0xFF;
    switch (hi2) {
        case 0x69: {
            int rh = (lo2 >> 4) & 0x7;
            int rl = lo2 & 0x7;
            uint32_t addr = cpu->er[rh];
            prefix0100_addr_profile_record(cpu->bus, (lo2 & 0x80) != 0, addr);
            if ((lo2 & 0x80) == 0) {
                cpu->er[rl] = bus_read32(cpu->bus, addr);
                set_nz_l(cpu, (int32_t)cpu->er[rl]); set_flag(cpu, BIT_V, false);
            } else {
                bus_write32(cpu->bus, addr, cpu->er[rl]);
                set_nz_l(cpu, (int32_t)cpu->er[rl]); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x6B: decode6B_32(cpu, op2, true); break;
        case 0x6D: {
            int rh = (lo2 >> 4) & 0x7;
            int rl = lo2 & 0x7;
            if ((lo2 & 0x80) == 0) {
                uint32_t addr = cpu->er[rh];
                prefix0100_addr_profile_record(cpu->bus, false, addr);
                uint32_t val = bus_read32(cpu->bus, addr);
                cpu->er[rh] += 4;
                cpu->er[rl] = val;
                set_nz_l(cpu, (int32_t)cpu->er[rl]); set_flag(cpu, BIT_V, false);
            } else {
                uint32_t val = cpu->er[rl];
                cpu->er[rh] -= 4;
                prefix0100_addr_profile_record(cpu->bus, true, cpu->er[rh]);
                bus_write32(cpu->bus, cpu->er[rh], val);
                set_nz_l(cpu, (int32_t)val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x6F: {
            int rh = (lo2 >> 4) & 0x7;
            int rl = lo2 & 0x7;
            int16_t disp = (int16_t)fetch16(cpu);
            uint32_t addr = cpu->er[rh] + disp;
            prefix0100_addr_profile_record(cpu->bus, (lo2 & 0x80) != 0, addr);
            if ((lo2 & 0x80) == 0) {
                cpu->er[rl] = bus_read32(cpu->bus, addr);
                set_nz_l(cpu, (int32_t)cpu->er[rl]); set_flag(cpu, BIT_V, false);
            } else {
                bus_write32(cpu->bus, addr, cpu->er[rl]);
                set_nz_l(cpu, (int32_t)cpu->er[rl]); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x78: {
            int r1 = (lo2 >> 4) & 0x7;
            uint16_t op3 = fetch16(cpu);
            int32_t disp = (int32_t)fetch32(cpu);
            int r2 = op3 & 0x7;
            uint32_t addr = cpu->er[r1] + disp;
            prefix0100_addr_profile_record(cpu->bus, (op3 & 0x0080) != 0, addr);
            if ((op3 & 0x0080) == 0) {
                cpu->er[r2] = bus_read32(cpu->bus, addr);
                set_nz_l(cpu, (int32_t)cpu->er[r2]); set_flag(cpu, BIT_V, false);
            } else {
                bus_write32(cpu->bus, addr, cpu->er[r2]);
                set_nz_l(cpu, (int32_t)cpu->er[r2]); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        default: unimplemented(cpu, 0x0100, cpu->pc - 4); break;
    }
}

/* ---- decode01: prefix 0x01xx ---- */
static void decode01(h8s_cpu_t *cpu, int lo) {
    switch (lo) {
        case 0x00: { uint16_t op2 = fetch16(cpu); prefix01_profile_record((uint8_t)lo, op2); decode0100(cpu, op2); break; }
        case 0x10: case 0x20: case 0x30: {
            int count = (lo >> 4);
            uint16_t op2 = fetch16(cpu);
            int rn = op2 & 0x7;
            if ((op2 & 0xFFF8) == 0x6D70) {
                for (int i = 0; i <= count; i++) {
                    cpu->er[rn - i] = bus_read32(cpu->bus, cpu->er[7]);
                    cpu->er[7] += 4;
                }
            } else if ((op2 & 0xFFF8) == 0x6DF0) {
                for (int i = 0; i <= count; i++) {
                    cpu->er[7] -= 4;
                    bus_write32(cpu->bus, cpu->er[7], cpu->er[rn + i]);
                }
            } else { unimplemented(cpu, 0x0100 | lo, cpu->pc - 4); }
            break;
        }
        case 0x40: { uint16_t op2 = fetch16(cpu); decode0140(cpu, op2); break; }
        case 0x41: { uint16_t op2 = fetch16(cpu); decode0141(cpu, op2); break; }
        case 0xC0: case 0xD0: case 0xF0: {
            uint16_t op2 = fetch16(cpu);
            prefix01_profile_record((uint8_t)lo, op2);
            int hi2 = (op2 >> 8) & 0xFF;
            if (hi2 == 0x6B) { decode6B_32(cpu, op2, false); }
            else if (hi2 == 0x50 || hi2 == 0x52) { decode_mulxs(cpu, hi2, op2); }
            else if (hi2 == 0x51 || hi2 == 0x53) { decode_divxs(cpu, hi2, op2); }
            else if (lo == 0xF0 && hi2 == 0x64) {
                int rs = (op2 >> 4) & 0x7; int rd = op2 & 0x7;
                cpu->er[rd] = cpu->er[rd] | cpu->er[rs];
                set_nz_l(cpu, (int32_t)cpu->er[rd]); set_flag(cpu, BIT_V, false);
            } else if (lo == 0xF0 && hi2 == 0x65) {
                int rs = (op2 >> 4) & 0x7; int rd = op2 & 0x7;
                cpu->er[rd] = cpu->er[rd] ^ cpu->er[rs];
                set_nz_l(cpu, (int32_t)cpu->er[rd]); set_flag(cpu, BIT_V, false);
            } else if (lo == 0xF0 && hi2 == 0x66) {
                int rs = (op2 >> 4) & 0x7; int rd = op2 & 0x7;
                cpu->er[rd] = cpu->er[rd] & cpu->er[rs];
                set_nz_l(cpu, (int32_t)cpu->er[rd]); set_flag(cpu, BIT_V, false);
            } else { unimplemented(cpu, 0x0100 | lo, cpu->pc - 4); }
            break;
        }
        case 0x80:
            cpu->halted = true;
            branch_profile_record(10, true, cpu->pc);
            break;
        default: { uint16_t op2 = fetch16(cpu); (void)op2; unimplemented(cpu, 0x0100 | lo, cpu->pc - 4); break; }
    }
}

/* ---- decode0A: INC.B, ADD.L reg ---- */
static void decode0A(h8s_cpu_t *cpu, int lo) {
    if ((lo & 0x80) != 0) {
        int rs = (lo >> 4) & 0x7; int rd = lo & 0x7;
        uint32_t s = cpu->er[rs], d = cpu->er[rd];
        uint32_t result = d + s;
        cpu->er[rd] = result;
        set_arithmetic_l(cpu, d, s, result, false);
    } else if ((lo & 0xF0) == 0x00) {
        int rd = lo & 0xF;
        int val = get_reg_b(cpu, rd);
        int result = (val + 1) & 0xFF;
        set_reg_b(cpu, rd, (uint8_t)result);
        set_nz_b(cpu, result);
        set_flag(cpu, BIT_V, val == 0x7F);
    } else if ((lo & 0xF0) == 0x50) {
        int rd = lo & 0xF;
        int val = get_r(cpu, rd);
        int result = (val + 1) & 0xFFFF;
        set_r(cpu, rd, (uint16_t)result);
        set_nz_w(cpu, result);
        set_flag(cpu, BIT_V, val == 0x7FFF);
    } else if ((lo & 0xF0) == 0x70) {
        int rd = lo & 0x7;
        int32_t val = (int32_t)cpu->er[rd];
        cpu->er[rd] = (uint32_t)(val + 1);
        set_nz_l(cpu, (int32_t)cpu->er[rd]);
        set_flag(cpu, BIT_V, val == 0x7FFFFFFF);
    } else { unimplemented(cpu, 0x0A00 | lo, cpu->pc - 2); }
}

/* ---- decode0B: ADDS, INC.W, INC.L ---- */
static void decode0B(h8s_cpu_t *cpu, int lo) {
    switch (lo & 0xF0) {
        case 0x00: cpu->er[lo & 0x7] += 1; break;
        case 0x50: {
            int rd = lo & 0xF; int val = get_r(cpu, rd);
            int result = (val + 1) & 0xFFFF;
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result);
            set_flag(cpu, BIT_V, val == 0x7FFF);
            break;
        }
        case 0x70: {
            int rd = lo & 0x7; int32_t val = (int32_t)cpu->er[rd];
            cpu->er[rd] = (uint32_t)(val + 1);
            set_nz_l(cpu, (int32_t)cpu->er[rd]);
            set_flag(cpu, BIT_V, val == 0x7FFFFFFF);
            break;
        }
        case 0x80: cpu->er[lo & 0x7] += 2; break;
        case 0x90: cpu->er[lo & 0x7] += 4; break;
        case 0xD0: {
            int rd = lo & 0xF; int val = get_r(cpu, rd);
            int result = (val + 2) & 0xFFFF;
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result);
            break;
        }
        case 0xF0: {
            int rd = lo & 0x7; cpu->er[rd] += 2;
            set_nz_l(cpu, (int32_t)cpu->er[rd]);
            break;
        }
        default: unimplemented(cpu, 0x0B00 | lo, cpu->pc - 2); break;
    }
}

/* ---- decode0F: MOV.L reg, DAA ---- */
static void decode0F(h8s_cpu_t *cpu, int lo) {
    if ((lo & 0x80) != 0) {
        int rs = (lo >> 4) & 0x7; int rd = lo & 0x7;
        cpu->er[rd] = cpu->er[rs];
        set_nz_l(cpu, (int32_t)cpu->er[rd]);
        set_flag(cpu, BIT_V, false);
    } else if ((lo & 0xF0) == 0x00) {
        /* DAA stub */
    } else { unimplemented(cpu, 0x0F00 | lo, cpu->pc - 2); }
}

/* ---- decode17: NOT, EXTU, NEG, EXTS ---- */
static void decode17(h8s_cpu_t *cpu, int lo) {
    int subop = (lo >> 4) & 0xF;
    int rd = lo & 0xF;
    int erd = lo & 0x7;
    switch (subop) {
        case 0x0: { int v = (~get_reg_b(cpu, rd)) & 0xFF; set_reg_b(cpu, rd, (uint8_t)v); set_nz_b(cpu, v); set_flag(cpu, BIT_V, false); break; }
        case 0x1: { int v = (~get_r(cpu, rd)) & 0xFFFF; set_r(cpu, rd, (uint16_t)v); set_nz_w(cpu, v); set_flag(cpu, BIT_V, false); break; }
        case 0x3: { cpu->er[erd] = ~cpu->er[erd]; set_nz_l(cpu, (int32_t)cpu->er[erd]); set_flag(cpu, BIT_V, false); break; }
        case 0x5: { set_r(cpu, rd, get_rl(cpu, rd)); set_nz_w(cpu, get_r(cpu, rd)); set_flag(cpu, BIT_V, false); break; }
        case 0x7: { cpu->er[erd] = get_r(cpu, erd); set_nz_l(cpu, (int32_t)cpu->er[erd]); set_flag(cpu, BIT_V, false); break; }
        case 0x8: {
            int v = get_reg_b(cpu, rd); int r = (-v) & 0xFF;
            set_reg_b(cpu, rd, (uint8_t)r); set_nz_b(cpu, r);
            set_flag(cpu, BIT_C, r != 0); set_flag(cpu, BIT_V, v == 0x80);
            set_flag(cpu, BIT_H, ((v ^ r) & 0x10) != 0);
            break;
        }
        case 0x9: {
            int v = get_r(cpu, rd); int r = (-v) & 0xFFFF;
            set_r(cpu, rd, (uint16_t)r); set_nz_w(cpu, r);
            set_flag(cpu, BIT_C, r != 0); set_flag(cpu, BIT_V, v == 0x8000);
            break;
        }
        case 0xB: {
            uint32_t v = cpu->er[erd]; cpu->er[erd] = 0u - v;
            set_nz_l(cpu, (int32_t)cpu->er[erd]);
            set_flag(cpu, BIT_C, cpu->er[erd] != 0);
            set_flag(cpu, BIT_V, v == 0x80000000u);
            break;
        }
        case 0xD: { int v = (int8_t)get_rl(cpu, rd); set_r(cpu, rd, (uint16_t)(v & 0xFFFF)); set_nz_w(cpu, v); set_flag(cpu, BIT_V, false); break; }
        case 0xF: { int32_t v = (int16_t)get_r(cpu, erd); cpu->er[erd] = (uint32_t)v; set_nz_l(cpu, v); set_flag(cpu, BIT_V, false); break; }
        default: unimplemented(cpu, 0x1700 | lo, cpu->pc - 2); break;
    }
}

/* ---- Shift/rotate/extend group (0x10-0x17) ---- */
static void decode10_17(h8s_cpu_t *cpu, int hi, int lo) {
    if (hi == 0x14) { /* OR.B reg */
        int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
        int result = get_reg_b(cpu, rd) | get_reg_b(cpu, rs);
        set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result); set_flag(cpu, BIT_V, false);
        return;
    }
    if (hi == 0x15) { /* XOR.B reg */
        int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
        int result = get_reg_b(cpu, rd) ^ get_reg_b(cpu, rs);
        set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result); set_flag(cpu, BIT_V, false);
        return;
    }
    if (hi == 0x16) { /* AND.B reg */
        int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
        int result = get_reg_b(cpu, rd) & get_reg_b(cpu, rs);
        set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result); set_flag(cpu, BIT_V, false);
        return;
    }
    if (hi == 0x17) { decode17(cpu, lo); return; }

    int subop = (lo >> 4) & 0xF;
    int rd = lo & 0xF;
    int erd = lo & 0x7;
    switch (hi) {
        case 0x10:
            switch (subop) {
                case 0x0: shift_b(cpu, rd, true, true, 1); break;
                case 0x1: shift_w(cpu, rd, true, true, 1); break;
                case 0x3: shift_l(cpu, erd, true, true, 1); break;
                case 0x4: shift_b(cpu, rd, true, true, 2); break;
                case 0x5: shift_w(cpu, rd, true, true, 2); break;
                case 0x7: shift_l(cpu, erd, true, true, 2); break;
                case 0x8: shift_b(cpu, rd, true, false, 1); break;
                case 0x9: shift_w(cpu, rd, true, false, 1); break;
                case 0xB: shift_l(cpu, erd, true, false, 1); break;
                case 0xC: shift_b(cpu, rd, true, false, 2); break;
                case 0xD: shift_w(cpu, rd, true, false, 2); break;
                case 0xF: shift_l(cpu, erd, true, false, 2); break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        case 0x11:
            switch (subop) {
                case 0x0: shift_b(cpu, rd, false, true, 1); break;
                case 0x1: shift_w(cpu, rd, false, true, 1); break;
                case 0x3: shift_l(cpu, erd, false, true, 1); break;
                case 0x4: shift_b(cpu, rd, false, true, 2); break;
                case 0x5: shift_w(cpu, rd, false, true, 2); break;
                case 0x7: shift_l(cpu, erd, false, true, 2); break;
                case 0x8: shift_b(cpu, rd, false, false, 1); break;
                case 0x9: shift_w(cpu, rd, false, false, 1); break;
                case 0xB: shift_l(cpu, erd, false, false, 1); break;
                case 0xC: shift_b(cpu, rd, false, false, 2); break;
                case 0xD: shift_w(cpu, rd, false, false, 2); break;
                case 0xF: shift_l(cpu, erd, false, false, 2); break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        case 0x12:
            switch (subop) {
                case 0x0: rotate_b(cpu, rd, true, true, 1); break;
                case 0x1: rotate_w(cpu, rd, true, true, 1); break;
                case 0x3: rotate_l(cpu, erd, true, true, 1); break;
                case 0x4: rotate_b(cpu, rd, true, true, 2); break;
                case 0x5: rotate_w(cpu, rd, true, true, 2); break;
                case 0x7: rotate_l(cpu, erd, true, true, 2); break;
                case 0x8: rotate_b(cpu, rd, true, false, 1); break;
                case 0x9: rotate_w(cpu, rd, true, false, 1); break;
                case 0xB: rotate_l(cpu, erd, true, false, 1); break;
                case 0xC: rotate_b(cpu, rd, true, false, 2); break;
                case 0xD: rotate_w(cpu, rd, true, false, 2); break;
                case 0xF: rotate_l(cpu, erd, true, false, 2); break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        case 0x13:
            switch (subop) {
                case 0x0: rotate_b(cpu, rd, false, true, 1); break;
                case 0x1: rotate_w(cpu, rd, false, true, 1); break;
                case 0x3: rotate_l(cpu, erd, false, true, 1); break;
                case 0x4: rotate_b(cpu, rd, false, true, 2); break;
                case 0x5: rotate_w(cpu, rd, false, true, 2); break;
                case 0x7: rotate_l(cpu, erd, false, true, 2); break;
                case 0x8: rotate_b(cpu, rd, false, false, 1); break;
                case 0x9: rotate_w(cpu, rd, false, false, 1); break;
                case 0xB: rotate_l(cpu, erd, false, false, 1); break;
                case 0xC: rotate_b(cpu, rd, false, false, 2); break;
                case 0xD: rotate_w(cpu, rd, false, false, 2); break;
                case 0xF: rotate_l(cpu, erd, false, false, 2); break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
    }
}

/* ---- decode1A: DEC.B, SUB.L reg ---- */
static void decode1A(h8s_cpu_t *cpu, int lo) {
    if ((lo & 0x80) != 0) {
        int rs = (lo >> 4) & 0x7; int rd = lo & 0x7;
        uint32_t s = cpu->er[rs], d = cpu->er[rd];
        uint32_t result = d - s;
        cpu->er[rd] = result;
        set_arithmetic_l(cpu, d, s, result, true);
    } else if ((lo & 0xF0) == 0x00) {
        int rd = lo & 0xF;
        int val = get_reg_b(cpu, rd);
        int result = (val - 1) & 0xFF;
        set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result);
        set_flag(cpu, BIT_V, val == 0x80);
    } else if ((lo & 0xF0) == 0x50) {
        int rd = lo & 0xF;
        int val = get_r(cpu, rd);
        int result = (val - 1) & 0xFFFF;
        set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result);
        set_flag(cpu, BIT_V, val == 0x8000);
    } else if ((lo & 0xF0) == 0x70) {
        int rd = lo & 0x7;
        int32_t val = (int32_t)cpu->er[rd];
        cpu->er[rd] = (uint32_t)(val - 1);
        set_nz_l(cpu, (int32_t)cpu->er[rd]);
        set_flag(cpu, BIT_V, (uint32_t)val == 0x80000000u);
    } else { unimplemented(cpu, 0x1A00 | lo, cpu->pc - 2); }
}

/* ---- decode1B: SUBS, DEC.W, DEC.L ---- */
static void decode1B(h8s_cpu_t *cpu, int lo) {
    switch (lo & 0xF0) {
        case 0x00: cpu->er[lo & 0x7] -= 1; break;
        case 0x50: {
            int rd = lo & 0xF; int val = get_r(cpu, rd);
            int result = (val - 1) & 0xFFFF;
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result);
            set_flag(cpu, BIT_V, val == 0x8000);
            break;
        }
        case 0x70: {
            int rd = lo & 0x7; int32_t val = (int32_t)cpu->er[rd];
            cpu->er[rd] = (uint32_t)(val - 1);
            set_nz_l(cpu, (int32_t)cpu->er[rd]);
            set_flag(cpu, BIT_V, (uint32_t)val == 0x80000000u);
            break;
        }
        case 0x80: cpu->er[lo & 0x7] -= 2; break;
        case 0x90: cpu->er[lo & 0x7] -= 4; break;
        case 0xD0: {
            int rd = lo & 0xF; int val = get_r(cpu, rd);
            int result = (val - 2) & 0xFFFF;
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result);
            set_flag(cpu, BIT_V, val == 0x8000 || val == 0x8001);
            break;
        }
        case 0xF0: {
            int rd = lo & 0x7; cpu->er[rd] -= 2;
            set_nz_l(cpu, (int32_t)cpu->er[rd]);
            break;
        }
        default: unimplemented(cpu, 0x1B00 | lo, cpu->pc - 2); break;
    }
}

/* ---- decode1F: CMP.L reg, DAS ---- */
static void decode1F(h8s_cpu_t *cpu, int lo) {
    if ((lo & 0x80) != 0) {
        int rs = (lo >> 4) & 0x7; int rd = lo & 0x7;
        uint32_t s = cpu->er[rs], d = cpu->er[rd];
        set_arithmetic_l(cpu, d, s, d - s, true);
    } else if ((lo & 0xF0) == 0x00) {
        /* DAS stub */
    } else { unimplemented(cpu, 0x1F00 | lo, cpu->pc - 2); }
}

/* ---- decode6A: MOV.B absolute / compound bit ---- */
static void decode6A(h8s_cpu_t *cpu, int lo) {
    int topNibble = (lo >> 4) & 0xF;
    if (topNibble == 1 || topNibble == 3) {
        bool addr32 = (topNibble == 3);
        uint32_t addr;
        if (addr32) addr = fetch32(cpu) & 0xFFFFFF;
        else { addr = (uint32_t)(int32_t)(int16_t)fetch16(cpu); addr &= 0xFFFFFF; }
        uint16_t bitOp = fetch16(cpu);
        execute_bit_op_at_address(cpu, addr, bitOp);
        return;
    }
    bool write = (lo & 0x80) != 0;
    bool addr24 = (lo & 0x20) != 0;
    int rd = lo & 0xF;
    uint32_t addr;
    if (addr24) addr = fetch32(cpu) & 0xFFFFFF;
    else { addr = (uint32_t)(int32_t)(int16_t)fetch16(cpu); addr &= 0xFFFFFF; }
    if (!write) {
        int val = bus_read8(cpu->bus, addr);
        set_reg_b(cpu, rd, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
    } else {
        int val = get_reg_b(cpu, rd);
        bus_write8(cpu->bus, addr, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
    }
}

/* ---- decode6B: MOV.W absolute ---- */
static void decode6B(h8s_cpu_t *cpu, int lo) {
    bool write = (lo & 0x80) != 0;
    bool addr24 = (lo & 0x20) != 0;
    int rd = lo & 0xF;
    uint32_t addr;
    if (addr24) addr = fetch32(cpu) & 0xFFFFFF;
    else { addr = (uint32_t)(int32_t)(int16_t)fetch16(cpu); addr &= 0xFFFFFF; }
    if (!write) {
        int val = bus_read16(cpu->bus, addr);
        set_r(cpu, rd, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
    } else {
        int val = get_r(cpu, rd);
        bus_write16(cpu->bus, addr, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
    }
}

/* ---- decode79: word immediate ---- */
static void decode79(h8s_cpu_t *cpu, int lo) {
    int subop = (lo >> 4) & 0xF;
    int rd = lo & 0xF;
    int imm = fetch16(cpu);
    switch (subop) {
        case 0x0: set_r(cpu, rd, (uint16_t)imm); set_nz_w(cpu, imm); set_flag(cpu, BIT_V, false); break;
        case 0x1: {
            int d = get_r(cpu, rd); int result = d + imm;
            set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result);
            set_flag(cpu, BIT_C, result > 0xFFFF);
            set_flag(cpu, BIT_V, ((d ^ result) & (imm ^ result) & 0x8000) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x1000) != 0);
            break;
        }
        case 0x2: {
            int d = get_r(cpu, rd); int result = d - imm;
            set_nz_w(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x10000) != 0);
            set_flag(cpu, BIT_V, ((d ^ imm) & (d ^ result) & 0x8000) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x1000) != 0);
            break;
        }
        case 0x3: {
            int d = get_r(cpu, rd); int result = d - imm;
            set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x10000) != 0);
            set_flag(cpu, BIT_V, ((d ^ imm) & (d ^ result) & 0x8000) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x1000) != 0);
            break;
        }
        case 0x4: { int result = get_r(cpu, rd) | imm; set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result); set_flag(cpu, BIT_V, false); break; }
        case 0x5: { int result = get_r(cpu, rd) ^ imm; set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result); set_flag(cpu, BIT_V, false); break; }
        case 0x6: { int result = get_r(cpu, rd) & imm; set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result); set_flag(cpu, BIT_V, false); break; }
        default: unimplemented(cpu, 0x7900 | lo, cpu->pc - 4); break;
    }
}

/* ---- decode7A: longword immediate ---- */
static void decode7A(h8s_cpu_t *cpu, int lo) {
    int subop = (lo >> 4) & 0xF;
    int rd = lo & 0x7;
    uint32_t imm = fetch32(cpu);
    switch (subop) {
        case 0x0: cpu->er[rd] = imm; set_nz_l(cpu, (int32_t)imm); set_flag(cpu, BIT_V, false); break;
        case 0x1: {
            uint32_t d = cpu->er[rd], result = d + imm;
            cpu->er[rd] = result;
            set_arithmetic_l(cpu, d, imm, result, false);
            break;
        }
        case 0x2: {
            uint32_t d = cpu->er[rd];
            set_arithmetic_l(cpu, d, imm, d - imm, true);
            break;
        }
        case 0x3: {
            uint32_t d = cpu->er[rd], result = d - imm;
            cpu->er[rd] = result;
            set_arithmetic_l(cpu, d, imm, result, true);
            break;
        }
        case 0x4: cpu->er[rd] |= imm; set_nz_l(cpu, (int32_t)cpu->er[rd]); set_flag(cpu, BIT_V, false); break;
        case 0x5: cpu->er[rd] ^= imm; set_nz_l(cpu, (int32_t)cpu->er[rd]); set_flag(cpu, BIT_V, false); break;
        case 0x6: cpu->er[rd] &= imm; set_nz_l(cpu, (int32_t)cpu->er[rd]); set_flag(cpu, BIT_V, false); break;
        default: unimplemented(cpu, 0x7A00 | lo, cpu->pc - 6); break;
    }
}

/* ---- decode7C_7F: bit ops on memory ---- */
static void decode7C_7F(h8s_cpu_t *cpu, int hi, int lo) {
    int r = (lo >> 4) & 0x7;
    uint16_t op2 = fetch16(cpu);
    int bit = (op2 >> 4) & 0x7;
    int subop = (op2 >> 8) & 0xFF;

    switch (hi) {
        case 0x7C: {
            uint32_t addr = cpu->er[r];
            int val = bus_read8(cpu->bus, addr);
            switch (subop) {
                case 0x63: case 0x73: set_flag(cpu, BIT_Z, (val & (1 << bit)) == 0); break;
                case 0x77:
                    if ((op2 & 0x80) == 0) set_flag(cpu, BIT_C, (val & (1 << bit)) != 0);
                    else set_flag(cpu, BIT_C, (val & (1 << bit)) == 0);
                    break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        }
        case 0x7D: {
            uint32_t addr = cpu->er[r];
            int val = bus_read8(cpu->bus, addr);
            switch (subop) {
                case 0x60: bus_write8(cpu->bus, addr, (uint8_t)(val | (1 << (get_rl(cpu, op2 & 0xF) & 0x7)))); break;
                case 0x62: bus_write8(cpu->bus, addr, (uint8_t)(val & ~(1 << (get_rl(cpu, op2 & 0xF) & 0x7)))); break;
                case 0x67:
                    if ((op2 & 0x80) == 0) {
                        if (get_flag(cpu, BIT_C)) val |= (1 << bit); else val &= ~(1 << bit);
                    } else {
                        if (!get_flag(cpu, BIT_C)) val |= (1 << bit); else val &= ~(1 << bit);
                    }
                    bus_write8(cpu->bus, addr, (uint8_t)val);
                    break;
                case 0x70: bus_write8(cpu->bus, addr, (uint8_t)(val | (1 << bit))); break;
                case 0x72: bus_write8(cpu->bus, addr, (uint8_t)(val & ~(1 << bit))); break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        }
        case 0x7E: {
            uint32_t addr = 0xFFFF00 | (lo & 0xFF);
            int val = bus_read8(cpu->bus, addr);
            switch (subop) {
                case 0x63: case 0x73: set_flag(cpu, BIT_Z, (val & (1 << bit)) == 0); break;
                case 0x77:
                    if ((op2 & 0x80) == 0) set_flag(cpu, BIT_C, (val & (1 << bit)) != 0);
                    else set_flag(cpu, BIT_C, (val & (1 << bit)) == 0);
                    break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        }
        case 0x7F: {
            uint32_t addr = 0xFFFF00 | (lo & 0xFF);
            int val = bus_read8(cpu->bus, addr);
            switch (subop) {
                case 0x70: bus_write8(cpu->bus, addr, (uint8_t)(val | (1 << bit))); break;
                case 0x72: bus_write8(cpu->bus, addr, (uint8_t)(val & ~(1 << bit))); break;
                default: unimplemented(cpu, (hi << 8) | lo, cpu->pc - 2); break;
            }
            break;
        }
        default: break;
    }
}

/* ======== Group 0 ======== */
static void decode0(h8s_cpu_t *cpu, uint16_t op, int hi, int lo) {
    switch (hi) {
        case 0x00: break; /* NOP */
        case 0x01: decode01(cpu, lo); break;
        case 0x02: { int rd = lo & 0xF; set_reg_b(cpu, rd, cpu->ccr); break; }
        case 0x03: { int rs = lo & 0xF; cpu->ccr = get_reg_b(cpu, rs); cpu->irq_deferred = true; break; }
        case 0x04: cpu->ccr |= (uint8_t)lo; cpu->irq_deferred = true; break;
        case 0x05: cpu->ccr ^= (uint8_t)lo; cpu->irq_deferred = true; break;
        case 0x06: cpu->ccr &= (uint8_t)lo; cpu->irq_deferred = true; break;
        case 0x07: cpu->ccr = (uint8_t)lo; cpu->irq_deferred = true; break;
        case 0x08: { /* ADD.B Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_reg_b(cpu, rs); int d = get_reg_b(cpu, rd);
            int result = d + s;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF)); set_nz_b(cpu, result);
            set_flag(cpu, BIT_C, result > 0xFF);
            set_flag(cpu, BIT_V, ((d ^ result) & (s ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x10) != 0);
            break;
        }
        case 0x09: { /* ADD.W Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_r(cpu, rs); int d = get_r(cpu, rd);
            int result = d + s;
            set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result);
            set_flag(cpu, BIT_C, result > 0xFFFF);
            set_flag(cpu, BIT_V, ((d ^ result) & (s ^ result) & 0x8000) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x1000) != 0);
            break;
        }
        case 0x0A: decode0A(cpu, lo); break;
        case 0x0B: decode0B(cpu, lo); break;
        case 0x0C: { /* MOV.B Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int val = get_reg_b(cpu, rs);
            set_reg_b(cpu, rd, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x0D: { /* MOV.W Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int val = get_r(cpu, rs);
            set_r(cpu, rd, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x0E: { /* ADDX.B Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_reg_b(cpu, rs); int d = get_reg_b(cpu, rd);
            int c = get_flag(cpu, BIT_C) ? 1 : 0;
            int result = d + s + c;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF));
            set_flag(cpu, BIT_C, result > 0xFF);
            set_flag(cpu, BIT_V, ((d ^ result) & (s ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x10) != 0);
            if ((result & 0xFF) != 0) set_flag(cpu, BIT_Z, false);
            set_flag(cpu, BIT_N, (result & 0x80) != 0);
            break;
        }
        case 0x0F: decode0F(cpu, lo); break;
        default: unimplemented(cpu, op, cpu->pc - 2); break;
    }
}

/* ======== Group 1 ======== */
static void decode1(h8s_cpu_t *cpu, uint16_t op, int hi, int lo) {
    switch (hi) {
        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x15: case 0x16: case 0x17:
            decode10_17(cpu, hi, lo); break;
        case 0x18: { /* SUB.B Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_reg_b(cpu, rs); int d = get_reg_b(cpu, rd);
            int result = d - s;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF)); set_nz_b(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x100) != 0);
            set_flag(cpu, BIT_V, ((d ^ s) & (d ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x10) != 0);
            break;
        }
        case 0x19: { /* SUB.W Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_r(cpu, rs); int d = get_r(cpu, rd);
            int result = d - s;
            set_r(cpu, rd, (uint16_t)(result & 0xFFFF)); set_nz_w(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x10000) != 0);
            set_flag(cpu, BIT_V, ((d ^ s) & (d ^ result) & 0x8000) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x1000) != 0);
            break;
        }
        case 0x1A: decode1A(cpu, lo); break;
        case 0x1B: decode1B(cpu, lo); break;
        case 0x1C: { /* CMP.B Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_reg_b(cpu, rs); int d = get_reg_b(cpu, rd);
            int result = d - s;
            set_nz_b(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x100) != 0);
            set_flag(cpu, BIT_V, ((d ^ s) & (d ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x10) != 0);
            break;
        }
        case 0x1D: { /* CMP.W Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_r(cpu, rs); int d = get_r(cpu, rd);
            int result = d - s;
            set_nz_w(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x10000) != 0);
            set_flag(cpu, BIT_V, ((d ^ s) & (d ^ result) & 0x8000) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x1000) != 0);
            break;
        }
        case 0x1E: { /* SUBX.B Rs, Rd */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int s = get_reg_b(cpu, rs); int d = get_reg_b(cpu, rd);
            int c = get_flag(cpu, BIT_C) ? 1 : 0;
            int result = d - s - c;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF));
            set_flag(cpu, BIT_C, result < 0);
            set_flag(cpu, BIT_V, ((d ^ s) & (d ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ s ^ result) & 0x10) != 0);
            if ((result & 0xFF) != 0) set_flag(cpu, BIT_Z, false);
            set_flag(cpu, BIT_N, (result & 0x80) != 0);
            break;
        }
        case 0x1F: decode1F(cpu, lo); break;
        default: unimplemented(cpu, op, cpu->pc - 2); break;
    }
}

/* ======== Group 5 ======== */
static void decode5(h8s_cpu_t *cpu, uint16_t op, int hi, int lo) {
    switch (hi) {
        case 0x50: { /* MULXU.B */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int result = get_reg_b(cpu, rs) * (uint8_t)get_r(cpu, rd);
            set_r(cpu, rd, (uint16_t)(result & 0xFFFF));
            break;
        }
        case 0x51: { /* DIVXU.B */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int divisor = get_reg_b(cpu, rs);
            if (divisor == 0) { set_flag(cpu, BIT_Z, true); }
            else {
                int dividend = get_r(cpu, rd);
                int quotient = (dividend / divisor) & 0xFF;
                int remainder = (dividend % divisor) & 0xFF;
                set_r(cpu, rd, (uint16_t)((remainder << 8) | quotient));
                set_flag(cpu, BIT_N, (quotient & 0x80) != 0);
                set_flag(cpu, BIT_Z, quotient == 0);
            }
            break;
        }
        case 0x52: { /* MULXU.W */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0x7;
            uint32_t result = (uint32_t)get_r(cpu, rs) * (uint32_t)get_r(cpu, rd);
            cpu->er[rd] = result;
            break;
        }
        case 0x53: { /* DIVXU.W */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0x7;
            uint32_t divisor = get_r(cpu, rs) & 0xFFFF;
            if (divisor == 0) { set_flag(cpu, BIT_Z, true); }
            else {
                uint32_t dividend = cpu->er[rd];
                uint32_t quotient = (uint32_t)((dividend / divisor) & 0xFFFF);
                uint32_t remainder = (uint32_t)((dividend % divisor) & 0xFFFF);
                cpu->er[rd] = (remainder << 16) | quotient;
                set_flag(cpu, BIT_N, (quotient & 0x8000) != 0);
                set_flag(cpu, BIT_Z, quotient == 0);
            }
            break;
        }
        case 0x54: { /* RTS */
            if (lo == 0x70) {
                cpu->pc = bus_read32(cpu->bus, cpu->er[7]) & 0xFFFFFF;
                cpu->er[7] += 4;
                branch_profile_record(8, true, cpu->pc);
            } else { unimplemented(cpu, op, cpu->pc - 2); }
            break;
        }
        case 0x55: { /* BSR d:8 */
            int8_t disp = (int8_t)lo;
            uint32_t target = (cpu->pc + disp) & 0xFFFFFF;
            cpu->er[7] -= 4;
            bus_write32(cpu->bus, cpu->er[7], cpu->pc);
            cpu->pc = target;
            branch_profile_record(3, true, target);
            break;
        }
        case 0x56: { /* RTE */
            if (lo == 0x70) {
                /* Advanced mode: CCR occupies the high byte of the saved PC.
                 * This also permits the OS to construct task-switch frames. */
                uint32_t frame = bus_read32(cpu->bus, cpu->er[7]);
                cpu->ccr = (uint8_t)(frame >> 24);
                cpu->pc = frame & 0xFFFFFF;
                cpu->er[7] += 4;
                branch_profile_record(8, true, cpu->pc);
            } else { unimplemented(cpu, op, cpu->pc - 2); }
            break;
        }
        case 0x57: { /* TRAPA */
            if ((lo & 0xC0) == 0x00) {
                int vec = (lo >> 4) & 0x3;
                cpu->er[7] -= 4;
                bus_write32(cpu->bus, cpu->er[7], ((uint32_t)cpu->ccr << 24) | cpu->pc);
                cpu->pc = bus_read32(cpu->bus, 0x20 + (uint32_t)vec * 4) & 0xFFFFFF;
                set_flag(cpu, BIT_I, true);
                branch_profile_record(9, true, cpu->pc);
            } else { unimplemented(cpu, op, cpu->pc - 2); }
            break;
        }
        case 0x58: { /* Bcc d:16 */
            int cond = (lo >> 4) & 0xF;
            int16_t disp = (int16_t)fetch16(cpu);
            uint32_t target = (cpu->pc + disp) & 0xFFFFFF;
            bool taken = evaluate_condition(cpu, cond);
            if (taken) cpu->pc = target;
            branch_profile_record(2, taken, target);
            break;
        }
        case 0x59: { /* JMP @ERn */
            int rn = (lo >> 4) & 0x7;
            cpu->pc = cpu->er[rn] & 0xFFFFFF;
            branch_profile_record(7, true, cpu->pc);
            break;
        }
        case 0x5A: { /* JMP @aa:24 */
            uint32_t addr = ((uint32_t)lo << 16) | fetch16(cpu);
            cpu->pc = addr & 0xFFFFFF;
            branch_profile_record(5, true, cpu->pc);
            break;
        }
        case 0x5B: { /* JMP @@aa:8 */
            cpu->pc = bus_read32(cpu->bus, lo & 0xFF) & 0xFFFFFF;
            branch_profile_record(7, true, cpu->pc);
            break;
        }
        case 0x5C: { /* BSR d:16 */
            int16_t disp = (int16_t)fetch16(cpu);
            uint32_t target = (cpu->pc + disp) & 0xFFFFFF;
            cpu->er[7] -= 4;
            bus_write32(cpu->bus, cpu->er[7], cpu->pc);
            cpu->pc = target;
            branch_profile_record(4, true, target);
            break;
        }
        case 0x5D: { /* JSR @ERn */
            int rn = (lo >> 4) & 0x7;
            cpu->er[7] -= 4;
            bus_write32(cpu->bus, cpu->er[7], cpu->pc);
            cpu->pc = cpu->er[rn] & 0xFFFFFF;
            branch_profile_record(7, true, cpu->pc);
            break;
        }
        case 0x5E: { /* JSR @aa:24 */
            uint32_t addr = ((uint32_t)lo << 16) | fetch16(cpu);
            cpu->er[7] -= 4;
            bus_write32(cpu->bus, cpu->er[7], cpu->pc);
            cpu->pc = addr & 0xFFFFFF;
            branch_profile_record(6, true, cpu->pc);
            break;
        }
        case 0x5F: { /* JSR @@aa:8 */
            uint32_t addr = bus_read32(cpu->bus, lo & 0xFF) & 0xFFFFFF;
            cpu->er[7] -= 4;
            bus_write32(cpu->bus, cpu->er[7], cpu->pc);
            cpu->pc = addr;
            branch_profile_record(7, true, cpu->pc);
            break;
        }
        default: unimplemented(cpu, op, cpu->pc - 2); break;
    }
}

/* ======== Group 6 ======== */
static void decode6(h8s_cpu_t *cpu, uint16_t op, int hi, int lo) {
    switch (hi) {
        case 0x60: { int rn = (lo >> 4) & 0xF; int rd = lo & 0xF; int bit = get_reg_b(cpu, rn) & 0x7; set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) | (1 << bit))); break; }
        case 0x61: { int rn = (lo >> 4) & 0xF; int rd = lo & 0xF; int bit = get_reg_b(cpu, rn) & 0x7; set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) ^ (1 << bit))); break; }
        case 0x62: { int rn = (lo >> 4) & 0xF; int rd = lo & 0xF; int bit = get_reg_b(cpu, rn) & 0x7; set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) & ~(1 << bit))); break; }
        case 0x63: { int rn = (lo >> 4) & 0xF; int rd = lo & 0xF; int bit = get_reg_b(cpu, rn) & 0x7; set_flag(cpu, BIT_Z, (get_reg_b(cpu, rd) & (1 << bit)) == 0); break; }
        case 0x64: { /* OR.W */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int result = get_r(cpu, rd) | get_r(cpu, rs);
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x65: { /* XOR.W */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int result = get_r(cpu, rd) ^ get_r(cpu, rs);
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x66: { /* AND.W */
            int rs = (lo >> 4) & 0xF; int rd = lo & 0xF;
            int result = get_r(cpu, rd) & get_r(cpu, rs);
            set_r(cpu, rd, (uint16_t)result); set_nz_w(cpu, result); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x67: { /* BST/BIST */
            int bit = (lo >> 4) & 0x7; int rd = lo & 0xF;
            if ((lo & 0x80) == 0) {
                if (get_flag(cpu, BIT_C)) set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) | (1 << bit)));
                else set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) & ~(1 << bit)));
            } else {
                if (!get_flag(cpu, BIT_C)) set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) | (1 << bit)));
                else set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) & ~(1 << bit)));
            }
            break;
        }
        case 0x68: { /* MOV.B @ER, reg */
            int erReg = (lo >> 4) & 0x7; int bReg = lo & 0xF;
            if ((lo & 0x80) == 0) {
                int val = bus_read8(cpu->bus, cpu->er[erReg]);
                set_reg_b(cpu, bReg, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            } else {
                int val = get_reg_b(cpu, bReg);
                bus_write8(cpu->bus, cpu->er[erReg], (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x69: { /* MOV.W @ER, reg */
            int erReg = (lo >> 4) & 0x7; int wReg = lo & 0xF;
            if ((lo & 0x80) == 0) {
                int val = bus_read16(cpu->bus, cpu->er[erReg]);
                set_r(cpu, wReg, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            } else {
                int val = get_r(cpu, wReg);
                bus_write16(cpu->bus, cpu->er[erReg], (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x6A: decode6A(cpu, lo); break;
        case 0x6B: decode6B(cpu, lo); break;
        case 0x6C: { /* MOV.B post-inc/pre-dec */
            int erReg = (lo >> 4) & 0x7; int bReg = lo & 0xF;
            if ((lo & 0x80) == 0) {
                int val = bus_read8(cpu->bus, cpu->er[erReg]);
                cpu->er[erReg] += 1;
                set_reg_b(cpu, bReg, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            } else {
                int val = get_reg_b(cpu, bReg);
                cpu->er[erReg] -= 1;
                bus_write8(cpu->bus, cpu->er[erReg], (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x6D: { /* MOV.W post-inc/pre-dec */
            int erReg = (lo >> 4) & 0x7; int wReg = lo & 0xF;
            if ((lo & 0x80) == 0) {
                int val = bus_read16(cpu->bus, cpu->er[erReg]);
                cpu->er[erReg] += 2;
                set_r(cpu, wReg, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            } else {
                int val = get_r(cpu, wReg);
                cpu->er[erReg] -= 2;
                bus_write16(cpu->bus, cpu->er[erReg], (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x6E: { /* MOV.B @(d:16, ER) */
            int erReg = (lo >> 4) & 0x7; int bReg = lo & 0xF;
            int16_t disp = (int16_t)fetch16(cpu);
            if ((lo & 0x80) == 0) {
                int val = bus_read8(cpu->bus, cpu->er[erReg] + disp);
                set_reg_b(cpu, bReg, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            } else {
                int val = get_reg_b(cpu, bReg);
                bus_write8(cpu->bus, cpu->er[erReg] + disp, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        case 0x6F: { /* MOV.W @(d:16, ER) */
            int erReg = (lo >> 4) & 0x7; int wReg = lo & 0xF;
            int16_t disp = (int16_t)fetch16(cpu);
            if ((lo & 0x80) == 0) {
                int val = bus_read16(cpu->bus, cpu->er[erReg] + disp);
                set_r(cpu, wReg, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            } else {
                int val = get_r(cpu, wReg);
                bus_write16(cpu->bus, cpu->er[erReg] + disp, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
            }
            break;
        }
        default: unimplemented(cpu, op, cpu->pc - 2); break;
    }
}

/* ======== Group 7 ======== */
static void decode7(h8s_cpu_t *cpu, uint16_t op, int hi, int lo) {
    switch (hi) {
        case 0x70: { int bit = (lo >> 4) & 0x7; int rd = lo & 0xF; set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) | (1 << bit))); break; }
        case 0x71: { int bit = (lo >> 4) & 0x7; int rd = lo & 0xF; set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) ^ (1 << bit))); break; }
        case 0x72: { int bit = (lo >> 4) & 0x7; int rd = lo & 0xF; set_reg_b(cpu, rd, (uint8_t)(get_reg_b(cpu, rd) & ~(1 << bit))); break; }
        case 0x73: { int bit = (lo >> 4) & 0x7; int rd = lo & 0xF; set_flag(cpu, BIT_Z, (get_reg_b(cpu, rd) & (1 << bit)) == 0); break; }
        case 0x74: { /* BOR/BIOR */
            int bit = (lo >> 4) & 0x7; int rd = lo & 0xF;
            bool bitVal = (get_reg_b(cpu, rd) & (1 << bit)) != 0;
            if ((lo & 0x80) == 0) set_flag(cpu, BIT_C, get_flag(cpu, BIT_C) | bitVal);
            else set_flag(cpu, BIT_C, get_flag(cpu, BIT_C) | !bitVal);
            break;
        }
        case 0x75: { /* BXOR/BIXOR */
            int bit = (lo >> 4) & 0x7; int rd = lo & 0xF;
            bool bitVal = (get_reg_b(cpu, rd) & (1 << bit)) != 0;
            if ((lo & 0x80) == 0) set_flag(cpu, BIT_C, get_flag(cpu, BIT_C) ^ bitVal);
            else set_flag(cpu, BIT_C, get_flag(cpu, BIT_C) ^ !bitVal);
            break;
        }
        case 0x76: { /* BAND/BIAND */
            int bit = (lo >> 4) & 0x7; int rd = lo & 0xF;
            bool bitVal = (get_reg_b(cpu, rd) & (1 << bit)) != 0;
            if ((lo & 0x80) == 0) set_flag(cpu, BIT_C, get_flag(cpu, BIT_C) & bitVal);
            else set_flag(cpu, BIT_C, get_flag(cpu, BIT_C) & !bitVal);
            break;
        }
        case 0x77: { /* BLD/BILD */
            int bit = (lo >> 4) & 0x7; int rd = lo & 0xF;
            bool bitVal = (get_reg_b(cpu, rd) & (1 << bit)) != 0;
            if ((lo & 0x80) == 0) set_flag(cpu, BIT_C, bitVal);
            else set_flag(cpu, BIT_C, !bitVal);
            break;
        }
        case 0x78: { /* MOV.B/W @(d:32, ER) */
            int erReg = (lo >> 4) & 0x7;
            uint16_t op2 = fetch16(cpu);
            int hi2 = (op2 >> 8) & 0xFF;
            if (hi2 == 0x6A) {
                int bReg = op2 & 0xF;
                int32_t disp = (int32_t)fetch32(cpu);
                if ((op2 & 0x80) == 0) {
                    int val = bus_read8(cpu->bus, cpu->er[erReg] + disp);
                    set_reg_b(cpu, bReg, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
                } else {
                    int val = get_reg_b(cpu, bReg);
                    bus_write8(cpu->bus, cpu->er[erReg] + disp, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
                }
            } else if (hi2 == 0x6B) {
                int wReg = op2 & 0xF;
                int32_t disp = (int32_t)fetch32(cpu);
                if ((op2 & 0x80) == 0) {
                    int val = bus_read16(cpu->bus, cpu->er[erReg] + disp);
                    set_r(cpu, wReg, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
                } else {
                    int val = get_r(cpu, wReg);
                    bus_write16(cpu->bus, cpu->er[erReg] + disp, (uint16_t)val); set_nz_w(cpu, val); set_flag(cpu, BIT_V, false);
                }
            } else { unimplemented(cpu, op, cpu->pc - 4); }
            break;
        }
        case 0x79: decode79(cpu, lo); break;
        case 0x7A: decode7A(cpu, lo); break;
        case 0x7B: { /* EEPMOV */
            uint16_t op2 = fetch16(cpu);
            if (lo == 0x5C && op2 == 0x598F) {
                while (get_reg_b(cpu, 0xC) != 0) {
                    uint8_t b = bus_read8(cpu->bus, cpu->er[5]);
                    bus_write8(cpu->bus, cpu->er[6], b);
                    cpu->er[5]++; cpu->er[6]++;
                    set_reg_b(cpu, 0xC, (uint8_t)((get_reg_b(cpu, 0xC) - 1) & 0xFF));
                }
            } else if (lo == 0xD4 && op2 == 0x598F) {
                while (get_r(cpu, 4) != 0) {
                    uint8_t b = bus_read8(cpu->bus, cpu->er[5]);
                    bus_write8(cpu->bus, cpu->er[6], b);
                    cpu->er[5]++; cpu->er[6]++;
                    set_r(cpu, 4, (uint16_t)((get_r(cpu, 4) - 1) & 0xFFFF));
                }
            } else { unimplemented(cpu, op, cpu->pc - 4); }
            break;
        }
        case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            decode7C_7F(cpu, hi, lo); break;
        default: unimplemented(cpu, op, cpu->pc - 2); break;
    }
}

/* ======== Main decode ======== */
CPU_INLINE void decode(h8s_cpu_t *cpu, uint16_t op) {
    int hi = (op >> 8) & 0xFF;
    int lo = op & 0xFF;

    switch (hi >> 4) {
        case 0x0: decode0(cpu, op, hi, lo); break;
        case 0x1: decode1(cpu, op, hi, lo); break;
        case 0x2: { /* MOV.B @aa:8, Rd */
            int rd = (op >> 8) & 0xF;
            uint32_t addr = 0xFFFF00 | (op & 0xFF);
            int val = bus_read8(cpu->bus, addr);
            set_reg_b(cpu, rd, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x3: { /* MOV.B Rs, @aa:8 */
            int rd = (op >> 8) & 0xF;
            uint32_t addr = 0xFFFF00 | (op & 0xFF);
            int val = get_reg_b(cpu, rd);
            bus_write8(cpu->bus, addr, (uint8_t)val); set_nz_b(cpu, val); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0x4: { /* Bcc d:8 */
            int cond = (op >> 8) & 0xF;
            int8_t disp = (int8_t)(op & 0xFF);
            uint32_t target = (cpu->pc + disp) & 0xFFFFFF;
            bool taken = evaluate_condition(cpu, cond);
            if (taken) cpu->pc = target;
            branch_profile_record(1, taken, target);
            break;
        }
        case 0x5: decode5(cpu, op, hi, lo); break;
        case 0x6: decode6(cpu, op, hi, lo); break;
        case 0x7: decode7(cpu, op, hi, lo); break;
        case 0x8: { /* ADD.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int d = get_reg_b(cpu, rd); int result = d + imm;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF)); set_nz_b(cpu, result);
            set_flag(cpu, BIT_C, result > 0xFF);
            set_flag(cpu, BIT_V, ((d ^ result) & (imm ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x10) != 0);
            break;
        }
        case 0x9: { /* ADDX.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int d = get_reg_b(cpu, rd); int c = get_flag(cpu, BIT_C) ? 1 : 0;
            int result = d + imm + c;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF));
            set_flag(cpu, BIT_C, result > 0xFF);
            set_flag(cpu, BIT_V, ((d ^ result) & (imm ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x10) != 0);
            if ((result & 0xFF) != 0) set_flag(cpu, BIT_Z, false);
            set_flag(cpu, BIT_N, (result & 0x80) != 0);
            break;
        }
        case 0xA: { /* CMP.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int d = get_reg_b(cpu, rd); int result = d - imm;
            set_nz_b(cpu, result);
            set_flag(cpu, BIT_C, (result & 0x100) != 0);
            set_flag(cpu, BIT_V, ((d ^ imm) & (d ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x10) != 0);
            break;
        }
        case 0xB: { /* SUBX.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int d = get_reg_b(cpu, rd); int c = get_flag(cpu, BIT_C) ? 1 : 0;
            int result = d - imm - c;
            set_reg_b(cpu, rd, (uint8_t)(result & 0xFF));
            set_flag(cpu, BIT_C, result < 0);
            set_flag(cpu, BIT_V, ((d ^ imm) & (d ^ result) & 0x80) != 0);
            set_flag(cpu, BIT_H, ((d ^ imm ^ result) & 0x10) != 0);
            if ((result & 0xFF) != 0) set_flag(cpu, BIT_Z, false);
            set_flag(cpu, BIT_N, (result & 0x80) != 0);
            break;
        }
        case 0xC: { /* OR.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int result = get_reg_b(cpu, rd) | imm;
            set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0xD: { /* XOR.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int result = get_reg_b(cpu, rd) ^ imm;
            set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0xE: { /* AND.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            int result = get_reg_b(cpu, rd) & imm;
            set_reg_b(cpu, rd, (uint8_t)result); set_nz_b(cpu, result); set_flag(cpu, BIT_V, false);
            break;
        }
        case 0xF: { /* MOV.B #imm, Rd */
            int rd = (op >> 8) & 0xF; int imm = op & 0xFF;
            set_reg_b(cpu, rd, (uint8_t)imm); set_nz_b(cpu, imm); set_flag(cpu, BIT_V, false);
            break;
        }
        default: unimplemented(cpu, op, cpu->pc - 2); break;
    }
}

/* ======== Interrupt handling ======== */
static bool process_interrupts(h8s_cpu_t *cpu) {
    if (cpu->pending_irq_count == 0) return false;
    if (get_flag(cpu, BIT_I)) return false;

    int vector = cpu->pending_irqs[0];
    cpu->pending_irq_count--;
    if (cpu->pending_irq_count > 0)
        memmove(&cpu->pending_irqs[0], &cpu->pending_irqs[1],
                (size_t)cpu->pending_irq_count * sizeof(int));

    cpu->er[7] -= 4;
    bus_write32(cpu->bus, cpu->er[7], ((uint32_t)cpu->ccr << 24) |
                                    (cpu->pc & 0xFFFFFF));

    set_flag(cpu, BIT_I, true);

    uint32_t handler = bus_read32(cpu->bus, (uint32_t)vector * 4) & 0xFFFFFF;
    cpu->pc = handler;

    if (cpu->halted) cpu->halted = false;
    return true;
}

/* ======== Public API ======== */

void h8s_cpu_init(h8s_cpu_t *cpu, address_bus_t *bus) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus = bus;
    cpu->ccr = 0x80;
    h8s_block_cache_init(&cpu->semantic_block_cache, H8S_BLOCK_MAX_INSTRUCTIONS);
    h8s_mutable_block_cache_init(&cpu->mutable_block_cache, H8S_BLOCK_MAX_INSTRUCTIONS);
    h8s_branch_edge_cache_init(&cpu->semantic_edge_cache);
}

void h8s_cpu_reset(h8s_cpu_t *cpu) {
    for (int i = 0; i < 8; i++) cpu->er[i] = 0;
    cpu->ccr = 0x80;
    cpu->exr = 0;
    cpu->halted = false;
    cpu->cycle_count = 0;
    cpu->irq_deferred = false;
    cpu->pending_irq_count = 0;
    cpu->fetch_data = NULL;
    cpu->fetch_base = cpu->fetch_end = 0;
    cpu->prefetch_pc = 0;
    cpu->prefetch_word = 0;
    cpu->prefetch_valid = false;
    cpu->fetch_immutable = false;
    cpu->rom_block_base = 0;
    cpu->rom_block_count = 0;
    cpu->rom_block_valid = false;
    h8s_block_cache_clear(&cpu->semantic_block_cache);
    h8s_mutable_block_cache_clear(&cpu->mutable_block_cache);
    if (cpu->bus) bus_clear_code_watches(cpu->bus);
    h8s_branch_edge_cache_clear(&cpu->semantic_edge_cache);
    memset(cpu->semantic_reject_cache, 0, sizeof(cpu->semantic_reject_cache));
    memset(cpu->mutable_reject_cache, 0, sizeof(cpu->mutable_reject_cache));
    cpu->semantic_reject_backoff = 0;
    cpu->semantic_fast_blocks = 0;
    cpu->semantic_fast_cycles = 0;
    cpu->semantic_mutable_fast_blocks = 0;
    cpu->semantic_mutable_fast_cycles = 0;
    cpu->semantic_fast_rejects = 0;
    cpu->semantic_fast_cached_rejects = 0;
    cpu->semantic_fast_backoff_skips = 0;
    cpu->semantic_fast_reject_guard = 0;
    cpu->semantic_fast_reject_irq = 0;
    cpu->semantic_fast_reject_window = 0;
    cpu->semantic_fast_reject_cached = 0;
    cpu->semantic_fast_reject_unsupported_block = 0;
    cpu->semantic_fast_reject_unsupported_exit = 0;
    cpu->semantic_fast_reject_cycle_budget = 0;
    cpu->semantic_fast_reject_branch_resolve = 0;
    cpu->semantic_fast_reject_target = 0;
    cpu->semantic_mutable_reject_cached = 0;
    cpu->semantic_mutable_reject_unsupported_block = 0;
    cpu->semantic_mutable_reject_static_nonplain = 0;
    cpu->semantic_mutable_reject_unsupported_exit = 0;
    cpu->semantic_mutable_reject_cycle_budget = 0;
    cpu->semantic_mutable_reject_execute = 0;
    cpu->semantic_mutable_reject_target = 0;
    cpu->semantic_mutable_execute_nonplain_read = 0;
    cpu->semantic_mutable_execute_nonplain_write = 0;
    cpu->semantic_mutable_execute_semantic_run = 0;
    cpu->semantic_mutable_execute_branch_resolve = 0;
    cpu->semantic_mutable_execute_return_read = 0;
    cpu->semantic_mutable_execute_call_write = 0;
    cpu->semantic_mutable_execute_other = 0;
    cpu->semantic_mutable_prefix_blocks = 0;
    cpu->semantic_mutable_prefix_cycles = 0;
    cpu->hot_plain_memory_2b_instructions = 0;
    cpu->hot_plain_memory_4b_instructions = 0;
    cpu->hot_prefix0100_plain_memory_instructions = 0;
    cpu->pc = bus_read32(cpu->bus, 0x000000) & 0xFFFFFF;
}

CPU_INLINE h8s_semantic_reject_entry_t *
semantic_reject_entry(h8s_cpu_t *cpu, const uint8_t *data, uint32_t pc)
{
    uintptr_t key = ((uintptr_t)data >> 4) ^ (uintptr_t)(pc * 2654435761u);
    return &cpu->semantic_reject_cache[key & (H8S_SEMANTIC_REJECT_CACHE_ENTRIES - 1)];
}

CPU_INLINE bool semantic_reject_cached(h8s_cpu_t *cpu,
                                       const uint8_t *data,
                                       uint32_t pc)
{
    h8s_semantic_reject_entry_t *entry =
        semantic_reject_entry(cpu, data, pc);
    return entry->valid && entry->data == data && entry->pc == pc;
}

CPU_INLINE void semantic_reject_cache_store(h8s_cpu_t *cpu,
                                            const uint8_t *data,
                                            uint32_t pc)
{
    h8s_semantic_reject_entry_t *entry =
        semantic_reject_entry(cpu, data, pc);
    entry->valid = true;
    entry->data = data;
    entry->pc = pc;
}

CPU_INLINE h8s_mutable_reject_entry_t *
mutable_reject_entry(h8s_cpu_t *cpu, uint32_t pc)
{
    return &cpu->mutable_reject_cache[
        ((pc >> 1) ^ (pc >> 12)) & (H8S_MUTABLE_REJECT_CACHE_ENTRIES - 1)];
}

CPU_INLINE bool mutable_reject_cached(h8s_cpu_t *cpu, uint32_t pc)
{
    h8s_mutable_reject_entry_t *entry = mutable_reject_entry(cpu, pc);
    if (!entry->valid || entry->pc != (pc & 0xffffffu)) return false;
    if (bus_code_page_generation(cpu->bus, entry->first_page << 12) !=
        entry->first_generation) return false;
    if (entry->last_page != entry->first_page &&
        bus_code_page_generation(cpu->bus, entry->last_page << 12) !=
        entry->last_generation) return false;
    return true;
}

CPU_INLINE void mutable_reject_cache_store(h8s_cpu_t *cpu, uint32_t pc,
                                           const h8s_block_t *block)
{
    if (!cpu || !block) return;
    uint32_t bytes = block->bytes ? block->bytes : block->branch_bytes;
    if (bytes == 0) bytes = 2;
    pc &= 0xffffffu;
    uint32_t first_page = pc >> 12;
    uint32_t last_page = ((pc + bytes - 1) & 0xffffffu) >> 12;
    if (last_page < first_page) last_page = 4095u;
    bus_watch_code_range(cpu->bus, pc, bytes);
    h8s_mutable_reject_entry_t *entry = mutable_reject_entry(cpu, pc);
    entry->valid = true;
    entry->pc = pc;
    entry->first_page = first_page;
    entry->last_page = last_page;
    entry->first_generation = bus_code_page_generation(cpu->bus, pc);
    entry->last_generation = bus_code_page_generation(cpu->bus, last_page << 12);
}

CPU_INLINE bool semantic_fast_reject_with_backoff(h8s_cpu_t *cpu,
                                                  uint64_t *counter,
                                                  unsigned profile_reason,
                                                  uint32_t pc)
{
    cpu->semantic_reject_backoff = H8S_SEMANTIC_REJECT_BACKOFF;
    return semantic_fast_reject(cpu, counter, profile_reason, pc);
}

static void record_mutable_execute_failure(h8s_cpu_t *cpu,
                                           h8s_mixed_failure_t failure)
{
    switch (failure) {
    case H8S_MIXED_FAILURE_DYNAMIC_NONPLAIN_READ:
        cpu->semantic_mutable_execute_nonplain_read++;
        break;
    case H8S_MIXED_FAILURE_DYNAMIC_NONPLAIN_WRITE:
        cpu->semantic_mutable_execute_nonplain_write++;
        break;
    case H8S_MIXED_FAILURE_SEMANTIC_RUN:
        cpu->semantic_mutable_execute_semantic_run++;
        break;
    case H8S_MIXED_FAILURE_BRANCH_RESOLVE:
        cpu->semantic_mutable_execute_branch_resolve++;
        break;
    case H8S_MIXED_FAILURE_RETURN_READ:
        cpu->semantic_mutable_execute_return_read++;
        break;
    case H8S_MIXED_FAILURE_CALL_WRITE:
        cpu->semantic_mutable_execute_call_write++;
        break;
    default:
        cpu->semantic_mutable_execute_other++;
        break;
    }
}

CPU_INLINE void execute_step(h8s_cpu_t *cpu) {
    /* CCR-control instructions inhibit interrupt acceptance through the next
     * instruction. CyOS uses ANDC followed by a stack-pointer load during a
     * task switch; taking an IRQ between them corrupts the task context. */
    if (cpu->irq_deferred) cpu->irq_deferred = false;
    else if (cpu->pending_irq_count && !(cpu->ccr & CCR_I) && process_interrupts(cpu)) {
        cpu->cycle_count++;
        return;
    }

    if (cpu->halted) {
        if (cpu->pending_irq_count > 0) {
            cpu->halted = false;
        } else {
            cpu->cycle_count++;
            return;
        }
    }

    cpu->last_start_pc = cpu->pc;
    uint16_t op = fetch16(cpu);
    opcode_profile_record(op);
    if (execute_hot_plain_memory_2b(cpu, op)) {
        cpu->cycle_count++;
        return;
    }
    if (execute_hot_plain_memory_4b(cpu, op)) {
        cpu->cycle_count++;
        return;
    }
    if (execute_hot_prefix0100_plain_memory(cpu, op)) {
        cpu->cycle_count++;
        return;
    }
    decode(cpu, op);
    cpu->cycle_count++;
}

void h8s_cpu_step(h8s_cpu_t *cpu) {
    execute_step(cpu);
}

bool h8s_cpu_get_immutable_fetch_window(h8s_cpu_t *cpu, const uint8_t **data,
                                        uint32_t *base, uint32_t *size)
{
    if (!cpu || !data || !base || !size) return false;
    uint32_t pc = cpu->pc & 0xffffff;
    if (!cpu->fetch_data || pc < cpu->fetch_base || pc >= cpu->fetch_end)
        cache_instruction_memory(cpu, pc);
    if (!cpu->fetch_immutable || !cpu->fetch_data ||
        pc < cpu->fetch_base || pc >= cpu->fetch_end)
        return false;

    *data = cpu->fetch_data;
    *base = cpu->fetch_base;
    *size = cpu->fetch_end - cpu->fetch_base;
    return true;
}

static bool h8s_cpu_get_mutable_fetch_window(h8s_cpu_t *cpu, const uint8_t **data,
                                             uint32_t *base, uint32_t *size)
{
    if (!cpu || !data || !base || !size) return false;
    uint32_t pc = cpu->pc & 0xffffff;
    if (!cpu->fetch_data || pc < cpu->fetch_base || pc >= cpu->fetch_end)
        cache_instruction_memory(cpu, pc);
    if (cpu->fetch_immutable || !cpu->fetch_data ||
        pc < cpu->fetch_base || pc >= cpu->fetch_end)
        return false;
    if (!bus_is_plain_read_range(cpu->bus, pc, 2))
        return false;

    *data = cpu->fetch_data;
    *base = cpu->fetch_base;
    *size = cpu->fetch_end - cpu->fetch_base;
    return true;
}

static bool h8s_cpu_pc_is_immutable_target(const h8s_cpu_t *cpu, uint32_t pc)
{
    if (!cpu || !cpu->bus || !cpu->bus->machine) return false;
    const cybiko_machine_t *m = cpu->bus->machine;
    pc &= 0xffffffu;
    return pc <= m->boot_end ||
           (m->flash_size && pc >= m->flash_base && pc <= m->flash_end);
}

static bool h8s_cpu_try_execute_semantic_mutable_block(h8s_cpu_t *cpu, int limit,
                                                       int *cycles,
                                                       uint32_t start_pc)
{
    if (!cpu->bus || !cpu->bus->machine ||
        cpu->bus->machine->model != CYBIKO_XTREME)
        return false;
    const uint8_t *data = NULL;
    uint32_t base = 0, size = 0;
    if (!h8s_cpu_get_mutable_fetch_window(cpu, &data, &base, &size))
        return false;
    if (start_pc < base || start_pc >= base + size)
        return false;
    if (mutable_reject_cached(cpu, start_pc)) {
        cpu->semantic_mutable_reject_cached++;
        cpu->semantic_reject_backoff = H8S_SEMANTIC_CACHED_REJECT_BACKOFF;
        return false;
    }

    const h8s_block_t *block =
        h8s_mutable_block_cache_get(&cpu->mutable_block_cache, cpu->bus,
                                    data, size, base, start_pc);
    if (!block)
        return false;
    if (!h8s_mixed_plain_block_supported(block)) {
        cpu->semantic_mutable_reject_unsupported_block++;
        mutable_reject_cache_store(cpu, start_pc, block);
        return false;
    }
    if (h8s_mixed_plain_block_has_static_nonplain_memory(block, cpu->bus)) {
        cpu->semantic_mutable_reject_static_nonplain++;
        mutable_reject_cache_store(cpu, start_pc, block);
        return false;
    }
    if (block->branch_kind != H8S_BLOCK_BRANCH_BCC8 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BCC16 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BSR8 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BSR16 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JMP_ABS24 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JSR_ABS24 &&
        !(block->branch_kind == H8S_BLOCK_BRANCH_INDIRECT &&
          (((uint8_t)(block->branch_op >> 8)) == 0x59 ||
           ((uint8_t)(block->branch_op >> 8)) == 0x5d)) &&
        !(block->branch_kind == H8S_BLOCK_BRANCH_RETURN &&
          block->branch_op == 0x5470)) {
        cpu->semantic_mutable_reject_unsupported_exit++;
        mutable_reject_cache_store(cpu, start_pc, block);
        return false;
    }

    int block_cycles = (int)block->instructions + 1;
    if (block_cycles < 1 || block_cycles > limit) {
        cpu->semantic_mutable_reject_cycle_budget++;
        return false;
    }

    uint32_t offset = start_pc - base;
    h8s_block_cpu_state_t state = {.ccr = cpu->ccr, .pc = offset};
    for (unsigned i = 0; i < 8; ++i)
        state.er[i] = cpu->er[i];

    uint32_t next_offset = 0;
    if (!h8s_execute_mixed_plain_block_exit(block, &cpu->semantic_edge_cache,
                                            cpu->bus, base, &state, &next_offset)) {
        h8s_mixed_failure_t failure =
            h8s_classify_mixed_plain_block_failure(block,
                                                   &cpu->semantic_edge_cache,
                                                   cpu->bus, base, &state);
        record_mutable_execute_failure(cpu, failure);
        if (failure == H8S_MIXED_FAILURE_DYNAMIC_NONPLAIN_READ ||
            failure == H8S_MIXED_FAILURE_DYNAMIC_NONPLAIN_WRITE) {
            uint32_t failed_pc = 0, failed_address = 0;
            uint16_t failed_op = 0;
            bool failed_write = false;
            if (h8s_find_mixed_plain_block_first_nonplain(block, cpu->bus,
                                                          &state, &failed_pc,
                                                          &failed_op,
                                                          &failed_address,
                                                          &failed_write)) {
                mutable_nonplain_profile_record((base + failed_pc) & 0xffffffu,
                                                failed_op, failed_address,
                                                failed_write);
            }
            int prefix_cycles = 0;
            h8s_block_cpu_state_t prefix_state = state;
            if (h8s_execute_mixed_plain_block_prefix(block, cpu->bus,
                                                     &prefix_state,
                                                     &prefix_cycles) &&
                prefix_cycles > 0 && prefix_cycles <= limit) {
                cpu->last_start_pc = start_pc;
                for (unsigned i = 0; i < 8; ++i)
                    cpu->er[i] = prefix_state.er[i];
                cpu->ccr = prefix_state.ccr;
                cpu->pc = (base + prefix_state.pc) & 0xffffffu;
                cpu->cycle_count += (uint64_t)prefix_cycles;
                cpu->semantic_reject_backoff = 0;
                cpu->semantic_fast_blocks++;
                cpu->semantic_fast_cycles += (uint64_t)prefix_cycles;
                cpu->semantic_mutable_fast_blocks++;
                cpu->semantic_mutable_fast_cycles += (uint64_t)prefix_cycles;
                cpu->semantic_mutable_prefix_blocks++;
                cpu->semantic_mutable_prefix_cycles += (uint64_t)prefix_cycles;
                *cycles = prefix_cycles;
                return true;
            }
        }
        cpu->semantic_mutable_reject_execute++;
        return false;
    }

    uint32_t next_pc = next_offset;
    if (block->branch_kind != H8S_BLOCK_BRANCH_JMP_ABS24 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JSR_ABS24 &&
        block->branch_kind != H8S_BLOCK_BRANCH_INDIRECT &&
        block->branch_kind != H8S_BLOCK_BRANCH_RETURN) {
        if (next_offset >= size) {
            cpu->semantic_mutable_reject_target++;
            return false;
        }
        next_pc = (base + next_offset) & 0xffffff;
    }
    if (!bus_is_plain_read_range(cpu->bus, next_pc, 2)) {
        cpu->semantic_mutable_reject_target++;
        return false;
    }

    cpu->last_start_pc = start_pc;
    for (unsigned i = 0; i < 8; ++i)
        cpu->er[i] = state.er[i];
    cpu->ccr = state.ccr;
    cpu->pc = next_pc & 0xffffff;
    cpu->cycle_count += (uint64_t)block_cycles;
    cpu->semantic_reject_backoff = 0;
    cpu->semantic_fast_blocks++;
    cpu->semantic_fast_cycles += (uint64_t)block_cycles;
    cpu->semantic_mutable_fast_blocks++;
    cpu->semantic_mutable_fast_cycles += (uint64_t)block_cycles;
    *cycles = block_cycles;
    return true;
}

bool h8s_cpu_try_execute_semantic_rom_block(h8s_cpu_t *cpu, int limit,
                                            int *cycles)
{
    if (!cpu || !cycles) return false;
    uint32_t start_pc = cpu->pc & 0xffffff;
    if (limit <= 0 || cpu->halted || cpu->irq_deferred) {
        return semantic_fast_reject(cpu, &cpu->semantic_fast_reject_guard,
                                    SEM_REJECT_GUARD, start_pc);
    }
    if (cpu->pending_irq_count && !(cpu->ccr & CCR_I)) {
        return semantic_fast_reject(cpu, &cpu->semantic_fast_reject_irq,
                                    SEM_REJECT_IRQ, start_pc);
    }

    const uint8_t *data = NULL;
    uint32_t base = 0, size = 0;
    if (!h8s_cpu_get_immutable_fetch_window(cpu, &data, &base, &size)) {
        if (h8s_cpu_try_execute_semantic_mutable_block(cpu, limit, cycles, start_pc))
            return true;
        if (cpu->semantic_reject_backoff) {
            semantic_window_profile_record(cpu, start_pc);
            return semantic_fast_reject(cpu, &cpu->semantic_fast_reject_window,
                                        SEM_REJECT_WINDOW, start_pc);
        }
        semantic_window_profile_record(cpu, start_pc);
        return semantic_fast_reject_with_backoff(cpu, &cpu->semantic_fast_reject_window,
                                                 SEM_REJECT_WINDOW, start_pc);
    }
    if (start_pc < base || start_pc >= base + size) {
        return semantic_fast_reject_with_backoff(cpu, &cpu->semantic_fast_reject_window,
                                                 SEM_REJECT_WINDOW, start_pc);
    }
    if (semantic_reject_cached(cpu, data, start_pc)) {
        cpu->semantic_fast_cached_rejects++;
        cpu->semantic_fast_reject_cached++;
        cpu->semantic_reject_backoff = H8S_SEMANTIC_CACHED_REJECT_BACKOFF;
        return semantic_fast_reject(cpu, NULL, SEM_REJECT_CACHED, start_pc);
    }

    uint32_t offset = start_pc - base;
    const h8s_block_t *block =
        h8s_block_cache_get(&cpu->semantic_block_cache, data, size, offset);
    bool rom_return_block = block &&
        block->branch_kind == H8S_BLOCK_BRANCH_RETURN &&
        block->branch_op == 0x5470 &&
        h8s_mixed_plain_block_supported(block);
    if (!block || (!h8s_semantic_block_supported(block) && !rom_return_block)) {
        semantic_reject_cache_store(cpu, data, start_pc);
        return semantic_fast_reject_with_backoff(cpu, &cpu->semantic_fast_reject_unsupported_block,
                                                 SEM_REJECT_UNSUPPORTED_BLOCK, start_pc);
    }
    if (block->branch_kind != H8S_BLOCK_BRANCH_BCC8 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BCC16 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JMP_ABS24 &&
        !(block->branch_kind == H8S_BLOCK_BRANCH_RETURN &&
          block->branch_op == 0x5470)) {
        semantic_reject_cache_store(cpu, data, start_pc);
        return semantic_fast_reject_with_backoff(cpu, &cpu->semantic_fast_reject_unsupported_exit,
                                                 SEM_REJECT_UNSUPPORTED_EXIT, start_pc);
    }

    int block_cycles = (int)block->instructions + 1; /* Include branch exit. */
    if (block_cycles < 1 || block_cycles > limit) {
        return semantic_fast_reject(cpu, &cpu->semantic_fast_reject_cycle_budget,
                                    SEM_REJECT_CYCLE_BUDGET, start_pc);
    }

    h8s_block_cpu_state_t state = {.ccr = cpu->ccr, .pc = offset};
    for (unsigned i = 0; i < 8; ++i)
        state.er[i] = cpu->er[i];

    uint32_t next_offset = 0;
    bool executed = false;
    if (block->branch_kind == H8S_BLOCK_BRANCH_RETURN) {
        executed = h8s_execute_mixed_plain_block_exit(block,
                                                      &cpu->semantic_edge_cache,
                                                      cpu->bus, base, &state,
                                                      &next_offset);
    } else {
        executed = h8s_execute_semantic_block_exit(block,
                                                   &cpu->semantic_edge_cache,
                                                   &state, &next_offset);
    }
    if (!executed) {
        return semantic_fast_reject(cpu, &cpu->semantic_fast_reject_branch_resolve,
                                    SEM_REJECT_BRANCH_RESOLVE, start_pc);
    }
    uint32_t next_pc = next_offset;
    if (block->branch_kind == H8S_BLOCK_BRANCH_JMP_ABS24 ||
        block->branch_kind == H8S_BLOCK_BRANCH_RETURN) {
        if (!h8s_cpu_pc_is_immutable_target(cpu, next_pc)) {
            semantic_reject_cache_store(cpu, data, start_pc);
            return semantic_fast_reject_with_backoff(cpu, &cpu->semantic_fast_reject_target,
                                                     SEM_REJECT_TARGET, start_pc);
        }
    } else {
        if (next_offset >= size) {
            return semantic_fast_reject_with_backoff(cpu, &cpu->semantic_fast_reject_target,
                                                     SEM_REJECT_TARGET, start_pc);
        }
        next_pc = (base + next_offset) & 0xffffff;
    }

    cpu->last_start_pc = start_pc;
    for (unsigned i = 0; i < 8; ++i)
        cpu->er[i] = state.er[i];
    cpu->ccr = state.ccr;
    cpu->pc = next_pc & 0xffffff;
    cpu->cycle_count += (uint64_t)block_cycles;
    cpu->semantic_reject_backoff = 0;
    cpu->semantic_fast_blocks++;
    cpu->semantic_fast_cycles += (uint64_t)block_cycles;
    *cycles = block_cycles;
    return true;
}

int h8s_cpu_run(h8s_cpu_t *cpu, int limit, int frame_cycle,
                int *timer_debt, int *completion_debt, bool *io_access) {
    int done = 0;
    while (done < limit) {
        if (cpu->semantic_reject_backoff) {
            int backoff_cycles = cpu->semantic_reject_backoff;
            int remaining = limit - done;
            if (backoff_cycles > remaining) backoff_cycles = remaining;
            for (int i = 0; i < backoff_cycles; ++i) {
                cpu->bus->speaker->frame_cycle = frame_cycle + done;
                cpu->semantic_reject_backoff--;
                cpu->semantic_fast_backoff_skips++;
                ++*timer_debt;
                if (done + 1 == limit && cpu->bus->sync_peripherals)
                    cpu->bus->sync_peripherals(cpu->bus->sync_ctx);
                execute_step(cpu);
                ++*completion_debt;
                ++done;
                if (CPU_UNLIKELY(*io_access || cpu->halted)) break;
            }
            if (CPU_UNLIKELY(*io_access || cpu->halted) || done >= limit)
                break;
        }
        cpu->bus->speaker->frame_cycle = frame_cycle + done;
        int fast_cycles = 0;
        int remaining = limit - done;
        bool can_skip_mid_block_sync =
            !cpu->bus->sync_peripherals ||
            remaining > (H8S_BLOCK_MAX_INSTRUCTIONS + 1);
        if (can_skip_mid_block_sync &&
            h8s_cpu_try_execute_semantic_rom_block(cpu, remaining, &fast_cycles)) {
            *timer_debt += fast_cycles;
            *completion_debt += fast_cycles;
            done += fast_cycles;
            if (CPU_UNLIKELY(*io_access || cpu->halted)) break;
            continue;
        }

        ++*timer_debt;
        if (done + 1 == limit && cpu->bus->sync_peripherals)
            cpu->bus->sync_peripherals(cpu->bus->sync_ctx);
        execute_step(cpu);
        ++*completion_debt;
        ++done;
        if (CPU_UNLIKELY(*io_access || cpu->halted)) break;
    }
    return done;
}

void h8s_cpu_request_interrupt(h8s_cpu_t *cpu, int vector) {
    for (int i = 0; i < cpu->pending_irq_count; i++) {
        if (cpu->pending_irqs[i] == vector) return;
        if (cpu->pending_irqs[i] > vector) {
            if (cpu->pending_irq_count < MAX_PENDING_IRQS) {
                memmove(&cpu->pending_irqs[i + 1], &cpu->pending_irqs[i],
                        (size_t)(cpu->pending_irq_count - i) * sizeof(int));
                cpu->pending_irqs[i] = vector;
                cpu->pending_irq_count++;
            }
            return;
        }
    }
    if (cpu->pending_irq_count < MAX_PENDING_IRQS) {
        cpu->pending_irqs[cpu->pending_irq_count++] = vector;
    }
}

uint32_t h8s_cpu_get_er(const h8s_cpu_t *cpu, int n) { return cpu->er[n]; }
void h8s_cpu_cancel_interrupt(h8s_cpu_t *cpu, int vector) {
    for (int i = 0; i < cpu->pending_irq_count; ++i) {
        if (cpu->pending_irqs[i] != vector) continue;
        --cpu->pending_irq_count;
        memmove(&cpu->pending_irqs[i], &cpu->pending_irqs[i + 1],
                (size_t)(cpu->pending_irq_count - i) * sizeof(int));
        return;
    }
}
void h8s_cpu_set_er(h8s_cpu_t *cpu, int n, uint32_t value) { cpu->er[n] = value; }

void h8s_cpu_dump_registers(const h8s_cpu_t *cpu) {
    printf("=== CPU Registers ===\n");
    for (int i = 0; i < 8; i++) {
        printf("  ER%d = 0x%08X  (E%d=0x%04X R%d=0x%04X R%dH=0x%02X R%dL=0x%02X)\n",
            i, cpu->er[i], i, get_e(cpu, i), i, get_r(cpu, i),
            i, get_rh(cpu, i), i, get_rl(cpu, i));
    }
    printf("  PC  = 0x%06X\n", cpu->pc);
    printf("  CCR = 0x%02X [%s%s%s%s%s%s%s%s]\n", cpu->ccr,
        get_flag(cpu, BIT_I) ? "I" : "-", get_flag(cpu, BIT_UI) ? "U" : "-",
        get_flag(cpu, BIT_H) ? "H" : "-", get_flag(cpu, BIT_U) ? "u" : "-",
        get_flag(cpu, BIT_N) ? "N" : "-", get_flag(cpu, BIT_Z) ? "Z" : "-",
        get_flag(cpu, BIT_V) ? "V" : "-", get_flag(cpu, BIT_C) ? "C" : "-");
    printf("  EXR = 0x%02X\n", cpu->exr);
    printf("  Cycles: %llu\n", (unsigned long long)cpu->cycle_count);
}

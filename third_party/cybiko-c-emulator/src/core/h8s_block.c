#include "core/h8s_block.h"
#include <string.h>

#define BLOCK_CCR_C 0x01
#define BLOCK_CCR_V 0x02
#define BLOCK_CCR_Z 0x04
#define BLOCK_CCR_N 0x08
#define BLOCK_CCR_H 0x20

static uint8_t block_get_reg_b(const h8s_block_cpu_state_t *state, unsigned n)
{
    return n < 8 ? (uint8_t)((state->er[n] >> 8) & 0xff) :
                   (uint8_t)(state->er[n - 8] & 0xff);
}

static void block_set_reg_b(h8s_block_cpu_state_t *state, unsigned n, uint8_t value)
{
    if (n < 8)
        state->er[n] = (state->er[n] & 0xffff00ffu) | ((uint32_t)value << 8);
    else
        state->er[n - 8] = (state->er[n - 8] & 0xffffff00u) | value;
}

static uint16_t block_get_r(const h8s_block_cpu_state_t *state, unsigned n)
{
    if (n < 8) return (uint16_t)(state->er[n] & 0xffff);
    return (uint16_t)((state->er[n - 8] >> 16) & 0xffff);
}

static void block_set_r(h8s_block_cpu_state_t *state, unsigned n, uint16_t value)
{
    if (n < 8)
        state->er[n] = (state->er[n] & 0xffff0000u) | value;
    else
        state->er[n - 8] = (state->er[n - 8] & 0x0000ffffu) | ((uint32_t)value << 16);
}

static void block_set_flag(h8s_block_cpu_state_t *state, uint8_t mask, bool value)
{
    if (value) state->ccr |= mask;
    else state->ccr &= (uint8_t)~mask;
}

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static bool is_control_transfer(uint16_t op)
{
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;

    if ((hi >> 4) == 0x4) return true;       /* Bcc d:8 */
    if (hi == 0x01 && lo == 0x80) return true; /* SLEEP */
    if (hi >= 0x54 && hi <= 0x5f) return true; /* RTS/BSR/RTE/TRAPA/JMP/JSR */
    return false;
}

static bool is_tier1_executable(uint16_t op)
{
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;

    if ((hi >> 4) >= 0x8) return true; /* immediate byte ALU/MOV */

    switch (hi) {
    case 0x08: case 0x09: case 0x0c: case 0x0d: case 0x0e:
    case 0x0a: case 0x0b: case 0x0f:
    case 0x18: case 0x19: case 0x1c: case 0x1d: case 0x1e:
    case 0x1a: case 0x1b: case 0x1f:
        return true;
    case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x64: case 0x65: case 0x66:
        return true;
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
        return true;
    default:
        break;
    }

    if (hi >= 0x10 && hi <= 0x13) return true; /* shifts/rotates */
    if (hi == 0x60 || hi == 0x61 || hi == 0x62 || hi == 0x63) return true;
    if (hi == 0x79 && ((lo >> 4) & 0xf) <= 6) return true;
    if (hi == 0x7a && ((lo >> 4) & 0xf) <= 6) return true;
    return false;
}

static bool prefixed_01_length(const uint8_t *rom, size_t rom_size,
                               uint32_t pc, uint16_t op, unsigned *bytes,
                               bool *control_stop)
{
    uint8_t lo = (uint8_t)op;
    if (lo == 0x80) {
        *control_stop = true;
        return false;
    }
    if ((size_t)pc + 3 >= rom_size) return false;

    uint16_t op2 = read_be16(rom + pc + 2);
    uint8_t hi2 = (uint8_t)(op2 >> 8);
    uint8_t lo2 = (uint8_t)op2;

    switch (lo) {
    case 0x00:
        switch (hi2) {
        case 0x69: case 0x6d:
            *bytes = 4;
            return true;
        case 0x6b:
            *bytes = (lo2 & 0x20) ? 8 : 6;
            return true;
        case 0x6f:
            *bytes = 6;
            return true;
        case 0x78:
            *bytes = 8;
            return true;
        default:
            *bytes = 4;
            return true;
        }
    case 0x10: case 0x20: case 0x30:
    case 0x41:
        *bytes = 4;
        return true;
    case 0x40:
        switch (hi2) {
        case 0x69: case 0x6d:
            *bytes = 4;
            return true;
        case 0x6b:
            *bytes = (lo2 & 0x20) ? 8 : 6;
            return true;
        case 0x6f:
            *bytes = 6;
            return true;
        default:
            *bytes = 4;
            return true;
        }
    case 0xc0: case 0xd0: case 0xf0:
        if (hi2 == 0x6b) {
            *bytes = (lo2 & 0x20) ? 8 : 6;
        } else {
            *bytes = 4;
        }
        return true;
    default:
        *bytes = 4;
        return true;
    }
}

static bool instruction_length(const uint8_t *rom, size_t rom_size,
                               uint32_t pc, uint16_t op, unsigned *bytes,
                               bool *prefix_stop, bool *control_stop)
{
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;
    *prefix_stop = false;
    *control_stop = false;

    switch (hi >> 4) {
    case 0x0:
        if (hi == 0x01)
            return prefixed_01_length(rom, rom_size, pc, op, bytes, control_stop);
        *bytes = 2;
        return true;
    case 0x1:
        *bytes = 2;
        return true;
    case 0x2: case 0x3: case 0x4: case 0x5:
        *bytes = 2;
        return true;
    case 0x6:
        if (hi == 0x6a) {
            uint8_t top_nibble = (uint8_t)(lo >> 4);
            if (top_nibble == 1 || top_nibble == 3) {
                *bytes = (top_nibble == 3) ? 8 : 6;
            } else {
                *bytes = (lo & 0x20) ? 6 : 4;
            }
            return true;
        }
        if (hi == 0x6b) {
            *bytes = (lo & 0x20) ? 6 : 4;
            return true;
        }
        if (hi == 0x6e || hi == 0x6f) {
            *bytes = 4;
            return true;
        }
        *bytes = 2;
        return true;
    case 0x7:
        if (hi == 0x78) {
            *bytes = 8;
            return true;
        }
        if (hi == 0x7b || hi >= 0x7c) {
            *bytes = 4;
            return true;
        }
        if (hi == 0x79) {
            *bytes = 4;
            return true;
        }
        if (hi == 0x7a) {
            *bytes = 6;
            return true;
        }
        *bytes = 2;
        return true;
    case 0x8: case 0x9: case 0xa: case 0xb:
    case 0xc: case 0xd: case 0xe: case 0xf:
        *bytes = 2;
        return true;
    default:
        return false;
    }
}

bool h8s_analyze_rom_block(const uint8_t *rom, size_t rom_size, uint32_t start,
                           unsigned max_instructions, h8s_block_t *out)
{
    if (!rom || !out || start >= rom_size || max_instructions == 0) return false;
    if (max_instructions > H8S_BLOCK_MAX_INSTRUCTIONS)
        max_instructions = H8S_BLOCK_MAX_INSTRUCTIONS;

    h8s_block_t block = {
        .start = start,
        .bytes = 0,
        .instructions = 0,
        .stop = H8S_BLOCK_STOP_LIMIT,
        .stop_pc = start,
        .executable_prefix_instructions = 0,
        .executable = true
    };

    uint32_t pc = start;
    while (block.instructions < max_instructions) {
        if ((size_t)pc + 1 >= rom_size) {
            block.stop = H8S_BLOCK_STOP_TRUNCATED;
            block.stop_pc = pc;
            block.executable = false;
            break;
        }

        uint16_t op = read_be16(rom + pc);
        if (is_control_transfer(op)) {
            block.stop = H8S_BLOCK_STOP_BRANCH;
            block.stop_pc = pc;
            break;
        }
        if (!is_tier1_executable(op)) block.executable = false;

        unsigned bytes = 0;
        bool prefix_stop = false;
        bool control_stop = false;
        if (!instruction_length(rom, rom_size, pc, op, &bytes, &prefix_stop, &control_stop)) {
            block.stop = control_stop ? H8S_BLOCK_STOP_BRANCH :
                         prefix_stop ? H8S_BLOCK_STOP_PREFIX : H8S_BLOCK_STOP_UNSUPPORTED;
            block.stop_pc = pc;
            if (!control_stop) block.executable = false;
            break;
        }
        if ((size_t)pc + bytes > rom_size) {
            block.stop = H8S_BLOCK_STOP_TRUNCATED;
            block.stop_pc = pc;
            block.executable = false;
            break;
        }

        pc += bytes;
        block.bytes += bytes;
        block.decoded[block.instructions].op = op;
        if (bytes == 4) {
            block.decoded[block.instructions].imm = read_be16(rom + block.stop_pc + 2);
        } else if (bytes >= 6) {
            block.decoded[block.instructions].imm =
                ((uint32_t)read_be16(rom + block.stop_pc + 2) << 16) |
                read_be16(rom + block.stop_pc + 4);
        }
        block.decoded[block.instructions].bytes = (uint8_t)bytes;
        block.instructions++;
        if (block.executable) block.executable_prefix_instructions++;
        block.stop_pc = pc;
    }

    *out = block;
    return true;
}

static unsigned cache_index(uint32_t start)
{
    return (start >> 1) & (H8S_BLOCK_CACHE_ENTRIES - 1);
}

void h8s_block_cache_init(h8s_block_cache_t *cache, unsigned max_instructions)
{
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
    cache->max_instructions = max_instructions ? max_instructions : 32;
    if (cache->max_instructions > H8S_BLOCK_MAX_INSTRUCTIONS)
        cache->max_instructions = H8S_BLOCK_MAX_INSTRUCTIONS;
    cache->generation = 1;
}

void h8s_block_cache_clear(h8s_block_cache_t *cache)
{
    if (!cache) return;
    unsigned max_instructions = cache->max_instructions;
    uint32_t generation = cache->generation + 1;
    cache->hits = 0;
    cache->misses = 0;
    cache->evictions = 0;
    cache->max_instructions = max_instructions ? max_instructions : 32;
    if (cache->max_instructions > H8S_BLOCK_MAX_INSTRUCTIONS)
        cache->max_instructions = H8S_BLOCK_MAX_INSTRUCTIONS;
    cache->generation = generation;
    if (generation == 0) {
        memset(cache->entries, 0, sizeof(cache->entries));
        cache->generation = 1;
    }
}

const h8s_block_t *h8s_block_cache_get(h8s_block_cache_t *cache,
                                       const uint8_t *rom, size_t rom_size,
                                       uint32_t start)
{
    if (!cache || !rom) return NULL;

    unsigned index = cache_index(start);
    h8s_block_cache_entry_t *entry = &cache->entries[index];
    if (entry->valid && entry->generation == cache->generation && entry->tag == start) {
        cache->hits++;
        return &entry->block;
    }

    h8s_block_t block;
    if (!h8s_analyze_rom_block(rom, rom_size, start, cache->max_instructions, &block))
        return NULL;

    if (entry->valid && entry->generation == cache->generation) cache->evictions++;
    entry->valid = true;
    entry->tag = start;
    entry->generation = cache->generation;
    entry->block = block;
    cache->misses++;
    return &entry->block;
}

static void block_set_nz_l(h8s_block_cpu_state_t *state, uint32_t value)
{
    state->ccr = (uint8_t)((state->ccr & (uint8_t)~(BLOCK_CCR_N | BLOCK_CCR_Z)) |
                           ((value >> 28) & BLOCK_CCR_N) |
                           (value == 0 ? BLOCK_CCR_Z : 0));
}

static void block_set_nz_b(h8s_block_cpu_state_t *state, int result)
{
    uint8_t value = (uint8_t)result;
    state->ccr = (uint8_t)((state->ccr & (uint8_t)~(BLOCK_CCR_N | BLOCK_CCR_Z)) |
                           ((value >> 4) & BLOCK_CCR_N) |
                           (value == 0 ? BLOCK_CCR_Z : 0));
}

static void block_set_nz_w(h8s_block_cpu_state_t *state, int result)
{
    uint16_t value = (uint16_t)result;
    state->ccr = (uint8_t)((state->ccr & (uint8_t)~(BLOCK_CCR_N | BLOCK_CCR_Z)) |
                           ((value >> 12) & BLOCK_CCR_N) |
                           (value == 0 ? BLOCK_CCR_Z : 0));
}

static void block_set_arithmetic_l(h8s_block_cpu_state_t *state, uint32_t d,
                                   uint32_t s, uint32_t result, bool subtract)
{
    uint32_t overflow = subtract ? ((d ^ s) & (d ^ result))
                                 : ((d ^ result) & (s ^ result));
    state->ccr = (uint8_t)((state->ccr & 0xd0) |
        ((result >> 28) & BLOCK_CCR_N) |
        (result == 0 ? BLOCK_CCR_Z : 0) |
        ((overflow >> 30) & BLOCK_CCR_V) |
        (((d ^ s ^ result) >> 23) & BLOCK_CCR_H) |
        (subtract ? d < s : result < d));
}

static bool block_shift_rotate_form(uint8_t lo)
{
    switch ((lo >> 4) & 0xf) {
    case 0x0: case 0x1: case 0x3:
    case 0x4: case 0x5: case 0x7:
    case 0x8: case 0x9: case 0xb:
    case 0xc: case 0xd: case 0xf:
        return true;
    default:
        return false;
    }
}

static void block_shift_b(h8s_block_cpu_state_t *state, unsigned rd,
                          bool left, bool logical, int count)
{
    int val = block_get_reg_b(state, rd);
    for (int i = 0; i < count; ++i) {
        if (left) {
            block_set_flag(state, BLOCK_CCR_C, (val & 0x80) != 0);
            int old = val;
            val = (val << 1) & 0xff;
            if (!logical) block_set_flag(state, BLOCK_CCR_V, ((old ^ val) & 0x80) != 0);
        } else {
            block_set_flag(state, BLOCK_CCR_C, (val & 1) != 0);
            val = logical ? ((val >> 1) & 0x7f) : (((int8_t)val >> 1) & 0xff);
        }
    }
    block_set_reg_b(state, rd, (uint8_t)val);
    block_set_nz_b(state, val);
    if ((left && logical) || !left) block_set_flag(state, BLOCK_CCR_V, false);
}

static void block_shift_w(h8s_block_cpu_state_t *state, unsigned rd,
                          bool left, bool logical, int count)
{
    int val = block_get_r(state, rd);
    for (int i = 0; i < count; ++i) {
        if (left) {
            block_set_flag(state, BLOCK_CCR_C, (val & 0x8000) != 0);
            int old = val;
            val = (val << 1) & 0xffff;
            if (!logical) block_set_flag(state, BLOCK_CCR_V, ((old ^ val) & 0x8000) != 0);
        } else {
            block_set_flag(state, BLOCK_CCR_C, (val & 1) != 0);
            val = logical ? ((val >> 1) & 0x7fff) : (((int16_t)val >> 1) & 0xffff);
        }
    }
    block_set_r(state, rd, (uint16_t)val);
    block_set_nz_w(state, val);
    if ((left && logical) || !left) block_set_flag(state, BLOCK_CCR_V, false);
}

static void block_shift_l(h8s_block_cpu_state_t *state, unsigned erd,
                          bool left, bool logical, int count)
{
    uint32_t val = state->er[erd];
    for (int i = 0; i < count; ++i) {
        if (left) {
            block_set_flag(state, BLOCK_CCR_C, (val & 0x80000000u) != 0);
            uint32_t old = val;
            val <<= 1;
            if (!logical) block_set_flag(state, BLOCK_CCR_V, ((old ^ val) & 0x80000000u) != 0);
        } else {
            block_set_flag(state, BLOCK_CCR_C, (val & 1) != 0);
            val = logical ? (val >> 1) : ((val >> 1) | (val & 0x80000000u));
        }
    }
    state->er[erd] = val;
    block_set_nz_l(state, val);
    if ((left && logical) || !left) block_set_flag(state, BLOCK_CCR_V, false);
}

static void block_rotate_b(h8s_block_cpu_state_t *state, unsigned rd,
                           bool left, bool through_carry, int count)
{
    int val = block_get_reg_b(state, rd);
    for (int i = 0; i < count; ++i) {
        if (left) {
            int msb = (val >> 7) & 1;
            int old_c = (state->ccr & BLOCK_CCR_C) ? 1 : 0;
            block_set_flag(state, BLOCK_CCR_C, msb != 0);
            val = ((val << 1) | (through_carry ? old_c : msb)) & 0xff;
        } else {
            int lsb = val & 1;
            int old_c = (state->ccr & BLOCK_CCR_C) ? 0x80 : 0;
            block_set_flag(state, BLOCK_CCR_C, lsb != 0);
            val = ((val >> 1) | (through_carry ? old_c : (lsb << 7))) & 0xff;
        }
    }
    block_set_reg_b(state, rd, (uint8_t)val);
    block_set_nz_b(state, val);
    block_set_flag(state, BLOCK_CCR_V, false);
}

static void block_rotate_w(h8s_block_cpu_state_t *state, unsigned rd,
                           bool left, bool through_carry, int count)
{
    int val = block_get_r(state, rd);
    for (int i = 0; i < count; ++i) {
        if (left) {
            int msb = (val >> 15) & 1;
            int old_c = (state->ccr & BLOCK_CCR_C) ? 1 : 0;
            block_set_flag(state, BLOCK_CCR_C, msb != 0);
            val = ((val << 1) | (through_carry ? old_c : msb)) & 0xffff;
        } else {
            int lsb = val & 1;
            int old_c = (state->ccr & BLOCK_CCR_C) ? 0x8000 : 0;
            block_set_flag(state, BLOCK_CCR_C, lsb != 0);
            val = ((val >> 1) | (through_carry ? old_c : (lsb << 15))) & 0xffff;
        }
    }
    block_set_r(state, rd, (uint16_t)val);
    block_set_nz_w(state, val);
    block_set_flag(state, BLOCK_CCR_V, false);
}

static void block_rotate_l(h8s_block_cpu_state_t *state, unsigned erd,
                           bool left, bool through_carry, int count)
{
    uint32_t val = state->er[erd];
    for (int i = 0; i < count; ++i) {
        if (left) {
            uint32_t msb = (val >> 31) & 1;
            uint32_t old_c = (state->ccr & BLOCK_CCR_C) ? 1u : 0u;
            block_set_flag(state, BLOCK_CCR_C, msb != 0);
            val = (val << 1) | (through_carry ? old_c : msb);
        } else {
            uint32_t lsb = val & 1;
            uint32_t old_c = (state->ccr & BLOCK_CCR_C) ? 0x80000000u : 0u;
            block_set_flag(state, BLOCK_CCR_C, lsb != 0);
            val = (val >> 1) | (through_carry ? old_c : (lsb << 31));
        }
    }
    state->er[erd] = val;
    block_set_nz_l(state, val);
    block_set_flag(state, BLOCK_CCR_V, false);
}

static bool block_execute_shift_rotate(h8s_block_cpu_state_t *state, uint8_t hi, uint8_t lo)
{
    unsigned subop = (lo >> 4) & 0xf;
    unsigned rd = lo & 0xf;
    unsigned erd = lo & 0x7;

    switch (hi) {
    case 0x10:
        switch (subop) {
        case 0x0: block_shift_b(state, rd, true, true, 1); return true;
        case 0x1: block_shift_w(state, rd, true, true, 1); return true;
        case 0x3: block_shift_l(state, erd, true, true, 1); return true;
        case 0x4: block_shift_b(state, rd, true, true, 2); return true;
        case 0x5: block_shift_w(state, rd, true, true, 2); return true;
        case 0x7: block_shift_l(state, erd, true, true, 2); return true;
        case 0x8: block_shift_b(state, rd, true, false, 1); return true;
        case 0x9: block_shift_w(state, rd, true, false, 1); return true;
        case 0xb: block_shift_l(state, erd, true, false, 1); return true;
        case 0xc: block_shift_b(state, rd, true, false, 2); return true;
        case 0xd: block_shift_w(state, rd, true, false, 2); return true;
        case 0xf: block_shift_l(state, erd, true, false, 2); return true;
        default: return false;
        }
    case 0x11:
        switch (subop) {
        case 0x0: block_shift_b(state, rd, false, true, 1); return true;
        case 0x1: block_shift_w(state, rd, false, true, 1); return true;
        case 0x3: block_shift_l(state, erd, false, true, 1); return true;
        case 0x4: block_shift_b(state, rd, false, true, 2); return true;
        case 0x5: block_shift_w(state, rd, false, true, 2); return true;
        case 0x7: block_shift_l(state, erd, false, true, 2); return true;
        case 0x8: block_shift_b(state, rd, false, false, 1); return true;
        case 0x9: block_shift_w(state, rd, false, false, 1); return true;
        case 0xb: block_shift_l(state, erd, false, false, 1); return true;
        case 0xc: block_shift_b(state, rd, false, false, 2); return true;
        case 0xd: block_shift_w(state, rd, false, false, 2); return true;
        case 0xf: block_shift_l(state, erd, false, false, 2); return true;
        default: return false;
        }
    case 0x12:
        switch (subop) {
        case 0x0: block_rotate_b(state, rd, true, true, 1); return true;
        case 0x1: block_rotate_w(state, rd, true, true, 1); return true;
        case 0x3: block_rotate_l(state, erd, true, true, 1); return true;
        case 0x4: block_rotate_b(state, rd, true, true, 2); return true;
        case 0x5: block_rotate_w(state, rd, true, true, 2); return true;
        case 0x7: block_rotate_l(state, erd, true, true, 2); return true;
        case 0x8: block_rotate_b(state, rd, true, false, 1); return true;
        case 0x9: block_rotate_w(state, rd, true, false, 1); return true;
        case 0xb: block_rotate_l(state, erd, true, false, 1); return true;
        case 0xc: block_rotate_b(state, rd, true, false, 2); return true;
        case 0xd: block_rotate_w(state, rd, true, false, 2); return true;
        case 0xf: block_rotate_l(state, erd, true, false, 2); return true;
        default: return false;
        }
    case 0x13:
        switch (subop) {
        case 0x0: block_rotate_b(state, rd, false, true, 1); return true;
        case 0x1: block_rotate_w(state, rd, false, true, 1); return true;
        case 0x3: block_rotate_l(state, erd, false, true, 1); return true;
        case 0x4: block_rotate_b(state, rd, false, true, 2); return true;
        case 0x5: block_rotate_w(state, rd, false, true, 2); return true;
        case 0x7: block_rotate_l(state, erd, false, true, 2); return true;
        case 0x8: block_rotate_b(state, rd, false, false, 1); return true;
        case 0x9: block_rotate_w(state, rd, false, false, 1); return true;
        case 0xb: block_rotate_l(state, erd, false, false, 1); return true;
        case 0xc: block_rotate_b(state, rd, false, false, 2); return true;
        case 0xd: block_rotate_w(state, rd, false, false, 2); return true;
        case 0xf: block_rotate_l(state, erd, false, false, 2); return true;
        default: return false;
        }
    default:
        return false;
    }
}

bool h8s_semantic_instruction_supported(uint16_t op)
{
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;

    switch (hi) {
    case 0x08: case 0x09: case 0x0c: case 0x0d: case 0x0e:
    case 0x14: case 0x15: case 0x16:
    case 0x18: case 0x19: case 0x1c: case 0x1d: case 0x1e:
        return true;
    case 0x10: case 0x11: case 0x12: case 0x13:
        return block_shift_rotate_form(lo);
    case 0x0a: case 0x1a: case 0x1f:
        return (lo & 0x80) != 0;
    case 0x0b: case 0x1b:
        switch (lo & 0xf0) {
        case 0x00: case 0x80: case 0x90: case 0xf0:
            return true;
        default:
            return false;
        }
    case 0x0f:
        return (lo & 0x80) != 0;
    case 0x17:
        switch ((lo >> 4) & 0xf) {
        case 0x0: case 0x1: case 0x3: case 0x5: case 0x7:
        case 0x8: case 0x9: case 0xb: case 0xd: case 0xf:
            return true;
        default:
            return false;
        }
    case 0x79: case 0x7a:
        return ((lo >> 4) & 0xf) <= 6;
    default:
        return (hi >> 4) >= 0x8;
    }
}

bool h8s_semantic_block_supported(const h8s_block_t *block)
{
    if (!block || !block->executable || block->instructions == 0) return false;
    for (unsigned i = 0; i < block->instructions; ++i) {
        if (!h8s_semantic_instruction_supported(block->decoded[i].op))
            return false;
    }
    return true;
}

bool h8s_execute_semantic_block(const h8s_block_t *block,
                                h8s_block_cpu_state_t *state)
{
    if (!block || !state || !block->executable) return false;

    uint32_t pc = block->start;
    for (unsigned i = 0; i < block->instructions; ++i) {
        uint16_t op = block->decoded[i].op;
        uint8_t hi = (uint8_t)(op >> 8);
        uint8_t lo = (uint8_t)op;

        switch (hi) {
        case 0x08: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_reg_b(state, rs);
            int d = block_get_reg_b(state, rd);
            int result = d + s;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_C, result > 0xff);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ result) & (s ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x10) != 0);
            break;
        }
        case 0x09: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_r(state, rs);
            int d = block_get_r(state, rd);
            int result = d + s;
            block_set_r(state, rd, (uint16_t)result);
            block_set_nz_w(state, result);
            block_set_flag(state, BLOCK_CCR_C, result > 0xffff);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ result) & (s ^ result) & 0x8000) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x1000) != 0);
            break;
        }
        case 0x0a:
            if ((lo & 0x80) == 0) return false;
            {
                unsigned rs = (lo >> 4) & 0x7;
                unsigned rd = lo & 0x7;
                uint32_t s = state->er[rs];
                uint32_t d = state->er[rd];
                uint32_t result = d + s;
                state->er[rd] = result;
                block_set_arithmetic_l(state, d, s, result, false);
            }
            break;
        case 0x0c: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int value = block_get_reg_b(state, rs);
            block_set_reg_b(state, rd, (uint8_t)value);
            block_set_nz_b(state, value);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x0d: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int value = block_get_r(state, rs);
            block_set_r(state, rd, (uint16_t)value);
            block_set_nz_w(state, value);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x0e: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_reg_b(state, rs);
            int d = block_get_reg_b(state, rd);
            int c = (state->ccr & BLOCK_CCR_C) ? 1 : 0;
            int result = d + s + c;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_flag(state, BLOCK_CCR_C, result > 0xff);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ result) & (s ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x10) != 0);
            if ((result & 0xff) != 0) block_set_flag(state, BLOCK_CCR_Z, false);
            block_set_flag(state, BLOCK_CCR_N, (result & 0x80) != 0);
            break;
        }
        case 0x10: case 0x11: case 0x12: case 0x13:
            if (!block_execute_shift_rotate(state, hi, lo)) return false;
            break;
        case 0x14: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int result = block_get_reg_b(state, rd) | block_get_reg_b(state, rs);
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x15: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int result = block_get_reg_b(state, rd) ^ block_get_reg_b(state, rs);
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x16: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int result = block_get_reg_b(state, rd) & block_get_reg_b(state, rs);
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x17: {
            unsigned subop = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            unsigned erd = lo & 0x7;
            switch (subop) {
            case 0x0: {
                int v = (~block_get_reg_b(state, rd)) & 0xff;
                block_set_reg_b(state, rd, (uint8_t)v);
                block_set_nz_b(state, v);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            case 0x1: {
                int v = (~block_get_r(state, rd)) & 0xffff;
                block_set_r(state, rd, (uint16_t)v);
                block_set_nz_w(state, v);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            case 0x3:
                state->er[erd] = ~state->er[erd];
                block_set_nz_l(state, state->er[erd]);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x5:
                block_set_r(state, rd, block_get_reg_b(state, (rd & 0x7) + 8));
                block_set_nz_w(state, block_get_r(state, rd));
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x7:
                state->er[erd] = block_get_r(state, erd);
                block_set_nz_l(state, state->er[erd]);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x8: {
                int v = block_get_reg_b(state, rd);
                int r = (-v) & 0xff;
                block_set_reg_b(state, rd, (uint8_t)r);
                block_set_nz_b(state, r);
                block_set_flag(state, BLOCK_CCR_C, r != 0);
                block_set_flag(state, BLOCK_CCR_V, v == 0x80);
                block_set_flag(state, BLOCK_CCR_H, ((v ^ r) & 0x10) != 0);
                break;
            }
            case 0x9: {
                int v = block_get_r(state, rd);
                int r = (-v) & 0xffff;
                block_set_r(state, rd, (uint16_t)r);
                block_set_nz_w(state, r);
                block_set_flag(state, BLOCK_CCR_C, r != 0);
                block_set_flag(state, BLOCK_CCR_V, v == 0x8000);
                break;
            }
            case 0xb: {
                uint32_t v = state->er[erd];
                state->er[erd] = (uint32_t)(-(int32_t)v);
                block_set_nz_l(state, state->er[erd]);
                block_set_flag(state, BLOCK_CCR_C, state->er[erd] != 0);
                block_set_flag(state, BLOCK_CCR_V, v == 0x80000000u);
                break;
            }
            case 0xd: {
                int v = (int8_t)block_get_reg_b(state, (rd & 0x7) + 8);
                block_set_r(state, rd, (uint16_t)(v & 0xffff));
                block_set_nz_w(state, v);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            case 0xf: {
                int32_t v = (int16_t)block_get_r(state, erd);
                state->er[erd] = (uint32_t)v;
                block_set_nz_l(state, (uint32_t)v);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            default:
                return false;
            }
            break;
        }
        case 0x18: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_reg_b(state, rs);
            int d = block_get_reg_b(state, rd);
            int result = d - s;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_C, (result & 0x100) != 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ s) & (d ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x10) != 0);
            break;
        }
        case 0x19: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_r(state, rs);
            int d = block_get_r(state, rd);
            int result = d - s;
            block_set_r(state, rd, (uint16_t)result);
            block_set_nz_w(state, result);
            block_set_flag(state, BLOCK_CCR_C, (result & 0x10000) != 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ s) & (d ^ result) & 0x8000) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x1000) != 0);
            break;
        }
        case 0x1a:
            if ((lo & 0x80) == 0) return false;
            {
                unsigned rs = (lo >> 4) & 0x7;
                unsigned rd = lo & 0x7;
                uint32_t s = state->er[rs];
                uint32_t d = state->er[rd];
                uint32_t result = d - s;
                state->er[rd] = result;
                block_set_arithmetic_l(state, d, s, result, true);
            }
            break;
        case 0x1c: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_reg_b(state, rs);
            int d = block_get_reg_b(state, rd);
            int result = d - s;
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_C, (result & 0x100) != 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ s) & (d ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x10) != 0);
            break;
        }
        case 0x1d: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_r(state, rs);
            int d = block_get_r(state, rd);
            int result = d - s;
            block_set_nz_w(state, result);
            block_set_flag(state, BLOCK_CCR_C, (result & 0x10000) != 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ s) & (d ^ result) & 0x8000) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x1000) != 0);
            break;
        }
        case 0x1e: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int s = block_get_reg_b(state, rs);
            int d = block_get_reg_b(state, rd);
            int c = (state->ccr & BLOCK_CCR_C) ? 1 : 0;
            int result = d - s - c;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_flag(state, BLOCK_CCR_C, result < 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ s) & (d ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ s ^ result) & 0x10) != 0);
            if ((result & 0xff) != 0) block_set_flag(state, BLOCK_CCR_Z, false);
            block_set_flag(state, BLOCK_CCR_N, (result & 0x80) != 0);
            break;
        }
        case 0x1f:
            if ((lo & 0x80) == 0) return false;
            {
                unsigned rs = (lo >> 4) & 0x7;
                unsigned rd = lo & 0x7;
                uint32_t s = state->er[rs];
                uint32_t d = state->er[rd];
                block_set_arithmetic_l(state, d, s, d - s, true);
            }
            break;
        case 0x80: case 0x81: case 0x82: case 0x83:
        case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8a: case 0x8b:
        case 0x8c: case 0x8d: case 0x8e: case 0x8f: {
            unsigned rd = hi & 0xf;
            int imm = lo;
            int d = block_get_reg_b(state, rd);
            int result = d + imm;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_C, result > 0xff);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ result) & (imm ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x10) != 0);
            break;
        }
        case 0x90: case 0x91: case 0x92: case 0x93:
        case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9a: case 0x9b:
        case 0x9c: case 0x9d: case 0x9e: case 0x9f: {
            unsigned rd = hi & 0xf;
            int imm = lo;
            int d = block_get_reg_b(state, rd);
            int c = (state->ccr & BLOCK_CCR_C) ? 1 : 0;
            int result = d + imm + c;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_flag(state, BLOCK_CCR_C, result > 0xff);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ result) & (imm ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x10) != 0);
            if ((result & 0xff) != 0) block_set_flag(state, BLOCK_CCR_Z, false);
            block_set_flag(state, BLOCK_CCR_N, (result & 0x80) != 0);
            break;
        }
        case 0xa0: case 0xa1: case 0xa2: case 0xa3:
        case 0xa4: case 0xa5: case 0xa6: case 0xa7:
        case 0xa8: case 0xa9: case 0xaa: case 0xab:
        case 0xac: case 0xad: case 0xae: case 0xaf: {
            unsigned rd = hi & 0xf;
            int imm = lo;
            int d = block_get_reg_b(state, rd);
            int result = d - imm;
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_C, (result & 0x100) != 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ imm) & (d ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x10) != 0);
            break;
        }
        case 0xb0: case 0xb1: case 0xb2: case 0xb3:
        case 0xb4: case 0xb5: case 0xb6: case 0xb7:
        case 0xb8: case 0xb9: case 0xba: case 0xbb:
        case 0xbc: case 0xbd: case 0xbe: case 0xbf: {
            unsigned rd = hi & 0xf;
            int imm = lo;
            int d = block_get_reg_b(state, rd);
            int c = (state->ccr & BLOCK_CCR_C) ? 1 : 0;
            int result = d - imm - c;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_flag(state, BLOCK_CCR_C, result < 0);
            block_set_flag(state, BLOCK_CCR_V, ((d ^ imm) & (d ^ result) & 0x80) != 0);
            block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x10) != 0);
            if ((result & 0xff) != 0) block_set_flag(state, BLOCK_CCR_Z, false);
            block_set_flag(state, BLOCK_CCR_N, (result & 0x80) != 0);
            break;
        }
        case 0xc0: case 0xc1: case 0xc2: case 0xc3:
        case 0xc4: case 0xc5: case 0xc6: case 0xc7:
        case 0xc8: case 0xc9: case 0xca: case 0xcb:
        case 0xcc: case 0xcd: case 0xce: case 0xcf: {
            unsigned rd = hi & 0xf;
            int result = block_get_reg_b(state, rd) | lo;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0xd0: case 0xd1: case 0xd2: case 0xd3:
        case 0xd4: case 0xd5: case 0xd6: case 0xd7:
        case 0xd8: case 0xd9: case 0xda: case 0xdb:
        case 0xdc: case 0xdd: case 0xde: case 0xdf: {
            unsigned rd = hi & 0xf;
            int result = block_get_reg_b(state, rd) ^ lo;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0xe0: case 0xe1: case 0xe2: case 0xe3:
        case 0xe4: case 0xe5: case 0xe6: case 0xe7:
        case 0xe8: case 0xe9: case 0xea: case 0xeb:
        case 0xec: case 0xed: case 0xee: case 0xef: {
            unsigned rd = hi & 0xf;
            int result = block_get_reg_b(state, rd) & lo;
            block_set_reg_b(state, rd, (uint8_t)result);
            block_set_nz_b(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0xf0: case 0xf1: case 0xf2: case 0xf3:
        case 0xf4: case 0xf5: case 0xf6: case 0xf7:
        case 0xf8: case 0xf9: case 0xfa: case 0xfb:
        case 0xfc: case 0xfd: case 0xfe: case 0xff: {
            unsigned rd = hi & 0xf;
            block_set_reg_b(state, rd, lo);
            block_set_nz_b(state, lo);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x79: {
            unsigned subop = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int imm = (int)(uint16_t)block->decoded[i].imm;
            switch (subop) {
            case 0x0:
                block_set_r(state, rd, (uint16_t)imm);
                block_set_nz_w(state, imm);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x1: {
                int d = block_get_r(state, rd);
                int result = d + imm;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_C, result > 0xffff);
                block_set_flag(state, BLOCK_CCR_V, ((d ^ result) & (imm ^ result) & 0x8000) != 0);
                block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x1000) != 0);
                break;
            }
            case 0x2: {
                int d = block_get_r(state, rd);
                int result = d - imm;
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_C, (result & 0x10000) != 0);
                block_set_flag(state, BLOCK_CCR_V, ((d ^ imm) & (d ^ result) & 0x8000) != 0);
                block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x1000) != 0);
                break;
            }
            case 0x3: {
                int d = block_get_r(state, rd);
                int result = d - imm;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_C, (result & 0x10000) != 0);
                block_set_flag(state, BLOCK_CCR_V, ((d ^ imm) & (d ^ result) & 0x8000) != 0);
                block_set_flag(state, BLOCK_CCR_H, ((d ^ imm ^ result) & 0x1000) != 0);
                break;
            }
            case 0x4: {
                int result = block_get_r(state, rd) | imm;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            case 0x5: {
                int result = block_get_r(state, rd) ^ imm;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            case 0x6: {
                int result = block_get_r(state, rd) & imm;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            }
            default:
                return false;
            }
            break;
        }
        case 0x7a: {
            unsigned subop = (lo >> 4) & 0xf;
            unsigned rd = lo & 0x7;
            uint32_t imm = block->decoded[i].imm;
            switch (subop) {
            case 0x0:
                state->er[rd] = imm;
                block_set_nz_l(state, imm);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x1: {
                uint32_t d = state->er[rd];
                uint32_t result = d + imm;
                state->er[rd] = result;
                block_set_arithmetic_l(state, d, imm, result, false);
                break;
            }
            case 0x2: {
                uint32_t d = state->er[rd];
                block_set_arithmetic_l(state, d, imm, d - imm, true);
                break;
            }
            case 0x3: {
                uint32_t d = state->er[rd];
                uint32_t result = d - imm;
                state->er[rd] = result;
                block_set_arithmetic_l(state, d, imm, result, true);
                break;
            }
            case 0x4:
                state->er[rd] |= imm;
                block_set_nz_l(state, state->er[rd]);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x5:
                state->er[rd] ^= imm;
                block_set_nz_l(state, state->er[rd]);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            case 0x6:
                state->er[rd] &= imm;
                block_set_nz_l(state, state->er[rd]);
                block_set_flag(state, BLOCK_CCR_V, false);
                break;
            default:
                return false;
            }
            break;
        }
        case 0x0b:
            switch (lo & 0xf0) {
            case 0x00: state->er[lo & 0x7] += 1; break;
            case 0x80: state->er[lo & 0x7] += 2; break;
            case 0x90: state->er[lo & 0x7] += 4; break;
            case 0xf0:
                state->er[lo & 0x7] += 2;
                block_set_nz_l(state, state->er[lo & 0x7]);
                break;
            default:
                return false;
            }
            break;
        case 0x1b:
            switch (lo & 0xf0) {
            case 0x00: state->er[lo & 0x7] -= 1; break;
            case 0x80: state->er[lo & 0x7] -= 2; break;
            case 0x90: state->er[lo & 0x7] -= 4; break;
            case 0xf0:
                state->er[lo & 0x7] -= 2;
                block_set_nz_l(state, state->er[lo & 0x7]);
                break;
            default:
                return false;
            }
            break;
        case 0x0f:
            if ((lo & 0x80) == 0) return false;
            state->er[lo & 0x7] = state->er[(lo >> 4) & 0x7];
            block_set_nz_l(state, state->er[lo & 0x7]);
            state->ccr &= (uint8_t)~BLOCK_CCR_V;
            break;
        default:
            return false;
        }

        pc += block->decoded[i].bytes;
    }

    state->pc = pc;
    return true;
}

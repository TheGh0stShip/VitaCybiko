#include "core/h8s_block.h"
#include "core/address_bus.h"
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

static void describe_control_transfer(const uint8_t *rom, size_t rom_size,
                                      uint32_t pc, uint16_t op,
                                      h8s_block_t *block)
{
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;
    block->branch_op = op;
    block->branch_bytes = 2;
    block->branch_condition = 0;
    block->branch_conditional = false;
    block->branch_has_target = false;
    block->branch_fallthrough = (pc + 2) & 0xffffffu;
    block->branch_target = 0;

    if ((hi >> 4) == 0x4) {
        int8_t disp = (int8_t)lo;
        block->branch_kind = H8S_BLOCK_BRANCH_BCC8;
        block->branch_condition = hi & 0xf;
        block->branch_conditional = true;
        block->branch_has_target = true;
        block->branch_target = (uint32_t)(block->branch_fallthrough + disp) & 0xffffffu;
        return;
    }

    if (hi == 0x01 && lo == 0x80) {
        block->branch_kind = H8S_BLOCK_BRANCH_SLEEP;
        return;
    }

    switch (hi) {
    case 0x54:
    case 0x56:
        block->branch_kind = H8S_BLOCK_BRANCH_RETURN;
        return;
    case 0x55: {
        int8_t disp = (int8_t)lo;
        block->branch_kind = H8S_BLOCK_BRANCH_BSR8;
        block->branch_has_target = true;
        block->branch_target = (uint32_t)(block->branch_fallthrough + disp) & 0xffffffu;
        return;
    }
    case 0x57:
        block->branch_kind = H8S_BLOCK_BRANCH_TRAP;
        return;
    case 0x58:
        block->branch_bytes = 4;
        block->branch_fallthrough = (pc + 4) & 0xffffffu;
        if ((size_t)pc + 3 < rom_size) {
            int16_t disp = (int16_t)read_be16(rom + pc + 2);
            block->branch_kind = H8S_BLOCK_BRANCH_BCC16;
            block->branch_condition = (lo >> 4) & 0xf;
            block->branch_conditional = true;
            block->branch_has_target = true;
            block->branch_target = (uint32_t)(block->branch_fallthrough + disp) & 0xffffffu;
        } else {
            block->branch_kind = H8S_BLOCK_BRANCH_INDIRECT;
        }
        return;
    case 0x59:
    case 0x5b:
    case 0x5d:
    case 0x5f:
        block->branch_kind = H8S_BLOCK_BRANCH_INDIRECT;
        return;
    case 0x5a:
        block->branch_bytes = 4;
        block->branch_fallthrough = (pc + 4) & 0xffffffu;
        block->branch_kind = H8S_BLOCK_BRANCH_JMP_ABS24;
        if ((size_t)pc + 3 < rom_size) {
            block->branch_has_target = true;
            block->branch_target = (((uint32_t)lo << 16) | read_be16(rom + pc + 2)) & 0xffffffu;
        }
        return;
    case 0x5c:
        block->branch_bytes = 4;
        block->branch_fallthrough = (pc + 4) & 0xffffffu;
        block->branch_kind = H8S_BLOCK_BRANCH_BSR16;
        if ((size_t)pc + 3 < rom_size) {
            int16_t disp = (int16_t)read_be16(rom + pc + 2);
            block->branch_has_target = true;
            block->branch_target = (uint32_t)(block->branch_fallthrough + disp) & 0xffffffu;
        }
        return;
    case 0x5e:
        block->branch_bytes = 4;
        block->branch_fallthrough = (pc + 4) & 0xffffffu;
        block->branch_kind = H8S_BLOCK_BRANCH_JSR_ABS24;
        if ((size_t)pc + 3 < rom_size) {
            block->branch_has_target = true;
            block->branch_target = (((uint32_t)lo << 16) | read_be16(rom + pc + 2)) & 0xffffffu;
        }
        return;
    default:
        block->branch_kind = H8S_BLOCK_BRANCH_INDIRECT;
        return;
    }
}

static bool block_evaluate_condition(uint8_t ccr, unsigned cond)
{
    bool c = (ccr & BLOCK_CCR_C) != 0;
    bool z = (ccr & BLOCK_CCR_Z) != 0;
    bool n = (ccr & BLOCK_CCR_N) != 0;
    bool v = (ccr & BLOCK_CCR_V) != 0;

    switch (cond & 0xf) {
    case 0x0: return true;          /* BRA/BT */
    case 0x1: return false;         /* BRN/BF */
    case 0x2: return !c && !z;      /* BHI */
    case 0x3: return c || z;        /* BLS */
    case 0x4: return !c;            /* BCC/BHS */
    case 0x5: return c;             /* BCS/BLO */
    case 0x6: return !z;            /* BNE */
    case 0x7: return z;             /* BEQ */
    case 0x8: return !v;            /* BVC */
    case 0x9: return v;             /* BVS */
    case 0xa: return !n;            /* BPL */
    case 0xb: return n;             /* BMI */
    case 0xc: return !(n ^ v);      /* BGE */
    case 0xd: return n ^ v;         /* BLT */
    case 0xe: return !(z || (n ^ v)); /* BGT */
    case 0xf: return z || (n ^ v);  /* BLE */
    default: return false;
    }
}

bool h8s_block_resolve_static_branch(const h8s_block_t *block,
                                     uint8_t ccr, uint32_t *next_pc)
{
    if (!block || !next_pc || block->stop != H8S_BLOCK_STOP_BRANCH)
        return false;

    switch (block->branch_kind) {
    case H8S_BLOCK_BRANCH_BCC8:
    case H8S_BLOCK_BRANCH_BCC16:
        if (!block->branch_has_target) return false;
        *next_pc = block_evaluate_condition(ccr, block->branch_condition) ?
            block->branch_target : block->branch_fallthrough;
        return true;
    case H8S_BLOCK_BRANCH_BSR8:
    case H8S_BLOCK_BRANCH_BSR16:
    case H8S_BLOCK_BRANCH_JMP_ABS24:
    case H8S_BLOCK_BRANCH_JSR_ABS24:
        if (!block->branch_has_target) return false;
        *next_pc = block->branch_target;
        return true;
    default:
        return false;
    }
}

static uint8_t branch_ccr_key(const h8s_block_t *block, uint8_t ccr)
{
    if (!block || !block->branch_conditional) return 0;
    return ccr & (BLOCK_CCR_C | BLOCK_CCR_V | BLOCK_CCR_Z | BLOCK_CCR_N);
}

static unsigned edge_cache_index(uint32_t branch_pc, uint8_t ccr_key)
{
    return ((branch_pc >> 1) ^ (branch_pc >> 10) ^ ccr_key) &
           (H8S_BRANCH_EDGE_CACHE_ENTRIES - 1);
}

void h8s_branch_edge_cache_init(h8s_branch_edge_cache_t *cache)
{
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
    cache->generation = 1;
}

void h8s_branch_edge_cache_clear(h8s_branch_edge_cache_t *cache)
{
    if (!cache) return;
    uint32_t generation = cache->generation + 1;
    cache->hits = 0;
    cache->misses = 0;
    cache->evictions = 0;
    cache->generation = generation;
    if (generation == 0) {
        memset(cache->entries, 0, sizeof(cache->entries));
        cache->generation = 1;
    }
}

bool h8s_branch_edge_cache_get(h8s_branch_edge_cache_t *cache,
                               const h8s_block_t *block, uint8_t ccr,
                               uint32_t *next_pc)
{
    if (!cache || !block || !next_pc || block->stop != H8S_BLOCK_STOP_BRANCH)
        return false;

    uint8_t ccr_key = branch_ccr_key(block, ccr);
    unsigned index = edge_cache_index(block->stop_pc, ccr_key);
    h8s_branch_edge_cache_entry_t *entry = &cache->entries[index];
    if (entry->valid && entry->generation == cache->generation &&
        entry->tag == block->stop_pc && entry->ccr_key == ccr_key) {
        cache->hits++;
        *next_pc = entry->next_pc;
        return true;
    }

    uint32_t resolved = 0;
    if (!h8s_block_resolve_static_branch(block, ccr, &resolved))
        return false;

    if (entry->valid && entry->generation == cache->generation)
        cache->evictions++;
    entry->valid = true;
    entry->generation = cache->generation;
    entry->tag = block->stop_pc;
    entry->ccr_key = ccr_key;
    entry->next_pc = resolved;
    cache->misses++;
    *next_pc = resolved;
    return true;
}

const h8s_block_t *h8s_block_cache_get_chain_target(h8s_block_cache_t *block_cache,
                                                    h8s_branch_edge_cache_t *edge_cache,
                                                    const uint8_t *rom, size_t rom_size,
                                                    const h8s_block_t *block,
                                                    uint8_t ccr, uint32_t *next_pc)
{
    if (!block_cache || !edge_cache || !rom || !block) return NULL;
    if (block->branch_kind != H8S_BLOCK_BRANCH_BCC8 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BCC16 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JMP_ABS24)
        return NULL;

    uint32_t resolved = 0;
    if (!h8s_branch_edge_cache_get(edge_cache, block, ccr, &resolved))
        return NULL;

    if (next_pc) *next_pc = resolved;
    if (resolved >= rom_size || (resolved & 1u) != 0)
        return NULL;

    const h8s_block_t *target =
        h8s_block_cache_get(block_cache, rom, rom_size, resolved);
    if (!h8s_semantic_block_supported(target))
        return NULL;
    return target;
}

static bool is_shift_rotate_form(uint8_t lo)
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

static bool is_tier1_inc_dec_form(uint8_t lo, bool allow_short_add_sub)
{
    if ((lo & 0x80) != 0 && !allow_short_add_sub) return true;
    switch (lo & 0xf0) {
    case 0x00: case 0x50: case 0x70:
        return true;
    case 0x80: case 0x90: case 0xd0: case 0xf0:
        return allow_short_add_sub;
    default:
        return false;
    }
}

static bool is_tier1_unary_form(uint8_t lo)
{
    switch ((lo >> 4) & 0xf) {
    case 0x0: case 0x1: case 0x3: case 0x5: case 0x7:
    case 0x8: case 0x9: case 0xb: case 0xd: case 0xf:
        return true;
    default:
        return false;
    }
}

static bool is_tier1_executable(uint16_t op)
{
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;

    if ((hi >> 4) >= 0x8) return true; /* immediate byte ALU/MOV */

    switch (hi) {
    case 0x08: case 0x09: case 0x0c: case 0x0d: case 0x0e:
    case 0x18: case 0x19: case 0x1c: case 0x1d: case 0x1e:
    case 0x14: case 0x15: case 0x16:
    case 0x64: case 0x65: case 0x66:
        return true;
    case 0x0a: case 0x1a:
        return is_tier1_inc_dec_form(lo, false);
    case 0x0b: case 0x1b:
        return is_tier1_inc_dec_form(lo, true);
    case 0x0f: case 0x1f:
        return (lo & 0x80) != 0;
    case 0x17:
        return is_tier1_unary_form(lo);
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
        return true;
    default:
        break;
    }

    if (hi >= 0x10 && hi <= 0x13) return is_shift_rotate_form(lo);
    if (hi == 0x60 || hi == 0x61 || hi == 0x62 || hi == 0x63) return true;
    if (hi == 0x79 && ((lo >> 4) & 0xf) <= 6) return true;
    if (hi == 0x7a && ((lo >> 4) & 0xf) <= 6) return true;
    return false;
}

static bool is_tier1_decoded_executable(uint16_t op, uint32_t imm, unsigned bytes)
{
    if (op == 0x01f0 && bytes == 4) {
        uint8_t hi2 = (uint8_t)(imm >> 8);
        return hi2 == 0x64 || hi2 == 0x65 || hi2 == 0x66;
    }
    return is_tier1_executable(op);
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
            describe_control_transfer(rom, rom_size, pc, op, &block);
            break;
        }
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
        block.decoded[block.instructions].imm = 0;
        if (bytes == 4) {
            block.decoded[block.instructions].imm = read_be16(rom + block.stop_pc + 2);
        } else if (bytes >= 6) {
            block.decoded[block.instructions].imm =
                ((uint32_t)read_be16(rom + block.stop_pc + 2) << 16) |
                read_be16(rom + block.stop_pc + 4);
        }
        block.decoded[block.instructions].bytes = (uint8_t)bytes;
        if (!is_tier1_decoded_executable(op, block.decoded[block.instructions].imm, bytes))
            block.executable = false;
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

void h8s_mutable_block_cache_init(h8s_mutable_block_cache_t *cache,
                                  unsigned max_instructions)
{
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
    cache->max_instructions = max_instructions ? max_instructions : 32;
    if (cache->max_instructions > H8S_BLOCK_MAX_INSTRUCTIONS)
        cache->max_instructions = H8S_BLOCK_MAX_INSTRUCTIONS;
}

void h8s_mutable_block_cache_clear(h8s_mutable_block_cache_t *cache)
{
    if (!cache) return;
    unsigned max_instructions = cache->max_instructions;
    memset(cache, 0, sizeof(*cache));
    cache->max_instructions = max_instructions ? max_instructions : 32;
    if (cache->max_instructions > H8S_BLOCK_MAX_INSTRUCTIONS)
        cache->max_instructions = H8S_BLOCK_MAX_INSTRUCTIONS;
}

static bool mutable_entry_generation_valid(const h8s_mutable_block_cache_entry_t *entry,
                                           const address_bus_t *bus)
{
    if (!entry->valid || !bus) return false;
    if (bus_code_page_generation(bus, entry->first_page << 12) != entry->first_generation)
        return false;
    if (entry->last_page != entry->first_page &&
        bus_code_page_generation(bus, entry->last_page << 12) != entry->last_generation)
        return false;
    return true;
}

const h8s_block_t *h8s_mutable_block_cache_get(h8s_mutable_block_cache_t *cache,
                                               address_bus_t *bus,
                                               const uint8_t *data,
                                               size_t data_size,
                                               uint32_t source_base,
                                               uint32_t start_pc)
{
    if (!cache || !bus || !data) return NULL;
    source_base &= 0xffffff;
    start_pc &= 0xffffff;
    if (start_pc < source_base) return NULL;
    uint32_t offset = start_pc - source_base;
    if (offset >= data_size) return NULL;

    unsigned index = cache_index(start_pc) & (H8S_MUTABLE_BLOCK_CACHE_ENTRIES - 1);
    h8s_mutable_block_cache_entry_t *entry = &cache->entries[index];
    if (entry->valid && entry->tag == start_pc && entry->source_base == source_base &&
        mutable_entry_generation_valid(entry, bus)) {
        cache->hits++;
        return &entry->block;
    }

    h8s_block_t block;
    if (!h8s_analyze_rom_block(data, data_size, offset, cache->max_instructions, &block))
        return NULL;

    uint32_t bytes = block.bytes ? block.bytes : block.branch_bytes;
    if (bytes == 0) bytes = 2;
    bus_watch_code_range(bus, start_pc, bytes);
    uint32_t first_page = start_pc >> 12;
    uint32_t last_page = ((start_pc + bytes - 1) & 0xffffff) >> 12;
    if (last_page < first_page) last_page = 4095u;

    if (entry->valid) cache->evictions++;
    entry->valid = true;
    entry->tag = start_pc;
    entry->source_base = source_base;
    entry->first_page = first_page;
    entry->last_page = last_page;
    entry->first_generation = bus_code_page_generation(bus, start_pc);
    entry->last_generation = bus_code_page_generation(bus, last_page << 12);
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
    return is_shift_rotate_form(lo);
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
    case 0x60: case 0x61: case 0x62: case 0x63:
    case 0x64: case 0x65: case 0x66:
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
        return true;
    case 0x10: case 0x11: case 0x12: case 0x13:
        return block_shift_rotate_form(lo);
    case 0x0a: case 0x1a:
        return (lo & 0x80) != 0 || (lo & 0xf0) == 0x00 ||
               (lo & 0xf0) == 0x50 || (lo & 0xf0) == 0x70;
    case 0x1f:
        return (lo & 0x80) != 0;
    case 0x0b: case 0x1b:
        switch (lo & 0xf0) {
        case 0x00: case 0x50: case 0x70:
        case 0x80: case 0x90: case 0xd0: case 0xf0:
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
    if (!block || !block->executable) return false;
    if (block->instructions == 0) {
        return block->stop == H8S_BLOCK_STOP_BRANCH &&
            (block->branch_kind == H8S_BLOCK_BRANCH_BCC8 ||
             block->branch_kind == H8S_BLOCK_BRANCH_BCC16 ||
             block->branch_kind == H8S_BLOCK_BRANCH_JMP_ABS24);
    }
    for (unsigned i = 0; i < block->instructions; ++i) {
        if (!is_tier1_decoded_executable(block->decoded[i].op,
                                         block->decoded[i].imm,
                                         block->decoded[i].bytes))
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
        case 0x01: {
            if (lo != 0xf0 || block->decoded[i].bytes != 4) return false;
            uint16_t op2 = (uint16_t)block->decoded[i].imm;
            uint8_t hi2 = (uint8_t)(op2 >> 8);
            uint8_t lo2 = (uint8_t)op2;
            unsigned rs = (lo2 >> 4) & 0x7;
            unsigned rd = lo2 & 0x7;
            switch (hi2) {
            case 0x64:
                state->er[rd] |= state->er[rs];
                break;
            case 0x65:
                state->er[rd] ^= state->er[rs];
                break;
            case 0x66:
                state->er[rd] &= state->er[rs];
                break;
            default:
                return false;
            }
            block_set_nz_l(state, state->er[rd]);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
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
            if ((lo & 0x80) != 0) {
                unsigned rs = (lo >> 4) & 0x7;
                unsigned rd = lo & 0x7;
                uint32_t s = state->er[rs];
                uint32_t d = state->er[rd];
                uint32_t result = d + s;
                state->er[rd] = result;
                block_set_arithmetic_l(state, d, s, result, false);
            } else {
                switch (lo & 0xf0) {
                case 0x00: {
                    unsigned rd = lo & 0xf;
                    int val = block_get_reg_b(state, rd);
                    int result = (val + 1) & 0xff;
                    block_set_reg_b(state, rd, (uint8_t)result);
                    block_set_nz_b(state, result);
                    block_set_flag(state, BLOCK_CCR_V, val == 0x7f);
                    break;
                }
                case 0x50: {
                    unsigned rd = lo & 0xf;
                    int val = block_get_r(state, rd);
                    int result = (val + 1) & 0xffff;
                    block_set_r(state, rd, (uint16_t)result);
                    block_set_nz_w(state, result);
                    block_set_flag(state, BLOCK_CCR_V, val == 0x7fff);
                    break;
                }
                case 0x70: {
                    unsigned rd = lo & 0x7;
                    uint32_t val = state->er[rd];
                    state->er[rd] = val + 1u;
                    block_set_nz_l(state, state->er[rd]);
                    block_set_flag(state, BLOCK_CCR_V, val == 0x7fffffff);
                    break;
                }
                default:
                    return false;
                }
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
                state->er[erd] = 0u - v;
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
            if ((lo & 0x80) != 0) {
                unsigned rs = (lo >> 4) & 0x7;
                unsigned rd = lo & 0x7;
                uint32_t s = state->er[rs];
                uint32_t d = state->er[rd];
                uint32_t result = d - s;
                state->er[rd] = result;
                block_set_arithmetic_l(state, d, s, result, true);
            } else {
                switch (lo & 0xf0) {
                case 0x00: {
                    unsigned rd = lo & 0xf;
                    int val = block_get_reg_b(state, rd);
                    int result = (val - 1) & 0xff;
                    block_set_reg_b(state, rd, (uint8_t)result);
                    block_set_nz_b(state, result);
                    block_set_flag(state, BLOCK_CCR_V, val == 0x80);
                    break;
                }
                case 0x50: {
                    unsigned rd = lo & 0xf;
                    int val = block_get_r(state, rd);
                    int result = (val - 1) & 0xffff;
                    block_set_r(state, rd, (uint16_t)result);
                    block_set_nz_w(state, result);
                    block_set_flag(state, BLOCK_CCR_V, val == 0x8000);
                    break;
                }
                case 0x70: {
                    unsigned rd = lo & 0x7;
                    uint32_t val = state->er[rd];
                    state->er[rd] = val - 1u;
                    block_set_nz_l(state, state->er[rd]);
                    block_set_flag(state, BLOCK_CCR_V, val == 0x80000000u);
                    break;
                }
                default:
                    return false;
                }
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
        case 0x60: {
            unsigned rn = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            unsigned bit = block_get_reg_b(state, rn) & 0x7;
            block_set_reg_b(state, rd, (uint8_t)(block_get_reg_b(state, rd) | (1u << bit)));
            break;
        }
        case 0x61: {
            unsigned rn = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            unsigned bit = block_get_reg_b(state, rn) & 0x7;
            block_set_reg_b(state, rd, (uint8_t)(block_get_reg_b(state, rd) ^ (1u << bit)));
            break;
        }
        case 0x62: {
            unsigned rn = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            unsigned bit = block_get_reg_b(state, rn) & 0x7;
            block_set_reg_b(state, rd, (uint8_t)(block_get_reg_b(state, rd) & ~(1u << bit)));
            break;
        }
        case 0x63: {
            unsigned rn = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            unsigned bit = block_get_reg_b(state, rn) & 0x7;
            block_set_flag(state, BLOCK_CCR_Z, (block_get_reg_b(state, rd) & (1u << bit)) == 0);
            break;
        }
        case 0x64: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int result = block_get_r(state, rd) | block_get_r(state, rs);
            block_set_r(state, rd, (uint16_t)result);
            block_set_nz_w(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x65: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int result = block_get_r(state, rd) ^ block_get_r(state, rs);
            block_set_r(state, rd, (uint16_t)result);
            block_set_nz_w(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x66: {
            unsigned rs = (lo >> 4) & 0xf;
            unsigned rd = lo & 0xf;
            int result = block_get_r(state, rd) & block_get_r(state, rs);
            block_set_r(state, rd, (uint16_t)result);
            block_set_nz_w(state, result);
            block_set_flag(state, BLOCK_CCR_V, false);
            break;
        }
        case 0x70: {
            unsigned bit = (lo >> 4) & 0x7;
            unsigned rd = lo & 0xf;
            block_set_reg_b(state, rd, (uint8_t)(block_get_reg_b(state, rd) | (1u << bit)));
            break;
        }
        case 0x71: {
            unsigned bit = (lo >> 4) & 0x7;
            unsigned rd = lo & 0xf;
            block_set_reg_b(state, rd, (uint8_t)(block_get_reg_b(state, rd) ^ (1u << bit)));
            break;
        }
        case 0x72: {
            unsigned bit = (lo >> 4) & 0x7;
            unsigned rd = lo & 0xf;
            block_set_reg_b(state, rd, (uint8_t)(block_get_reg_b(state, rd) & ~(1u << bit)));
            break;
        }
        case 0x73: {
            unsigned bit = (lo >> 4) & 0x7;
            unsigned rd = lo & 0xf;
            block_set_flag(state, BLOCK_CCR_Z, (block_get_reg_b(state, rd) & (1u << bit)) == 0);
            break;
        }
        case 0x74: case 0x75: case 0x76: case 0x77: {
            unsigned bit = (lo >> 4) & 0x7;
            unsigned rd = lo & 0xf;
            bool bit_value = (block_get_reg_b(state, rd) & (1u << bit)) != 0;
            if (lo & 0x80) bit_value = !bit_value;
            bool carry = (state->ccr & BLOCK_CCR_C) != 0;
            switch (hi) {
            case 0x74: carry = carry || bit_value; break;
            case 0x75: carry = carry != bit_value; break;
            case 0x76: carry = carry && bit_value; break;
            case 0x77: carry = bit_value; break;
            default: return false;
            }
            block_set_flag(state, BLOCK_CCR_C, carry);
            break;
        }
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
            case 0x50: {
                unsigned rd = lo & 0xf;
                int val = block_get_r(state, rd);
                int result = (val + 1) & 0xffff;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_V, val == 0x7fff);
                break;
            }
            case 0x70: {
                unsigned rd = lo & 0x7;
                uint32_t val = state->er[rd];
                state->er[rd] = val + 1u;
                block_set_nz_l(state, state->er[rd]);
                block_set_flag(state, BLOCK_CCR_V, val == 0x7fffffff);
                break;
            }
            case 0x80: state->er[lo & 0x7] += 2; break;
            case 0x90: state->er[lo & 0x7] += 4; break;
            case 0xd0: {
                unsigned rd = lo & 0xf;
                int val = block_get_r(state, rd);
                int result = (val + 2) & 0xffff;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                break;
            }
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
            case 0x50: {
                unsigned rd = lo & 0xf;
                int val = block_get_r(state, rd);
                int result = (val - 1) & 0xffff;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_V, val == 0x8000);
                break;
            }
            case 0x70: {
                unsigned rd = lo & 0x7;
                uint32_t val = state->er[rd];
                state->er[rd] = val - 1u;
                block_set_nz_l(state, state->er[rd]);
                block_set_flag(state, BLOCK_CCR_V, val == 0x80000000u);
                break;
            }
            case 0x80: state->er[lo & 0x7] -= 2; break;
            case 0x90: state->er[lo & 0x7] -= 4; break;
            case 0xd0: {
                unsigned rd = lo & 0xf;
                int val = block_get_r(state, rd);
                int result = (val - 2) & 0xffff;
                block_set_r(state, rd, (uint16_t)result);
                block_set_nz_w(state, result);
                block_set_flag(state, BLOCK_CCR_V, val == 0x8000 || val == 0x8001);
                break;
            }
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

bool h8s_execute_plain_memory_instruction(const h8s_block_instruction_t *insn,
                                          address_bus_t *bus,
                                          h8s_block_cpu_state_t *state)
{
    if (!insn || !bus || !state) return false;

    uint16_t op = insn->op;
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)op;
    uint32_t address = 0;
    unsigned reg = 0;
    unsigned bytes = 0;
    bool write = false;
    bool post_increment = false;
    bool pre_decrement = false;

    if (op == 0x0110 || op == 0x0120 || op == 0x0130) {
        if (insn->bytes != 4) return false;
        uint16_t op2 = (uint16_t)insn->imm;
        unsigned count = ((op & 0x00f0u) >> 4);
        unsigned rn = op2 & 0x7;
        if ((op2 & 0xfff8u) == 0x6d70u) {
            if (rn < count) return false;
            for (unsigned i = 0; i <= count; ++i) {
                uint32_t addr = state->er[7] & 0xffffffu;
                const uint8_t *ptr = bus_plain_read_ptr(bus, addr, 4);
                if (!ptr) return false;
                state->er[rn - i] = ((uint32_t)ptr[0] << 24) |
                                    ((uint32_t)ptr[1] << 16) |
                                    ((uint32_t)ptr[2] << 8) |
                                    ptr[3];
                state->er[7] += 4;
            }
            state->pc += insn->bytes;
            return true;
        }
        if ((op2 & 0xfff8u) == 0x6df0u) {
            if (rn + count >= 8) return false;
            for (unsigned i = 0; i <= count; ++i) {
                state->er[7] -= 4;
                uint32_t addr = state->er[7] & 0xffffffu;
                if (!bus_is_plain_write_range(bus, addr, 4)) return false;
                bus_write32(bus, addr, state->er[rn + i]);
            }
            state->pc += insn->bytes;
            return true;
        }
        return false;
    }

    if (op == 0x0100) {
        uint16_t op2 = insn->bytes == 4 ? (uint16_t)insn->imm :
                       (uint16_t)(insn->imm >> 16);
        uint8_t hi2 = (uint8_t)(op2 >> 8);
        uint8_t lo2 = (uint8_t)op2;
        write = (lo2 & 0x80) != 0;
        reg = lo2 & 0x7;
        bytes = 4;
        switch (hi2) {
        case 0x69:
            if (insn->bytes != 4) return false;
            address = state->er[(lo2 >> 4) & 0x7];
            break;
        case 0x6b:
            if (insn->bytes != 6 || (lo2 & 0x20) != 0) return false;
            address = (uint32_t)(int32_t)(int16_t)(insn->imm & 0xffffu);
            break;
        case 0x6f:
            if (insn->bytes != 6) return false;
            address = state->er[(lo2 >> 4) & 0x7] +
                      (uint32_t)(int32_t)(int16_t)(insn->imm & 0xffffu);
            break;
        default:
            return false;
        }
    } else {
        write = (lo & 0x80) != 0;
        reg = lo & 0xf;
        switch (hi) {
        case 0x6a: /* MOV.B @aa:16/24,Rd / Rs,@aa:16/24 */
            if (insn->bytes != 4 && insn->bytes != 6) return false;
            bytes = 1;
            if ((lo >> 4) == 1 || (lo >> 4) == 3) return false;
            if (lo & 0x20) {
                if (insn->bytes != 6) return false;
                address = insn->imm & 0xffffffu;
            } else {
                if (insn->bytes != 4) return false;
                address = (uint32_t)(int32_t)(int16_t)(insn->imm & 0xffffu);
            }
            break;
        case 0x6b: /* MOV.W @aa:16/24,Rd / Rs,@aa:16/24 */
            if (insn->bytes != 4 && insn->bytes != 6) return false;
            bytes = 2;
            if (lo & 0x20) {
                if (insn->bytes != 6) return false;
                address = insn->imm & 0xffffffu;
            } else {
                if (insn->bytes != 4) return false;
                address = (uint32_t)(int32_t)(int16_t)(insn->imm & 0xffffu);
            }
            break;
        case 0x68: /* MOV.B @ERn,Rd / Rs,@ERn */
            if (insn->bytes != 2) return false;
            bytes = 1;
            address = state->er[(lo >> 4) & 0x7];
            break;
        case 0x69: /* MOV.W @ERn,Rd / Rs,@ERn */
            if (insn->bytes != 2) return false;
            bytes = 2;
            address = state->er[(lo >> 4) & 0x7];
            break;
        case 0x6c: /* MOV.B @ERn+,Rd / Rs,@-ERn */
            if (insn->bytes != 2) return false;
            bytes = 1;
            post_increment = !write;
            pre_decrement = write;
            address = state->er[(lo >> 4) & 0x7];
            break;
        case 0x6d: /* MOV.W @ERn+,Rd / Rs,@-ERn */
            if (insn->bytes != 2) return false;
            bytes = 2;
            post_increment = !write;
            pre_decrement = write;
            address = state->er[(lo >> 4) & 0x7];
            break;
        case 0x6e: /* MOV.B @(d:16,ERn),Rd / Rs,@(d:16,ERn) */
            if (insn->bytes != 4) return false;
            bytes = 1;
            address = state->er[(lo >> 4) & 0x7] +
                      (uint32_t)(int32_t)(int16_t)(insn->imm & 0xffffu);
            break;
        case 0x6f: /* MOV.W @(d:16,ERn),Rd / Rs,@(d:16,ERn) */
            if (insn->bytes != 4) return false;
            bytes = 2;
            address = state->er[(lo >> 4) & 0x7] +
                      (uint32_t)(int32_t)(int16_t)(insn->imm & 0xffffu);
            break;
        default:
            return false;
        }
    }

    address &= 0xffffffu;
    if (pre_decrement) {
        state->er[(lo >> 4) & 0x7] = (state->er[(lo >> 4) & 0x7] - bytes) & 0xffffffffu;
        address = state->er[(lo >> 4) & 0x7] & 0xffffffu;
    }

    if (write) {
        if (!bus_is_plain_write_range(bus, address, bytes)) return false;
        if (bytes == 1) {
            uint8_t value = block_get_reg_b(state, reg);
            bus_write8(bus, address, value);
            block_set_nz_b(state, value);
        } else if (bytes == 2) {
            uint16_t value = block_get_r(state, reg);
            bus_write16(bus, address, value);
            block_set_nz_w(state, value);
        } else {
            uint32_t value = state->er[reg];
            bus_write32(bus, address, value);
            block_set_nz_l(state, value);
        }
    } else {
        const uint8_t *ptr = bus_plain_read_ptr(bus, address, bytes);
        if (!ptr) return false;
        if (post_increment) {
            state->er[(lo >> 4) & 0x7] =
                (state->er[(lo >> 4) & 0x7] + bytes) & 0xffffffffu;
            post_increment = false;
        }
        if (bytes == 1) {
            uint8_t value = ptr[0];
            block_set_reg_b(state, reg, value);
            block_set_nz_b(state, value);
        } else if (bytes == 2) {
            uint16_t value = (uint16_t)((ptr[0] << 8) | ptr[1]);
            block_set_r(state, reg, value);
            block_set_nz_w(state, value);
        } else {
            uint32_t value = ((uint32_t)ptr[0] << 24) |
                             ((uint32_t)ptr[1] << 16) |
                             ((uint32_t)ptr[2] << 8) |
                             ptr[3];
            state->er[reg] = value;
            block_set_nz_l(state, value);
        }
    }
    if (post_increment)
        state->er[(lo >> 4) & 0x7] = (state->er[(lo >> 4) & 0x7] + bytes) & 0xffffffffu;
    block_set_flag(state, BLOCK_CCR_V, false);
    state->pc += insn->bytes;
    return true;
}

static bool plain_memory_instruction_supported(const h8s_block_instruction_t *insn)
{
    if (!insn) return false;
    uint16_t op = insn->op;
    uint8_t hi = (uint8_t)(op >> 8);
    if (op == 0x0100) {
        if (insn->bytes != 4 && insn->bytes != 6) return false;
        uint16_t op2 = insn->bytes == 4 ? (uint16_t)insn->imm :
                       (uint16_t)(insn->imm >> 16);
        uint8_t hi2 = (uint8_t)(op2 >> 8);
        uint8_t lo2 = (uint8_t)op2;
        if (hi2 == 0x69) return insn->bytes == 4;
        if (hi2 == 0x6b) return insn->bytes == 6 && !(lo2 & 0x20);
        if (hi2 == 0x6f) return insn->bytes == 6;
        return false;
    }
    if (op == 0x0110 || op == 0x0120 || op == 0x0130) {
        if (insn->bytes != 4) return false;
        uint16_t op2 = (uint16_t)insn->imm;
        unsigned count = ((op & 0x00f0u) >> 4);
        unsigned rn = op2 & 0x7;
        if ((op2 & 0xfff8u) == 0x6d70u)
            return rn >= count;
        if ((op2 & 0xfff8u) == 0x6df0u)
            return rn + count < 8;
        return false;
    }
    if (hi == 0x6a) {
        if ((op & 0x00f0u) == 0x0010u || (op & 0x00f0u) == 0x0030u)
            return false;
        return (op & 0x0020u) ? insn->bytes == 6 : insn->bytes == 4;
    }
    if (hi == 0x6b)
        return (op & 0x0020u) ? insn->bytes == 6 : insn->bytes == 4;
    return (hi == 0x68 || hi == 0x69 || hi == 0x6c || hi == 0x6d) ?
           insn->bytes == 2 :
           (hi == 0x6e || hi == 0x6f) ? insn->bytes == 4 : false;
}

static bool execute_one_semantic_instruction(const h8s_block_instruction_t *insn,
                                             h8s_block_cpu_state_t *state)
{
    h8s_block_t single = {
        .start = state->pc,
        .bytes = insn->bytes,
        .instructions = 1,
        .stop = H8S_BLOCK_STOP_LIMIT,
        .stop_pc = state->pc + insn->bytes,
        .executable_prefix_instructions = 1,
        .executable = true
    };
    single.decoded[0] = *insn;
    return h8s_semantic_block_supported(&single) &&
           h8s_execute_semantic_block(&single, state);
}

bool h8s_mixed_plain_block_supported(const h8s_block_t *block)
{
    if (!block) return false;
    if (block->branch_kind != H8S_BLOCK_BRANCH_BCC8 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BCC16 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JMP_ABS24)
        return false;
    for (unsigned i = 0; i < block->instructions; ++i) {
        h8s_block_t single = {
            .start = block->start,
            .bytes = block->decoded[i].bytes,
            .instructions = 1,
            .executable_prefix_instructions = 1,
            .executable = true
        };
        single.decoded[0] = block->decoded[i];
        if (!h8s_semantic_block_supported(&single)) {
            if (!plain_memory_instruction_supported(&block->decoded[i]))
                return false;
        }
    }
    return true;
}

bool h8s_execute_mixed_plain_block_exit(const h8s_block_t *block,
                                        h8s_branch_edge_cache_t *edge_cache,
                                        address_bus_t *bus,
                                        h8s_block_cpu_state_t *state,
                                        uint32_t *next_pc)
{
    if (!block || !bus || !state || !next_pc ||
        !h8s_mixed_plain_block_supported(block))
        return false;

    h8s_block_cpu_state_t updated = *state;
    unsigned i = 0;
    while (i < block->instructions) {
        if (plain_memory_instruction_supported(&block->decoded[i])) {
            if (!h8s_execute_plain_memory_instruction(&block->decoded[i], bus, &updated))
                return false;
            ++i;
            continue;
        }

        h8s_block_t run = {
            .start = updated.pc,
            .bytes = 0,
            .instructions = 0,
            .stop = H8S_BLOCK_STOP_LIMIT,
            .stop_pc = updated.pc,
            .executable_prefix_instructions = 0,
            .executable = true
        };
        while (i < block->instructions &&
               !plain_memory_instruction_supported(&block->decoded[i]) &&
               run.instructions < H8S_BLOCK_MAX_INSTRUCTIONS) {
            run.decoded[run.instructions] = block->decoded[i];
            run.bytes += block->decoded[i].bytes;
            run.instructions++;
            run.executable_prefix_instructions++;
            ++i;
        }
        run.stop_pc = run.start + run.bytes;
        if (!h8s_semantic_block_supported(&run) ||
            !h8s_execute_semantic_block(&run, &updated))
            return false;
    }

    uint32_t resolved = 0;
    bool ok = edge_cache ?
        h8s_branch_edge_cache_get(edge_cache, block, updated.ccr, &resolved) :
        h8s_block_resolve_static_branch(block, updated.ccr, &resolved);
    if (!ok) return false;
    *state = updated;
    *next_pc = resolved;
    return true;
}

bool h8s_execute_semantic_block_exit(const h8s_block_t *block,
                                     h8s_branch_edge_cache_t *edge_cache,
                                     h8s_block_cpu_state_t *state,
                                     uint32_t *next_pc)
{
    if (!block || !state || !next_pc || !h8s_semantic_block_supported(block))
        return false;
    if (block->branch_kind != H8S_BLOCK_BRANCH_BCC8 &&
        block->branch_kind != H8S_BLOCK_BRANCH_BCC16 &&
        block->branch_kind != H8S_BLOCK_BRANCH_JMP_ABS24)
        return false;

    h8s_block_cpu_state_t updated = *state;
    if (!h8s_execute_semantic_block(block, &updated))
        return false;

    uint32_t resolved = 0;
    bool ok = edge_cache ?
        h8s_branch_edge_cache_get(edge_cache, block, updated.ccr, &resolved) :
        h8s_block_resolve_static_branch(block, updated.ccr, &resolved);
    if (!ok)
        return false;

    updated.pc = resolved;
    *state = updated;
    *next_pc = resolved;
    return true;
}

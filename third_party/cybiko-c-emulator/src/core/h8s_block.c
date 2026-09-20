#include "core/h8s_block.h"
#include <string.h>

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

    h8s_block_t block = {
        .start = start,
        .bytes = 0,
        .instructions = 0,
        .stop = H8S_BLOCK_STOP_LIMIT,
        .stop_pc = start
    };

    uint32_t pc = start;
    while (block.instructions < max_instructions) {
        if ((size_t)pc + 1 >= rom_size) {
            block.stop = H8S_BLOCK_STOP_TRUNCATED;
            block.stop_pc = pc;
            break;
        }

        uint16_t op = read_be16(rom + pc);
        if (is_control_transfer(op)) {
            block.stop = H8S_BLOCK_STOP_BRANCH;
            block.stop_pc = pc;
            break;
        }

        unsigned bytes = 0;
        bool prefix_stop = false;
        bool control_stop = false;
        if (!instruction_length(rom, rom_size, pc, op, &bytes, &prefix_stop, &control_stop)) {
            block.stop = control_stop ? H8S_BLOCK_STOP_BRANCH :
                         prefix_stop ? H8S_BLOCK_STOP_PREFIX : H8S_BLOCK_STOP_UNSUPPORTED;
            block.stop_pc = pc;
            break;
        }
        if ((size_t)pc + bytes > rom_size) {
            block.stop = H8S_BLOCK_STOP_TRUNCATED;
            block.stop_pc = pc;
            break;
        }

        pc += bytes;
        block.bytes += bytes;
        block.instructions++;
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
}

void h8s_block_cache_clear(h8s_block_cache_t *cache)
{
    if (!cache) return;
    unsigned max_instructions = cache->max_instructions;
    memset(cache, 0, sizeof(*cache));
    cache->max_instructions = max_instructions ? max_instructions : 32;
}

const h8s_block_t *h8s_block_cache_get(h8s_block_cache_t *cache,
                                       const uint8_t *rom, size_t rom_size,
                                       uint32_t start)
{
    if (!cache || !rom) return NULL;

    unsigned index = cache_index(start);
    h8s_block_cache_entry_t *entry = &cache->entries[index];
    if (entry->valid && entry->tag == start) {
        cache->hits++;
        return &entry->block;
    }

    h8s_block_t block;
    if (!h8s_analyze_rom_block(rom, rom_size, start, cache->max_instructions, &block))
        return NULL;

    if (entry->valid) cache->evictions++;
    entry->valid = true;
    entry->tag = start;
    entry->block = block;
    cache->misses++;
    return &entry->block;
}

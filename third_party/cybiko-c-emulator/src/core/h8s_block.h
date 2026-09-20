#ifndef CYBIKO_H8S_BLOCK_H
#define CYBIKO_H8S_BLOCK_H

#include "types.h"

typedef enum {
    H8S_BLOCK_STOP_LIMIT,
    H8S_BLOCK_STOP_BRANCH,
    H8S_BLOCK_STOP_PREFIX,
    H8S_BLOCK_STOP_TRUNCATED,
    H8S_BLOCK_STOP_UNSUPPORTED
} h8s_block_stop_t;

typedef struct {
    uint32_t start;
    uint32_t bytes;
    unsigned instructions;
    h8s_block_stop_t stop;
    uint32_t stop_pc;
} h8s_block_t;

#define H8S_BLOCK_CACHE_ENTRIES 256

typedef struct {
    bool valid;
    uint32_t tag;
    h8s_block_t block;
} h8s_block_cache_entry_t;

typedef struct {
    h8s_block_cache_entry_t entries[H8S_BLOCK_CACHE_ENTRIES];
    unsigned max_instructions;
    unsigned hits;
    unsigned misses;
    unsigned evictions;
} h8s_block_cache_t;

bool h8s_analyze_rom_block(const uint8_t *rom, size_t rom_size, uint32_t start,
                           unsigned max_instructions, h8s_block_t *out);
void h8s_block_cache_init(h8s_block_cache_t *cache, unsigned max_instructions);
void h8s_block_cache_clear(h8s_block_cache_t *cache);
const h8s_block_t *h8s_block_cache_get(h8s_block_cache_t *cache,
                                       const uint8_t *rom, size_t rom_size,
                                       uint32_t start);

#endif

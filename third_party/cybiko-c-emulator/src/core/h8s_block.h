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

bool h8s_analyze_rom_block(const uint8_t *rom, size_t rom_size, uint32_t start,
                           unsigned max_instructions, h8s_block_t *out);

#endif

#ifndef CYBIKO_H8S_BLOCK_H
#define CYBIKO_H8S_BLOCK_H

#include "types.h"

typedef struct address_bus address_bus_t;

typedef enum {
    H8S_BLOCK_STOP_LIMIT,
    H8S_BLOCK_STOP_BRANCH,
    H8S_BLOCK_STOP_PREFIX,
    H8S_BLOCK_STOP_TRUNCATED,
    H8S_BLOCK_STOP_UNSUPPORTED
} h8s_block_stop_t;

typedef enum {
    H8S_BLOCK_BRANCH_NONE,
    H8S_BLOCK_BRANCH_BCC8,
    H8S_BLOCK_BRANCH_BCC16,
    H8S_BLOCK_BRANCH_BSR8,
    H8S_BLOCK_BRANCH_BSR16,
    H8S_BLOCK_BRANCH_JMP_ABS24,
    H8S_BLOCK_BRANCH_JSR_ABS24,
    H8S_BLOCK_BRANCH_INDIRECT,
    H8S_BLOCK_BRANCH_RETURN,
    H8S_BLOCK_BRANCH_TRAP,
    H8S_BLOCK_BRANCH_SLEEP
} h8s_block_branch_kind_t;

#define H8S_BLOCK_MAX_INSTRUCTIONS 32

typedef struct {
    uint16_t op;
    uint32_t imm;
    uint8_t bytes;
} h8s_block_instruction_t;

typedef struct {
    uint32_t er[8];
    uint8_t ccr;
    uint32_t pc;
} h8s_block_cpu_state_t;

typedef struct {
    uint32_t start;
    uint32_t bytes;
    unsigned instructions;
    h8s_block_stop_t stop;
    uint32_t stop_pc;
    h8s_block_branch_kind_t branch_kind;
    uint16_t branch_op;
    uint8_t branch_bytes;
    uint8_t branch_condition;
    bool branch_conditional;
    bool branch_has_target;
    uint32_t branch_fallthrough;
    uint32_t branch_target;
    /* Number of leading instructions covered by the initial semantic tier. */
    unsigned executable_prefix_instructions;
    /* True only when every instruction in this block is covered by that tier. */
    bool executable;
    h8s_block_instruction_t decoded[H8S_BLOCK_MAX_INSTRUCTIONS];
} h8s_block_t;

#define H8S_BLOCK_CACHE_ENTRIES 256
#define H8S_BRANCH_EDGE_CACHE_ENTRIES 512

typedef struct {
    bool valid;
    uint32_t tag;
    uint32_t generation;
    h8s_block_t block;
} h8s_block_cache_entry_t;

typedef struct {
    h8s_block_cache_entry_t entries[H8S_BLOCK_CACHE_ENTRIES];
    unsigned max_instructions;
    unsigned hits;
    unsigned misses;
    unsigned evictions;
    uint32_t generation;
} h8s_block_cache_t;

typedef struct {
    bool valid;
    uint32_t tag;
    uint32_t generation;
    uint8_t ccr_key;
    uint32_t next_pc;
} h8s_branch_edge_cache_entry_t;

typedef struct {
    h8s_branch_edge_cache_entry_t entries[H8S_BRANCH_EDGE_CACHE_ENTRIES];
    unsigned hits;
    unsigned misses;
    unsigned evictions;
    uint32_t generation;
} h8s_branch_edge_cache_t;

bool h8s_analyze_rom_block(const uint8_t *rom, size_t rom_size, uint32_t start,
                           unsigned max_instructions, h8s_block_t *out);
void h8s_block_cache_init(h8s_block_cache_t *cache, unsigned max_instructions);
void h8s_block_cache_clear(h8s_block_cache_t *cache);
const h8s_block_t *h8s_block_cache_get(h8s_block_cache_t *cache,
                                       const uint8_t *rom, size_t rom_size,
                                       uint32_t start);
/* Single-word helper for non-prefixed opcodes. Use h8s_semantic_block_supported
 * for decoded blocks because prefixed instructions need their retained second
 * word/immediate to determine semantic coverage. */
bool h8s_semantic_instruction_supported(uint16_t op);
bool h8s_semantic_block_supported(const h8s_block_t *block);
bool h8s_execute_semantic_block(const h8s_block_t *block,
                                h8s_block_cpu_state_t *state);
bool h8s_execute_plain_memory_instruction(const h8s_block_instruction_t *insn,
                                          address_bus_t *bus,
                                          h8s_block_cpu_state_t *state);
bool h8s_execute_semantic_block_exit(const h8s_block_t *block,
                                     h8s_branch_edge_cache_t *edge_cache,
                                     h8s_block_cpu_state_t *state,
                                     uint32_t *next_pc);
bool h8s_block_resolve_static_branch(const h8s_block_t *block,
                                     uint8_t ccr, uint32_t *next_pc);
void h8s_branch_edge_cache_init(h8s_branch_edge_cache_t *cache);
void h8s_branch_edge_cache_clear(h8s_branch_edge_cache_t *cache);
bool h8s_branch_edge_cache_get(h8s_branch_edge_cache_t *cache,
                               const h8s_block_t *block, uint8_t ccr,
                               uint32_t *next_pc);
const h8s_block_t *h8s_block_cache_get_chain_target(h8s_block_cache_t *block_cache,
                                                    h8s_branch_edge_cache_t *edge_cache,
                                                    const uint8_t *rom, size_t rom_size,
                                                    const h8s_block_t *block,
                                                    uint8_t ccr, uint32_t *next_pc);

#endif

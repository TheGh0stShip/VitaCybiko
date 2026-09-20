#include "acutest.h"
#include "core/h8s_block.h"
#include "core/h8s_cpu.h"
#include "core/address_bus.h"
#include <stdio.h>
#include <string.h>

#define EQUIV_CODE_BASE 0xFFDC00u

static void write_equiv_code(address_bus_t *bus, const uint8_t *code, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        memory_write8(&bus->on_chip_ram, (uint32_t)i, code[i]);
}

static void setup_equiv_cpu(address_bus_t *bus, h8s_cpu_t *cpu,
                            const uint8_t *code, size_t size,
                            const uint32_t er[8], uint8_t ccr)
{
    bus_init(bus);
    memory_init(&bus->on_chip_ram, 0x2400, true);
    h8s_cpu_init(cpu, bus);
    bus->cpu = cpu;
    cpu->pc = EQUIV_CODE_BASE;
    cpu->ccr = ccr;
    for (unsigned i = 0; i < 8; ++i)
        cpu->er[i] = er[i];
    write_equiv_code(bus, code, size);
}

static void teardown_equiv_cpu(address_bus_t *bus)
{
    memory_free(&bus->on_chip_ram);
    bus_free(bus);
}

static void check_semantic_matches_interpreter(const char *name,
                                               const uint8_t *code, size_t size,
                                               const uint32_t er[8], uint8_t ccr)
{
    uint8_t rom[10] = {0};
    TEST_ASSERT(size + 2 <= sizeof(rom));
    memcpy(rom, code, size);
    rom[size] = 0x54;
    rom[size + 1] = 0x70; /* RTS boundary after the tested instruction. */

    h8s_block_t block;
    TEST_ASSERT_(h8s_analyze_rom_block(rom, size + 2, 0, 4, &block),
                 "%s should analyze", name);
    TEST_ASSERT_(block.instructions == 1, "%s should produce a one-instruction block", name);
    TEST_ASSERT_(h8s_semantic_block_supported(&block), "%s should be semantic-supported", name);

    h8s_block_cpu_state_t semantic = {.ccr = ccr, .pc = 0};
    for (unsigned i = 0; i < 8; ++i)
        semantic.er[i] = er[i];
    TEST_ASSERT_(h8s_execute_semantic_block(&block, &semantic),
                 "%s semantic execution should succeed", name);

    address_bus_t bus;
    h8s_cpu_t cpu;
    setup_equiv_cpu(&bus, &cpu, code, size, er, ccr);
    h8s_cpu_step(&cpu);

    for (unsigned i = 0; i < 8; ++i) {
        TEST_CHECK_(semantic.er[i] == cpu.er[i],
                    "%s ER%u semantic=0x%08x interpreter=0x%08x",
                    name, i, semantic.er[i], cpu.er[i]);
    }
    TEST_CHECK_(semantic.ccr == cpu.ccr,
                "%s CCR semantic=0x%02x interpreter=0x%02x",
                name, semantic.ccr, cpu.ccr);
    TEST_CHECK_(semantic.pc == size,
                "%s semantic PC should advance by %zu, got %u",
                name, size, semantic.pc);
    TEST_CHECK_(cpu.pc == EQUIV_CODE_BASE + size,
                "%s interpreter PC should advance by %zu, got 0x%06x",
                name, size, cpu.pc);
    teardown_equiv_cpu(&bus);
}

static void check_semantic_block_matches_interpreter(const char *name,
                                                     const uint8_t *code, size_t size,
                                                     const uint32_t er[8], uint8_t ccr)
{
    uint8_t rom[64] = {0};
    TEST_ASSERT(size + 2 <= sizeof(rom));
    memcpy(rom, code, size);
    rom[size] = 0x54;
    rom[size + 1] = 0x70; /* RTS boundary after the tested block. */

    h8s_block_t block;
    TEST_ASSERT_(h8s_analyze_rom_block(rom, size + 2, 0, H8S_BLOCK_MAX_INSTRUCTIONS, &block),
                 "%s should analyze", name);
    TEST_ASSERT_(block.instructions > 1, "%s should produce a multi-instruction block", name);
    TEST_ASSERT_(block.bytes == size, "%s should stop exactly before RTS boundary", name);
    TEST_ASSERT_(h8s_semantic_block_supported(&block), "%s should be semantic-supported", name);

    h8s_block_cpu_state_t semantic = {.ccr = ccr, .pc = 0};
    for (unsigned i = 0; i < 8; ++i)
        semantic.er[i] = er[i];
    TEST_ASSERT_(h8s_execute_semantic_block(&block, &semantic),
                 "%s semantic execution should succeed", name);

    address_bus_t bus;
    h8s_cpu_t cpu;
    setup_equiv_cpu(&bus, &cpu, code, size, er, ccr);
    for (unsigned i = 0; i < block.instructions; ++i)
        h8s_cpu_step(&cpu);

    for (unsigned i = 0; i < 8; ++i) {
        TEST_CHECK_(semantic.er[i] == cpu.er[i],
                    "%s ER%u semantic=0x%08x interpreter=0x%08x",
                    name, i, semantic.er[i], cpu.er[i]);
    }
    TEST_CHECK_(semantic.ccr == cpu.ccr,
                "%s CCR semantic=0x%02x interpreter=0x%02x",
                name, semantic.ccr, cpu.ccr);
    TEST_CHECK_(semantic.pc == size,
                "%s semantic PC should advance by %zu, got %u",
                name, size, semantic.pc);
    TEST_CHECK_(cpu.pc == EQUIV_CODE_BASE + size,
                "%s interpreter PC should advance by %zu, got 0x%06x",
                name, size, cpu.pc);
    teardown_equiv_cpu(&bus);
}

static void check_two_byte_opcode_matrix(uint16_t op)
{
    static const uint32_t ers[][8] = {
        {
            0x12345678, 0x87654321, 0x7fffffff, 0x80000001,
            0x0000f0f0, 0x00000f0f, 0x00000003, 0x00000080
        },
        {
            0x00000000, 0x00000001, 0xffffffff, 0x80000000,
            0x00007fff, 0x00008000, 0x00000007, 0x0000ff00
        },
        {
            0x00ff00ff, 0xff00ff00, 0x00010000, 0xffff0001,
            0xaaaaaaaa, 0x55555555, 0x00000004, 0x0000007f
        }
    };
    static const uint8_t ccrs[] = {0x00, 0x01, 0x25, 0xff};
    uint8_t code[] = {(uint8_t)(op >> 8), (uint8_t)op};
    char name[32];
    snprintf(name, sizeof(name), "opcode 0x%04x", op);

    for (unsigned e = 0; e < sizeof(ers) / sizeof(ers[0]); ++e) {
        for (unsigned c = 0; c < sizeof(ccrs) / sizeof(ccrs[0]); ++c)
            check_semantic_matches_interpreter(name, code, sizeof(code), ers[e], ccrs[c]);
    }
}

static void check_word_immediate_opcode_matrix(uint8_t subop, uint8_t rd, uint16_t imm)
{
    static const uint32_t ers[][8] = {
        {
            0x12345678, 0x87654321, 0x7fffffff, 0x80000001,
            0x0000f0f0, 0x00000f0f, 0x00000003, 0x00000080
        },
        {
            0x00000000, 0x00000001, 0xffffffff, 0x80000000,
            0x00007fff, 0x00008000, 0x00000007, 0x0000ff00
        },
        {
            0x00ff00ff, 0xff00ff00, 0x00010000, 0xffff0001,
            0xaaaaaaaa, 0x55555555, 0x00000004, 0x0000007f
        }
    };
    static const uint8_t ccrs[] = {0x00, 0x01, 0x25, 0xff};
    uint8_t code[] = {
        0x79, (uint8_t)((subop << 4) | (rd & 0xf)),
        (uint8_t)(imm >> 8), (uint8_t)imm
    };
    char name[48];
    snprintf(name, sizeof(name), "opcode 0x79%02x imm 0x%04x", code[1], imm);

    for (unsigned e = 0; e < sizeof(ers) / sizeof(ers[0]); ++e) {
        for (unsigned c = 0; c < sizeof(ccrs) / sizeof(ccrs[0]); ++c)
            check_semantic_matches_interpreter(name, code, sizeof(code), ers[e], ccrs[c]);
    }
}

static void check_long_immediate_opcode_matrix(uint8_t subop, uint8_t rd, uint32_t imm)
{
    static const uint32_t ers[][8] = {
        {
            0x12345678, 0x87654321, 0x7fffffff, 0x80000001,
            0x0000f0f0, 0x00000f0f, 0x00000003, 0x00000080
        },
        {
            0x00000000, 0x00000001, 0xffffffff, 0x80000000,
            0x00007fff, 0x00008000, 0x00000007, 0x0000ff00
        },
        {
            0x00ff00ff, 0xff00ff00, 0x00010000, 0xffff0001,
            0xaaaaaaaa, 0x55555555, 0x00000004, 0x0000007f
        }
    };
    static const uint8_t ccrs[] = {0x00, 0x01, 0x25, 0xff};
    uint8_t code[] = {
        0x7a, (uint8_t)((subop << 4) | (rd & 0x7)),
        (uint8_t)(imm >> 24), (uint8_t)(imm >> 16),
        (uint8_t)(imm >> 8), (uint8_t)imm
    };
    char name[64];
    snprintf(name, sizeof(name), "opcode 0x7a%02x imm 0x%08x", code[1], imm);

    for (unsigned e = 0; e < sizeof(ers) / sizeof(ers[0]); ++e) {
        for (unsigned c = 0; c < sizeof(ccrs) / sizeof(ccrs[0]); ++c)
            check_semantic_matches_interpreter(name, code, sizeof(code), ers[e], ccrs[c]);
    }
}

static void test_stops_before_branch(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,       /* ADDS #1, ER0 */
        0x0b, 0x81,       /* ADDS #2, ER1 */
        0x40, 0x02,       /* BRA d:8 */
        0x0b, 0x02
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 2);
    TEST_CHECK(block.bytes == 4);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_BRANCH);
    TEST_CHECK(block.stop_pc == 4);
    TEST_CHECK(block.branch_kind == H8S_BLOCK_BRANCH_BCC8);
    TEST_CHECK(block.branch_op == 0x4002);
    TEST_CHECK(block.branch_bytes == 2);
    TEST_CHECK(block.branch_conditional);
    TEST_CHECK(block.branch_condition == 0);
    TEST_CHECK(block.branch_has_target);
    TEST_CHECK(block.branch_fallthrough == 6);
    TEST_CHECK(block.branch_target == 8);
    TEST_CHECK(block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 2);
    TEST_CHECK(block.decoded[0].op == 0x0b00);
    TEST_CHECK(block.decoded[0].bytes == 2);
    TEST_CHECK(block.decoded[1].op == 0x0b81);
    TEST_CHECK(block.decoded[1].bytes == 2);
}

static void test_branch_metadata_for_static_exits(void)
{
    struct branch_case {
        const char *name;
        uint8_t rom[10];
        size_t size;
        h8s_block_branch_kind_t kind;
        uint8_t bytes;
        bool conditional;
        uint8_t condition;
        bool has_target;
        uint32_t fallthrough;
        uint32_t target;
    } cases[] = {
        {"BNE d:8", {0x46, 0xfc}, 2, H8S_BLOCK_BRANCH_BCC8, 2, true, 6, true, 2, 0xfffffe},
        {"BGT d:16", {0x58, 0xe0, 0x00, 0x06}, 4, H8S_BLOCK_BRANCH_BCC16, 4, true, 14, true, 4, 10},
        {"BSR d:8", {0x55, 0x04}, 2, H8S_BLOCK_BRANCH_BSR8, 2, false, 0, true, 2, 6},
        {"BSR d:16", {0x5c, 0x00, 0xff, 0xfc}, 4, H8S_BLOCK_BRANCH_BSR16, 4, false, 0, true, 4, 0},
        {"JMP @aa:24", {0x5a, 0x12, 0x34, 0x56}, 4, H8S_BLOCK_BRANCH_JMP_ABS24, 4, false, 0, true, 4, 0x123456},
        {"JSR @aa:24", {0x5e, 0xab, 0xcd, 0xef}, 4, H8S_BLOCK_BRANCH_JSR_ABS24, 4, false, 0, true, 4, 0xabcdef},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        h8s_block_t block;
        TEST_ASSERT_(h8s_analyze_rom_block(cases[i].rom, cases[i].size, 0, 16, &block),
                     "%s should analyze", cases[i].name);
        TEST_CHECK_(block.instructions == 0, "%s should have no straight-line instructions", cases[i].name);
        TEST_CHECK_(block.stop == H8S_BLOCK_STOP_BRANCH, "%s should stop at branch", cases[i].name);
        TEST_CHECK_(block.stop_pc == 0, "%s stop pc", cases[i].name);
        TEST_CHECK_(block.branch_kind == cases[i].kind, "%s kind", cases[i].name);
        TEST_CHECK_(block.branch_bytes == cases[i].bytes, "%s bytes", cases[i].name);
        TEST_CHECK_(block.branch_conditional == cases[i].conditional, "%s conditional", cases[i].name);
        TEST_CHECK_(block.branch_condition == cases[i].condition, "%s condition", cases[i].name);
        TEST_CHECK_(block.branch_has_target == cases[i].has_target, "%s has target", cases[i].name);
        TEST_CHECK_(block.branch_fallthrough == cases[i].fallthrough, "%s fallthrough", cases[i].name);
        TEST_CHECK_(block.branch_target == cases[i].target, "%s target", cases[i].name);
    }
}

static void test_branch_metadata_for_indirect_and_system_exits(void)
{
    struct branch_case {
        const char *name;
        uint8_t rom[4];
        size_t size;
        h8s_block_branch_kind_t kind;
        bool has_target;
    } cases[] = {
        {"SLEEP", {0x01, 0x80}, 2, H8S_BLOCK_BRANCH_SLEEP, false},
        {"RTS", {0x54, 0x70}, 2, H8S_BLOCK_BRANCH_RETURN, false},
        {"RTE", {0x56, 0x70}, 2, H8S_BLOCK_BRANCH_RETURN, false},
        {"TRAPA", {0x57, 0x00}, 2, H8S_BLOCK_BRANCH_TRAP, false},
        {"JMP @ERn", {0x59, 0x00}, 2, H8S_BLOCK_BRANCH_INDIRECT, false},
        {"JMP @@aa:8", {0x5b, 0x20}, 2, H8S_BLOCK_BRANCH_INDIRECT, false},
        {"JSR @ERn", {0x5d, 0x00}, 2, H8S_BLOCK_BRANCH_INDIRECT, false},
        {"JSR @@aa:8", {0x5f, 0x20}, 2, H8S_BLOCK_BRANCH_INDIRECT, false},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        h8s_block_t block;
        TEST_ASSERT_(h8s_analyze_rom_block(cases[i].rom, cases[i].size, 0, 16, &block),
                     "%s should analyze", cases[i].name);
        TEST_CHECK_(block.stop == H8S_BLOCK_STOP_BRANCH, "%s should stop at branch", cases[i].name);
        TEST_CHECK_(block.branch_kind == cases[i].kind, "%s kind", cases[i].name);
        TEST_CHECK_(block.branch_has_target == cases[i].has_target, "%s has target", cases[i].name);
        TEST_CHECK_(block.branch_fallthrough == 2, "%s fallthrough", cases[i].name);
    }
}

static void test_resolves_static_branch_exits(void)
{
    struct branch_case {
        const char *name;
        uint8_t rom[4];
        size_t size;
        uint8_t ccr;
        uint32_t expected;
    } cases[] = {
        {"BRA d:8", {0x40, 0x06}, 2, 0x00, 8},
        {"BRN d:8", {0x41, 0x06}, 2, 0x00, 2},
        {"BNE taken", {0x46, 0x06}, 2, 0x00, 8},
        {"BNE fallthrough", {0x46, 0x06}, 2, 0x04, 2},
        {"BEQ taken", {0x47, 0x06}, 2, 0x04, 8},
        {"BVS taken", {0x49, 0x06}, 2, 0x02, 8},
        {"BMI taken", {0x4b, 0x06}, 2, 0x08, 8},
        {"BGE fallthrough", {0x4c, 0x06}, 2, 0x08, 2},
        {"BLT taken", {0x4d, 0x06}, 2, 0x08, 8},
        {"BGT taken", {0x4e, 0x06}, 2, 0x00, 8},
        {"BLE taken zero", {0x4f, 0x06}, 2, 0x04, 8},
        {"BGT d:16 taken", {0x58, 0xe0, 0x00, 0x06}, 4, 0x00, 10},
        {"BGT d:16 fallthrough", {0x58, 0xe0, 0x00, 0x06}, 4, 0x04, 4},
        {"BSR d:8", {0x55, 0x06}, 2, 0xff, 8},
        {"BSR d:16", {0x5c, 0x00, 0x00, 0x06}, 4, 0xff, 10},
        {"JMP abs24", {0x5a, 0x12, 0x34, 0x56}, 4, 0xff, 0x123456},
        {"JSR abs24", {0x5e, 0xab, 0xcd, 0xef}, 4, 0xff, 0xabcdef},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        h8s_block_t block;
        uint32_t next = 0xdeadbeef;
        TEST_ASSERT_(h8s_analyze_rom_block(cases[i].rom, cases[i].size, 0, 16, &block),
                     "%s should analyze", cases[i].name);
        TEST_CHECK_(h8s_block_resolve_static_branch(&block, cases[i].ccr, &next),
                    "%s should resolve", cases[i].name);
        TEST_CHECK_(next == cases[i].expected,
                    "%s next=0x%06x expected=0x%06x",
                    cases[i].name, next, cases[i].expected);
    }
}

static void test_rejects_dynamic_branch_exits(void)
{
    const uint8_t *roms[] = {
        (const uint8_t[]){0x54, 0x70}, /* RTS */
        (const uint8_t[]){0x56, 0x70}, /* RTE */
        (const uint8_t[]){0x57, 0x00}, /* TRAPA */
        (const uint8_t[]){0x59, 0x00}, /* JMP @ERn */
        (const uint8_t[]){0x5d, 0x00}, /* JSR @ERn */
        (const uint8_t[]){0x01, 0x80}, /* SLEEP */
    };

    for (unsigned i = 0; i < sizeof(roms) / sizeof(roms[0]); ++i) {
        h8s_block_t block;
        uint32_t next = 0;
        TEST_ASSERT(h8s_analyze_rom_block(roms[i], 2, 0, 16, &block));
        TEST_CHECK(!h8s_block_resolve_static_branch(&block, 0, &next));
    }
}

static void test_branch_edge_cache_hit_miss_and_ccr_keys(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,       /* ADDS #1, ER0 */
        0x46, 0x04,       /* BNE d:8 */
        0x0b, 0x01,
        0x0b, 0x02
    };
    h8s_block_t block;
    h8s_branch_edge_cache_t cache;
    uint32_t next = 0;

    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.stop_pc == 2);
    h8s_branch_edge_cache_init(&cache);
    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &block, 0x00, &next));
    TEST_CHECK(next == 8);
    TEST_CHECK(cache.misses == 1 && cache.hits == 0);
    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &block, 0x20, &next)); /* H does not affect BNE. */
    TEST_CHECK(next == 8);
    TEST_CHECK(cache.misses == 1 && cache.hits == 1);
    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &block, 0x04, &next));
    TEST_CHECK(next == 4);
    TEST_CHECK(cache.misses == 2 && cache.hits == 1);
}

static void test_branch_edge_cache_clear_and_collision(void)
{
    uint8_t rom[1030] = {0};
    h8s_branch_edge_cache_t cache;
    uint32_t next = 0;
    rom[0] = 0x40; rom[1] = 0x02;
    rom[1026] = 0x40; rom[1027] = 0x04;

    h8s_block_t first, colliding;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &first));
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 1026, 16, &colliding));

    h8s_branch_edge_cache_init(&cache);
    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &first, 0, &next));
    TEST_CHECK(next == 4);
    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &first, 0, &next));
    TEST_CHECK(cache.hits == 1);

    h8s_branch_edge_cache_clear(&cache);
    TEST_CHECK(cache.hits == 0 && cache.misses == 0 && cache.evictions == 0);
    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &first, 0, &next));
    TEST_CHECK(cache.misses == 1 && cache.hits == 0);

    TEST_CHECK(h8s_branch_edge_cache_get(&cache, &colliding, 0, &next));
    TEST_CHECK(next == 1032);
    TEST_CHECK(cache.evictions == 1);
}

static void test_branch_edge_cache_rejects_dynamic_exits(void)
{
    const uint8_t rom[] = {0x54, 0x70}; /* RTS */
    h8s_block_t block;
    h8s_branch_edge_cache_t cache;
    uint32_t next = 0x123456;

    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_branch_edge_cache_init(&cache);
    TEST_CHECK(!h8s_branch_edge_cache_get(&cache, &block, 0, &next));
    TEST_CHECK(next == 0x123456);
    TEST_CHECK(cache.hits == 0 && cache.misses == 0 && cache.evictions == 0);
}

static void test_chain_target_returns_semantic_cached_block(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,       /* 0: ADDS #1, ER0 */
        0x46, 0x04,       /* 2: BNE 8 */
        0x0f, 0x00,       /* 4: unsupported fallback block */
        0x54, 0x70,       /* 6: RTS */
        0x0b, 0x01,       /* 8: ADDS #2, ER1 */
        0x0b, 0x02,       /* 10: ADDS #4, ER2 */
        0x54, 0x70        /* 12: RTS */
    };
    h8s_block_cache_t block_cache;
    h8s_branch_edge_cache_t edge_cache;
    uint32_t next = 0;

    h8s_block_cache_init(&block_cache, 16);
    h8s_branch_edge_cache_init(&edge_cache);
    const h8s_block_t *entry = h8s_block_cache_get(&block_cache, rom, sizeof(rom), 0);
    TEST_ASSERT(entry != NULL);
    const h8s_block_t *target =
        h8s_block_cache_get_chain_target(&block_cache, &edge_cache, rom, sizeof(rom),
                                         entry, 0x00, &next);

    TEST_ASSERT(target != NULL);
    TEST_CHECK(next == 8);
    TEST_CHECK(target->start == 8);
    TEST_CHECK(target->instructions == 2);
    TEST_CHECK(h8s_semantic_block_supported(target));
    TEST_CHECK(edge_cache.misses == 1);
    TEST_CHECK(block_cache.misses == 2);

    const h8s_block_t *again =
        h8s_block_cache_get_chain_target(&block_cache, &edge_cache, rom, sizeof(rom),
                                         entry, 0x00, &next);
    TEST_CHECK(again == target);
    TEST_CHECK(edge_cache.hits == 1);
    TEST_CHECK(block_cache.hits >= 1);
}

static void test_chain_target_rejects_fallbacks(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,       /* 0: ADDS #1, ER0 */
        0x46, 0x00,       /* 2: BNE 4, BNE false also falls through to 4 */
        0x0f, 0x00,       /* 4: unsupported semantic target */
        0x54, 0x70        /* 6: RTS */
    };
    h8s_block_cache_t block_cache;
    h8s_branch_edge_cache_t edge_cache;
    uint32_t next = 0x123456;

    h8s_block_cache_init(&block_cache, 16);
    h8s_branch_edge_cache_init(&edge_cache);
    const h8s_block_t *entry = h8s_block_cache_get(&block_cache, rom, sizeof(rom), 0);
    TEST_ASSERT(entry != NULL);
    const h8s_block_t *target =
        h8s_block_cache_get_chain_target(&block_cache, &edge_cache, rom, sizeof(rom),
                                         entry, 0x00, &next);

    TEST_CHECK(target == NULL);
    TEST_CHECK(next == 4);
    TEST_CHECK(edge_cache.misses == 1);
    TEST_CHECK(block_cache.misses == 2);
}

static void test_chain_target_rejects_static_calls(void)
{
    const uint8_t bsr_rom[] = {
        0x0b, 0x00,       /* 0: ADDS #1, ER0 */
        0x55, 0x04,       /* 2: BSR 8 */
        0x54, 0x70,       /* 4: RTS */
        0x0b, 0x01,       /* 6: ADDS #2, ER1 */
        0x0b, 0x02,       /* 8: ADDS #4, ER2 */
        0x54, 0x70        /* 10: RTS */
    };
    const uint8_t jsr_rom[] = {
        0x0b, 0x00,       /* 0: ADDS #1, ER0 */
        0x5e, 0x00, 0x00, 0x08, /* 2: JSR @0x000008 */
        0x54, 0x70,       /* 6: RTS */
        0x0b, 0x01,       /* 8: ADDS #2, ER1 */
        0x54, 0x70        /* 10: RTS */
    };
    h8s_block_cache_t block_cache;
    h8s_branch_edge_cache_t edge_cache;
    uint32_t next = 0x123456;

    h8s_block_cache_init(&block_cache, 16);
    h8s_branch_edge_cache_init(&edge_cache);
    const h8s_block_t *entry = h8s_block_cache_get(&block_cache, bsr_rom, sizeof(bsr_rom), 0);
    TEST_ASSERT(entry != NULL);
    TEST_CHECK(h8s_block_cache_get_chain_target(&block_cache, &edge_cache,
                                                bsr_rom, sizeof(bsr_rom),
                                                entry, 0, &next) == NULL);
    TEST_CHECK(next == 0x123456);
    TEST_CHECK(edge_cache.misses == 0);

    h8s_block_cache_clear(&block_cache);
    h8s_branch_edge_cache_clear(&edge_cache);
    entry = h8s_block_cache_get(&block_cache, jsr_rom, sizeof(jsr_rom), 0);
    TEST_ASSERT(entry != NULL);
    TEST_CHECK(h8s_block_cache_get_chain_target(&block_cache, &edge_cache,
                                                jsr_rom, sizeof(jsr_rom),
                                                entry, 0, &next) == NULL);
    TEST_CHECK(next == 0x123456);
    TEST_CHECK(edge_cache.misses == 0);
}

static void test_semantic_block_exit_uses_updated_ccr(void)
{
    const uint8_t rom[] = {
        0xf8, 0x00,       /* 0: MOV.B #0,R0L -> sets Z */
        0x46, 0x04,       /* 2: BNE 8; must fall through after updated Z */
        0x0b, 0x01,       /* 4: fall-through target */
        0x54, 0x70,       /* 6: RTS */
        0x0b, 0x02,       /* 8: stale-CCR target */
        0x54, 0x70        /* 10: RTS */
    };
    h8s_block_t block;
    h8s_branch_edge_cache_t edge_cache;
    h8s_block_cpu_state_t state = {.er = {0xffffffffu}, .ccr = 0, .pc = 0};
    uint32_t next = 0;

    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.stop_pc == 2);
    h8s_branch_edge_cache_init(&edge_cache);
    TEST_CHECK(h8s_execute_semantic_block_exit(&block, &edge_cache, &state, &next));
    TEST_CHECK(next == 4);
    TEST_CHECK(state.pc == 4);
    TEST_CHECK((state.ccr & 0x04) != 0);
    TEST_CHECK((state.er[0] & 0xff) == 0);
    TEST_CHECK(edge_cache.misses == 1);
}

static void test_semantic_block_exit_rejects_calls_without_mutation(void)
{
    const uint8_t rom[] = {
        0xf8, 0x00,       /* 0: MOV.B #0,R0L */
        0x55, 0x04,       /* 2: BSR 8 */
        0x54, 0x70,       /* 4: RTS */
        0x0b, 0x01,       /* 6: ADDS #2, ER1 */
        0x0b, 0x02,       /* 8: ADDS #4, ER2 */
        0x54, 0x70        /* 10: RTS */
    };
    h8s_block_t block;
    h8s_branch_edge_cache_t edge_cache;
    h8s_block_cpu_state_t state = {.er = {0xffffffffu}, .ccr = 0, .pc = 0};
    uint32_t next = 0x123456;

    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_branch_edge_cache_init(&edge_cache);
    TEST_CHECK(!h8s_execute_semantic_block_exit(&block, &edge_cache, &state, &next));
    TEST_CHECK(next == 0x123456);
    TEST_CHECK(state.pc == 0);
    TEST_CHECK(state.er[0] == 0xffffffffu);
    TEST_CHECK(state.ccr == 0);
    TEST_CHECK(edge_cache.misses == 0);
}

static void test_counts_variable_immediates(void)
{
    const uint8_t rom[] = {
        0x79, 0x00, 0x12, 0x34,             /* MOV.W #imm, R0 */
        0x7a, 0x00, 0x12, 0x34, 0x56, 0x78, /* MOV.L #imm, ER0 */
        0x54, 0x70                          /* RTS */
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 2);
    TEST_CHECK(block.bytes == 10);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_BRANCH);
    TEST_CHECK(block.stop_pc == 10);
    TEST_CHECK(block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 2);
    TEST_CHECK(block.decoded[0].op == 0x7900);
    TEST_CHECK(block.decoded[0].imm == 0x1234);
    TEST_CHECK(block.decoded[0].bytes == 4);
    TEST_CHECK(block.decoded[1].op == 0x7a00);
    TEST_CHECK(block.decoded[1].imm == 0x12345678);
    TEST_CHECK(block.decoded[1].bytes == 6);
}

static void test_counts_absolute_and_compound_bit_lengths(void)
{
    const uint8_t rom[] = {
        0x6a, 0x00, 0x12, 0x34,                   /* MOV.B @aa:16, R0 */
        0x6b, 0x20, 0x12, 0x34, 0x56, 0x78,       /* MOV.W @aa:24, R0 */
        0x6a, 0x10, 0x12, 0x34, 0x70, 0x00,       /* bit op @aa:16 */
        0x6a, 0x30, 0x12, 0x34, 0x56, 0x78, 0x70, 0x00,
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 4);
    TEST_CHECK(block.bytes == 24);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_BRANCH);
    TEST_CHECK(block.stop_pc == 24);
    TEST_CHECK(!block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 0);
}

static void test_counts_prefix_lengths(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,                         /* ADDS #1, ER0 */
        0x01, 0x00, 0x69, 0x00,             /* MOV.L @ER0, ER0 */
        0x01, 0x00, 0x6b, 0x20, 0x12, 0x34, 0x56, 0x78,
        0x78, 0x00, 0x6a, 0x00, 0, 0, 0, 4, /* MOV.B @(d:32, ER0), R0 */
        0x7c, 0x00, 0x70, 0x00,             /* bit op memory prefix */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 5);
    TEST_CHECK(block.bytes == 26);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_BRANCH);
    TEST_CHECK(block.stop_pc == 26);
    TEST_CHECK(!block.executable);
}

static void test_sleep_is_control_boundary(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,
        0x01, 0x80,
        0x0b, 0x01
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 1);
    TEST_CHECK(block.bytes == 2);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_BRANCH);
    TEST_CHECK(block.stop_pc == 2);
    TEST_CHECK(block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 1);
}

static void test_truncated_instruction(void)
{
    const uint8_t rom[] = {
        0x79, 0x00, 0x12
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 0);
    TEST_CHECK(block.bytes == 0);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_TRUNCATED);
    TEST_CHECK(block.stop_pc == 0);
    TEST_CHECK(!block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 0);
}

static void test_executable_prefix_immediate_block(void)
{
    const uint8_t rom[] = {
        0x79, 0x00, 0x12, 0x34,
        0x7a, 0x40, 0x00, 0x00, 0xff, 0xff,
        0x8a, 0x7f,
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 3);
    TEST_CHECK(block.bytes == 12);
    TEST_CHECK(block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 3);
}

static void test_memory_instruction_makes_block_non_executable(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,
        0x68, 0x00,
        0x0b, 0x01,
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 3);
    TEST_CHECK(!block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 1);
}

static void test_invalid_register_subforms_make_block_non_executable(void)
{
    const uint8_t rom[] = {
        0x0b, 0x2b, /* invalid/unsupported ADDS/INC subform */
        0x0a, 0x61, /* invalid/unsupported INC/SUB.L subform */
        0x10, 0x20, /* invalid/unsupported shift subform */
        0x17, 0xc0, /* invalid/unsupported unary subform */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 4);
    TEST_CHECK(!block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 0);
}

static void test_cache_hit_and_miss_accounting(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,
        0x0b, 0x01,
        0x54, 0x70
    };
    h8s_block_cache_t cache;
    h8s_block_cache_init(&cache, 16);
    const h8s_block_t *first = h8s_block_cache_get(&cache, rom, sizeof(rom), 0);
    const h8s_block_t *second = h8s_block_cache_get(&cache, rom, sizeof(rom), 0);
    TEST_ASSERT(first != NULL);
    TEST_ASSERT(second != NULL);
    TEST_CHECK(first == second);
    TEST_CHECK(cache.misses == 1);
    TEST_CHECK(cache.hits == 1);
    TEST_CHECK(cache.evictions == 0);
    TEST_CHECK(second->instructions == 2);
}

static void test_cache_collision_evicts(void)
{
    uint8_t rom[1024] = {0};
    rom[0] = 0x0b; rom[1] = 0x00; rom[2] = 0x54; rom[3] = 0x70;
    rom[512] = 0x0b; rom[513] = 0x01; rom[514] = 0x54; rom[515] = 0x70;
    h8s_block_cache_t cache;
    h8s_block_cache_init(&cache, 16);
    TEST_ASSERT(h8s_block_cache_get(&cache, rom, sizeof(rom), 0) != NULL);
    TEST_ASSERT(h8s_block_cache_get(&cache, rom, sizeof(rom), 512) != NULL);
    TEST_CHECK(cache.misses == 2);
    TEST_CHECK(cache.hits == 0);
    TEST_CHECK(cache.evictions == 1);
}

static void test_cache_clear_preserves_limit(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,
        0x0b, 0x01,
        0x0b, 0x02,
        0x54, 0x70
    };
    h8s_block_cache_t cache;
    h8s_block_cache_init(&cache, 2);
    const h8s_block_t *block = h8s_block_cache_get(&cache, rom, sizeof(rom), 0);
    TEST_ASSERT(block != NULL);
    TEST_CHECK(block->instructions == 2);
    TEST_CHECK(block->stop == H8S_BLOCK_STOP_LIMIT);
    h8s_block_cache_clear(&cache);
    TEST_CHECK(cache.max_instructions == 2);
    TEST_CHECK(cache.misses == 0);
    block = h8s_block_cache_get(&cache, rom, sizeof(rom), 0);
    TEST_ASSERT(block != NULL);
    TEST_CHECK(block->instructions == 2);
    TEST_CHECK(cache.misses == 1);
    TEST_CHECK(cache.hits == 0);
    TEST_CHECK(cache.evictions == 0);
}

static void test_cache_rejects_invalid_start(void)
{
    const uint8_t rom[] = {0x0b, 0x00};
    h8s_block_cache_t cache;
    h8s_block_cache_init(&cache, 16);
    TEST_CHECK(h8s_block_cache_get(&cache, rom, sizeof(rom), sizeof(rom)) == NULL);
    TEST_CHECK(cache.misses == 0);
    TEST_CHECK(cache.hits == 0);
}

static void test_cache_caps_instruction_limit(void)
{
    uint8_t rom[128] = {0};
    for (unsigned i = 0; i < sizeof(rom); i += 2) {
        rom[i] = 0x0b;
        rom[i + 1] = 0x00;
    }
    h8s_block_cache_t cache;
    h8s_block_cache_init(&cache, 200);
    const h8s_block_t *block = h8s_block_cache_get(&cache, rom, sizeof(rom), 0);
    TEST_ASSERT(block != NULL);
    TEST_CHECK(cache.max_instructions == H8S_BLOCK_MAX_INSTRUCTIONS);
    TEST_CHECK(block->instructions == H8S_BLOCK_MAX_INSTRUCTIONS);
    TEST_CHECK(block->decoded[H8S_BLOCK_MAX_INSTRUCTIONS - 1].op == 0x0b00);
    TEST_CHECK(block->decoded[H8S_BLOCK_MAX_INSTRUCTIONS - 1].bytes == 2);
}

static void test_semantic_block_executes_register_ops(void)
{
    const uint8_t rom[] = {
        0x0b, 0x90, /* ADDS #4, ER0 */
        0x0f, 0x81, /* MOV.L ER0, ER1 */
        0x1b, 0xf1, /* DEC.L #2, ER1; updates NZ */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(h8s_semantic_block_supported(&block));
    h8s_block_cpu_state_t state = {
        .er = {0x10, 0, 0, 0, 0, 0, 0, 0},
        .ccr = 0xff,
        .pc = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK(state.er[0] == 0x14);
    TEST_CHECK(state.er[1] == 0x12);
    TEST_CHECK(state.pc == 6);
    TEST_CHECK((state.ccr & 0x0e) == 0);
}

static void test_semantic_block_rejects_unsupported_tier1(void)
{
    const uint8_t rom[] = {
        0x0f, 0x00, /* DAA-style opcode: not a straight-line semantic tier candidate */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {.pc = 0};
    TEST_CHECK(!block.executable);
    TEST_CHECK(!h8s_semantic_block_supported(&block));
    TEST_CHECK(!h8s_execute_semantic_block(&block, &state));
}

static void test_semantic_block_executes_byte_immediates(void)
{
    const uint8_t rom[] = {
        0xf8, 0x7f, /* MOV.B #0x7f,R0L */
        0x88, 0x01, /* ADD.B #1,R0L -> 0x80, V/N */
        0xc0, 0x0f, /* OR.B #0x0f,R0H */
        0xd0, 0xff, /* XOR.B #0xff,R0H */
        0xe0, 0xf0, /* AND.B #0xf0,R0H */
        0xa8, 0x80, /* CMP.B #0x80,R0L; no write, Z */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {.er = {0}, .ccr = 0, .pc = 0};
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK(state.er[0] == 0xf080);
    TEST_CHECK(state.pc == 12);
    TEST_CHECK(state.ccr & 0x04); /* Z from CMP */
    TEST_CHECK(!(state.ccr & 0x08));
}

static void test_semantic_block_addx_subx_sticky_zero(void)
{
    const uint8_t rom[] = {
        0xf8, 0x00, /* MOV.B #0,R0L -> Z */
        0x98, 0x00, /* ADDX.B #0,R0L with C=0 keeps Z set */
        0xb8, 0x01, /* SUBX.B #1,R0L -> 0xff, clears Z, sets C/N */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {.er = {0}, .ccr = 0, .pc = 0};
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xff) == 0xff);
    TEST_CHECK(state.ccr & 0x01);
    TEST_CHECK(state.ccr & 0x08);
    TEST_CHECK(!(state.ccr & 0x04));
}

static void test_semantic_block_executes_word_long_immediates(void)
{
    const uint8_t rom[] = {
        0x79, 0x00, 0x12, 0x34,             /* MOV.W #0x1234,R0 */
        0x79, 0x10, 0x00, 0x02,             /* ADD.W #2,R0 */
        0x79, 0x20, 0x12, 0x36,             /* CMP.W #0x1236,R0; no write */
        0x7a, 0x01, 0x80, 0x00, 0x00, 0x00, /* MOV.L #0x80000000,ER1 */
        0x7a, 0x31, 0x00, 0x00, 0x00, 0x01, /* SUB.L #1,ER1 */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {.ccr = 0xff};
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xffff) == 0x1236);
    TEST_CHECK(state.er[1] == 0x7fffffff);
    TEST_CHECK(state.pc == 24);
    TEST_CHECK(!(state.ccr & 0x04));
}

static void test_semantic_block_long_compare_no_write(void)
{
    const uint8_t rom[] = {
        0x7a, 0x20, 0x12, 0x34, 0x56, 0x78, /* CMP.L #0x12345678,ER0 */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {.er = {0x12345678}, .ccr = 0};
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK(state.er[0] == 0x12345678);
    TEST_CHECK(state.ccr & 0x04);
    TEST_CHECK(state.pc == 6);
}

static void test_semantic_block_executes_register_alu_ops(void)
{
    const uint8_t rom[] = {
        0x0a, 0x90, /* ADD.L ER1,ER0 */
        0x1f, 0x90, /* CMP.L ER1,ER0; no write */
        0x1a, 0x90, /* SUB.L ER1,ER0 */
        0x14, 0x89, /* OR.B R0L,R1L */
        0x15, 0x98, /* XOR.B R1L,R0L */
        0x16, 0x89, /* AND.B R0L,R1L */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {
        .er = {0x10, 0x02, 0, 0, 0, 0, 0, 0},
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK(state.er[0] == 0x02);
    TEST_CHECK((state.er[1] & 0xff) == 0x02);
    TEST_CHECK(state.pc == 12);
}

static void test_semantic_block_executes_byte_word_register_ops(void)
{
    const uint8_t rom[] = {
        0x0c, 0x89, /* MOV.B R0L,R1L */
        0x08, 0x89, /* ADD.B R0L,R1L */
        0x1c, 0x89, /* CMP.B R0L,R1L; no write */
        0x1e, 0x89, /* SUBX.B R0L,R1L */
        0x0d, 0x12, /* MOV.W R1,R2 */
        0x09, 0x12, /* ADD.W R1,R2 */
        0x1d, 0x12, /* CMP.W R1,R2; no write */
        0x19, 0x12, /* SUB.W R1,R2 */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {
        .er = {0x00000003, 0x00000004, 0, 0, 0, 0, 0, 0},
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[1] & 0xff) == 0x03);
    TEST_CHECK((state.er[2] & 0xffff) == 0x0003);
    TEST_CHECK(state.pc == 16);
}

static void test_semantic_block_executes_unary_register_ops(void)
{
    const uint8_t rom[] = {
        0x17, 0x08, /* NOT.B R0L */
        0x17, 0x51, /* EXTU.W R1 */
        0x17, 0xd2, /* EXTS.W R2 */
        0x17, 0x93, /* NEG.W R3 */
        0x17, 0xb4, /* NEG.L ER4 */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {
        .er = {
            0x0000000f,
            0x00001234,
            0x00000080,
            0x00000001,
            0x00000002,
            0, 0, 0
        },
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xff) == 0xf0);
    TEST_CHECK((state.er[1] & 0xffff) == 0x34);
    TEST_CHECK((state.er[2] & 0xffff) == 0xff80);
    TEST_CHECK((state.er[3] & 0xffff) == 0xffff);
    TEST_CHECK(state.er[4] == 0xfffffffe);
    TEST_CHECK(state.pc == 10);
    TEST_CHECK(state.ccr & 0x01);
    TEST_CHECK(state.ccr & 0x08);
}

static void test_semantic_block_executes_shift_rotate_ops(void)
{
    const uint8_t rom[] = {
        0x10, 0x08, /* SHAL.B R0L: 0x81 -> 0x02, C/V */
        0x11, 0xd1, /* SHAR.W #2,R1: 0x8003 -> 0xe000, C */
        0x12, 0xb2, /* ROTL.L ER2: 0x80000000 -> 0x00000001, C */
        0x13, 0x04, /* ROTXR.B R4H: old C -> bit 7 */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(h8s_semantic_block_supported(&block));
    h8s_block_cpu_state_t state = {
        .er = {
            0x00000081,
            0x00008003,
            0x80000000,
            0,
            0x00000000,
            0, 0, 0
        },
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xff) == 0x02);
    TEST_CHECK((state.er[1] & 0xffff) == 0xe000);
    TEST_CHECK(state.er[2] == 0x00000001);
    TEST_CHECK(((state.er[4] >> 8) & 0xff) == 0x80);
    TEST_CHECK(state.pc == 8);
    TEST_CHECK(!(state.ccr & 0x02));
    TEST_CHECK(state.ccr & 0x08);
}

static void test_semantic_block_executes_inc_dec_forms(void)
{
    const uint8_t rom[] = {
        0x0b, 0x50, /* INC.W #1,R0: 0x7fff -> 0x8000, V/N */
        0x0b, 0x71, /* INC.L #1,ER1: 0x7fffffff -> 0x80000000, V/N */
        0x0b, 0xd2, /* INC.W #2,R2 */
        0x1b, 0x53, /* DEC.W #1,R3: 0x8000 -> 0x7fff, V */
        0x1b, 0x74, /* DEC.L #1,ER4: 0x80000000 -> 0x7fffffff, V */
        0x1b, 0xd5, /* DEC.W #2,R5: 0x8001 -> 0x7fff, V */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(h8s_semantic_block_supported(&block));
    h8s_block_cpu_state_t state = {
        .er = {
            0x00007fff,
            0x7fffffff,
            0x00000001,
            0x00008000,
            0x80000000,
            0x00008001,
            0, 0
        },
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xffff) == 0x8000);
    TEST_CHECK(state.er[1] == 0x80000000);
    TEST_CHECK((state.er[2] & 0xffff) == 0x0003);
    TEST_CHECK((state.er[3] & 0xffff) == 0x7fff);
    TEST_CHECK(state.er[4] == 0x7fffffff);
    TEST_CHECK((state.er[5] & 0xffff) == 0x7fff);
    TEST_CHECK(state.pc == 12);
    TEST_CHECK(state.ccr & 0x02);
}

static void test_semantic_block_executes_zero_a_inc_dec_forms(void)
{
    const uint8_t rom[] = {
        0x0a, 0x08, /* INC.B R0L: 0x7f -> 0x80, V/N */
        0x0a, 0x51, /* INC.W R1: 0x7fff -> 0x8000, V/N */
        0x0a, 0x72, /* INC.L ER2: 0x7fffffff -> 0x80000000, V/N */
        0x1a, 0x03, /* DEC.B R3H: 0x80 -> 0x7f, V */
        0x1a, 0x54, /* DEC.W R4: 0x8000 -> 0x7fff, V */
        0x1a, 0x75, /* DEC.L ER5: 0x80000000 -> 0x7fffffff, V */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(h8s_semantic_block_supported(&block));
    h8s_block_cpu_state_t state = {
        .er = {
            0x0000007f,
            0x00007fff,
            0x7fffffff,
            0x00008000,
            0x00008000,
            0x80000000,
            0, 0
        },
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xff) == 0x80);
    TEST_CHECK((state.er[1] & 0xffff) == 0x8000);
    TEST_CHECK(state.er[2] == 0x80000000);
    TEST_CHECK(((state.er[3] >> 8) & 0xff) == 0x7f);
    TEST_CHECK((state.er[4] & 0xffff) == 0x7fff);
    TEST_CHECK(state.er[5] == 0x7fffffff);
    TEST_CHECK(state.pc == 12);
    TEST_CHECK(state.ccr & 0x02);
}

static void test_semantic_block_executes_register_bit_and_word_logic(void)
{
    const uint8_t rom[] = {
        0x60, 0x89, /* BSET R0L bit, R1L */
        0x61, 0x89, /* BNOT R0L bit, R1L */
        0x62, 0x89, /* BCLR R0L bit, R1L */
        0x64, 0x23, /* OR.W R2,R3 */
        0x65, 0x45, /* XOR.W R4,R5 */
        0x66, 0x67, /* AND.W R6,R7 */
        0x63, 0x89, /* BTST R0L bit, R1L -> Z */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(h8s_semantic_block_supported(&block));
    h8s_block_cpu_state_t state = {
        .er = {
            0x00000003,
            0x00000008,
            0x00001200,
            0x00000034,
            0x000000f0,
            0x0000000f,
            0x000000f0,
            0x000000cc
        },
        .ccr = 0xff
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[1] & 0xff) == 0x00);
    TEST_CHECK((state.er[3] & 0xffff) == 0x1234);
    TEST_CHECK((state.er[5] & 0xffff) == 0x00ff);
    TEST_CHECK((state.er[7] & 0xffff) == 0x00c0);
    TEST_CHECK(state.pc == 14);
    TEST_CHECK(state.ccr & 0x04);
    TEST_CHECK(!(state.ccr & 0x02));
}

static void test_semantic_block_executes_immediate_bit_ops(void)
{
    const uint8_t rom[] = {
        0x70, 0x08, /* BSET #0,R0L */
        0x71, 0x18, /* BNOT #1,R0L */
        0x72, 0x08, /* BCLR #0,R0L */
        0x73, 0x28, /* BTST #2,R0L -> Z clear */
        0x74, 0x28, /* BOR #2,R0L -> C set */
        0x75, 0xa8, /* BIXOR #2,R0L -> C unchanged true xor false */
        0x76, 0x28, /* BAND #2,R0L -> C remains true */
        0x77, 0xa8, /* BILD #2,R0L -> C false */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(h8s_semantic_block_supported(&block));
    h8s_block_cpu_state_t state = {
        .er = {0x00000004, 0, 0, 0, 0, 0, 0, 0},
        .ccr = 0
    };
    TEST_CHECK(h8s_execute_semantic_block(&block, &state));
    TEST_CHECK((state.er[0] & 0xff) == 0x06);
    TEST_CHECK(state.pc == 16);
    TEST_CHECK(!(state.ccr & 0x01));
    TEST_CHECK(!(state.ccr & 0x04));
}

static void test_semantic_block_matches_interpreter_representative_ops(void)
{
    const uint32_t ers[][8] = {
        {
            0x12345678, 0x87654321, 0x7fffffff, 0x80000001,
            0x0000f0f0, 0x00000f0f, 0x00000003, 0x00000080
        },
        {
            0x00000000, 0x00000001, 0xffffffff, 0x80000000,
            0x00007fff, 0x00008000, 0x00000007, 0x0000ff00
        },
        {
            0x00ff00ff, 0xff00ff00, 0x00010000, 0xffff0001,
            0xaaaaaaaa, 0x55555555, 0x00000004, 0x0000007f
        }
    };
    const uint8_t ccrs[] = {
        0x00, 0x01, 0x25, 0xff
    };

    const uint8_t add_b_reg[] = {0x08, 0x89};
    const uint8_t mov_w_reg[] = {0x0d, 0x45};
    const uint8_t add_l_reg[] = {0x0a, 0x90};
    const uint8_t inc_w[] = {0x0b, 0x54};
    const uint8_t sub_l_reg[] = {0x1a, 0xa3};
    const uint8_t mov_l_reg[] = {0x0f, 0x81};
    const uint8_t neg_l[] = {0x17, 0xb3};
    const uint8_t shal_b[] = {0x10, 0x08};
    const uint8_t rotxr_b[] = {0x13, 0x0f};
    const uint8_t or_w[] = {0x64, 0x45};
    const uint8_t btst_reg[] = {0x63, 0x67};
    const uint8_t bld_imm[] = {0x77, 0x07};
    const uint8_t add_b_imm[] = {0x88, 0x7f};
    const uint8_t mov_w_imm[] = {0x79, 0x04, 0x80, 0x00};
    const uint8_t xor_l_imm[] = {0x7a, 0x53, 0x12, 0x34, 0x56, 0x78};

    for (unsigned e = 0; e < sizeof(ers) / sizeof(ers[0]); ++e) {
        for (unsigned c = 0; c < sizeof(ccrs) / sizeof(ccrs[0]); ++c) {
            check_semantic_matches_interpreter("ADD.B R0L,R1L", add_b_reg, sizeof(add_b_reg), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("MOV.W R4,R5", mov_w_reg, sizeof(mov_w_reg), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("ADD.L ER1,ER0", add_l_reg, sizeof(add_l_reg), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("INC.W R4", inc_w, sizeof(inc_w), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("SUB.L ER2,ER3", sub_l_reg, sizeof(sub_l_reg), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("MOV.L ER0,ER1", mov_l_reg, sizeof(mov_l_reg), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("NEG.L ER3", neg_l, sizeof(neg_l), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("SHAL.B R0L", shal_b, sizeof(shal_b), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("ROTXR.B R7L", rotxr_b, sizeof(rotxr_b), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("OR.W R4,R5", or_w, sizeof(or_w), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("BTST R6H bit,R7H", btst_reg, sizeof(btst_reg), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("BLD #0,R7H", bld_imm, sizeof(bld_imm), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("ADD.B #0x7f,R0L", add_b_imm, sizeof(add_b_imm), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("MOV.W #0x8000,R4", mov_w_imm, sizeof(mov_w_imm), ers[e], ccrs[c]);
            check_semantic_matches_interpreter("XOR.L #0x12345678,ER3", xor_l_imm, sizeof(xor_l_imm), ers[e], ccrs[c]);
        }
    }
}

static void test_semantic_block_matches_interpreter_generated_two_byte_ops(void)
{
    const uint8_t reg_los[] = {0x01, 0x89, 0xfe};
    const uint8_t long_reg_los[] = {0x80, 0x91, 0xf7};
    const uint8_t inc_dec_los[] = {
        0x00, 0x08, 0x50, 0x58, 0x70, 0x77,
        0x80, 0x87, 0x90, 0x97, 0xd0, 0xdf, 0xf0, 0xf7
    };
    const uint8_t zero_a_inc_dec_los[] = {
        0x00, 0x08, 0x50, 0x58, 0x70, 0x77,
        0x80, 0x91, 0xf7
    };
    const uint8_t unary_subops[] = {0x0, 0x1, 0x3, 0x5, 0x7, 0x8, 0x9, 0xb, 0xd, 0xf};
    const uint8_t shift_subops[] = {0x0, 0x1, 0x3, 0x4, 0x5, 0x7, 0x8, 0x9, 0xb, 0xc, 0xd, 0xf};
    const uint8_t bit_los[] = {0x08, 0x37, 0x89, 0xfe};
    const uint8_t word_los[] = {0x01, 0x45, 0xfe};

    const uint8_t simple_reg_hi[] = {
        0x08, 0x09, 0x0c, 0x0d, 0x0e,
        0x14, 0x15, 0x16,
        0x18, 0x19, 0x1c, 0x1d, 0x1e
    };
    for (unsigned h = 0; h < sizeof(simple_reg_hi) / sizeof(simple_reg_hi[0]); ++h) {
        for (unsigned l = 0; l < sizeof(reg_los) / sizeof(reg_los[0]); ++l)
            check_two_byte_opcode_matrix((uint16_t)((simple_reg_hi[h] << 8) | reg_los[l]));
    }

    for (unsigned l = 0; l < sizeof(long_reg_los) / sizeof(long_reg_los[0]); ++l) {
        check_two_byte_opcode_matrix((uint16_t)(0x0a00 | long_reg_los[l]));
        check_two_byte_opcode_matrix((uint16_t)(0x0f00 | long_reg_los[l]));
        check_two_byte_opcode_matrix((uint16_t)(0x1a00 | long_reg_los[l]));
        check_two_byte_opcode_matrix((uint16_t)(0x1f00 | long_reg_los[l]));
    }

    for (unsigned l = 0; l < sizeof(zero_a_inc_dec_los) / sizeof(zero_a_inc_dec_los[0]); ++l) {
        check_two_byte_opcode_matrix((uint16_t)(0x0a00 | zero_a_inc_dec_los[l]));
        check_two_byte_opcode_matrix((uint16_t)(0x1a00 | zero_a_inc_dec_los[l]));
    }

    for (unsigned l = 0; l < sizeof(inc_dec_los) / sizeof(inc_dec_los[0]); ++l) {
        check_two_byte_opcode_matrix((uint16_t)(0x0b00 | inc_dec_los[l]));
        check_two_byte_opcode_matrix((uint16_t)(0x1b00 | inc_dec_los[l]));
    }

    for (unsigned s = 0; s < sizeof(unary_subops) / sizeof(unary_subops[0]); ++s)
        check_two_byte_opcode_matrix((uint16_t)(0x1700 | (unary_subops[s] << 4) | (s & 0x7)));

    for (unsigned h = 0x10; h <= 0x13; ++h) {
        for (unsigned s = 0; s < sizeof(shift_subops) / sizeof(shift_subops[0]); ++s)
            check_two_byte_opcode_matrix((uint16_t)((h << 8) | (shift_subops[s] << 4) | (s & 0x7)));
    }

    for (unsigned h = 0x60; h <= 0x63; ++h) {
        for (unsigned l = 0; l < sizeof(bit_los) / sizeof(bit_los[0]); ++l)
            check_two_byte_opcode_matrix((uint16_t)((h << 8) | bit_los[l]));
    }
    for (unsigned h = 0x64; h <= 0x66; ++h) {
        for (unsigned l = 0; l < sizeof(word_los) / sizeof(word_los[0]); ++l)
            check_two_byte_opcode_matrix((uint16_t)((h << 8) | word_los[l]));
    }
    for (unsigned h = 0x70; h <= 0x77; ++h) {
        for (unsigned l = 0; l < sizeof(bit_los) / sizeof(bit_los[0]); ++l)
            check_two_byte_opcode_matrix((uint16_t)((h << 8) | bit_los[l]));
    }

    for (unsigned h = 0x80; h <= 0xff; ++h) {
        check_two_byte_opcode_matrix((uint16_t)((h << 8) | 0x00));
        check_two_byte_opcode_matrix((uint16_t)((h << 8) | 0x7f));
        check_two_byte_opcode_matrix((uint16_t)((h << 8) | 0x80));
        check_two_byte_opcode_matrix((uint16_t)((h << 8) | 0xff));
    }
}

static void test_semantic_block_matches_interpreter_generated_immediate_word_long_ops(void)
{
    const uint8_t subops[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6};
    const uint16_t word_imms[] = {0x0000, 0x0001, 0x7fff, 0x8000, 0xffff};
    const uint32_t long_imms[] = {
        0x00000000u, 0x00000001u, 0x7fffffffu, 0x80000000u, 0xffffffffu
    };

    for (unsigned s = 0; s < sizeof(subops) / sizeof(subops[0]); ++s) {
        for (unsigned rd = 0; rd < 8; ++rd) {
            for (unsigned i = 0; i < sizeof(word_imms) / sizeof(word_imms[0]); ++i)
                check_word_immediate_opcode_matrix(subops[s], (uint8_t)rd, word_imms[i]);
            for (unsigned i = 0; i < sizeof(long_imms) / sizeof(long_imms[0]); ++i)
                check_long_immediate_opcode_matrix(subops[s], (uint8_t)rd, long_imms[i]);
        }
    }
}

static void test_semantic_block_matches_interpreter_mixed_multi_instruction_blocks(void)
{
    static const uint32_t ers[][8] = {
        {
            0x12345678, 0x87654321, 0x7fffffff, 0x80000001,
            0x0000f0f0, 0x00000f0f, 0x00000003, 0x00000080
        },
        {
            0x00000000, 0x00000001, 0xffffffff, 0x80000000,
            0x00007fff, 0x00008000, 0x00000007, 0x0000ff00
        },
        {
            0x00ff00ff, 0xff00ff00, 0x00010000, 0xffff0001,
            0xaaaaaaaa, 0x55555555, 0x00000004, 0x0000007f
        }
    };
    static const uint8_t ccrs[] = {0x00, 0x01, 0x25, 0xff};
    const uint8_t flag_chain[] = {
        0xf0, 0xff,             /* MOV.B #0xff,R0L */
        0x90, 0x01,             /* ADDX.B #1,R0L; consumes C, sets Z/C */
        0xb0, 0x00,             /* SUBX.B #0,R0L; consumes C */
        0x17, 0xd0,             /* EXTU.W R0 */
        0x79, 0x10, 0x00, 0x01, /* ADD.W #1,R0 */
        0x79, 0x20, 0x01, 0x00  /* CMP.W #0x100,R0 */
    };
    const uint8_t reg_long_chain[] = {
        0x7a, 0x01, 0x12, 0x34, 0x56, 0x78, /* MOV.L #0x12345678,ER1 */
        0x0f, 0x90,                         /* MOV.L ER1,ER0 */
        0x1a, 0x90,                         /* SUB.L ER1,ER0 */
        0x0b, 0x80,                         /* ADDS #2,ER0 */
        0x7a, 0x20, 0x00, 0x00, 0x00, 0x02  /* CMP.L #2,ER0 */
    };
    const uint8_t logic_shift_bit_chain[] = {
        0xf0, 0xf0,             /* MOV.B #0xf0,R0L */
        0x74, 0x04,             /* BOR #0,R0L */
        0x14, 0x01,             /* OR.B R0L,R1L */
        0x16, 0x01,             /* AND.B R0L,R1L */
        0x10, 0x91,             /* SHLR.B R1L */
        0x11, 0xd1,             /* ROTXL.B R1L */
        0x60, 0x01              /* BSET R0L bit,R1L */
    };
    const uint8_t immediate_logic_chain[] = {
        0x79, 0x04, 0x80, 0x00,             /* MOV.W #0x8000,R4 */
        0x79, 0x44, 0x00, 0xff,             /* OR.W #0x00ff,R4 */
        0x79, 0x54, 0x0f, 0x0f,             /* XOR.W #0x0f0f,R4 */
        0x79, 0x64, 0xff, 0x00,             /* AND.W #0xff00,R4 */
        0x7a, 0x05, 0x7f, 0xff, 0xff, 0xff, /* MOV.L #0x7fffffff,ER5 */
        0x7a, 0x15, 0x00, 0x00, 0x00, 0x01, /* ADD.L #1,ER5 */
        0x7a, 0x35, 0x00, 0x00, 0x00, 0x02  /* SUB.L #2,ER5 */
    };

    for (unsigned e = 0; e < sizeof(ers) / sizeof(ers[0]); ++e) {
        for (unsigned c = 0; c < sizeof(ccrs) / sizeof(ccrs[0]); ++c) {
            check_semantic_block_matches_interpreter("flag dependency chain",
                                                     flag_chain, sizeof(flag_chain), ers[e], ccrs[c]);
            check_semantic_block_matches_interpreter("register long dependency chain",
                                                     reg_long_chain, sizeof(reg_long_chain), ers[e], ccrs[c]);
            check_semantic_block_matches_interpreter("logic shift bit chain",
                                                     logic_shift_bit_chain, sizeof(logic_shift_bit_chain), ers[e], ccrs[c]);
            check_semantic_block_matches_interpreter("immediate logic chain",
                                                     immediate_logic_chain, sizeof(immediate_logic_chain), ers[e], ccrs[c]);
        }
    }
}

TEST_LIST = {
    { "stops_before_branch", test_stops_before_branch },
    { "branch_metadata_for_static_exits", test_branch_metadata_for_static_exits },
    { "branch_metadata_for_indirect_and_system_exits", test_branch_metadata_for_indirect_and_system_exits },
    { "resolves_static_branch_exits", test_resolves_static_branch_exits },
    { "rejects_dynamic_branch_exits", test_rejects_dynamic_branch_exits },
    { "branch_edge_cache_hit_miss_and_ccr_keys", test_branch_edge_cache_hit_miss_and_ccr_keys },
    { "branch_edge_cache_clear_and_collision", test_branch_edge_cache_clear_and_collision },
    { "branch_edge_cache_rejects_dynamic_exits", test_branch_edge_cache_rejects_dynamic_exits },
    { "chain_target_returns_semantic_cached_block", test_chain_target_returns_semantic_cached_block },
    { "chain_target_rejects_fallbacks", test_chain_target_rejects_fallbacks },
    { "chain_target_rejects_static_calls", test_chain_target_rejects_static_calls },
    { "semantic_block_exit_uses_updated_ccr", test_semantic_block_exit_uses_updated_ccr },
    { "semantic_block_exit_rejects_calls_without_mutation", test_semantic_block_exit_rejects_calls_without_mutation },
    { "counts_variable_immediates", test_counts_variable_immediates },
    { "counts_absolute_and_compound_bit_lengths", test_counts_absolute_and_compound_bit_lengths },
    { "counts_prefix_lengths", test_counts_prefix_lengths },
    { "sleep_is_control_boundary", test_sleep_is_control_boundary },
    { "truncated_instruction", test_truncated_instruction },
    { "executable_prefix_immediate_block", test_executable_prefix_immediate_block },
    { "memory_instruction_makes_block_non_executable", test_memory_instruction_makes_block_non_executable },
    { "invalid_register_subforms_make_block_non_executable", test_invalid_register_subforms_make_block_non_executable },
    { "cache_hit_and_miss_accounting", test_cache_hit_and_miss_accounting },
    { "cache_collision_evicts", test_cache_collision_evicts },
    { "cache_clear_preserves_limit", test_cache_clear_preserves_limit },
    { "cache_rejects_invalid_start", test_cache_rejects_invalid_start },
    { "cache_caps_instruction_limit", test_cache_caps_instruction_limit },
    { "semantic_block_executes_register_ops", test_semantic_block_executes_register_ops },
    { "semantic_block_rejects_unsupported_tier1", test_semantic_block_rejects_unsupported_tier1 },
    { "semantic_block_executes_byte_immediates", test_semantic_block_executes_byte_immediates },
    { "semantic_block_addx_subx_sticky_zero", test_semantic_block_addx_subx_sticky_zero },
    { "semantic_block_executes_word_long_immediates", test_semantic_block_executes_word_long_immediates },
    { "semantic_block_long_compare_no_write", test_semantic_block_long_compare_no_write },
    { "semantic_block_executes_register_alu_ops", test_semantic_block_executes_register_alu_ops },
    { "semantic_block_executes_byte_word_register_ops", test_semantic_block_executes_byte_word_register_ops },
    { "semantic_block_executes_unary_register_ops", test_semantic_block_executes_unary_register_ops },
    { "semantic_block_executes_shift_rotate_ops", test_semantic_block_executes_shift_rotate_ops },
    { "semantic_block_executes_inc_dec_forms", test_semantic_block_executes_inc_dec_forms },
    { "semantic_block_executes_zero_a_inc_dec_forms", test_semantic_block_executes_zero_a_inc_dec_forms },
    { "semantic_block_executes_register_bit_and_word_logic", test_semantic_block_executes_register_bit_and_word_logic },
    { "semantic_block_executes_immediate_bit_ops", test_semantic_block_executes_immediate_bit_ops },
    { "semantic_block_matches_interpreter_representative_ops", test_semantic_block_matches_interpreter_representative_ops },
    { "semantic_block_matches_interpreter_generated_two_byte_ops", test_semantic_block_matches_interpreter_generated_two_byte_ops },
    { "semantic_block_matches_interpreter_generated_immediate_word_long_ops", test_semantic_block_matches_interpreter_generated_immediate_word_long_ops },
    { "semantic_block_matches_interpreter_mixed_multi_instruction_blocks", test_semantic_block_matches_interpreter_mixed_multi_instruction_blocks },
    { NULL, NULL }
};

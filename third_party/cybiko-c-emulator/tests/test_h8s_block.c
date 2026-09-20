#include "acutest.h"
#include "core/h8s_block.h"

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
    TEST_CHECK(block.executable);
    TEST_CHECK(block.executable_prefix_instructions == 2);
    TEST_CHECK(block.decoded[0].op == 0x0b00);
    TEST_CHECK(block.decoded[0].bytes == 2);
    TEST_CHECK(block.decoded[1].op == 0x0b81);
    TEST_CHECK(block.decoded[1].bytes == 2);
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
        0x70, 0x00, /* BSET/BCLR-style immediate bit op: classified for future tier */
        0x54, 0x70
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    h8s_block_cpu_state_t state = {.pc = 0};
    TEST_CHECK(block.executable);
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

TEST_LIST = {
    { "stops_before_branch", test_stops_before_branch },
    { "counts_variable_immediates", test_counts_variable_immediates },
    { "counts_absolute_and_compound_bit_lengths", test_counts_absolute_and_compound_bit_lengths },
    { "counts_prefix_lengths", test_counts_prefix_lengths },
    { "sleep_is_control_boundary", test_sleep_is_control_boundary },
    { "truncated_instruction", test_truncated_instruction },
    { "executable_prefix_immediate_block", test_executable_prefix_immediate_block },
    { "memory_instruction_makes_block_non_executable", test_memory_instruction_makes_block_non_executable },
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
    { NULL, NULL }
};

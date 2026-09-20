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
    { NULL, NULL }
};

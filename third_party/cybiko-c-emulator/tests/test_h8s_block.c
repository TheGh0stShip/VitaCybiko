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
}

static void test_prefix_is_conservative_boundary(void)
{
    const uint8_t rom[] = {
        0x0b, 0x00,
        0x01, 0x00, 0x69, 0x00
    };
    h8s_block_t block;
    TEST_ASSERT(h8s_analyze_rom_block(rom, sizeof(rom), 0, 16, &block));
    TEST_CHECK(block.instructions == 1);
    TEST_CHECK(block.bytes == 2);
    TEST_CHECK(block.stop == H8S_BLOCK_STOP_PREFIX);
    TEST_CHECK(block.stop_pc == 2);
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
}

TEST_LIST = {
    { "stops_before_branch", test_stops_before_branch },
    { "counts_variable_immediates", test_counts_variable_immediates },
    { "counts_absolute_and_compound_bit_lengths", test_counts_absolute_and_compound_bit_lengths },
    { "prefix_is_conservative_boundary", test_prefix_is_conservative_boundary },
    { "truncated_instruction", test_truncated_instruction },
    { NULL, NULL }
};

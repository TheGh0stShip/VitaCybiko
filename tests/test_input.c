#include "acutest.h"
#include "frontend/input.h"
#include <string.h>

static uint16_t column(const input_state_t *a, const input_state_t *b, int col)
{
    uint16_t matrix[CYBIKO_KEYBOARD_COLUMNS] = {0};
    input_merge(a, matrix, CYBIKO_KEYBOARD_COLUMNS);
    input_merge(b, matrix, CYBIKO_KEYBOARD_COLUMNS);
    return matrix[col];
}

static void test_independent_sources(void)
{
    input_state_t touch = {0}, controller = {0};
    input_key(&touch, 4, 8, true);
    for (int i = 0; i < 100; ++i) {
        input_key(&controller, 4, 8, false);
        TEST_CHECK(column(&touch, &controller, 4) == 8);
        input_tick(&touch);
        input_tick(&controller);
    }
    input_key(&controller, 4, 8, true);
    input_key(&touch, 4, 8, false);
    TEST_CHECK(column(&touch, &controller, 4) == 8);
    for (int i = 0; i < 3; ++i) input_tick(&controller);
    input_key(&controller, 4, 8, false);
    TEST_CHECK(column(&touch, &controller, 4) == 0);
}

static void test_short_tap(void)
{
    input_state_t state = {0}, empty = {0};
    input_key(&state, 3, 2, true);
    input_key(&state, 3, 2, false);
    for (int i = 0; i < 3; ++i) {
        TEST_CHECK(column(&state, &empty, 3) == 2);
        input_tick(&state);
    }
    TEST_CHECK(column(&state, &empty, 3) == 0);
}

static void test_number_order_and_modifiers(void)
{
    input_state_t state = {0}, keyboard = {0};
    input_number(&state, 3, 2, true);
    input_number(&state, 3, 2, false);
    for (int i = 0; i < 8; ++i) {
        TEST_CHECK(column(&state, &keyboard, 7) == 0x8000);
        TEST_CHECK(column(&state, &keyboard, 3) == 0);
        input_tick(&state);
    }
    for (int i = 0; i < 6; ++i) {
        TEST_CHECK(column(&state, &keyboard, 3) == 2);
        TEST_CHECK(column(&state, &keyboard, 7) == 0x8000);
        input_tick(&state);
    }
    TEST_CHECK(column(&state, &keyboard, 3) == 0);
    input_key(&keyboard, 7, 0x8000, true);
    for (int i = 0; i < 20; ++i) input_tick(&state);
    TEST_CHECK(column(&state, &keyboard, 7) == 0x8000);
    memset(&keyboard, 0, sizeof(keyboard));
    TEST_CHECK(column(&state, &keyboard, 7) == 0);
}

static void test_classic_mapping_and_dedicated_numbers(void)
{
    input_state_t state = {0};
    uint16_t matrix[CYBIKO_KEYBOARD_COLUMNS] = {0};
    input_number(&state, 3, 2, true); /* 1: immediate dedicated key, not Fn+Q. */
    input_key(&state, 4, 8, true); /* Enter */
    input_key(&state, 8, 0x8000, true); /* Shift */
    input_key(&state, 3, 2, true); /* Q */
    input_merge_classic(&state, matrix, CYBIKO_KEYBOARD_COLUMNS);
    TEST_CHECK(matrix[3] == 2);
    TEST_CHECK(matrix[0] == 0x90);
    TEST_CHECK(matrix[1] == 0); /* No implicit Fn. */
    TEST_CHECK(matrix[5] == 4);
    for (int i = 9; i < CYBIKO_KEYBOARD_COLUMNS; ++i) TEST_CHECK(matrix[i] == 0);
    input_key(&state, 7, 0x8000, true);
    input_merge_classic(&state, matrix, CYBIKO_KEYBOARD_COLUMNS);
    TEST_CHECK(matrix[1] == 0x80); /* Explicit Fn still works. */
    uint16_t bounded[] = {0, 0xCAFE};
    input_merge_classic(&state, bounded, 1);
    TEST_CHECK(bounded[0] == 0x90);
    TEST_CHECK(bounded[1] == 0xCAFE);
}

static void test_classic_scan_duration(void)
{
    input_state_t state;
    input_reset(&state, CYBIKO_CLASSIC_V2);
    input_key(&state, 5, 0x400, true); /* Esc */
    input_key(&state, 5, 0x400, false);
    input_number(&state, 3, 2, true); /* Dedicated 1 */
    input_number(&state, 3, 2, false);
    for (int frame = 0; frame < 8; ++frame) {
        uint16_t matrix[CYBIKO_KEYBOARD_COLUMNS] = {0};
        input_merge_classic(&state, matrix, CYBIKO_KEYBOARD_COLUMNS);
        TEST_CHECK(matrix[0] == 2 && matrix[3] == 2);
        TEST_CHECK(state.fn_tail == 0 && state.numbers[3][1].delay == 0);
        input_tick(&state);
    }
    uint16_t matrix[CYBIKO_KEYBOARD_COLUMNS] = {0};
    input_merge_classic(&state, matrix, CYBIKO_KEYBOARD_COLUMNS);
    TEST_CHECK(matrix[0] == 0 && matrix[3] == 0);
    input_key(&state, 5, 0x400, true);
    input_reset(&state, CYBIKO_CLASSIC_V1); /* Focus loss cancels even held taps. */
    memset(matrix, 0, sizeof(matrix));
    input_merge_classic(&state, matrix, CYBIKO_KEYBOARD_COLUMNS);
    TEST_CHECK(matrix[0] == 0 && state.classic);
    input_reset(&state, CYBIKO_XTREME);
    TEST_CHECK(!state.classic);
}

TEST_LIST = {
    {"classic_scan_duration", test_classic_scan_duration},
    {"classic_mapping_and_dedicated_numbers", test_classic_mapping_and_dedicated_numbers},
    {"independent_sources", test_independent_sources},
    {"short_tap", test_short_tap},
    {"number_order_and_modifiers", test_number_order_and_modifiers},
    {NULL, NULL}
};

#include "input.h"
#include <string.h>

/* Real Classic CyOS missed 3-frame Esc/F-key taps in firmware tests. Eight
 * emulated frames span its scan/debounce window even when host FPS is low. */
enum { MIN_HOLD = 3, CLASSIC_HOLD = 8, NUMBER_HOLD = 12, FN_FIRST_DELAY = 8, FN_NEXT_DELAY = 3, FN_TAIL = 16 };

void input_reset(input_state_t *state, cybiko_model_t model)
{
    memset(state, 0, sizeof(*state));
    state->classic = model != CYBIKO_XTREME;
}

static void set_key(input_key_t *key, bool down, int delay, int hold)
{
    if (down && !key->down) {
        key->hold = (uint8_t)hold;
        key->delay = (uint8_t)delay;
    }
    key->down = down;
}

static bool active(const input_key_t *key)
{
    return key->down || key->hold || key->delay;
}

static bool numbers_active(const input_state_t *state)
{
    for (int col = 0; col < CYBIKO_KEYBOARD_COLUMNS; ++col)
        for (int bit = 0; bit < 16; ++bit)
            if (active(&state->numbers[col][bit])) return true;
    return false;
}

void input_key(input_state_t *state, int column, uint16_t mask, bool down)
{
    if (column < 0 || column >= CYBIKO_KEYBOARD_COLUMNS) return;
    for (int bit = 0; bit < 16; ++bit)
        if (mask & (1u << bit)) set_key(&state->keys[column][bit], down, 0,
                                      state->classic ? CLASSIC_HOLD : MIN_HOLD);
}

void input_number(input_state_t *state, int column, uint16_t mask, bool down)
{
    if (column < 0 || column >= CYBIKO_KEYBOARD_COLUMNS) return;
    int delay = (numbers_active(state) || state->fn_tail) ? FN_NEXT_DELAY : FN_FIRST_DELAY;
    if (state->classic) delay = 0;
    for (int bit = 0; bit < 16; ++bit) {
        if (mask & (1u << bit)) {
            input_key_t *key = &state->numbers[column][bit];
            set_key(key, down, delay, state->classic ? CLASSIC_HOLD : NUMBER_HOLD);
        }
    }
    if (down && !state->classic) state->fn_tail = FN_TAIL;
}

void input_tick(input_state_t *state)
{
    for (int col = 0; col < CYBIKO_KEYBOARD_COLUMNS; ++col) {
        for (int bit = 0; bit < 16; ++bit) {
            input_key_t *regular = &state->keys[col][bit];
            input_key_t *number = &state->numbers[col][bit];
            if (regular->hold) --regular->hold;
            if (number->delay) --number->delay;
            else if (number->hold) --number->hold;
        }
    }
    if (!state->classic && numbers_active(state)) state->fn_tail = FN_TAIL;
    else if (state->fn_tail) --state->fn_tail;
}

void input_merge(const input_state_t *state, uint16_t *matrix, int columns)
{
    if (columns > CYBIKO_KEYBOARD_COLUMNS) columns = CYBIKO_KEYBOARD_COLUMNS;
    for (int col = 0; col < columns; ++col) {
        for (int bit = 0; bit < 16; ++bit) {
            const input_key_t *number = &state->numbers[col][bit];
            if (active(&state->keys[col][bit]) || (!number->delay && active(number)))
                matrix[col] |= (uint16_t)(1u << bit);
        }
    }
    if (columns > 7 && (state->fn_tail || numbers_active(state))) matrix[7] |= 0x8000;
}

/* Translate frontend key identities to the Classic 9-column, 8-bit matrix.
 * Numbers are dedicated keys: no Xtreme Fn prefix or delayed number chord. */
void input_merge_classic(const input_state_t *state, uint16_t *matrix, int columns)
{
    static const struct { uint8_t from_col, from_bit, col, bit; } map[] = {
        {0,0,0,0}, {5,10,0,1}, {5,8,0,2}, {6,14,0,3}, {3,1,0,4}, {3,2,0,5}, {8,15,0,7},
        {1,0,1,0}, {6,11,1,1}, {5,9,1,2}, {3,6,1,4}, {3,5,1,5}, {3,3,1,6}, {7,15,1,7},
        {2,0,2,0}, {4,0,2,1}, {4,6,2,2}, {3,7,2,4}, {2,8,2,5}, {3,4,2,6}, {9,0,2,7},
        {3,0,3,0}, {5,7,3,2}, {2,13,3,4}, {2,12,3,5}, {2,9,3,6},
        {6,12,4,0}, {6,13,4,1}, {4,4,4,2}, {2,14,4,4}, {1,1,4,5}, {2,11,4,6},
        {5,0,5,0}, {9,3,5,1}, {4,3,5,2}, {1,5,5,4}, {1,4,5,5}, {1,2,5,6},
        {6,0,6,0}, {1,6,6,4}, {1,7,6,5}, {1,3,6,6},
        {9,1,7,1}, {0,12,7,4}, {0,11,7,5}, {0,8,7,6},
        {9,4,8,3}, {0,13,8,4}, {0,14,8,5}, {0,10,8,6},
        {10,0,3,7}, {10,1,4,7}, {10,2,6,1}, {10,3,7,0},
        {10,4,8,1}, {10,5,8,0}, {10,6,5,7}, {10,7,0,6}, {10,8,6,2},
    };
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (map[i].col < columns && active(&state->keys[map[i].from_col][map[i].from_bit]))
            matrix[map[i].col] |= (uint16_t)(1u << map[i].bit);
    static const uint8_t from_col[] = {3,3,3,2,2,1,1,0,0,9};
    static const uint8_t from_bit[] = {1,6,7,13,14,5,6,12,13,4};
    static const uint8_t to_col[] = {3,1,2,3,4,5,6,7,8,7};
    static const uint8_t to_bit[] = {1,3,3,3,3,3,3,3,2,2};
    for (unsigned i = 0; i < 10; ++i)
        if (to_col[i] < columns && active(&state->numbers[from_col[i]][from_bit[i]]))
            matrix[to_col[i]] |= (uint16_t)(1u << to_bit[i]);
}

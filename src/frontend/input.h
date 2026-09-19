#ifndef VITACYBIKO_INPUT_H
#define VITACYBIKO_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "core/emulator.h"

/* Each input device owns a state. Merge only when polling the guest. */
typedef struct {
    bool down;
    uint8_t hold;
    uint8_t delay;
} input_key_t;

typedef struct {
    input_key_t keys[CYBIKO_KEYBOARD_COLUMNS][16];
    input_key_t numbers[CYBIKO_KEYBOARD_COLUMNS][16];
    uint8_t fn_tail;
} input_state_t;

void input_key(input_state_t *state, int column, uint16_t mask, bool down);
void input_number(input_state_t *state, int column, uint16_t mask, bool down);
void input_tick(input_state_t *state);
void input_merge(const input_state_t *state, uint16_t *matrix, int columns);
void input_merge_classic(const input_state_t *state, uint16_t *matrix, int columns);

#endif

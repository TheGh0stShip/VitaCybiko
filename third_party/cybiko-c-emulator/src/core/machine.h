#ifndef CYBIKO_MACHINE_H
#define CYBIKO_MACHINE_H
#include "types.h"

typedef enum { CYBIKO_XTREME, CYBIKO_CLASSIC_V1, CYBIKO_CLASSIC_V2, CYBIKO_MODEL_COUNT } cybiko_model_t;
typedef struct {
    cybiko_model_t model;
    const char *name, *directory;
    uint32_t clock_hz, ram_size, flash_size, on_chip_base;
    uint32_t ram_base, ram_end, lcd_base, lcd_end, flash_base, flash_end;
    uint32_t boot_end, keyboard_end, boot_crc, flash_crc;
    int timer_channels, keyboard_columns;
    uint8_t rtc_sda;
    /* Average instruction cost in half clock states, for code executing from
     * on-chip memory and from the external bus respectively. */
    uint8_t on_chip_half_states, external_half_states;
} cybiko_machine_t;

const cybiko_machine_t *cybiko_machine(cybiko_model_t model);
#endif

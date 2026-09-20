#ifndef CYBIKO_EMULATOR_H
#define CYBIKO_EMULATOR_H
#include "types.h"
#include "machine.h"
#include "dataflash.h"
#include "../hal/hal.h"

typedef struct cybiko_emu cybiko_emu_t;

typedef struct {
    uint64_t semantic_fast_blocks;
    uint64_t semantic_fast_cycles;
    uint64_t semantic_mutable_fast_blocks;
    uint64_t semantic_mutable_fast_cycles;
    uint64_t semantic_fast_rejects;
    uint64_t semantic_fast_cached_rejects;
    uint64_t semantic_fast_backoff_skips;
    uint64_t semantic_fast_reject_guard;
    uint64_t semantic_fast_reject_irq;
    uint64_t semantic_fast_reject_window;
    uint64_t semantic_fast_reject_cached;
    uint64_t semantic_fast_reject_unsupported_block;
    uint64_t semantic_fast_reject_unsupported_exit;
    uint64_t semantic_fast_reject_cycle_budget;
    uint64_t semantic_fast_reject_branch_resolve;
    uint64_t semantic_fast_reject_target;
    uint64_t semantic_mutable_reject_cached;
    uint64_t semantic_mutable_reject_unsupported_block;
    uint64_t semantic_mutable_reject_static_nonplain;
    uint64_t semantic_mutable_reject_unsupported_exit;
    uint64_t semantic_mutable_reject_cycle_budget;
    uint64_t semantic_mutable_reject_execute;
    uint64_t semantic_mutable_reject_target;
    uint64_t semantic_mutable_execute_nonplain_read;
    uint64_t semantic_mutable_execute_nonplain_write;
    uint64_t semantic_mutable_execute_semantic_run;
    uint64_t semantic_mutable_execute_branch_resolve;
    uint64_t semantic_mutable_execute_return_read;
    uint64_t semantic_mutable_execute_call_write;
    uint64_t semantic_mutable_execute_other;
    uint64_t semantic_mutable_prefix_blocks;
    uint64_t semantic_mutable_prefix_cycles;
    uint64_t hot_plain_memory_2b_instructions;
    uint64_t hot_plain_memory_4b_instructions;
} cybiko_cpu_stats_t;

cybiko_emu_t *cybiko_create(const cybiko_hal_t *hal);
cybiko_emu_t *cybiko_create_model(const cybiko_hal_t *hal, cybiko_model_t model);
cybiko_model_t cybiko_get_model(const cybiko_emu_t *emu);
uint32_t cybiko_get_program_counter(const cybiko_emu_t *emu);
bool cybiko_load_dataflash(cybiko_emu_t *emu, const uint8_t *data, size_t len);
const uint8_t *cybiko_get_dataflash(const cybiko_emu_t *emu, size_t *len);
void cybiko_get_clock(cybiko_emu_t *emu, uint8_t registers[16]);
bool cybiko_load_clock(cybiko_emu_t *emu, const uint8_t *registers, size_t len,
                       uint64_t elapsed_seconds);
bool cybiko_get_cpu_stats(const cybiko_emu_t *emu, cybiko_cpu_stats_t *stats);
bool cybiko_check_firmware(cybiko_model_t model, const uint8_t *boot, size_t boot_len,
                          const uint8_t *flash, size_t flash_len);
void          cybiko_destroy(cybiko_emu_t *emu);
bool cybiko_load_boot_rom(cybiko_emu_t *emu, const uint8_t *data, size_t len);
bool cybiko_load_flash_rom(cybiko_emu_t *emu, const uint8_t *data, size_t len);
bool cybiko_load_nvram(cybiko_emu_t *emu, const uint8_t *data, size_t len);
void cybiko_reset(cybiko_emu_t *emu);
void cybiko_run_frame(cybiko_emu_t *emu);
bool cybiko_is_running(const cybiko_emu_t *emu);
const uint8_t *cybiko_get_nvram(const cybiko_emu_t *emu, size_t *len);

#define CYBIKO_LCD_WIDTH   160
#define CYBIKO_LCD_HEIGHT  100
#define CYBIKO_CLOCK_HZ    18432000
#define CYBIKO_FPS         60
#define CYBIKO_CYCLES_PER_FRAME (CYBIKO_CLOCK_HZ / CYBIKO_FPS)
#define CYBIKO_BOOT_ROM_SIZE  0x8000
#define CYBIKO_FLASH_ROM_SIZE 0x80000
#define CYBIKO_NVRAM_SIZE     0x200000
#define CYBIKO_BOOT_ROM_CRC32  0x18B9B21Fu
#define CYBIKO_FLASH_ROM_CRC32 0xF79400BAu
#define CYBIKO_KEYBOARD_COLUMNS 15  /* total matrix entries (HAL copies all) */
#define CYBIKO_KEYBOARD_SCAN_COLS 10 /* columns scanned by keyboard_read (0-9) */

uint32_t cybiko_crc32(const uint8_t *data, size_t len);
bool cybiko_is_supported_firmware(const uint8_t *boot, size_t boot_len,
                                  const uint8_t *flash, size_t flash_len);

#endif

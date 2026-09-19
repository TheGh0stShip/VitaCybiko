/*
 * Cybiko emulator orchestrator.
 *
 * Ties together CPU, address bus, LCD, timers, RTC, speaker, and keyboard
 * into a single runnable emulator driven by a platform HAL.
 */
#include "emulator.h"
#include "h8s_cpu.h"
#include "address_bus.h"
#include "hd66421.h"
#include "timer8.h"
#include "timer16.h"
#include "rtc.h"
#include "speaker.h"
#include "keyboard.h"
#include "cfs.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HALT_FAST_FORWARD_MIN_CYCLES 32

struct cybiko_emu {
    cybiko_hal_t hal;

    h8s_cpu_t     cpu;
    address_bus_t bus;
    hd66421_t     lcd;
    timer8_t      timer8[2];
    timer16_t     timer16[6];
    rtc_t         rtc;
    speaker_t     speaker;
    keyboard_t    keyboard;

    bool     running;
    uint64_t total_steps;
    uint32_t frame_count;
};

/* Timer16 channel 1 TIOCB1 output compare → speaker (Port 1 bit 3) */
static void timer16_ch1_tiocb_cb(void *ctx, int level) {
    cybiko_emu_t *emu = ctx;
    speaker_set_level(&emu->speaker, level);
}

/* ---------- lifecycle ---------- */

cybiko_emu_t *cybiko_create(const cybiko_hal_t *hal) {
    return cybiko_create_model(hal, CYBIKO_XTREME);
}

cybiko_emu_t *cybiko_create_model(const cybiko_hal_t *hal, cybiko_model_t model) {
    if (!hal) return NULL;
    const cybiko_machine_t *m = cybiko_machine(model);
    if (!m) return NULL;
    cybiko_emu_t *emu = calloc(1, sizeof(cybiko_emu_t));
    if (!emu) return NULL;

    /* Copy the HAL callbacks */
    emu->hal = *hal;

    /* Initialize the bus itself (must come before memory_init - zeroes struct) */
    bus_init(&emu->bus);
    emu->bus.machine = m;

    /* Initialize memory regions in the bus */
    memory_init(&emu->bus.boot_rom,     CYBIKO_BOOT_ROM_SIZE,  false);
    memory_init(&emu->bus.external_ram, m->ram_size, true);
    memory_init(&emu->bus.flash_rom, m->flash_size ? m->flash_size : 1, false);
    memory_init(&emu->bus.on_chip_ram, 0x1000000 - m->on_chip_base, true);
    if (model != CYBIKO_XTREME) {
        emu->bus.dataflash = malloc(sizeof(dataflash_t));
        if (!emu->bus.dataflash) { cybiko_destroy(emu); return NULL; }
        dataflash_init(emu->bus.dataflash);
    }
    if (!emu->bus.boot_rom.data || !emu->bus.external_ram.data ||
        !emu->bus.flash_rom.data || !emu->bus.on_chip_ram.data) {
        cybiko_destroy(emu);
        return NULL;
    }

    /* Wire peripherals into bus */
    emu->bus.lcd       = &emu->lcd;
    emu->bus.timer8[0] = &emu->timer8[0];
    emu->bus.timer8[1] = &emu->timer8[1];
    for (int i = 0; i < m->timer_channels; i++) {
        emu->bus.timer16[i] = &emu->timer16[i];
    }
    emu->bus.rtc      = &emu->rtc;
    emu->bus.speaker  = &emu->speaker;
    emu->bus.keyboard = &emu->keyboard;

    /* Initialize peripherals */
    hd66421_init(&emu->lcd);

    timer8_init(&emu->timer8[0], 0, &emu->cpu);
    timer8_init(&emu->timer8[1], 1, &emu->cpu);

    /* Timer16 channels: (channel, tgrCount, baseVector, cpu) */
    timer16_init(&emu->timer16[0], 0, 4, 32, &emu->cpu);
    timer16_init(&emu->timer16[1], 1, 2, 40, &emu->cpu);
    timer16_init(&emu->timer16[2], 2, 2, 44, &emu->cpu);
    timer16_init(&emu->timer16[3], 3, 4, 48, &emu->cpu);
    timer16_init(&emu->timer16[4], 4, 2, 56, &emu->cpu);
    timer16_init(&emu->timer16[5], 5, 2, 60, &emu->cpu);

    /* Wire Timer16 ch1 TIOCB1 output compare to speaker */
    emu->timer16[1].output_b_cb = timer16_ch1_tiocb_cb;
    emu->timer16[1].output_b_ctx = emu;

    rtc_init(&emu->rtc);
    speaker_init(&emu->speaker, m->clock_hz);
    keyboard_init(&emu->keyboard, m->keyboard_columns);

    /* Initialize CPU (needs bus already wired) */
    h8s_cpu_init(&emu->cpu, &emu->bus);

    /* Wire CPU back into bus (for interrupt delivery, etc.) */
    emu->bus.cpu = &emu->cpu;

    emu->running = true;

    return emu;
}

void cybiko_destroy(cybiko_emu_t *emu) {
    if (!emu) return;
    memory_free(&emu->bus.boot_rom);
    memory_free(&emu->bus.external_ram);
    memory_free(&emu->bus.flash_rom);
    memory_free(&emu->bus.on_chip_ram);
    free(emu->bus.dataflash);
    free(emu);
}

/* ---------- ROM / NVRAM loading ---------- */

uint32_t cybiko_crc32(const uint8_t *data, size_t len) {
    if (!data && len != 0) return 0;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

bool cybiko_is_supported_firmware(const uint8_t *boot, size_t boot_len,
                                  const uint8_t *flash, size_t flash_len) {
    return cybiko_check_firmware(CYBIKO_XTREME, boot, boot_len, flash, flash_len);
}

bool cybiko_check_firmware(cybiko_model_t model, const uint8_t *boot, size_t boot_len,
                          const uint8_t *flash, size_t flash_len) {
    const cybiko_machine_t *m = cybiko_machine(model);
    if (!m || !boot || boot_len != CYBIKO_BOOT_ROM_SIZE ||
        cybiko_crc32(boot, boot_len) != m->boot_crc) return false;
    if (model == CYBIKO_CLASSIC_V1) return flash_len == 0;
    return boot && flash &&
           boot_len == CYBIKO_BOOT_ROM_SIZE &&
           flash_len == m->flash_size &&
           cybiko_crc32(flash, flash_len) == m->flash_crc;
}

bool cybiko_load_boot_rom(cybiko_emu_t *emu, const uint8_t *data, size_t len) {
    if (!emu || !data || len != CYBIKO_BOOT_ROM_SIZE) return false;
    memory_load(&emu->bus.boot_rom, data, len, 0);
    return true;
}

bool cybiko_load_flash_rom(cybiko_emu_t *emu, const uint8_t *data, size_t len) {
    if (!emu || !data || !emu->bus.machine->flash_size || len != emu->bus.machine->flash_size) return false;
    memory_load(&emu->bus.flash_rom, data, len, 0);
    return true;
}

bool cybiko_load_nvram(cybiko_emu_t *emu, const uint8_t *data, size_t len) {
    if (!emu || !data || len == 0 || len > emu->bus.machine->ram_size) return false;
    memory_load(&emu->bus.external_ram, data, len, 0);
    return true;
}

/* ---------- reset ---------- */

void cybiko_reset(cybiko_emu_t *emu) {
    h8s_cpu_reset(&emu->cpu);
}

/* ---------- main frame loop ---------- */

static void tick_peripherals(cybiko_emu_t *emu, int cycles)
{
    if (cycles <= 0) return;
    timer8_advance(&emu->timer8[0], cycles);
    timer8_advance(&emu->timer8[1], cycles);
    const cybiko_machine_t *m = emu->bus.machine;
    for (int i = 0; i < m->timer_channels; i++) {
        timer16_advance(&emu->timer16[i], cycles);
    }
    bus_advance_dma_completion(&emu->bus, cycles);
}

static int cycles_until_next_peripheral_event(cybiko_emu_t *emu)
{
    int next = INT_MAX;
    int t = timer8_cycles_until_event(&emu->timer8[0]);
    if (t > 0 && t < next) next = t;
    t = timer8_cycles_until_event(&emu->timer8[1]);
    if (t > 0 && t < next) next = t;

    const cybiko_machine_t *m = emu->bus.machine;
    for (int i = 0; i < m->timer_channels; i++) {
        t = timer16_cycles_until_event(&emu->timer16[i]);
        if (t > 0 && t < next) next = t;
    }

    t = bus_cycles_until_dma_completion(&emu->bus);
    if (t > 0 && t < next) next = t;
    return next == INT_MAX ? 0 : next;
}

void cybiko_run_frame(cybiko_emu_t *emu) {
    const cybiko_machine_t *m = emu->bus.machine;
    int frame_cycles = (int)(m->clock_hz / CYBIKO_FPS);
    uint16_t previous_keys[CYBIKO_KEYBOARD_COLUMNS];
    memcpy(previous_keys, emu->keyboard.columns, sizeof(previous_keys));
    /* Poll keyboard state from the HAL */
    if (emu->hal.keyboard_poll) {
        emu->hal.keyboard_poll(emu->hal.ctx,
                               emu->keyboard.columns,
                               CYBIKO_KEYBOARD_COLUMNS);
    }
    if (m->model != CYBIKO_XTREME) {
        for (int i = 0; i < m->keyboard_columns; ++i) {
            if ((emu->keyboard.columns[i] & ~previous_keys[i]) && (emu->bus.ier & 4)) {
                h8s_cpu_request_interrupt(&emu->cpu, 18);
                break;
            }
        }
    }

    /* Begin speaker frame (record start level, reset transitions) */
    speaker_begin_frame(&emu->speaker);

    /* Execute one frame worth of CPU cycles. While the guest CPU is in SLEEP,
     * skip directly to the next timer/DMA event instead of calling the CPU
     * halt fast path once per emulated cycle. */
    for (int cycle = 0; cycle < frame_cycles;) {
        if (m->model != CYBIKO_XTREME &&
            emu->cpu.halted && emu->cpu.pending_irq_count == 0) {
            int chunk = frame_cycles - cycle;
            int next_event = cycles_until_next_peripheral_event(emu);
            if (next_event > 0 && next_event < chunk) chunk = next_event;
            if (chunk >= HALT_FAST_FORWARD_MIN_CYCLES) {
                emu->speaker.frame_cycle = cycle + chunk - 1;
                tick_peripherals(emu, chunk);
                emu->cpu.cycle_count += (uint64_t)chunk;
                emu->total_steps += (uint64_t)chunk;
                cycle += chunk;
                continue;
            }
        }

        /* Track cycle position for speaker transition timestamps */
        emu->speaker.frame_cycle = cycle;

        /* Check the live prescalers: firmware can start/stop a timer during
         * this frame, including immediately before entering SLEEP. */
        timer8_tick(&emu->timer8[0]);
        timer8_tick(&emu->timer8[1]);
        for (int i = 0; i < m->timer_channels; i++) {
            timer16_tick(&emu->timer16[i]);
        }

        /* CPU step */
        h8s_cpu_step(&emu->cpu);

        /* DMA completion */
        bus_tick_dma_completion(&emu->bus);

        emu->total_steps++;
        cycle++;
    }

    /* Render the frame via HAL */
    if (emu->hal.render_frame) {
        const uint8_t *pixels = hd66421_render(&emu->lcd);
        emu->hal.render_frame(emu->hal.ctx, pixels,
                              CYBIKO_LCD_WIDTH, CYBIKO_LCD_HEIGHT);
    }

    /* Generate audio samples for this frame */
    if (emu->hal.audio_output) {
        uint8_t audio_buf[8192];
        int samples = speaker_generate_samples(&emu->speaker,
                                               frame_cycles,
                                               audio_buf, sizeof(audio_buf));
        if (samples > 0) {
            emu->hal.audio_output(emu->hal.ctx, audio_buf, samples);
        }
    }

    /* Tick RTC once per frame */
    bus_tick_rtc(&emu->bus);

    /* Drain serial output buffers to the HAL */
    if (emu->hal.serial_output) {
        for (int ch = 0; ch < 3; ch++) {
            for (int i = 0; i < emu->bus.sci_output_pos[ch]; i++) {
                emu->hal.serial_output(emu->hal.ctx,
                                       (uint8_t)emu->bus.sci_output[ch][i]);
            }
            emu->bus.sci_output_pos[ch] = 0;
        }
    }

    emu->frame_count++;
}

/* ---------- queries ---------- */

bool cybiko_is_running(const cybiko_emu_t *emu) {
    /* H8S SLEEP is normal idle behavior; peripherals must keep ticking. */
    return emu && emu->running;
}

const uint8_t *cybiko_get_nvram(const cybiko_emu_t *emu, size_t *len) {
    if (len) *len = emu->bus.external_ram.size;
    return memory_raw((memory_t *)&emu->bus.external_ram);
}

cybiko_model_t cybiko_get_model(const cybiko_emu_t *emu) { return emu->bus.machine->model; }
uint32_t cybiko_get_program_counter(const cybiko_emu_t *emu) { return emu ? emu->cpu.pc : 0; }
void cybiko_get_clock(cybiko_emu_t *emu, uint8_t registers[16]) {
    rtc_tick(&emu->rtc);
    memcpy(registers, emu->rtc.data, 16);
}
bool cybiko_load_clock(cybiko_emu_t *emu, const uint8_t *registers, size_t len,
                       uint64_t elapsed_seconds) {
    /* Reject implausible/corrupt timestamps before multiplying to nanoseconds. */
    if (!emu || !registers || len != 16 || elapsed_seconds > 3155760000ULL) return false;
    rtc_init(&emu->rtc);
    memcpy(emu->rtc.data, registers, 16);
    rtc_advance(&emu->rtc, elapsed_seconds * 1000000000ULL);
    return true;
}
bool cybiko_load_dataflash(cybiko_emu_t *emu, const uint8_t *data, size_t len) {
    if (!emu || !emu->bus.dataflash || !data || len != DATAFLASH_SIZE) return false;
    memcpy(emu->bus.dataflash->data, data, len);
    return true;
}
const uint8_t *cybiko_get_dataflash(const cybiko_emu_t *emu, size_t *len) {
    if (len) *len = emu && emu->bus.dataflash ? DATAFLASH_SIZE : 0;
    return emu && emu->bus.dataflash ? emu->bus.dataflash->data : NULL;
}

/* Compare event batching against the original cycle-by-cycle implementation.
 * Including the implementation exposes state only to this test executable. */
#define CYBIKO_SCHEDULER_TEST 1
#include "../third_party/cybiko-c-emulator/src/core/emulator.c"
#include "acutest.h"
#include <time.h>

static void compare_state(cybiko_emu_t *a, cybiko_emu_t *b)
{
    TEST_CHECK(a->cpu.pc == b->cpu.pc);
    TEST_CHECK(a->cpu.ccr == b->cpu.ccr);
    TEST_CHECK(a->cpu.halted == b->cpu.halted);
    TEST_CHECK(a->cpu.cycle_count == b->cpu.cycle_count);
    TEST_CHECK(a->cpu.irq_deferred == b->cpu.irq_deferred);
    TEST_CHECK(!memcmp(a->cpu.er, b->cpu.er, sizeof(a->cpu.er)));
    TEST_CHECK(a->cpu.pending_irq_count == b->cpu.pending_irq_count);
    TEST_CHECK(!memcmp(a->cpu.pending_irqs, b->cpu.pending_irqs,
                      (size_t)a->cpu.pending_irq_count * sizeof(int)));
    TEST_CHECK(!memcmp(a->bus.external_ram.data, b->bus.external_ram.data,
                      a->bus.external_ram.size));
    TEST_CHECK(!memcmp(a->bus.on_chip_ram.data, b->bus.on_chip_ram.data,
                      a->bus.on_chip_ram.size));
    TEST_CHECK(a->bus.adcsr == b->bus.adcsr);
    TEST_CHECK(a->bus.adc_completion_delay == b->bus.adc_completion_delay);
    TEST_CHECK(a->bus.dma_completion_delay == b->bus.dma_completion_delay);
    TEST_CHECK(a->bus.dtc_completion_delay == b->bus.dtc_completion_delay);
    TEST_CHECK(!memcmp(a->bus.sci_tx_delay, b->bus.sci_tx_delay, sizeof(a->bus.sci_tx_delay)));
    for (int i = 0; i < 2; ++i) {
        timer8_t x = a->timer8[i], y = b->timer8[i];
        x.cpu = y.cpu = NULL;
        TEST_CHECK(!memcmp(&x, &y, sizeof(x)));
    }
    for (int i = 0; i < a->bus.machine->timer_channels; ++i) {
        timer16_t x = a->timer16[i], y = b->timer16[i];
        x.cpu = y.cpu = NULL;
        x.output_b_ctx = y.output_b_ctx = NULL;
        TEST_CHECK(!memcmp(&x, &y, sizeof(x)));
    }
    TEST_CHECK(!memcmp(&a->speaker, &b->speaker, sizeof(a->speaker)));
    TEST_CHECK(!memcmp(&a->lcd, &b->lcd, sizeof(a->lcd)));
}

static void test_event_scheduler_matches_reference(void)
{
    for (int model = 0; model < CYBIKO_MODEL_COUNT; ++model) {
        cybiko_hal_t hal = {0};
        cybiko_emu_t *a = cybiko_create_model(&hal, model);
        cybiko_emu_t *b = cybiko_create_model(&hal, model);
        TEST_ASSERT(a && b);
        b->reference_scheduler = true;
        b->bus.sync_peripherals = NULL;
        memset(b->bus.read_pages, 0, sizeof(b->bus.read_pages));
        memset(b->bus.write_pages, 0, sizeof(b->bus.write_pages));
        uint8_t rom[CYBIKO_BOOT_ROM_SIZE] = {0};
        /* All IRQ vectors point at an RTE. Reset starts at 0x400. */
        for (int vector = 0; vector < 128; ++vector) rom[vector * 4 + 2] = 2;
        rom[2] = 4;
        rom[0x200] = 0x56; rom[0x201] = 0x70;
        /* ADC scan start, straight-line work, timer read/write, loop. */
        uint8_t start[] = {0xf8,0x7a,0x38,0x98};
        memcpy(rom + 0x400, start, sizeof(start));
        uint8_t tail[] = {0x28,0xb8,0x38,0xb9,0x5a,0x00,0x04,0x00};
        memcpy(rom + 0x440, tail, sizeof(tail));
        for (int instance = 0; instance < 2; ++instance) {
            cybiko_emu_t *e = instance ? b : a;
            TEST_ASSERT(cybiko_load_boot_rom(e, rom, sizeof(rom)));
            cybiko_reset(e);
            e->cpu.er[7] = e->bus.machine->ram_base + e->bus.machine->ram_size - 16;
            e->cpu.ccr = 0;
            timer8_write(&e->timer8[0], 0, 0x41);
            timer8_write(&e->timer8[0], 4, 13);
            for (int i = 0; i < e->bus.machine->timer_channels; ++i) {
                timer16_write8(&e->timer16[i], 0, 3);
                timer16_write8(&e->timer16[i], 2, 0x30);
                timer16_write8(&e->timer16[i], 4, 3);
                timer16_write16(&e->timer16[i], 8, 19 + i);
                timer16_write16(&e->timer16[i], 10, 7 + i);
                timer16_set_enabled(&e->timer16[i], true);
            }
            e->bus.dtc_completion_delay = 131;
            e->bus.dma_completion_delay = 177;
            e->bus.dma_completion_vector = 72;
            e->bus.sci_tx_delay[0] = 57;
        }
        for (int frame = 0; frame < 3; ++frame) {
            cybiko_run_frame(a);
            cybiko_run_frame(b);
            compare_state(a, b);
        }
        cybiko_destroy(a);
        cybiko_destroy(b);
    }
}

static void load_exact(const char *path, uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "rb");
    TEST_ASSERT(file != NULL);
    TEST_ASSERT(fread(data, 1, size, file) == size);
    TEST_ASSERT(fgetc(file) == EOF);
    fclose(file);
}

/* Opt-in local firmware comparison; proprietary fixtures are never bundled. */
static void test_firmware_scheduler_optional(void)
{
    const char *boot = getenv("CYBIKO_SCHEDULER_BOOT");
    if (!boot) return;
    const char *model = getenv("CYBIKO_SCHEDULER_MODEL");
    TEST_ASSERT(model != NULL);
    int id = atoi(model);
    TEST_ASSERT(id >= 0 && id < CYBIKO_MODEL_COUNT);
    cybiko_hal_t hal = {0};
    cybiko_emu_t *a = cybiko_create_model(&hal, id);
    cybiko_emu_t *b = cybiko_create_model(&hal, id);
    TEST_ASSERT(a && b);
    b->reference_scheduler = true;
    b->bus.sync_peripherals = NULL;
    memset(b->bus.read_pages, 0, sizeof(b->bus.read_pages));
    memset(b->bus.write_pages, 0, sizeof(b->bus.write_pages));
    for (int instance = 0; instance < 2; ++instance) {
        cybiko_emu_t *e = instance ? b : a;
        load_exact(boot, e->bus.boot_rom.data, e->bus.boot_rom.size);
        if (e->bus.machine->flash_size) {
            const char *flash = getenv("CYBIKO_SCHEDULER_FLASH");
            TEST_ASSERT(flash != NULL);
            load_exact(flash, e->bus.flash_rom.data, e->bus.flash_rom.size);
        }
        if (e->bus.dataflash) {
            const char *flash = getenv("CYBIKO_SCHEDULER_DATAFLASH");
            TEST_ASSERT(flash != NULL);
            load_exact(flash, e->bus.dataflash->data, DATAFLASH_SIZE);
        }
        const char *ram = getenv("CYBIKO_SCHEDULER_RAM");
        if (ram) load_exact(ram, e->bus.external_ram.data, e->bus.external_ram.size);
        memset(e->rtc.data, 0, sizeof(e->rtc.data));
        e->rtc.data[0] = 0x80; /* Freeze host wall-clock for exact comparison. */
        e->rtc.data[5] = e->rtc.data[6] = 1;
        cybiko_reset(e);
    }
    clock_t batched_time = 0, reference_time = 0;
    for (int frame = 0; frame < 600; ++frame) {
        TEST_CASE_("model %d frame %d", id, frame);
        a->rtc.last_tick_ns = b->rtc.last_tick_ns = UINT64_MAX;
        if (id != CYBIKO_XTREME)
            a->keyboard.columns[5] = b->keyboard.columns[5] =
                (frame % 180 >= 150 && frame % 180 < 162) ? 4 : 0;
        clock_t start = clock();
        cybiko_run_frame(a);
        batched_time += clock() - start;
        start = clock();
        cybiko_run_frame(b);
        reference_time += clock() - start;
        TEST_ASSERT_(a->cpu.pc == b->cpu.pc, "PC batch=%06x reference=%06x", a->cpu.pc, b->cpu.pc);
        compare_state(a, b);
    }
    printf("\nmodel=%d batch_cpu_s=%.6f reference_cpu_s=%.6f\n", id,
           (double)batched_time / CLOCKS_PER_SEC, (double)reference_time / CLOCKS_PER_SEC);
    cybiko_destroy(a);
    cybiko_destroy(b);
}

/* Optional saved-desktop fixture: the full-charge regression previously
 * reached the desktop but left every navigation input ineffective. */
static void test_classic_full_charge_navigation_optional(void)
{
    const char *root = getenv("CYBIKO_BATTERY_DESKTOP_FIXTURE");
    const char *boot = getenv("CYBIKO_SCHEDULER_BOOT");
    if (!root) return;
    TEST_ASSERT(boot != NULL);
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create_model(&hal, CYBIKO_CLASSIC_V1);
    TEST_ASSERT(emu != NULL);
    load_exact(boot, emu->bus.boot_rom.data, emu->bus.boot_rom.size);
    char path[1024];
    snprintf(path, sizeof(path), "%s/save.flash", root);
    load_exact(path, emu->bus.dataflash->data, DATAFLASH_SIZE);
    snprintf(path, sizeof(path), "%s/ram.raw", root);
    load_exact(path, emu->bus.external_ram.data, emu->bus.external_ram.size);
    snprintf(path, sizeof(path), "%s/clock.raw", root);
    load_exact(path, emu->rtc.data, 16);
    cybiko_reset(emu);
    uint8_t before[HD66421_WIDTH * HD66421_HEIGHT];
    for (int frame = 0; frame < 780; ++frame) {
        emu->rtc.last_tick_ns = UINT64_MAX;
        emu->keyboard.columns[4] = frame >= 600 && frame < 660 ? 1 : 0;
        cybiko_run_frame(emu);
        if (frame == 599) memcpy(before, hd66421_render(&emu->lcd), sizeof(before));
    }
    uint32_t battery = bus_read32(&emu->bus, 0x227fd0);
    TEST_ASSERT(battery >= 0x200000 && battery < 0x27ffe0);
    TEST_CHECK(bus_read16(&emu->bus, battery + 0x14) == 78);
    TEST_CHECK(bus_read8(&emu->bus, battery + 0x16) == 0);
    const uint8_t *after = hd66421_render(&emu->lcd);
    int changed = 0;
    /* Ignore the clock/battery bar: verify the actual desktop content moved. */
    for (int y = 15; y < 85; ++y)
        for (int x = 0; x < 160; ++x)
            changed += before[y * 160 + x] != after[y * 160 + x];
    TEST_CHECK(changed > 100);
    cybiko_destroy(emu);
}

TEST_LIST = {
    {"classic_full_charge_navigation", test_classic_full_charge_navigation_optional},
    {"local_firmware_scheduler_equivalence", test_firmware_scheduler_optional},
    {"event_scheduler_matches_cycle_reference", test_event_scheduler_matches_reference},
    {NULL, NULL}
};

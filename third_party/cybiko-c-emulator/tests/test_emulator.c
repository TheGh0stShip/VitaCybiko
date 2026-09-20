#include "acutest.h"
#include "core/emulator.h"
#include <stdlib.h>
#include <string.h>

static cybiko_emu_t *new_emulator(void)
{
    cybiko_hal_t hal;
    memset(&hal, 0, sizeof(hal));
    return cybiko_create(&hal);
}

static void test_rom_sizes_are_validated(void)
{
    cybiko_emu_t *emu = new_emulator();
    TEST_ASSERT(emu != NULL);
    uint8_t *boot = calloc(CYBIKO_BOOT_ROM_SIZE, 1);
    uint8_t *flash = calloc(CYBIKO_FLASH_ROM_SIZE, 1);
    TEST_ASSERT(boot != NULL);
    TEST_ASSERT(flash != NULL);

    TEST_CHECK(!cybiko_load_boot_rom(emu, NULL, CYBIKO_BOOT_ROM_SIZE));
    TEST_CHECK(!cybiko_load_boot_rom(emu, boot, CYBIKO_BOOT_ROM_SIZE - 1));
    TEST_CHECK(cybiko_load_boot_rom(emu, boot, CYBIKO_BOOT_ROM_SIZE));
    TEST_CHECK(!cybiko_load_flash_rom(emu, flash, CYBIKO_FLASH_ROM_SIZE - 1));
    TEST_CHECK(cybiko_load_flash_rom(emu, flash, CYBIKO_FLASH_ROM_SIZE));

    free(flash);
    free(boot);
    cybiko_destroy(emu);
}

static void test_nvram_size_is_bounded(void)
{
    cybiko_emu_t *emu = new_emulator();
    TEST_ASSERT(emu != NULL);
    uint8_t byte = 0;
    uint8_t *nvram = calloc(CYBIKO_NVRAM_SIZE + 1, 1);
    TEST_ASSERT(nvram != NULL);

    TEST_CHECK(!cybiko_load_nvram(emu, NULL, 1));
    TEST_CHECK(!cybiko_load_nvram(emu, &byte, 0));
    TEST_CHECK(cybiko_load_nvram(emu, &byte, 1));
    TEST_CHECK(cybiko_load_nvram(emu, nvram, CYBIKO_NVRAM_SIZE));
    TEST_CHECK(!cybiko_load_nvram(emu, nvram, CYBIKO_NVRAM_SIZE + 1));

    size_t size = 0;
    TEST_CHECK(cybiko_get_nvram(emu, &size) != NULL);
    TEST_CHECK(size == CYBIKO_NVRAM_SIZE);

    free(nvram);
    cybiko_destroy(emu);
}

static uint32_t reference_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffff;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffff;
}

static void test_crc32_matches_bitwise(void)
{
    uint8_t data[4097];
    uint32_t random = 0x641348;
    for (size_t i = 0; i < sizeof(data); ++i) {
        random = random * 1664525u + 1013904223u;
        data[i] = (uint8_t)(random >> 24);
    }
    for (size_t offset = 0; offset < 4; ++offset)
        for (size_t size = 0; size <= sizeof(data) - offset; size += 7)
            TEST_CHECK(cybiko_crc32(data + offset, size) == reference_crc32(data + offset, size));
    for (unsigned value = 0; value < 256; ++value) {
        data[0] = (uint8_t)value;
        TEST_CHECK(cybiko_crc32(data, 1) == reference_crc32(data, 1));
    }
}

static void test_firmware_identification(void)
{
    static const uint8_t check[] = "123456789";
    TEST_CHECK(cybiko_crc32(check, sizeof(check) - 1) == 0xCBF43926u);
    TEST_CHECK(cybiko_crc32(NULL, 0) == 0);

    uint8_t *boot = calloc(CYBIKO_BOOT_ROM_SIZE, 1);
    uint8_t *flash = calloc(CYBIKO_FLASH_ROM_SIZE, 1);
    TEST_ASSERT(boot != NULL);
    TEST_ASSERT(flash != NULL);
    TEST_CHECK(!cybiko_is_supported_firmware(
        boot, CYBIKO_BOOT_ROM_SIZE, flash, CYBIKO_FLASH_ROM_SIZE));
    TEST_CHECK(!cybiko_is_supported_firmware(
        NULL, CYBIKO_BOOT_ROM_SIZE, flash, CYBIKO_FLASH_ROM_SIZE));
    free(flash);
    free(boot);
}

static void test_sleep_does_not_stop_emulation(void)
{
    TEST_CHECK(cybiko_create(NULL) == NULL);
    TEST_CHECK(!cybiko_is_running(NULL));
    cybiko_emu_t *emu = new_emulator();
    TEST_ASSERT(emu != NULL);
    uint8_t *boot = calloc(CYBIKO_BOOT_ROM_SIZE, 1);
    TEST_ASSERT(boot != NULL);
    /* Reset vector points to SLEEP at 0x100. No interrupt is enabled. */
    boot[2] = 1;
    boot[0x100] = 0x01;
    boot[0x101] = 0x80;
    TEST_ASSERT(cybiko_load_boot_rom(emu, boot, CYBIKO_BOOT_ROM_SIZE));
    cybiko_reset(emu);
    cybiko_run_frame(emu);
    TEST_CHECK(cybiko_is_running(emu));
    cybiko_run_frame(emu);
    TEST_CHECK(cybiko_is_running(emu));
    free(boot);
    cybiko_destroy(emu);
}

/* Firmware can start a timer anywhere in a frame. The first ticks must not
 * wait until the next 60 Hz frame boundary. No proprietary ROM is needed. */
static void test_timer_starts_during_frame(void)
{
    for (int scenario = 0; scenario < 6; ++scenario) {
        cybiko_model_t model = (cybiko_model_t)(scenario / 2);
        bool timer16 = (scenario % 2) != 0;
        cybiko_hal_t hal = {0};
        cybiko_emu_t *emu = cybiko_create_model(&hal, model);
        TEST_ASSERT(emu != NULL);
        uint8_t *boot = calloc(CYBIKO_BOOT_ROM_SIZE, 1);
        TEST_ASSERT(boot != NULL);
        boot[2] = 1; /* Reset to 0x100. */
        size_t p = 0x100;
        boot[p++] = 0xf8; boot[p++] = 1;    /* MOV.B #1,R0L */
        /* TMR0: /8 clock, or 16-bit channel 0: TSTR enable, /1 clock. */
        boot[p++] = 0x38; boot[p++] = timer16 ? 0xc0 : 0xb0;
        p += 64 * 2;                      /* 64 NOPs */
        boot[p++] = 0x28; boot[p++] = timer16 ? 0xd7 : 0xb8;
        uint32_t ram = cybiko_machine(model)->ram_base;
        boot[p++] = 0x6a; boot[p++] = 0xa8; /* MOV.B R0L,@RAM:32 */
        boot[p++] = 0; boot[p++] = ram >> 16;
        boot[p++] = ram >> 8; boot[p++] = ram;
        boot[p++] = 0xf8; boot[p++] = 0;
        boot[p++] = 0x38; boot[p++] = timer16 ? 0xc0 : 0xb0;
        p += 16 * 2;
        boot[p++] = 0x28; boot[p++] = timer16 ? 0xd7 : 0xb8;
        boot[p++] = 0x6a; boot[p++] = 0xa8;
        boot[p++] = 0; boot[p++] = ram >> 16;
        boot[p++] = ram >> 8; boot[p++] = 1;
        boot[p++] = 0x01; boot[p++] = 0x80; /* SLEEP */
        TEST_ASSERT(cybiko_load_boot_rom(emu, boot, CYBIKO_BOOT_ROM_SIZE));
        cybiko_reset(emu);
        cybiko_run_frame(emu);
        size_t len;
        const uint8_t *data = cybiko_get_nvram(emu, &len);
        TEST_ASSERT(data && len >= 2);
        TEST_CHECK_(data[0] == (timer16 ? 65 : 8),
                    "model %d timer16=%d first-frame count %u", model, timer16, data[0]);
        TEST_CHECK_(data[1] == (timer16 ? 68 : 8),
                    "model %d timer16=%d stopped count %u", model, timer16, data[1]);
        free(boot);
        cybiko_destroy(emu);
    }
}

TEST_LIST = {
    { "crc32_matches_bitwise", test_crc32_matches_bitwise },
    { "timer_starts_during_frame", test_timer_starts_during_frame },
    { "sleep_keeps_emulation_running", test_sleep_does_not_stop_emulation },
    { "rom_sizes_are_validated", test_rom_sizes_are_validated },
    { "nvram_size_is_bounded",   test_nvram_size_is_bounded },
    { "firmware_identification", test_firmware_identification },
    { NULL, NULL }
};

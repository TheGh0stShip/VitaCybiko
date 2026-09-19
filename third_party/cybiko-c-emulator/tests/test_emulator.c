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

TEST_LIST = {
    { "sleep_keeps_emulation_running", test_sleep_does_not_stop_emulation },
    { "rom_sizes_are_validated", test_rom_sizes_are_validated },
    { "nvram_size_is_bounded",   test_nvram_size_is_bounded },
    { "firmware_identification", test_firmware_identification },
    { NULL, NULL }
};

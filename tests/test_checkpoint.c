// SPDX-License-Identifier: GPL-3.0-or-later
#include "acutest.h"
#include "checkpoint.h"
#include "core/dataflash.h"
#include <stdlib.h>
#include <string.h>

static void put_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void test_ram_sidecar(void)
{
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create_model(&hal, CYBIKO_CLASSIC_V2);
    TEST_ASSERT(emu != NULL);
    size_t ram_size = cybiko_machine(CYBIKO_CLASSIC_V2)->ram_size;
    uint8_t *storage = malloc(DATAFLASH_SIZE);
    uint8_t *sidecar = calloc(1, ram_size + 24);
    TEST_ASSERT(storage && sidecar);
    for (size_t i = 0; i < DATAFLASH_SIZE; ++i) storage[i] = (uint8_t)(i * 29u);
    for (size_t i = 0; i < ram_size; ++i) sidecar[24 + i] = (uint8_t)(i * 17u + 3u);
    memcpy(sidecar, "VCRM\1", 5);
    sidecar[5] = CYBIKO_CLASSIC_V2;
    put_u32le(sidecar + 8, (uint32_t)ram_size);
    put_u32le(sidecar + 12, cybiko_crc32(storage, DATAFLASH_SIZE));
    put_u32le(sidecar + 16, cybiko_crc32(sidecar + 24, ram_size));
    put_u32le(sidecar + 20, cybiko_crc32(sidecar, 20));

    TEST_CHECK(cybiko_checkpoint_load_ram(emu, CYBIKO_CLASSIC_V2, sidecar,
                                          ram_size + 24, storage, DATAFLASH_SIZE));
    size_t loaded_size = 0;
    const uint8_t *loaded = cybiko_get_nvram(emu, &loaded_size);
    TEST_CHECK(loaded_size == ram_size);
    TEST_CHECK(!memcmp(loaded, sidecar + 24, ram_size));

    sidecar[24 + ram_size / 2] ^= 1;
    TEST_CHECK(!cybiko_checkpoint_load_ram(emu, CYBIKO_CLASSIC_V2, sidecar,
                                           ram_size + 24, storage, DATAFLASH_SIZE));
    sidecar[24 + ram_size / 2] ^= 1;
    storage[7] ^= 1;
    TEST_CHECK(!cybiko_checkpoint_load_ram(emu, CYBIKO_CLASSIC_V2, sidecar,
                                           ram_size + 24, storage, DATAFLASH_SIZE));
    storage[7] ^= 1;
    sidecar[5] = CYBIKO_CLASSIC_V1;
    TEST_CHECK(!cybiko_checkpoint_load_ram(emu, CYBIKO_CLASSIC_V2, sidecar,
                                           ram_size + 24, storage, DATAFLASH_SIZE));
    sidecar[5] = CYBIKO_CLASSIC_V2;
    TEST_CHECK(cybiko_checkpoint_load_ram(emu, CYBIKO_CLASSIC_V2, sidecar + 24,
                                          ram_size, NULL, 0));
    free(sidecar);
    free(storage);
    cybiko_destroy(emu);
}

static void test_clock_sidecar(void)
{
    cybiko_hal_t hal = {0};
    cybiko_emu_t *emu = cybiko_create_model(&hal, CYBIKO_CLASSIC_V1);
    TEST_ASSERT(emu != NULL);
    uint8_t clock[36] = {'V','R','T','C',1,0,0,0};
    clock[8] = 0x80;
    clock[13] = clock[14] = 1;
    put_u32le(clock + 32, cybiko_crc32(clock, 32));
    TEST_CHECK(cybiko_checkpoint_load_clock(emu, clock, sizeof(clock)));
    clock[9] ^= 1;
    TEST_CHECK(!cybiko_checkpoint_load_clock(emu, clock, sizeof(clock)));
    clock[9] ^= 1;
    TEST_CHECK(cybiko_checkpoint_load_clock(emu, clock + 8, 16));
    TEST_CHECK(!cybiko_checkpoint_load_clock(emu, clock, sizeof(clock) - 1));
    cybiko_destroy(emu);
}

TEST_LIST = {
    {"ram_sidecar", test_ram_sidecar},
    {"clock_sidecar", test_clock_sidecar},
    {NULL, NULL}
};

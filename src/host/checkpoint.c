// SPDX-License-Identifier: GPL-3.0-or-later
#include "checkpoint.h"
#include "core/dataflash.h"
#include <string.h>

static uint32_t get_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

bool cybiko_checkpoint_load_ram(cybiko_emu_t *emu, cybiko_model_t model,
                                const uint8_t *data, size_t size,
                                const uint8_t *storage, size_t storage_size)
{
    const cybiko_machine_t *machine = cybiko_machine(model);
    if (!emu || !machine || !data || cybiko_get_model(emu) != model) return false;
    size_t ram_size = machine->ram_size;
    if (size == ram_size)
        return cybiko_load_nvram(emu, data, size);
    if (model == CYBIKO_XTREME || size != ram_size + 24 || !storage ||
        storage_size != DATAFLASH_SIZE ||
        memcmp(data, "VCRM\1", 5) || data[5] != (uint8_t)model ||
        data[6] || data[7] || get_u32le(data + 8) != ram_size ||
        get_u32le(data + 12) != cybiko_crc32(storage, storage_size) ||
        get_u32le(data + 16) != cybiko_crc32(data + 24, ram_size) ||
        get_u32le(data + 20) != cybiko_crc32(data, 20))
        return false;
    return cybiko_load_nvram(emu, data + 24, ram_size);
}

bool cybiko_checkpoint_load_clock(cybiko_emu_t *emu, const uint8_t *data,
                                  size_t size)
{
    if (!emu || !data) return false;
    if (size == 16) return cybiko_load_clock(emu, data, size, 0);
    if (size != 36 || memcmp(data, "VRTC\1\0\0\0", 8) ||
        get_u32le(data + 32) != cybiko_crc32(data, 32))
        return false;
    return cybiko_load_clock(emu, data + 8, 16, 0);
}

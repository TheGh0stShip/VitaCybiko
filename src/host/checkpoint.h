// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef VITACYBIKO_HOST_CHECKPOINT_H
#define VITACYBIKO_HOST_CHECKPOINT_H

#include "core/emulator.h"

bool cybiko_checkpoint_load_ram(cybiko_emu_t *emu, cybiko_model_t model,
                                const uint8_t *data, size_t size,
                                const uint8_t *storage, size_t storage_size);
bool cybiko_checkpoint_load_clock(cybiko_emu_t *emu, const uint8_t *data,
                                  size_t size);

#endif

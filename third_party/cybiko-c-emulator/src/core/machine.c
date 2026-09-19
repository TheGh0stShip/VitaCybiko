/* Hardware profiles: MAME Cybiko driver and upstream MachineConfig.java.
 * Classic V2 uses the stock 256 KiB RAM configuration, not the Java 2 MiB hack. */
#include "machine.h"
static const cybiko_machine_t machines[] = {
    { CYBIKO_XTREME, "Cybiko Xtreme", "xtreme",
      18432000, 0x200000, 0x80000, 0xFFDC00,
      0x400000, 0x5FFFFF, 0x100000, 0x100001, 0x600000, 0x7FFFFF,
      0x03FFFF, 0xEFFFFF, 0x18B9B21F, 0xF79400BA, 6, 10, 0x40 },
    { CYBIKO_CLASSIC_V1, "Cybiko Classic V1", "classic-v1",
      11059200, 0x80000, 0, 0xFFEC00,
      0x200000, 0x27FFFF, 0x600000, 0x600001, 0, 0,
      0x007FFF, 0xFFEBFF, 0x9E1F1A0F, 0, 3, 9, 0x01 },
    { CYBIKO_CLASSIC_V2, "Cybiko Classic V2", "classic-v2",
      11059200, 0x40000, 0x40000, 0xFFDC00,
      0x200000, 0x3FFFFF, 0x600000, 0x7FFFFF, 0x100000, 0x1FFFFF,
      0x007FFF, 0xFFDBFF, 0x268DA7BF, 0x05CA4ECE, 3, 9, 0x01 },
};
const cybiko_machine_t *cybiko_machine(cybiko_model_t model)
{
    return model >= 0 && model < CYBIKO_MODEL_COUNT ? &machines[model] : NULL;
}

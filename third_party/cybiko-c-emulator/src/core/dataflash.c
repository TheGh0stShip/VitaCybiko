/* AT45DB041 byte-level SPI subset, based on MIT-licensed AT45DB041Flash.java.
 * Commands used by Classic CyOS: status, page read, compare, page program.
 * Unsupported commands leave storage unchanged; all page offsets are bounded. */
#include "dataflash.h"
#include <string.h>
void dataflash_init(dataflash_t *f)
{
    memset(f, 0, sizeof(*f));
    memset(f->data, 0xFF, sizeof(f->data));
    memset(f->buffer, 0xFF, sizeof(f->buffer));
    f->status = 0x98;
}
void dataflash_select(dataflash_t *f, bool selected)
{
    if (f->selected == selected) return;
    if (!selected) {
        if (f->count >= 4 && f->command[0] == 0x82)
            memcpy(f->data + f->page * DATAFLASH_PAGE_SIZE, f->buffer, DATAFLASH_PAGE_SIZE);
        f->count = 0;
        f->position = 0;
        memset(f->command, 0, sizeof(f->command));
    }
    f->selected = selected;
}
uint8_t dataflash_transfer(dataflash_t *f, uint8_t byte)
{
    if (!f->selected) return 0xFF;
    uint8_t op = f->command[0];
    if (f->count && (op == 0x57 || op == 0xD7)) return f->status;
    if (f->count == 8 && (op == 0x52 || op == 0xD2)) {
        uint8_t result = f->data[f->page * DATAFLASH_PAGE_SIZE + f->position];
        f->position = (f->position + 1) % DATAFLASH_PAGE_SIZE;
        return result;
    }
    if (f->count >= 4 && op == 0x82) {
        f->buffer[f->position] = byte;
        f->position = (f->position + 1) % DATAFLASH_PAGE_SIZE;
        return 0;
    }
    if (f->count < 8) {
        f->command[f->count++] = byte;
        if (f->count == 4) {
            f->page = ((f->command[1] & 0x0F) << 7) | (f->command[2] >> 1);
            f->position = (((f->command[2] & 1) << 8) | f->command[3]) % DATAFLASH_PAGE_SIZE;
            op = f->command[0];
            if (op == 0x82) memset(f->buffer, 0xFF, sizeof(f->buffer));
            if (op == 0x60) {
                bool different = memcmp(f->data + f->page * DATAFLASH_PAGE_SIZE,
                                        f->buffer, DATAFLASH_PAGE_SIZE) != 0;
                f->status = 0x98 | (different ? 0x40 : 0);
            }
        }
    }
    return 0;
}

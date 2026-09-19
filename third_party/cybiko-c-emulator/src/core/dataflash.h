#ifndef CYBIKO_DATAFLASH_H
#define CYBIKO_DATAFLASH_H
#include "types.h"
#define DATAFLASH_PAGE_SIZE 264u
#define DATAFLASH_SIZE (2048u * DATAFLASH_PAGE_SIZE)
typedef struct {
    uint8_t data[DATAFLASH_SIZE], buffer[DATAFLASH_PAGE_SIZE];
    uint8_t command[8], status;
    unsigned count, page, position;
    bool selected;
} dataflash_t;
void dataflash_init(dataflash_t *flash);
void dataflash_select(dataflash_t *flash, bool selected);
uint8_t dataflash_transfer(dataflash_t *flash, uint8_t byte);
#endif

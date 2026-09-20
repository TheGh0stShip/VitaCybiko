#ifndef CYBIKO_ADDRESS_BUS_H
#define CYBIKO_ADDRESS_BUS_H

#include "types.h"
#include "memory.h"
#include "hd66421.h"
#include "timer8.h"
#include "timer16.h"
#include "rtc.h"
#include "speaker.h"
#include "keyboard.h"
#include "machine.h"
#include "dataflash.h"

/* Forward declare CPU */
typedef struct h8s_cpu h8s_cpu_t;

typedef struct address_bus {
    const cybiko_machine_t *machine;
    dataflash_t *dataflash;
    uint8_t sci1_rdr;
    bool sci1_rdrf;
    int dtc_completion_delay;
    uint32_t sci_tx_delay[3];
    uint8_t sci_tx_status[3];
    /* Memory regions */
    memory_t  boot_rom;       /* 32KB, read-only */
    memory_t  external_ram;   /* 2MB, writable */
    memory_t  flash_rom;      /* 512KB, read-only */
    memory_t  on_chip_ram;    /* 10KB (0x2400 bytes), writable */

    /* Peripherals (pointers, owned by emulator) */
    hd66421_t  *lcd;
    timer8_t   *timer8[2];
    timer16_t  *timer16[6];
    rtc_t      *rtc;
    speaker_t  *speaker;
    keyboard_t *keyboard;
    h8s_cpu_t  *cpu;
    /* Synchronize deferred clocks before a guest accesses peripheral state. */
    void (*sync_peripherals)(void *ctx);
    void *sync_ctx;

    /* I/O state */
    uint8_t tstr;            /* Timer start register (0xFFFFC0) */
    uint8_t ier;             /* Interrupt enable register (0xFFFF2E) */
    uint8_t isr;             /* Interrupt status register (0xFFFF2F) */
    uint8_t adcsr;           /* ADC status register */
    uint8_t adcr;            /* ADC control register */
    int adc_completion_delay; /* CPU clocks until a conversion/scan completes */

    /* DMA registers (XT) */
    uint8_t dma_regs[32];    /* 0xFFFEE0-0xFFFEFF */
    uint8_t dma_ctrl[8];     /* 0xFFFF00-0xFFFF07 */

    /* Serial output buffers */
    char    sci_output[3][256];
    int     sci_output_pos[3];

    /* DMA completion tracking */
    int     dma_completion_delay;
    int     dma_completion_vector;

    /* Debug log counters */
    int     unmapped_log_count;
    int     io_fallthrough_log;
    int     dma_debug_log;
    bool    in_dma;  /* true during DMA transfers */
    unsigned sync_suppressed; /* composite MMIO access already synchronized */
    bool    scheduler_dirty;  /* MMIO write changed timer/DMA/ADC deadlines */
    /* Only complete, side-effect-free 4 KiB pages are mapped. The last page
     * (on-chip I/O), partial pages and page-crossing accesses use the router.
     * Entries alias live backing storage, not a copy of guest bytes. */
    const uint8_t *read_pages[4096];
    uint8_t *write_pages[4096];
    /* Mutable code-page watch state for future RAM/on-chip block caching.
     * Pages are cold by default, so ordinary data writes pay one predictable
     * zero-byte test. A cached RAM-code block marks its source pages watched;
     * any write through the bus then bumps that page generation, including DMA
     * transfers because DMA uses the normal bus_write helpers. */
    uint32_t code_page_generation[4096];
    uint8_t code_page_watched[4096];
} address_bus_t;

void     bus_init(address_bus_t *bus);
void     bus_free(address_bus_t *bus);
void     bus_build_memory_map(address_bus_t *bus);
void     bus_watch_code_range(address_bus_t *bus, uint32_t address, unsigned bytes);
uint32_t bus_code_page_generation(const address_bus_t *bus, uint32_t address);
void     bus_clear_code_watches(address_bus_t *bus);

uint8_t  bus_read8_slow(address_bus_t *bus, uint32_t address);
uint16_t bus_read16_slow(address_bus_t *bus, uint32_t address);
uint32_t bus_read32_slow(address_bus_t *bus, uint32_t address);
void     bus_write8_slow(address_bus_t *bus, uint32_t address, uint8_t value);
void     bus_write16_slow(address_bus_t *bus, uint32_t address, uint16_t value);
void     bus_write32_slow(address_bus_t *bus, uint32_t address, uint32_t value);

static inline bool bus_is_plain_read_range(const address_bus_t *bus,
                                           uint32_t address, unsigned bytes) {
    if (!bus || bytes == 0 || bytes > 4) return false;
    address &= 0xffffff;
    if ((address & 4095u) + bytes > 4096u) return false;
    if (bus->read_pages[address >> 12]) return true;
    const cybiko_machine_t *m = bus->machine;
    return m && address >= m->on_chip_base && address + bytes <= 0xfffc00u;
}

static inline bool bus_is_plain_write_range(const address_bus_t *bus,
                                            uint32_t address, unsigned bytes) {
    if (!bus || bytes == 0 || bytes > 4) return false;
    address &= 0xffffff;
    if ((address & 4095u) + bytes > 4096u) return false;
    if (bus->write_pages[address >> 12]) return true;
    const cybiko_machine_t *m = bus->machine;
    return m && address >= m->on_chip_base && address + bytes <= 0xfffc00u;
}

static inline const uint8_t *bus_plain_read_ptr(const address_bus_t *bus,
                                                uint32_t address,
                                                unsigned bytes) {
    if (!bus_is_plain_read_range(bus, address, bytes)) return NULL;
    address &= 0xffffff;
    const uint8_t *page = bus->read_pages[address >> 12];
    if (page) return page + (address & 4095u);
    const cybiko_machine_t *m = bus->machine;
    return bus->on_chip_ram.data + (address - m->on_chip_base);
}

static inline uint8_t *bus_plain_write_ptr(address_bus_t *bus,
                                           uint32_t address,
                                           unsigned bytes) {
    if (!bus_is_plain_write_range(bus, address, bytes)) return NULL;
    address &= 0xffffff;
    uint8_t *page = bus->write_pages[address >> 12];
    if (page) return page + (address & 4095u);
    const cybiko_machine_t *m = bus->machine;
    return bus->on_chip_ram.data + (address - m->on_chip_base);
}

static inline void bus_note_code_write(address_bus_t *bus,
                                       uint32_t address,
                                       unsigned bytes) {
    if (!bus || bytes == 0) return;
    address &= 0xffffff;
    uint32_t first = address >> 12;
    uint32_t last = ((address + bytes - 1) & 0xffffff) >> 12;
    if (last < first) last = 4095u;
    if (bus->code_page_watched[first]) bus->code_page_generation[first]++;
    if (last != first && bus->code_page_watched[last])
        bus->code_page_generation[last]++;
}

static inline uint8_t bus_read8(address_bus_t *bus, uint32_t address) {
    address &= 0xffffff;
    const uint8_t *page = bus->read_pages[address >> 12];
    if (page) return page[address & 4095];
    const cybiko_machine_t *m = bus->machine;
    if (m && address >= m->on_chip_base && address < 0xfffc00)
        return memory_read8(&bus->on_chip_ram, address - m->on_chip_base);
    return bus_read8_slow(bus, address);
}
static inline uint16_t bus_read16(address_bus_t *bus, uint32_t address) {
    address &= 0xffffff;
    const uint8_t *page = bus->read_pages[address >> 12];
    unsigned offset = address & 4095;
    if (page && offset <= 4094)
        return (uint16_t)((page[offset] << 8) | page[offset + 1]);
    const cybiko_machine_t *m = bus->machine;
    if (m && address >= m->on_chip_base && address + 1 < 0xfffc00)
        return memory_read16(&bus->on_chip_ram, address - m->on_chip_base);
    return bus_read16_slow(bus, address);
}
static inline uint32_t bus_read32(address_bus_t *bus, uint32_t address) {
    address &= 0xffffff;
    const uint8_t *page = bus->read_pages[address >> 12];
    unsigned offset = address & 4095;
    if (page && offset <= 4092)
        return ((uint32_t)page[offset] << 24) | ((uint32_t)page[offset + 1] << 16) |
               ((uint32_t)page[offset + 2] << 8) | page[offset + 3];
    const cybiko_machine_t *m = bus->machine;
    if (m && address >= m->on_chip_base && address + 3 < 0xfffc00)
        return memory_read32(&bus->on_chip_ram, address - m->on_chip_base);
    return bus_read32_slow(bus, address);
}
static inline void bus_write8(address_bus_t *bus, uint32_t address, uint8_t value) {
    address &= 0xffffff;
    uint8_t *page = bus->write_pages[address >> 12];
    if (page) {
        page[address & 4095] = value;
        bus_note_code_write(bus, address, 1);
        return;
    }
    const cybiko_machine_t *m = bus->machine;
    if (m && address >= m->on_chip_base && address < 0xfffc00) {
        memory_write8(&bus->on_chip_ram, address - m->on_chip_base, value);
        bus_note_code_write(bus, address, 1);
        return;
    }
    bus_write8_slow(bus, address, value);
}
static inline void bus_write16(address_bus_t *bus, uint32_t address, uint16_t value) {
    address &= 0xffffff;
    uint8_t *page = bus->write_pages[address >> 12];
    unsigned offset = address & 4095;
    if (page && offset <= 4094) {
        page[offset] = (uint8_t)(value >> 8); page[offset + 1] = (uint8_t)value;
        bus_note_code_write(bus, address, 2);
        return;
    }
    const cybiko_machine_t *m = bus->machine;
    if (m && address >= m->on_chip_base && address + 1 < 0xfffc00) {
        memory_write16(&bus->on_chip_ram, address - m->on_chip_base, value);
        bus_note_code_write(bus, address, 2);
        return;
    }
    bus_write16_slow(bus, address, value);
}
static inline void bus_write32(address_bus_t *bus, uint32_t address, uint32_t value) {
    address &= 0xffffff;
    uint8_t *page = bus->write_pages[address >> 12];
    unsigned offset = address & 4095;
    if (page && offset <= 4092) {
        page[offset] = (uint8_t)(value >> 24); page[offset + 1] = (uint8_t)(value >> 16);
        page[offset + 2] = (uint8_t)(value >> 8); page[offset + 3] = (uint8_t)value;
        bus_note_code_write(bus, address, 4);
        return;
    }
    const cybiko_machine_t *m = bus->machine;
    if (m && address >= m->on_chip_base && address + 3 < 0xfffc00) {
        memory_write32(&bus->on_chip_ram, address - m->on_chip_base, value);
        bus_note_code_write(bus, address, 4);
        return;
    }
    bus_write32_slow(bus, address, value);
}

void     bus_tick_dma_completion(address_bus_t *bus);
int      bus_cycles_until_dma_completion(const address_bus_t *bus);
void     bus_advance_dma_completion(address_bus_t *bus, int cycles);
static inline bool bus_has_pending_completion(const address_bus_t *bus) {
    return bus->adc_completion_delay > 0 ||
           bus->dtc_completion_delay > 0 ||
           bus->dma_completion_delay > 0 ||
           bus->sci_tx_delay[0] > 0 ||
           bus->sci_tx_delay[2] > 0;
}
void     bus_tick_rtc(address_bus_t *bus);

#endif

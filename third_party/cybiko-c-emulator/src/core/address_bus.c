/*
 * Cybiko address bus - memory-mapped I/O router (XT configuration).
 *
 * Ported from AddressBus.java. Routes CPU reads/writes to the correct
 * peripheral based on the 24-bit address.
 *
 * XT Memory map:
 *   0x000000-0x03FFFF  Boot ROM (32KB mirrored)
 *   0x100000-0x100001  LCD controller (HD66421)
 *   0x200000-0x200003  USB controller (stub)
 *   0x400000-0x5FFFFF  External RAM (2MB)
 *   0x600000-0x7FFFFF  Flash ROM (512KB mirrored)
 *   0xE00000-0xEFFFFF  Keyboard matrix
 *   0xFFDC00-0xFFFFFF  On-chip RAM & I/O registers
 */
#include "core/address_bus.h"
#include "core/h8s_cpu.h"
#include <stdio.h>
#include <string.h>

/* --- Address region enumeration --- */
typedef enum {
    REGION_BOOT_ROM,
    REGION_LCD,
    REGION_USB,
    REGION_EXT_RAM,
    REGION_FLASH,
    REGION_KEYBOARD,
    REGION_ON_CHIP,
    REGION_UNMAPPED
} bus_region_t;

/* --- SSR bit definitions --- */
#define SSR_TDRE 0x80
#define SSR_RDRF 0x40
#define SSR_TEND 0x04

/* --- XT memory map constants --- */
#define BOOT_ROM_MIRROR_END    0x03FFFF
#define LCD_BASE               0x100000
#define LCD_END                0x100001
#define USB_BASE               0x200000
#define USB_END                0x200003
#define EXT_RAM_BASE           0x400000
#define EXT_RAM_END            0x5FFFFF
#define EXT_RAM_SIZE           0x200000
#define FLASH_BASE             0x600000
#define FLASH_END              0x7FFFFF
#define FLASH_SIZE             0x80000
#define KEYBOARD_BASE          0xE00000
#define KEYBOARD_END           0xEFFFFF
#define ON_CHIP_RAM_BASE       0xFFDC00

/* --- Forward declarations --- */
static uint8_t  read_on_chip8(address_bus_t *bus, uint32_t address);
static uint16_t read_on_chip16(address_bus_t *bus, uint32_t address);
static void     write_on_chip8(address_bus_t *bus, uint32_t address, uint8_t value);
static void     write_on_chip16(address_bus_t *bus, uint32_t address, uint16_t value);
static void     execute_dma_transfer(address_bus_t *bus, int channel);
static void update_rtc_pins(address_bus_t *bus);

/* --- Address decoding --- */

static bus_region_t decode_region(address_bus_t *bus, uint32_t address)
{
    const cybiko_machine_t *m = bus->machine;
    /* Stack, heap and Classic executable code dominate data accesses. */
    if (address >= m->ram_base && address <= m->ram_end)
        return REGION_EXT_RAM;

    if (address <= m->boot_end)
        return REGION_BOOT_ROM;

    if (address >= m->lcd_base && address <= m->lcd_end)
        return REGION_LCD;

    if (m->model == CYBIKO_XTREME && address >= USB_BASE && address <= USB_END)
        return REGION_USB;

    if (m->flash_size && address >= m->flash_base && address <= m->flash_end)
        return REGION_FLASH;

    if (address >= KEYBOARD_BASE && address <= m->keyboard_end)
        return REGION_KEYBOARD;

    if (address >= m->on_chip_base && address <= 0xFFFFFF)
        return REGION_ON_CHIP;

    return REGION_UNMAPPED;
}

static uint32_t ext_ram_offset(address_bus_t *bus, uint32_t address)
{
    /* Every supported physical RAM/ROM capacity is a power of two. Masking
     * models disconnected address lines and avoids software division in the
     * ARM interpreter's hottest path. Profile tests enforce that invariant. */
    return (address - bus->machine->ram_base) & (bus->machine->ram_size - 1);
}

static uint32_t flash_offset(address_bus_t *bus, uint32_t address)
{
    return (address - bus->machine->flash_base) & (bus->machine->flash_size - 1);
}

static uint32_t on_chip_offset(address_bus_t *bus, uint32_t address)
{
    return address - bus->machine->on_chip_base;
}

static void log_unmapped(address_bus_t *bus, const char *op, uint32_t address)
{
    if (bus->unmapped_log_count < 200) {
        uint32_t pc = bus->cpu ? bus->cpu->pc : 0;
        fprintf(stderr, "Bus: unmapped %s at 0x%06X (PC=0x%06X)\n", op, address, pc);
        bus->unmapped_log_count++;
        if (bus->unmapped_log_count == 200) {
            fprintf(stderr, "Bus: suppressing further unmapped warnings (200 reached)\n");
        }
    }
}

/* --- Timer16 routing helpers --- */

static int route_timer16_read8(address_bus_t *bus, uint32_t address)
{
    if (address >= 0xFFFEA0 && address <= 0xFFFEAB && bus->timer16[5])
        return timer16_read8(bus->timer16[5], address - 0xFFFEA0);
    if (address >= 0xFFFE90 && address <= 0xFFFE9B && bus->timer16[4])
        return timer16_read8(bus->timer16[4], address - 0xFFFE90);
    if (address >= 0xFFFE80 && address <= 0xFFFE8F && bus->timer16[3])
        return timer16_read8(bus->timer16[3], address - 0xFFFE80);
    if (address >= 0xFFFFD0 && address <= 0xFFFFDF && bus->timer16[0])
        return timer16_read8(bus->timer16[0], address - 0xFFFFD0);
    if (address >= 0xFFFFE0 && address <= 0xFFFFEB && bus->timer16[1])
        return timer16_read8(bus->timer16[1], address - 0xFFFFE0);
    if (address >= 0xFFFFF0 && address <= 0xFFFFFB && bus->timer16[2])
        return timer16_read8(bus->timer16[2], address - 0xFFFFF0);
    return -1;
}

static int route_timer16_read16(address_bus_t *bus, uint32_t address)
{
    if (address >= 0xFFFEA0 && address <= 0xFFFEAB && bus->timer16[5])
        return timer16_read16(bus->timer16[5], address - 0xFFFEA0);
    if (address >= 0xFFFE90 && address <= 0xFFFE9B && bus->timer16[4])
        return timer16_read16(bus->timer16[4], address - 0xFFFE90);
    if (address >= 0xFFFE80 && address <= 0xFFFE8F && bus->timer16[3])
        return timer16_read16(bus->timer16[3], address - 0xFFFE80);
    if (address >= 0xFFFFD0 && address <= 0xFFFFDF && bus->timer16[0])
        return timer16_read16(bus->timer16[0], address - 0xFFFFD0);
    if (address >= 0xFFFFE0 && address <= 0xFFFFEB && bus->timer16[1])
        return timer16_read16(bus->timer16[1], address - 0xFFFFE0);
    if (address >= 0xFFFFF0 && address <= 0xFFFFFB && bus->timer16[2])
        return timer16_read16(bus->timer16[2], address - 0xFFFFF0);
    return -1;
}

static bool route_timer16_write8(address_bus_t *bus, uint32_t address, uint8_t value)
{
    if (address >= 0xFFFEA0 && address <= 0xFFFEAB && bus->timer16[5]) {
        timer16_write8(bus->timer16[5], address - 0xFFFEA0, value); return true;
    }
    if (address >= 0xFFFE90 && address <= 0xFFFE9B && bus->timer16[4]) {
        timer16_write8(bus->timer16[4], address - 0xFFFE90, value); return true;
    }
    if (address >= 0xFFFE80 && address <= 0xFFFE8F && bus->timer16[3]) {
        timer16_write8(bus->timer16[3], address - 0xFFFE80, value); return true;
    }
    if (address >= 0xFFFFD0 && address <= 0xFFFFDF && bus->timer16[0]) {
        timer16_write8(bus->timer16[0], address - 0xFFFFD0, value); return true;
    }
    if (address >= 0xFFFFE0 && address <= 0xFFFFEB && bus->timer16[1]) {
        timer16_write8(bus->timer16[1], address - 0xFFFFE0, value); return true;
    }
    if (address >= 0xFFFFF0 && address <= 0xFFFFFB && bus->timer16[2]) {
        timer16_write8(bus->timer16[2], address - 0xFFFFF0, value); return true;
    }
    return false;
}

static bool route_timer16_write16(address_bus_t *bus, uint32_t address, uint16_t value)
{
    if (address >= 0xFFFEA0 && address <= 0xFFFEAB && bus->timer16[5]) {
        timer16_write16(bus->timer16[5], address - 0xFFFEA0, value); return true;
    }
    if (address >= 0xFFFE90 && address <= 0xFFFE9B && bus->timer16[4]) {
        timer16_write16(bus->timer16[4], address - 0xFFFE90, value); return true;
    }
    if (address >= 0xFFFE80 && address <= 0xFFFE8F && bus->timer16[3]) {
        timer16_write16(bus->timer16[3], address - 0xFFFE80, value); return true;
    }
    if (address >= 0xFFFFD0 && address <= 0xFFFFDF && bus->timer16[0]) {
        timer16_write16(bus->timer16[0], address - 0xFFFFD0, value); return true;
    }
    if (address >= 0xFFFFE0 && address <= 0xFFFFEB && bus->timer16[1]) {
        timer16_write16(bus->timer16[1], address - 0xFFFFE0, value); return true;
    }
    if (address >= 0xFFFFF0 && address <= 0xFFFFFB && bus->timer16[2]) {
        timer16_write16(bus->timer16[2], address - 0xFFFFF0, value); return true;
    }
    return false;
}

/* --- Timer8 routing helpers --- */

static uint8_t read_timer8(address_bus_t *bus, int bus_off)
{
    timer8_t *ch = (bus_off % 2 == 0) ? bus->timer8[0] : bus->timer8[1];
    int reg_off = bus_off & ~1;
    return ch ? timer8_read(ch, reg_off) : 0;
}

static void write_timer8(address_bus_t *bus, int bus_off, uint8_t value)
{
    timer8_t *ch = (bus_off % 2 == 0) ? bus->timer8[0] : bus->timer8[1];
    int reg_off = bus_off & ~1;
    if (ch) timer8_write(ch, reg_off, value);
}

static uint16_t adc_channel_value(const address_bus_t *bus, int channel)
{
    if (bus->machine->model == CYBIKO_XTREME) {
        return 0x0330;
    }

    /* Classic V1 CyOS battery state machine (0x21DD5A) uses raw voltage
     * 2*ch2_avg-ch1_avg. With ch1-ch2 > 15 it subtracts 349 (not 341),
     * clamps the displayed level to [31,78], and stops charging only when
     * raw voltage > 450. 768/610 gives raw=452 and a full, noncharging state.
     * This state needs correct CCR interrupt deferral in the CPU: taking an
     * interrupt during CyOS's task-stack switch caused the former input stall.
     * Samples are 10-bit; the register exposes them left-aligned in 15:6. */
    if (channel == 1) return 0x0300;
    if (channel == 2) return 0x0262;
    return 0x0300;
}

static uint8_t read_adc_data_byte(address_bus_t *bus, uint32_t address)
{
    int reg = (int)(address - 0xFFFF90);
    int channel = reg / 2;
    uint16_t value = adc_channel_value(bus, channel);
    return (reg & 1) ? (uint8_t)(value << 6) : (uint8_t)(value >> 2);
}

static void complete_adc(address_bus_t *bus)
{
    bus->adcsr = (bus->adcsr | 0x80) & (uint8_t)~0x20;
    if ((bus->adcsr & 0x40) && bus->cpu)
        h8s_cpu_request_interrupt(bus->cpu, 28);
}

static int adc_conversion_clocks(const address_bus_t *bus)
{
    /* H8 ADC2245/2319 software-triggered single conversion or channel scan.
     * A scan ends at CH[1:0]; constant input samples need only the final IRQ. */
    bool fast = (bus->adcsr & 8) != 0;
    int first = fast ? 134 : 266;
    int subsequent = fast ? 128 : 256;
    if (bus->machine->model == CYBIKO_XTREME && !(bus->adcr & 8)) {
        first = fast ? 68 : 530;
        subsequent = fast ? 64 : 512;
    }
    return first + ((bus->adcsr & 0x10) ? (bus->adcsr & 3) * subsequent : 0);
}

/* --- Init / Free --- */

void bus_init(address_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->machine = cybiko_machine(CYBIKO_XTREME);
    bus->adcr = 0x7E;
    for (int i = 0; i < 3; ++i) bus->sci_tx_status[i] = SSR_TDRE | SSR_TEND;
}

void bus_free(address_bus_t *bus)
{
    memory_free(&bus->boot_rom);
    memory_free(&bus->external_ram);
    memory_free(&bus->flash_rom);
    memory_free(&bus->on_chip_ram);
}

void bus_clear_code_watches(address_bus_t *bus)
{
    memset(bus->code_page_watched, 0, sizeof(bus->code_page_watched));
    memset(bus->code_page_generation, 0, sizeof(bus->code_page_generation));
}

void bus_watch_code_range(address_bus_t *bus, uint32_t address, unsigned bytes)
{
    if (!bus || bytes == 0) return;
    address &= 0xffffff;
    uint32_t first = address >> 12;
    uint32_t last = ((address + bytes - 1) & 0xffffff) >> 12;
    if (last < first) last = 4095u;
    for (uint32_t page = first; page <= last; ++page)
        bus->code_page_watched[page] = 1;
}

uint32_t bus_code_page_generation(const address_bus_t *bus, uint32_t address)
{
    if (!bus) return 0;
    return bus->code_page_generation[(address & 0xffffff) >> 12];
}

/* --- Read operations --- */

static uint16_t read_keyboard(address_bus_t *bus, uint32_t address)
{
    if (!bus->keyboard) return 0xffff;
    uint16_t value = keyboard_read(bus->keyboard, address);
    /* Classic V2 isolates the Esc sense line when column zero is selected.
     * Other columns' row-one keys must not look like Esc in an all-column scan.
     * Keep a real Esc press visible. See the MIT Java core's readKeyboard. */
    if (bus->machine->model == CYBIKO_CLASSIC_V2 && !(address & 2) &&
        !(bus->keyboard->columns[0] & 2)) value |= 2;
    return value;
}

void bus_build_memory_map(address_bus_t *bus)
{
    memset(bus->read_pages, 0, sizeof(bus->read_pages));
    memset(bus->write_pages, 0, sizeof(bus->write_pages));
    for (uint32_t address = 0; address < 0x1000000; address += 4096) {
        bus_region_t region = decode_region(bus, address);
        if (decode_region(bus, address + 4095) != region) continue;
        memory_t *mem = NULL;
        uint32_t offset = 0;
        switch (region) {
        case REGION_BOOT_ROM: mem = &bus->boot_rom; offset = address & 0x7fff; break;
        case REGION_EXT_RAM: mem = &bus->external_ram; offset = ext_ram_offset(bus, address); break;
        case REGION_FLASH: mem = &bus->flash_rom; offset = flash_offset(bus, address); break;
        case REGION_ON_CHIP:
            if (address + 4095 >= 0xfffc00) continue;
            mem = &bus->on_chip_ram; offset = on_chip_offset(bus, address); break;
        default: continue;
        }
        if (!mem->data || offset + 4096 > mem->size) continue;
        bus->read_pages[address >> 12] = mem->data + offset;
        if (mem->writable && region != REGION_BOOT_ROM)
            bus->write_pages[address >> 12] = mem->data + offset;
    }
}

uint8_t bus_read8_slow(address_bus_t *bus, uint32_t address)
{
    address &= 0xFFFFFF;
    switch (decode_region(bus, address)) {
    case REGION_BOOT_ROM:
        return memory_read8(&bus->boot_rom, address & 0x7FFF);
    case REGION_LCD:
        return bus->lcd ? hd66421_read8(bus->lcd, address & 1) : 0;
    case REGION_USB:
        return 0;
    case REGION_EXT_RAM:
        return memory_read8(&bus->external_ram, ext_ram_offset(bus, address));
    case REGION_FLASH:
        return memory_read8(&bus->flash_rom, flash_offset(bus, address));
    case REGION_KEYBOARD: {
        if (!bus->keyboard) return 0xFF;
        uint16_t kb_val = read_keyboard(bus, address & ~1);
        return (address & 1) == 0 ? (kb_val >> 8) & 0xFF : kb_val & 0xFF;
    }
    case REGION_ON_CHIP:
        return read_on_chip8(bus, address);
    case REGION_UNMAPPED:
        log_unmapped(bus, "read8", address);
        return 0;
    }
    return 0;
}

uint16_t bus_read16_slow(address_bus_t *bus, uint32_t address)
{
    address &= 0xFFFFFF;
    switch (decode_region(bus, address)) {
    case REGION_BOOT_ROM:
        return memory_read16(&bus->boot_rom, address & 0x7FFF);
    case REGION_LCD:
        if (!bus->lcd) return 0;
        return (hd66421_read8(bus->lcd, 0) << 8) | hd66421_read8(bus->lcd, 1);
    case REGION_USB:
        return 0;
    case REGION_EXT_RAM:
        return memory_read16(&bus->external_ram, ext_ram_offset(bus, address));
    case REGION_FLASH:
        return memory_read16(&bus->flash_rom, flash_offset(bus, address));
    case REGION_KEYBOARD:
        if (!bus->keyboard) return 0xFFFF;
        return read_keyboard(bus, address);
    case REGION_ON_CHIP:
        return read_on_chip16(bus, address);
    case REGION_UNMAPPED:
        log_unmapped(bus, "read16", address);
        return 0;
    }
    return 0;
}

uint32_t bus_read32_slow(address_bus_t *bus, uint32_t address)
{
    address &= 0xFFFFFF;
    const cybiko_machine_t *m = bus->machine;
    if (address >= m->ram_base && address + 3 <= m->ram_end) {
        uint32_t offset = ext_ram_offset(bus, address);
        if (offset + 3 < bus->external_ram.size)
            return memory_read32(&bus->external_ram, offset);
    }
    /* Never combine I/O reads: they have ordering and completion side effects.
     * Mirror edges also retain the original two-word semantics. */
    if (address >= m->on_chip_base && address + 3 < 0xFFFC00)
        return memory_read32(&bus->on_chip_ram, on_chip_offset(bus, address));
    return ((uint32_t)bus_read16(bus, address) << 16) | bus_read16(bus, address + 2);
}

/* --- Write operations --- */

void bus_write8_slow(address_bus_t *bus, uint32_t address, uint8_t value)
{
    address &= 0xFFFFFF;
    switch (decode_region(bus, address)) {
    case REGION_BOOT_ROM:
        /* Read-only, ignore */
        break;
    case REGION_LCD:
        if (bus->lcd) hd66421_write8(bus->lcd, address & 1, value);
        break;
    case REGION_USB:
        /* Stub, ignore */
        break;
    case REGION_EXT_RAM:
        memory_write8(&bus->external_ram, ext_ram_offset(bus, address), value);
        bus_note_code_write(bus, address, 1);
        break;
    case REGION_FLASH:
        memory_write8(&bus->flash_rom, flash_offset(bus, address), value);
        bus_note_code_write(bus, address, 1);
        break;
    case REGION_KEYBOARD:
        /* Read-only, ignore */
        break;
    case REGION_ON_CHIP:
        write_on_chip8(bus, address, value);
        break;
    case REGION_UNMAPPED:
        log_unmapped(bus, "write8", address);
        break;
    }
}

void bus_write16_slow(address_bus_t *bus, uint32_t address, uint16_t value)
{
    address &= 0xFFFFFF;
    switch (decode_region(bus, address)) {
    case REGION_BOOT_ROM:
        break;
    case REGION_LCD:
        if (bus->lcd) {
            hd66421_write8(bus->lcd, 0, value >> 8);
            hd66421_write8(bus->lcd, 1, value & 0xFF);
        }
        break;
    case REGION_USB:
        break;
    case REGION_EXT_RAM:
        memory_write16(&bus->external_ram, ext_ram_offset(bus, address), value);
        bus_note_code_write(bus, address, 2);
        break;
    case REGION_FLASH:
        memory_write16(&bus->flash_rom, flash_offset(bus, address), value);
        bus_note_code_write(bus, address, 2);
        break;
    case REGION_KEYBOARD:
        break;
    case REGION_ON_CHIP:
        write_on_chip16(bus, address, value);
        break;
    case REGION_UNMAPPED:
        log_unmapped(bus, "write16", address);
        break;
    }
}

void bus_write32_slow(address_bus_t *bus, uint32_t address, uint32_t value)
{
    address &= 0xFFFFFF;
    const cybiko_machine_t *m = bus->machine;
    if (address >= m->ram_base && address + 3 <= m->ram_end) {
        uint32_t offset = ext_ram_offset(bus, address);
        if (offset + 3 < bus->external_ram.size) {
            memory_write32(&bus->external_ram, offset, value);
            bus_note_code_write(bus, address, 4);
            return;
        }
    }
    if (address >= m->on_chip_base && address + 3 < 0xFFFC00) {
        memory_write32(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        bus_note_code_write(bus, address, 4);
        return;
    }
    bus_write16(bus, address, (uint16_t)(value >> 16));
    bus_write16(bus, address + 2, (uint16_t)(value & 0xFFFF));
}

/* --- On-chip 8-bit read --- */

static uint8_t read_on_chip8(address_bus_t *bus, uint32_t address)
{
    if (address < 0xFFFC00)
        return memory_read8(&bus->on_chip_ram, on_chip_offset(bus, address));
    if (address >= 0xFFFC00 && bus->sync_peripherals) bus->sync_peripherals(bus->sync_ctx);
    /* Timer16 channels (non-contiguous, check first) */
    int t16 = route_timer16_read8(bus, address);
    if (t16 >= 0) return (uint8_t)t16;

    /* Timer8 Channel 0/1: 0xFFFFB0-0xFFFFB9 */
    if (address >= 0xFFFFB0 && address <= 0xFFFFB9) {
        return read_timer8(bus, address - 0xFFFFB0);
    }

    /* TSTR - Timer Start Register */
    if (address == 0xFFFFC0) return bus->tstr;
    /* TSYR stub */
    if (address == 0xFFFFC1) return 0;

    /* IER / ISR */
    if (address == 0xFFFF2E) return bus->ier;
    if (address == 0xFFFF2F) return bus->isr;

    /* SCI SSR registers - always report transmit ready */
    if (address == 0xFFFF7C) return bus->sci_tx_status[0];
    if (address == 0xFFFF84) return SSR_TDRE | SSR_TEND | (bus->sci1_rdrf ? SSR_RDRF : 0);
    if (address == 0xFFFF85) { bus->sci1_rdrf = false; return bus->sci1_rdr; }
    if (address == 0xFFFF8C) return bus->sci_tx_status[2];

    /* DMA registers (XT) */
    if (bus->machine->model == CYBIKO_XTREME && address >= 0xFFFEE0 && address <= 0xFFFEFF) {
        return bus->dma_regs[address - 0xFFFEE0];
    }
    if (bus->machine->model == CYBIKO_XTREME && address >= 0xFFFF00 && address <= 0xFFFF07) {
        return bus->dma_ctrl[address - 0xFFFF00];
    }

    /* System control stubs */
    if (address == 0xFFFF38) return 0;  /* SBYCR */
    if (address == 0xFFFF39) return 0;  /* SYSCR */
    if (address == 0xFFFF3B) return 0;  /* MDCR */

    /* Port Input Data Registers (0xFFFF50-0xFFFF5E) */
    if (address >= 0xFFFF50 && address <= 0xFFFF5E) {
        /* Port A input (0xFFFF59) - power status for XT */
        if (address == 0xFFFF59 && bus->machine->model == CYBIKO_XTREME) return 0xC0;
        if (address == 0xFFFF50 && bus->machine->model != CYBIKO_XTREME) return 0x08;
        /* Port F input (0xFFFF5E) - RTC SDA on bit 6 */
        if (address == 0xFFFF5E) {
            uint8_t ddr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFEBE));
            uint8_t dr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFF6E));
            uint8_t input = bus->machine->model == CYBIKO_XTREME ? 0 : 4;
            if (bus->rtc && rtc_sda_r(bus->rtc)) input |= bus->machine->rtc_sda;
            return (dr & ddr) | (input & ~ddr);
        }
        return memory_read8(&bus->on_chip_ram, on_chip_offset(bus, address));
    }

    /* Port Data Registers (0xFFFF60-0xFFFF6F) */
    if (address >= 0xFFFF60 && address <= 0xFFFF6F) {
        /* Port A DR (0xFFFF69) - power status for XT */
        if (address == 0xFFFF69 && bus->machine->model == CYBIKO_XTREME) return 0xC0;
        /* Port F DR (0xFFFF6E) - RTC SDA on bit 6 */
        if (address == 0xFFFF6E) {
            return memory_read8(&bus->on_chip_ram, on_chip_offset(bus, address));
        }
        return memory_read8(&bus->on_chip_ram, on_chip_offset(bus, address));
    }

    /* Watchdog stub */
    if (address >= 0xFFFFBC && address <= 0xFFFFBF) return 0;

    /* ADC registers (0xFFFF90-0xFFFF99) */
    if (address >= 0xFFFF90 && address <= 0xFFFF99) {
        /* ADC data registers (0xFFFF90-0xFFFF97) */
        if (address <= 0xFFFF97) {
            return read_adc_data_byte(bus, address);
        }
        if (address == 0xFFFF98) return bus->adcsr;
        return bus->adcr;  /* 0xFFFF99 */
    }

    /* Default: on-chip RAM fallthrough */
    return memory_read8(&bus->on_chip_ram, on_chip_offset(bus, address));
}

/* --- On-chip 16-bit read --- */

static uint16_t read_on_chip16(address_bus_t *bus, uint32_t address)
{
    if (address < 0xFFFC00)
        return memory_read16(&bus->on_chip_ram, on_chip_offset(bus, address));
    if (address >= 0xFFFC00 && bus->sync_peripherals) bus->sync_peripherals(bus->sync_ctx);
    /* Timer16 routing first */
    int t16 = route_timer16_read16(bus, address);
    if (t16 >= 0) return (uint16_t)t16;

    /* I/O register region: compose from two 8-bit reads. In particular the
     * ADC sample is left-aligned in bits 15:6 even for a MOV.W access. The
     * H8S2245 map uses addr8_r, not the unrelated raw addr16_r helper. */
    if (address >= 0xFFFE00) {
        return (read_on_chip8(bus, address) << 8) | read_on_chip8(bus, address + 1);
    }

    /* Below 0xFFFE00: direct on-chip RAM read */
    return memory_read16(&bus->on_chip_ram, on_chip_offset(bus, address));
}

/* --- On-chip 8-bit write --- */

/* Classic CyOS's SCI1 DTC descriptor. This implements its byte receive/transmit
 * modes, not a general H8 DTC. Completion is delivered via the real interrupt
 * vector, never by patching a firmware-specific RAM flag. */
static void execute_spi_dtc(address_bus_t *bus, uint8_t scr)
{
    if (!bus->dataflash || bus->dtc_completion_delay) return;
    uint32_t enable = on_chip_offset(bus, 0xFFFF34);
    uint8_t dtcer = memory_read8(&bus->on_chip_ram, enable);
    if (!(dtcer & 2)) return;
    uint32_t desc = on_chip_offset(bus, 0xFFFBD0);
    uint32_t source = memory_read32(&bus->on_chip_ram, desc) & 0xFFFFFF;
    uint32_t dest = memory_read32(&bus->on_chip_ram, desc + 4) & 0xFFFFFF;
    unsigned count = memory_read16(&bus->on_chip_ram, desc + 8);
    uint8_t mode = memory_read8(&bus->on_chip_ram, desc);
    bool receive = mode == 0x20 && (scr & 0x50) == 0x50;
    bool transmit = mode == 0x80 && (scr & 0xA0) == 0xA0;
    if (!count || (!receive && !transmit)) return;
    bus->dtc_completion_delay = 5; /* Also prevents recursive mapped-I/O transfers. */
    /* Clear enable before memory accesses to prevent recursive I/O triggers. */
    memory_write8(&bus->on_chip_ram, enable, dtcer & ~2);
    for (unsigned i = 0; i < count; ++i) {
        if (receive) bus_write8(bus, dest + i, dataflash_transfer(bus->dataflash, 0xFF));
        else bus->sci1_rdr = dataflash_transfer(bus->dataflash, bus_read8(bus, source + i));
    }
    bus->sci1_rdrf = transmit;
    memory_write16(&bus->on_chip_ram, desc + 8, 0);
}

static void update_rtc_pins(address_bus_t *bus)
{
    if (!bus->rtc) return;
    uint8_t ddr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFEBE));
    uint8_t dr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFF6E));
    uint8_t output = dr | (uint8_t)~ddr;
    rtc_scl_w(bus->rtc, (output & 2) != 0);
    /* Firmware uses DDR to emulate open drain: output zero pulls SDA low,
     * input releases it to the pull-up. DR reads must retain the zero latch. */
    rtc_sda_w(bus->rtc, (output & bus->machine->rtc_sda) != 0);
}

static void write_on_chip8(address_bus_t *bus, uint32_t address, uint8_t value)
{
    if (address < 0xFFFC00) {
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        bus_note_code_write(bus, address, 1);
        return;
    }
    if (address >= 0xFFFC00 && bus->sync_peripherals) bus->sync_peripherals(bus->sync_ctx);
    /* Timer16 routing (check first, non-contiguous addresses) */
    if (route_timer16_write8(bus, address, value)) return;

    /* Timer8 Channel 0/1: 0xFFFFB0-0xFFFFB9 */
    if (address >= 0xFFFFB0 && address <= 0xFFFFB9) {
        write_timer8(bus, address - 0xFFFFB0, value);
        return;
    }

    /* TSTR - Timer Start Register (0xFFFFC0) */
    if (address == 0xFFFFC0) {
        bus->tstr = value & 0xFF;
        if (bus->timer16[0]) timer16_set_enabled(bus->timer16[0], (bus->tstr & 0x01) != 0);
        if (bus->timer16[1]) timer16_set_enabled(bus->timer16[1], (bus->tstr & 0x02) != 0);
        if (bus->timer16[2]) timer16_set_enabled(bus->timer16[2], (bus->tstr & 0x04) != 0);
        if (bus->timer16[3]) timer16_set_enabled(bus->timer16[3], (bus->tstr & 0x08) != 0);
        if (bus->timer16[4]) timer16_set_enabled(bus->timer16[4], (bus->tstr & 0x10) != 0);
        if (bus->timer16[5]) timer16_set_enabled(bus->timer16[5], (bus->tstr & 0x20) != 0);
        return;
    }

    /* IER */
    if (address == 0xFFFF2E) { bus->ier = value & 0xFF; return; }
    /* ISR - write-0-to-clear */
    if (address == 0xFFFF2F) { bus->isr &= value; return; }

    /* SCI registers (0xFFFF78-0xFFFF8E) */
    if (address >= 0xFFFF78 && address <= 0xFFFF8E) {
        int channel, reg;
        if (address >= 0xFFFF88) {
            channel = 2; reg = address - 0xFFFF88;
        } else if (address >= 0xFFFF80) {
            channel = 1; reg = address - 0xFFFF80;
        } else {
            channel = 0; reg = address - 0xFFFF78;
        }
        /* TDR - Transmit Data Register (reg offset 3) */
        if (reg == 3 && channel == 1 && bus->dataflash) {
            bus->sci1_rdr = dataflash_transfer(bus->dataflash, value);
            bus->sci1_rdrf = true;
        } else if (reg == 3) {
            char c = (char)(value & 0x7F);
            if (c >= 0x20 || c == '\n') {
                /* printable or newline */
            } else {
                c = '.';
            }
            if (bus->sci_output_pos[channel] < 255) {
                bus->sci_output[channel][bus->sci_output_pos[channel]++] = c;
                bus->sci_output[channel][bus->sci_output_pos[channel]] = '\0';
            }
        }
        /* Store to on-chip RAM regardless */
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        if (channel != 1) {
            uint32_t base = 0xFFFF78 + 8 * (uint32_t)channel;
            uint8_t scr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, base + 2));
            if (reg == 4 && !(value & SSR_TDRE) && (scr & 0x20) &&
                (bus->sci_tx_status[channel] & SSR_TDRE)) {
                /* CyOS hands TDR to the shift register by clearing TDRE.
                 * Do not leave TXI permanently asserted or silently drop it:
                 * the Classic console uses TX interrupts to drain its ring. */
                uint8_t smr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, base));
                uint8_t brr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, base + 1));
                bus->sci_tx_status[channel] &= ~(SSR_TDRE | SSR_TEND);
                bus->sci_tx_delay[channel] = (uint32_t)(brr + 1) *
                    ((smr & 0x80) ? 32u : 320u) * (1u << (2 * (smr & 3)));
            }
            if (reg == 2 || reg == 4) {
                int vector = 82 + 4 * channel;
                if (bus->cpu) {
                    if ((scr & 0xA0) == 0xA0 && (bus->sci_tx_status[channel] & SSR_TDRE))
                        h8s_cpu_request_interrupt(bus->cpu, vector);
                    else h8s_cpu_cancel_interrupt(bus->cpu, vector);
                }
            }
        }
        if (channel == 1 && reg == 4 && !(value & SSR_RDRF)) bus->sci1_rdrf = false;
        if (channel == 1 && reg == 2) execute_spi_dtc(bus, value);
        return;
    }

    /* Port DDR registers (0xFFFEB0-0xFFFEBF) */
    if (address >= 0xFFFEB0 && address <= 0xFFFEBF) {
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        if (address == 0xFFFEBE) update_rtc_pins(bus);
        if (address == 0xFFFEB2 && bus->dataflash) {
            uint8_t dr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFF62));
            dataflash_select(bus->dataflash, (value & 0x10) && !(dr & 0x10));
        }
        return;
    }

    if (address == 0xFFFF62 && bus->dataflash) {
        uint8_t ddr = memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFEB2));
        dataflash_select(bus->dataflash, (ddr & 0x10) && !(value & 0x10));
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        return;
    }
    if (address == 0xFFFF34 && bus->dataflash) {
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        execute_spi_dtc(bus, memory_read8(&bus->on_chip_ram, on_chip_offset(bus, 0xFFFF82)));
        return;
    }

    /* Port 1 write (0xFFFF60) - bit 3 drives speaker */
    if (address == 0xFFFF60) {
        int level = (value & 0x08) != 0 ? 1 : 0;
        if (bus->speaker) {
            speaker_set_level(bus->speaker, level);
        }
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        return;
    }

    /* Port F write (0xFFFF6E) - I2C RTC bit-banging */
    if (address == 0xFFFF6E) {
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        update_rtc_pins(bus);
        return;
    }

    /* Port data register writes (0xFFFF60-0xFFFF6F) - catch remaining ports */
    if (address >= 0xFFFF60 && address <= 0xFFFF6F) {
        memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        return;
    }

    /* DMA registers (XT) */
    if (bus->machine->model == CYBIKO_XTREME && address >= 0xFFFEE0 && address <= 0xFFFEFF) {
        bus->dma_regs[address - 0xFFFEE0] = value & 0xFF;
        return;
    }
    if (bus->machine->model == CYBIKO_XTREME && address >= 0xFFFF00 && address <= 0xFFFF07) {
        int idx = address - 0xFFFF00;
        uint8_t old_val = bus->dma_ctrl[idx];
        bus->dma_ctrl[idx] = value & 0xFF;

        /* DMA trigger edge detection on index 7 */
        if (idx == 7) {
            bool ch0_triggered = false;
            /* Channel 0: bit 4 (0x10) or bit 5 (0x20) rising edge */
            if ((value & 0x10) && !(old_val & 0x10)) {
                execute_dma_transfer(bus, 0);
                ch0_triggered = true;
            }
            if (!ch0_triggered && (value & 0x20) && !(old_val & 0x20)) {
                execute_dma_transfer(bus, 0);
            }
            bool ch1_triggered = false;
            /* Channel 1: bit 6 (0x40) or bit 7 (0x80) rising edge */
            if ((value & 0x40) && !(old_val & 0x40)) {
                execute_dma_transfer(bus, 1);
                ch1_triggered = true;
            }
            if (!ch1_triggered && (value & 0x80) && !(old_val & 0x80)) {
                execute_dma_transfer(bus, 1);
            }
        }
        return;
    }

    /* System control, watchdog stubs */
    if (address == 0xFFFF38 || address == 0xFFFF39) return;
    if (address >= 0xFFFFBC && address <= 0xFFFFBF) return;

    /* ADC registers */
    if (address >= 0xFFFF90 && address <= 0xFFFF99) {
        if (address == 0xFFFF98) {
            uint8_t previous = bus->adcsr;
            bus->adcsr = (value & 0x7F) | (previous & value & 0x80);
            if (!(bus->adcsr & 0x20)) bus->adc_completion_delay = 0;
            else if (!(previous & 0x20))
                bus->adc_completion_delay = adc_conversion_clocks(bus);
            if (!(bus->adcsr & 0x80) && bus->cpu)
                h8s_cpu_cancel_interrupt(bus->cpu, 28);
        } else if (address == 0xFFFF99) {
            bus->adcr = value & 0xFF;
        }
        return;
    }

    /* Default: write to on-chip RAM */
    memory_write8(&bus->on_chip_ram, on_chip_offset(bus, address), value);
    bus_note_code_write(bus, address, 1);
}

/* --- On-chip 16-bit write --- */

static void write_on_chip16(address_bus_t *bus, uint32_t address, uint16_t value)
{
    if (address < 0xFFFC00) {
        memory_write16(&bus->on_chip_ram, on_chip_offset(bus, address), value);
        bus_note_code_write(bus, address, 2);
        return;
    }
    if (address >= 0xFFFC00 && bus->sync_peripherals) bus->sync_peripherals(bus->sync_ctx);
    /* Timer16 routing first */
    if (route_timer16_write16(bus, address, value)) return;

    /* I/O register region: split into two 8-bit writes */
    if (address >= 0xFFFE00) {
        write_on_chip8(bus, address, (value >> 8) & 0xFF);
        write_on_chip8(bus, address + 1, value & 0xFF);
        return;
    }

    /* Below 0xFFFE00: direct on-chip RAM write */
    memory_write16(&bus->on_chip_ram, on_chip_offset(bus, address), value);
    bus_note_code_write(bus, address, 2);
}

/* --- DMA transfer (XT) --- */

static void execute_dma_transfer(address_bus_t *bus, int channel)
{
    int base = channel * 16;
    uint32_t src_addr = ((uint32_t)bus->dma_regs[base] << 24)
                      | ((uint32_t)bus->dma_regs[base + 1] << 16)
                      | ((uint32_t)bus->dma_regs[base + 2] << 8)
                      | bus->dma_regs[base + 3];
    uint32_t dst_addr = ((uint32_t)bus->dma_regs[base + 8] << 24)
                      | ((uint32_t)bus->dma_regs[base + 9] << 16)
                      | ((uint32_t)bus->dma_regs[base + 10] << 8)
                      | bus->dma_regs[base + 11];
    int count = (bus->dma_regs[base + 6] << 8) | bus->dma_regs[base + 7];

    uint8_t dmacr_h = bus->dma_ctrl[2 + channel * 2];
    uint8_t dmacr_l = bus->dma_ctrl[3 + channel * 2];
    bool mode16    = (dmacr_h & 0x80) != 0;  /* Bit 15: transfer size      */
    bool src_dec   = (dmacr_h & 0x40) != 0;  /* Bit 14: source decrement   */
    bool src_inc   = (dmacr_h & 0x20) != 0;  /* Bit 13: source inc enable  */
    bool dst_dec   = (dmacr_l & 0x40) != 0;  /* Bit  6: dest decrement     */
    bool dst_inc   = (dmacr_l & 0x20) != 0;  /* Bit  5: dest inc enable    */

    int step = mode16 ? 2 : 1;
    int src_step = src_inc ? (src_dec ? -step : step) : 0;
    int dst_step = dst_inc ? (dst_dec ? -step : step) : 0;

    src_addr &= 0xFFFFFF;
    dst_addr &= 0xFFFFFF;

    if (count == 0) {
        if (channel == 1) bus->dma_ctrl[7] &= ~0xC0;
        else bus->dma_ctrl[7] &= ~0x30;
        return;
    }

    bus->in_dma = true;
    if (mode16) {
        for (int i = 0; i < count; i++) {
            uint16_t val = bus_read16(bus, src_addr);
            bus_write16(bus, dst_addr, val);
            src_addr = (src_addr + src_step) & 0xFFFFFF;
            dst_addr = (dst_addr + dst_step) & 0xFFFFFF;
        }
    } else {
        for (int i = 0; i < count; i++) {
            uint8_t val = bus_read8(bus, src_addr);
            bus_write8(bus, dst_addr, val);
            src_addr = (src_addr + src_step) & 0xFFFFFF;
            dst_addr = (dst_addr + dst_step) & 0xFFFFFF;
        }
    }

    bus->in_dma = false;

    /* Clear trigger bits and count */
    if (channel == 1) bus->dma_ctrl[7] &= ~0xC0;
    else bus->dma_ctrl[7] &= ~0x30;
    bus->dma_regs[base + 6] = 0;
    bus->dma_regs[base + 7] = 0;
}

/* --- Tick functions --- */

void bus_tick_dma_completion(address_bus_t *bus)
{
    if (bus->adc_completion_delay > 0 && --bus->adc_completion_delay == 0)
        complete_adc(bus);
    for (int channel = 0; channel < 3; channel += 2) {
        if (bus->sci_tx_delay[channel] && --bus->sci_tx_delay[channel] == 0) {
            bus->sci_tx_status[channel] |= SSR_TDRE | SSR_TEND;
            uint8_t scr = memory_read8(&bus->on_chip_ram,
                on_chip_offset(bus, 0xFFFF7A + 8 * (uint32_t)channel));
            if (bus->cpu && (scr & 0xA0) == 0xA0)
                h8s_cpu_request_interrupt(bus->cpu, 82 + 4 * channel);
        }
    }
    if (bus->dtc_completion_delay > 0 && --bus->dtc_completion_delay == 0 && bus->cpu)
        h8s_cpu_request_interrupt(bus->cpu, 85);
    if (bus->dma_completion_delay > 0) {
        bus->dma_completion_delay--;
        if (bus->dma_completion_delay == 0 && bus->cpu) {
            h8s_cpu_request_interrupt(bus->cpu, bus->dma_completion_vector);
        }
    }
}

int bus_cycles_until_dma_completion(const address_bus_t *bus)
{
    int next = bus->adc_completion_delay;
    for (int channel = 0; channel < 3; channel += 2) {
        int delay = (int)bus->sci_tx_delay[channel];
        if (delay > 0 && (next == 0 || delay < next)) next = delay;
    }
    if (bus->dtc_completion_delay > 0 &&
        (next == 0 || bus->dtc_completion_delay < next))
        next = bus->dtc_completion_delay;
    if (bus->dma_completion_delay > 0 &&
        (next == 0 || bus->dma_completion_delay < next))
        next = bus->dma_completion_delay;
    return next;
}

void bus_advance_dma_completion(address_bus_t *bus, int cycles)
{
    if (cycles <= 0) return;
    if (bus->adc_completion_delay > 0) {
        if (cycles < bus->adc_completion_delay) bus->adc_completion_delay -= cycles;
        else { bus->adc_completion_delay = 0; complete_adc(bus); }
    }
    for (int channel = 0; channel < 3; channel += 2) {
        if (!bus->sci_tx_delay[channel]) continue;
        if ((uint32_t)cycles < bus->sci_tx_delay[channel]) {
            bus->sci_tx_delay[channel] -= (uint32_t)cycles;
            continue;
        }
        bus->sci_tx_delay[channel] = 0;
        bus->sci_tx_status[channel] |= SSR_TDRE | SSR_TEND;
        uint8_t scr = memory_read8(&bus->on_chip_ram,
            on_chip_offset(bus, 0xFFFF7A + 8 * (uint32_t)channel));
        if (bus->cpu && (scr & 0xA0) == 0xA0)
            h8s_cpu_request_interrupt(bus->cpu, 82 + 4 * channel);
    }
    if (bus->dtc_completion_delay > 0) {
        if (cycles < bus->dtc_completion_delay) {
            bus->dtc_completion_delay -= cycles;
        } else {
            bus->dtc_completion_delay = 0;
            if (bus->cpu) h8s_cpu_request_interrupt(bus->cpu, 85);
        }
    }
    if (bus->dma_completion_delay > 0) {
        if (cycles < bus->dma_completion_delay) {
            bus->dma_completion_delay -= cycles;
        } else {
            bus->dma_completion_delay = 0;
            if (bus->cpu)
                h8s_cpu_request_interrupt(bus->cpu, bus->dma_completion_vector);
        }
    }
}

void bus_tick_rtc(address_bus_t *bus)
{
    if (bus->rtc) {
        rtc_tick(bus->rtc);
    }
}

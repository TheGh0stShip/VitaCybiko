#include "acutest.h"
#include "core/emulator.h"
#include "core/address_bus.h"
#include "core/h8s_cpu.h"
#include <stdlib.h>
#include <string.h>

static void test_profiles_and_storage(void)
{
    cybiko_hal_t hal = {0};
    TEST_CHECK(cybiko_create_model(&hal, CYBIKO_MODEL_COUNT) == NULL);
    TEST_CHECK(cybiko_machine((cybiko_model_t)-1) == NULL);
    uint8_t *serial = malloc(DATAFLASH_SIZE);
    TEST_ASSERT(serial != NULL);
    memset(serial, 0x35, DATAFLASH_SIZE);
    for (int model = 0; model < CYBIKO_MODEL_COUNT; ++model) {
        const cybiko_machine_t *m = cybiko_machine((cybiko_model_t)model);
        TEST_CHECK(m->ram_size && !(m->ram_size & (m->ram_size - 1)));
        TEST_CHECK(!m->flash_size || !(m->flash_size & (m->flash_size - 1)));
        cybiko_emu_t *emu = cybiko_create_model(&hal, (cybiko_model_t)model);
        TEST_ASSERT(emu != NULL);
        TEST_CHECK(cybiko_get_model(emu) == (cybiko_model_t)model);
        size_t size;
        TEST_CHECK(cybiko_get_nvram(emu, &size) != NULL);
        TEST_CHECK(size == m->ram_size);
        bool classic = model != CYBIKO_XTREME;
        TEST_CHECK(cybiko_load_dataflash(emu, serial, DATAFLASH_SIZE) == classic);
        TEST_CHECK(!cybiko_load_dataflash(emu, serial, DATAFLASH_SIZE - 1));
        const uint8_t *data = cybiko_get_dataflash(emu, &size);
        TEST_CHECK(size == (classic ? DATAFLASH_SIZE : 0));
        if (classic) TEST_CHECK(memcmp(data, serial, DATAFLASH_SIZE) == 0);
        TEST_CHECK(!cybiko_check_firmware((cybiko_model_t)model, serial, 32768, serial, m->flash_size));
        TEST_CHECK(!cybiko_load_nvram(emu, serial, m->ram_size + 1));
        cybiko_destroy(emu);
    }
    free(serial);
}

static void flash_command(dataflash_t *f, uint8_t opcode, unsigned page, unsigned pos)
{
    dataflash_select(f, false);
    dataflash_select(f, true);
    dataflash_transfer(f, opcode);
    dataflash_transfer(f, (uint8_t)(page >> 7));
    dataflash_transfer(f, (uint8_t)((page << 1) | (pos >> 8)));
    dataflash_transfer(f, (uint8_t)pos);
}

static void test_dataflash_protocol(void)
{
    dataflash_t *f = malloc(sizeof(*f));
    TEST_ASSERT(f != NULL);
    dataflash_init(f);
    TEST_CHECK(dataflash_transfer(f, 0x57) == 0xFF);
    dataflash_select(f, true);
    dataflash_transfer(f, 0x57);
    TEST_CHECK(dataflash_transfer(f, 0xFF) == 0x98);
    flash_command(f, 0x82, 2047, 263);
    dataflash_transfer(f, 0x12);
    dataflash_transfer(f, 0x34); /* Wraps to beginning of the same page. */
    TEST_CHECK(f->data[DATAFLASH_SIZE - 1] == 0xFF);
    dataflash_select(f, false);
    TEST_CHECK(f->data[DATAFLASH_SIZE - 1] == 0x12);
    TEST_CHECK(f->data[2047 * DATAFLASH_PAGE_SIZE] == 0x34);
    TEST_CHECK(f->data[0] == 0xFF);
    flash_command(f, 0x52, 2047, 263);
    for (int i = 0; i < 4; ++i) dataflash_transfer(f, 0);
    TEST_CHECK(dataflash_transfer(f, 0) == 0x12);
    TEST_CHECK(dataflash_transfer(f, 0) == 0x34);
    flash_command(f, 0x60, 2047, 0);
    TEST_CHECK(!(f->status & 0x40));
    flash_command(f, 0x60, 0, 0);
    TEST_CHECK(f->status & 0x40);
    /* Non-power-of-two pages: malformed byte address cannot escape the buffer. */
    flash_command(f, 0x82, 2047, 511);
    for (unsigned i = 0; i < 600; ++i) dataflash_transfer(f, 0xA5);
    dataflash_select(f, false);
    TEST_CHECK(f->data[DATAFLASH_SIZE - 1] == 0xA5);
    free(f);
}

static void test_model_address_maps(void)
{
    for (int model = 0; model < CYBIKO_MODEL_COUNT; ++model) {
        address_bus_t bus;
        bus_init(&bus);
        bus.machine = cybiko_machine((cybiko_model_t)model);
        const cybiko_machine_t *m = bus.machine;
        memory_init(&bus.boot_rom, 32768, false);
        memory_init(&bus.external_ram, m->ram_size, true);
        memory_init(&bus.flash_rom, m->flash_size ? m->flash_size : 1, false);
        memory_init(&bus.on_chip_ram, 0x1000000 - m->on_chip_base, true);
        hd66421_t lcd;
        hd66421_init(&lcd);
        bus.lcd = &lcd;
        keyboard_t keyboard;
        keyboard_init(&keyboard, m->keyboard_columns);
        bus.keyboard = &keyboard;
        bus_write32(&bus, m->ram_base + 4, 0xAABBCCDD);
        TEST_CHECK(bus_read32(&bus, m->ram_base + 4) == 0xAABBCCDD);
        if (model == CYBIKO_CLASSIC_V2)
            TEST_CHECK(bus_read32(&bus, m->ram_base + m->ram_size + 4) == 0xAABBCCDD);
        bus_write8(&bus, m->on_chip_base + 8, 0x57);
        TEST_CHECK(bus_read8(&bus, m->on_chip_base + 8) == 0x57);
        if (m->flash_size) {
            bus.flash_rom.data[4] = 0xA3;
            TEST_CHECK(bus_read8(&bus, m->flash_base + m->flash_size + 4) == 0xA3);
            bus_write8(&bus, m->flash_base + 4, 0xF0);
            TEST_CHECK(bus_read8(&bus, m->flash_base + 4) == 0xA3);
        }
        keyboard_set_key(&keyboard, 0, 2, true);
        TEST_CHECK(bus_read16(&bus, 0xE00000) == 0xFFFD);
        TEST_CHECK(bus_read16(&bus, 0xE00002) == 0xFFFF);
        TEST_CHECK(bus_read8(&bus, model == CYBIKO_XTREME ? 0xFFFF59 : 0xFFFF50) ==
                   (model == CYBIKO_XTREME ? 0xC0 : 0x08));
        bus_free(&bus);
    }
}

static void test_longword_memory_paths(void)
{
    for (int model = 0; model < CYBIKO_MODEL_COUNT; ++model) {
        address_bus_t a, b;
        bus_init(&a); bus_init(&b);
        const cybiko_machine_t *m = cybiko_machine(model);
        a.machine = b.machine = m;
        for (int instance = 0; instance < 2; ++instance) {
            address_bus_t *bus = instance ? &b : &a;
            memory_init(&bus->boot_rom, 32768, false);
            memory_init(&bus->external_ram, m->ram_size, true);
            memory_init(&bus->flash_rom, m->flash_size ? m->flash_size : 1, false);
            memory_init(&bus->on_chip_ram, 0x1000000 - m->on_chip_base, true);
            bus->unmapped_log_count = 200;
            memset(bus->boot_rom.data, 0xa7, bus->boot_rom.size);
            memset(bus->external_ram.data, 0x59, bus->external_ram.size);
            memset(bus->flash_rom.data, 0x31, bus->flash_rom.size);
            memset(bus->on_chip_ram.data, 0xc2, bus->on_chip_ram.size);
        }
        bus_build_memory_map(&a); /* b deliberately retains the slow router. */
        TEST_CHECK(a.read_pages[m->ram_base >> 12] == a.external_ram.data);
        TEST_CHECK(a.write_pages[0] == NULL);
        TEST_CHECK(a.read_pages[4095] == NULL); /* Never cache I/O. */
        for (uint32_t page = 0; page < 4096; ++page) {
            if (!a.read_pages[page]) continue;
            unsigned offsets[] = {0, 1, 2, 3, 2047, 4092, 4093, 4094, 4095};
            for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
                uint32_t address = page * 4096 + offsets[i];
                TEST_CHECK(bus_read8(&a, address) == bus_read8(&b, address));
                TEST_CHECK(bus_read16(&a, address) == bus_read16(&b, address));
                TEST_CHECK(bus_read32(&a, address) == bus_read32(&b, address));
                uint32_t value = address * 0x9e3779b9u;
                bus_write8(&a, address, value); bus_write8(&b, address, value);
                bus_write16(&a, address, value); bus_write16(&b, address, value);
                bus_write32(&a, address, value); bus_write32(&b, address, value);
            }
        }
        TEST_CHECK(!memcmp(a.external_ram.data, b.external_ram.data, m->ram_size));
        TEST_CHECK(!memcmp(a.on_chip_ram.data, b.on_chip_ram.data, a.on_chip_ram.size));
        TEST_CHECK(!memcmp(a.boot_rom.data, b.boot_rom.data, a.boot_rom.size));
        TEST_CHECK(!memcmp(a.flash_rom.data, b.flash_rom.data, a.flash_rom.size));
        uint32_t edges[] = {0, 32768, m->boot_end + 1, m->ram_base, m->ram_base + 4096,
            m->ram_base + m->ram_size, m->ram_end + 1, m->on_chip_base,
            0xfffc00, m->flash_base, m->flash_base + m->flash_size,
            m->flash_end + 1, 0x1000000};
        for (size_t edge = 0; edge < sizeof(edges) / sizeof(edges[0]); ++edge) {
            for (int delta = -6; delta <= 6; ++delta) {
                uint32_t address = (edges[edge] + delta) & 0xffffff;
                /* Do not compare side-effectful I/O against a repeated read. */
                if (address >= 0xfffc00) continue;
                uint32_t expected = (uint32_t)bus_read16(&b, address) << 16;
                expected |= bus_read16(&b, address + 2);
                TEST_CHECK_(bus_read32(&a, address) == expected,
                            "model=%d address=%06x", model, address);
                uint32_t value = address * 0x9e3779b9u;
                bus_write32(&a, address, value);
                bus_write16(&b, address, value >> 16);
                bus_write16(&b, address + 2, value);
                TEST_CHECK(!memcmp(a.external_ram.data, b.external_ram.data, m->ram_size));
                TEST_CHECK(!memcmp(a.on_chip_ram.data, b.on_chip_ram.data, a.on_chip_ram.size));
                TEST_CHECK(!memcmp(a.boot_rom.data, b.boot_rom.data, a.boot_rom.size));
                TEST_CHECK(!memcmp(a.flash_rom.data, b.flash_rom.data, a.flash_rom.size));
            }
        }
        bus_free(&a); bus_free(&b);
    }
}

static void test_battery_adc_registers_and_conversion(void)
{
    for (int model = 0; model < CYBIKO_MODEL_COUNT; ++model) {
        address_bus_t bus;
        bus_init(&bus);
        bus.machine = cybiko_machine((cybiko_model_t)model);
        memory_init(&bus.on_chip_ram, 0x1000000 - bus.machine->on_chip_base, true);
        uint16_t ch1 = bus_read16(&bus, 0xFFFF92);
        uint16_t ch2 = bus_read16(&bus, 0xFFFF94);
        if (model == CYBIKO_XTREME) {
            TEST_CHECK(ch1 == 0xCC00);
            TEST_CHECK(ch2 == 0xCC00);
            TEST_CHECK(bus_read8(&bus, 0xFFFF92) == 0xCC);
            TEST_CHECK(bus_read8(&bus, 0xFFFF93) == 0x00);
        } else {
            TEST_CHECK(ch1 == 0xC000);
            TEST_CHECK(ch2 == 0x9880);
            TEST_CHECK(bus_read8(&bus, 0xFFFF92) == 0xC0);
            TEST_CHECK(bus_read8(&bus, 0xFFFF93) == 0x00);
            TEST_CHECK(bus_read8(&bus, 0xFFFF94) == 0x98);
            TEST_CHECK(bus_read8(&bus, 0xFFFF95) == 0x80);
            TEST_CHECK(ch1 > ch2 + 0x00F0);
            /* Reproduce CyOS's filtered level, not just its charging check. */
            /* Initial valid conversion seeds the filter; subsequent updates
             * round using +3 before arithmetic shift, as the firmware does. */
            int avg1 = ch1 >> 6, avg2 = ch2 >> 6;
            for (int sample = 0; sample < 32; ++sample) {
                avg1 = (7 * avg1 + (ch1 >> 6) + 3) / 8;
                avg2 = (7 * avg2 + (ch2 >> 6) + 3) / 8;
            }
            TEST_CHECK(avg1 - avg2 > 15);
            TEST_CHECK(2 * avg2 - avg1 > 450); /* charge-complete threshold */
            TEST_CHECK(2 * avg2 - avg1 - 349 >= 78); /* clamped full display */
        }
        /* Access width must not change the hardware register's contents. */
        for (uint32_t address = 0xFFFF90; address <= 0xFFFF96; address += 2) {
            uint16_t bytes = ((uint16_t)bus_read8(&bus, address) << 8) |
                             bus_read8(&bus, address + 1);
            TEST_CHECK(bus_read16(&bus, address) == bytes);
        }
        h8s_cpu_t cpu;
        h8s_cpu_init(&cpu, &bus);
        bus.cpu = &cpu;
        bus_write8(&bus, 0xFFFF98, 0x20 | 2);
        TEST_CHECK(cpu.pending_irq_count == 0); /* ADIE disabled. */
        TEST_CHECK(bus_cycles_until_dma_completion(&bus) == 266);
        bus_advance_dma_completion(&bus, 265);
        TEST_CHECK(!(bus_read8(&bus, 0xFFFF98) & 0x80));
        TEST_CHECK(bus_read8(&bus, 0xFFFF98) & 0x20);
        bus_tick_dma_completion(&bus);
        TEST_CHECK(bus_read8(&bus, 0xFFFF98) & 0x80);
        TEST_CHECK(!(bus_read8(&bus, 0xFFFF98) & 0x20));
        bus_write8(&bus, 0xFFFF98, 0);
        TEST_CHECK(!(bus_read8(&bus, 0xFFFF98) & 0x80));
        bus_write8(&bus, 0xFFFF98, 0x60 | 2);
        TEST_CHECK(cpu.pending_irq_count == 0);
        bus_advance_dma_completion(&bus, 266);
        TEST_CHECK(cpu.pending_irq_count == 1);
        TEST_CHECK(cpu.pending_irqs[0] == 28);
        bus_write8(&bus, 0xFFFF98, 0x72); /* Scan channels 0..2. */
        TEST_CHECK(cpu.pending_irq_count == 0);
        TEST_CHECK(bus_cycles_until_dma_completion(&bus) == 778);
        bus_advance_dma_completion(&bus, 777);
        TEST_CHECK(cpu.pending_irq_count == 0);
        bus_tick_dma_completion(&bus);
        TEST_CHECK(cpu.pending_irq_count == 1);
        bus_write8(&bus, 0xFFFF98, 0x62);
        bus_write8(&bus, 0xFFFF98, 0); /* Abort conversion. */
        bus_advance_dma_completion(&bus, 1000);
        TEST_CHECK(cpu.pending_irq_count == 0);
        TEST_CHECK(!(bus_read8(&bus, 0xFFFF98) & 0x80));
        bus_free(&bus);
    }
}

static void test_classic_spi_bus_and_dtc(void)
{
    address_bus_t bus;
    bus_init(&bus);
    bus.machine = cybiko_machine(CYBIKO_CLASSIC_V2);
    memory_init(&bus.on_chip_ram, 0x2400, true);
    memory_init(&bus.external_ram, 0x40000, true);
    bus.dataflash = malloc(sizeof(*bus.dataflash));
    TEST_ASSERT(bus.dataflash != NULL);
    dataflash_init(bus.dataflash);
    for (int i = 0; i < 4; ++i) bus.dataflash->data[i] = (uint8_t)(0x50 + i);
    h8s_cpu_t cpu;
    h8s_cpu_init(&cpu, &bus);
    bus.cpu = &cpu;
    bus_write8(&bus, 0xFFFF62, 0x10); /* deselect */
    bus_write8(&bus, 0xFFFEB2, 0x10); /* pin direction = output */
    TEST_CHECK(!bus.dataflash->selected);
    bus_write8(&bus, 0xFFFF62, 0);
    TEST_CHECK(bus.dataflash->selected);
    bus_write8(&bus, 0xFFFF83, 0x57);
    bus_write8(&bus, 0xFFFF83, 0xFF);
    TEST_CHECK(bus_read8(&bus, 0xFFFF84) & 0x40);
    TEST_CHECK(bus_read8(&bus, 0xFFFF85) == 0x98);
    TEST_CHECK(!(bus_read8(&bus, 0xFFFF84) & 0x40));
    bus_write8(&bus, 0xFFFF62, 0x10);
    bus_write8(&bus, 0xFFFF62, 0);
    bus_write8(&bus, 0xFFFF83, 0x52);
    for (int i = 0; i < 7; ++i) bus_write8(&bus, 0xFFFF83, 0);
    bus_write32(&bus, 0xFFFBD0, 0x20FFFF85);
    bus_write32(&bus, 0xFFFBD4, 0x00200200);
    bus_write16(&bus, 0xFFFBD8, 4);
    bus_write8(&bus, 0xFFFF34, 2);
    bus_write8(&bus, 0xFFFF82, 0x50);
    TEST_CHECK(bus_read32(&bus, 0x200200) == 0x50515253);
    TEST_CHECK(bus_read16(&bus, 0xFFFBD8) == 0);
    TEST_CHECK((bus_read8(&bus, 0xFFFF34) & 2) == 0);
    TEST_CHECK(cpu.pending_irq_count == 0);
    for (int i = 0; i < 5; ++i) bus_tick_dma_completion(&bus);
    TEST_CHECK(cpu.pending_irq_count == 1);
    TEST_CHECK(cpu.pending_irqs[0] == 85);
    free(bus.dataflash);
    bus_free(&bus);
}

static void test_serial_transmit_interrupts(void)
{
    address_bus_t bus;
    h8s_cpu_t cpu;
    bus_init(&bus);
    memory_init(&bus.on_chip_ram, 0x2400, true);
    h8s_cpu_init(&cpu, &bus);
    bus.cpu = &cpu;
    for (int channel = 0; channel < 3; channel += 2) {
        uint32_t base = 0xFFFF78 + 8 * (uint32_t)channel;
        int vector = 82 + 4 * channel;
        TEST_CHECK(bus_read8(&bus, base + 4) == 0x84);
        bus_write8(&bus, base + 2, 0xA0); /* TE + TIE: initial empty TDR. */
        TEST_CHECK(cpu.pending_irq_count == 1);
        TEST_CHECK(cpu.pending_irqs[0] == vector);
        bus_write8(&bus, base + 3, 'A');
        bus_write8(&bus, base + 4, 0); /* Hand byte to transmitter. */
        TEST_CHECK(cpu.pending_irq_count == 0);
        TEST_CHECK((bus_read8(&bus, base + 4) & 0x84) == 0);
        for (int n = 0; n < 319; ++n) bus_tick_dma_completion(&bus);
        TEST_CHECK(cpu.pending_irq_count == 0);
        bus_tick_dma_completion(&bus);
        TEST_CHECK(bus_read8(&bus, base + 4) == 0x84);
        TEST_CHECK(cpu.pending_irq_count == 1);
        TEST_CHECK(cpu.pending_irqs[0] == vector);
        TEST_CHECK(cpu.ccr & CCR_I); /* Request survives CPU masking. */
        bus_write8(&bus, base + 2, 0x20); /* Disabling TIE deasserts IRQ. */
        TEST_CHECK(cpu.pending_irq_count == 0);
        for (int n = 0; n < 640; ++n) bus_tick_dma_completion(&bus);
        TEST_CHECK(cpu.pending_irq_count == 0);
    }
    bus_free(&bus);
}

static void test_classic_v2_escape_isolation(void)
{
    address_bus_t bus; keyboard_t keyboard;
    bus_init(&bus); bus.keyboard = &keyboard;
    keyboard_init(&keyboard, 9);
    keyboard_set_key(&keyboard, 1, 2, true); /* Up, not Esc. */
    bus.machine = cybiko_machine(CYBIKO_CLASSIC_V2);
    TEST_CHECK(bus_read16(&bus, 0xe00000) & 2);
    TEST_CHECK(bus_read8(&bus, 0xe00001) & 2);
    TEST_CHECK(!(bus_read16(&bus, 0xe00002) & 2)); /* Column zero excluded. */
    keyboard_set_key(&keyboard, 0, 2, true);
    TEST_CHECK(!(bus_read16(&bus, 0xe00000) & 2));
    keyboard_set_key(&keyboard, 0, 2, false);
    bus.machine = cybiko_machine(CYBIKO_CLASSIC_V1);
    TEST_CHECK(!(bus_read16(&bus, 0xe00000) & 2)); /* V1 unchanged. */
}

TEST_LIST = {
    {"longword_memory_paths", test_longword_memory_paths},
    {"classic_v2_escape_isolation", test_classic_v2_escape_isolation},
    {"serial_transmit_interrupts", test_serial_transmit_interrupts},
    {"classic_spi_bus_and_dtc", test_classic_spi_bus_and_dtc},
    {"battery_adc_registers_and_conversion", test_battery_adc_registers_and_conversion},
    {"profiles_and_storage", test_profiles_and_storage},
    {"dataflash_protocol", test_dataflash_protocol},
    {"model_address_maps", test_model_address_maps},
    {NULL, NULL}
};

#include "acutest.h"
#include "core/address_bus.h"

static int sync_count;

static void sync_counter(void *ctx)
{
    int *counter = ctx;
    ++*counter;
}

static void setup_bus(address_bus_t *bus)
{
    bus_init(bus);
    memory_init(&bus->on_chip_ram, 0x2400, true);
    bus->sync_peripherals = sync_counter;
    bus->sync_ctx = &sync_count;
    sync_count = 0;
}

static void teardown_bus(address_bus_t *bus)
{
    bus_free(bus);
}

static void test_plain_on_chip_ram_fast_path_stays_below_io_boundary(void)
{
    address_bus_t bus;
    setup_bus(&bus);

    bus_write32(&bus, 0xfffbfc, 0x12345678);

    TEST_CHECK(sync_count == 0);
    TEST_CHECK(memory_read32(&bus.on_chip_ram, 0x1ffc) == 0x12345678);
    TEST_CHECK(bus_read32(&bus, 0xfffbfc) == 0x12345678);
    TEST_CHECK(sync_count == 0);

    teardown_bus(&bus);
}

static void test_on_chip_access_reaching_io_boundary_uses_slow_router(void)
{
    address_bus_t bus;
    setup_bus(&bus);

    bus_write32(&bus, 0xfffbfe, 0xaabbccdd);

    TEST_CHECK(sync_count == 1);
    TEST_CHECK(memory_read16(&bus.on_chip_ram, 0x1ffe) == 0xaabb);
    TEST_CHECK(memory_read16(&bus.on_chip_ram, 0x2000) == 0xccdd);

    sync_count = 0;
    TEST_CHECK(bus_read32(&bus, 0xfffbfe) == 0xaabbccdd);
    TEST_CHECK(sync_count == 1);

    teardown_bus(&bus);
}

static void test_io_boundary_byte_access_synchronizes(void)
{
    address_bus_t bus;
    setup_bus(&bus);

    bus_write8(&bus, 0xfffc00, 0x5a);

    TEST_CHECK(sync_count == 1);
    TEST_CHECK(memory_read8(&bus.on_chip_ram, 0x2000) == 0x5a);

    sync_count = 0;
    TEST_CHECK(bus_read8(&bus, 0xfffc00) == 0x5a);
    TEST_CHECK(sync_count == 1);

    teardown_bus(&bus);
}

static void test_plain_range_classifier_accepts_mapped_ram_and_rom(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.boot_rom, 32768, true);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    TEST_CHECK(bus_is_plain_read_range(&bus, 0x000100, 4));
    TEST_CHECK(!bus_is_plain_write_range(&bus, 0x000100, 4));
    TEST_CHECK(bus_is_plain_read_range(&bus, bus.machine->ram_base + 0x100, 4));
    TEST_CHECK(bus_is_plain_write_range(&bus, bus.machine->ram_base + 0x100, 4));

    teardown_bus(&bus);
}

static void test_plain_range_classifier_rejects_mmio_and_page_crossing(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    TEST_CHECK(!bus_is_plain_read_range(&bus, 0xffff84, 1));
    TEST_CHECK(!bus_is_plain_write_range(&bus, 0xffff84, 1));
    TEST_CHECK(bus_is_plain_read_range(&bus, 0xfffbfc, 4));
    TEST_CHECK(bus_is_plain_write_range(&bus, 0xfffbfc, 4));
    TEST_CHECK(!bus_is_plain_read_range(&bus, 0xfffbfd, 4));
    TEST_CHECK(!bus_is_plain_write_range(&bus, 0xfffbfd, 4));
    TEST_CHECK(!bus_is_plain_read_range(&bus, bus.machine->ram_base + 0xfff, 2));
    TEST_CHECK(!bus_is_plain_write_range(&bus, bus.machine->ram_base + 0xfff, 2));

    teardown_bus(&bus);
}

static void test_plain_pointer_helpers_return_live_backing_storage(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.boot_rom, 32768, true);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    memory_write16(&bus.boot_rom, 0x100, 0x1234);
    const uint8_t *rom = bus_plain_read_ptr(&bus, 0x100, 2);
    TEST_ASSERT(rom != NULL);
    TEST_CHECK(rom[0] == 0x12);
    TEST_CHECK(rom[1] == 0x34);
    TEST_CHECK(bus_plain_write_ptr(&bus, 0x100, 2) == NULL);

    uint32_t ram_addr = bus.machine->ram_base + 0x120;
    uint8_t *ram = bus_plain_write_ptr(&bus, ram_addr, 4);
    TEST_ASSERT(ram != NULL);
    ram[0] = 0xde; ram[1] = 0xad; ram[2] = 0xbe; ram[3] = 0xef;
    TEST_CHECK(bus_read32(&bus, ram_addr) == 0xdeadbeef);
    TEST_CHECK(bus_plain_read_ptr(&bus, ram_addr, 4) == ram);

    uint8_t *on_chip = bus_plain_write_ptr(&bus, 0xfffbfc, 4);
    TEST_ASSERT(on_chip != NULL);
    on_chip[0] = 0xca; on_chip[1] = 0xfe; on_chip[2] = 0xba; on_chip[3] = 0xbe;
    TEST_CHECK(bus_read32(&bus, 0xfffbfc) == 0xcafebabe);
    TEST_CHECK(bus_plain_read_ptr(&bus, 0xfffbfc, 4) == on_chip);

    teardown_bus(&bus);
}

static void test_plain_pointer_helpers_reject_slow_paths(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    TEST_CHECK(bus_plain_read_ptr(&bus, 0xffff84, 1) == NULL);
    TEST_CHECK(bus_plain_write_ptr(&bus, 0xffff84, 1) == NULL);
    TEST_CHECK(bus_plain_read_ptr(&bus, 0xfffbfd, 4) == NULL);
    TEST_CHECK(bus_plain_write_ptr(&bus, 0xfffbfd, 4) == NULL);
    TEST_CHECK(bus_plain_read_ptr(&bus, bus.machine->ram_base + 0xfff, 2) == NULL);
    TEST_CHECK(bus_plain_write_ptr(&bus, bus.machine->ram_base + 0xfff, 2) == NULL);

    teardown_bus(&bus);
}

static void test_code_page_generation_tracks_only_watched_writes(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    uint32_t ram = bus.machine->ram_base + 0x120;
    bus_write8(&bus, ram, 0x12);
    TEST_CHECK(bus_code_page_generation(&bus, ram) == 0);

    bus_watch_code_range(&bus, ram, 2);
    TEST_CHECK(bus_code_page_generation(&bus, ram) == 0);
    bus_write8(&bus, ram, 0x34);
    TEST_CHECK(bus_code_page_generation(&bus, ram) == 1);
    bus_write16(&bus, ram, 0x5678);
    TEST_CHECK(bus_code_page_generation(&bus, ram) == 2);

    teardown_bus(&bus);
}

static void test_code_page_generation_tracks_cross_page_writes(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    uint32_t edge = bus.machine->ram_base + 0x0fff;
    bus_watch_code_range(&bus, edge, 2);
    bus_write16(&bus, edge, 0xabcd);

    TEST_CHECK(bus_code_page_generation(&bus, edge) == 1);
    TEST_CHECK(bus_code_page_generation(&bus, edge + 1) == 1);

    teardown_bus(&bus);
}

static void test_code_page_generation_tracks_on_chip_ram_writes(void)
{
    address_bus_t bus;
    setup_bus(&bus);

    uint32_t ram = 0xfffbfc;
    bus_watch_code_range(&bus, ram, 4);
    bus_write32(&bus, ram, 0x12345678);

    TEST_CHECK(bus_code_page_generation(&bus, ram) == 1);
    TEST_CHECK(sync_count == 0);

    teardown_bus(&bus);
}

static void test_code_page_watches_can_be_cleared(void)
{
    address_bus_t bus;
    setup_bus(&bus);
    memory_init(&bus.external_ram, bus.machine->ram_size, true);
    bus_build_memory_map(&bus);

    uint32_t ram = bus.machine->ram_base + 0x220;
    bus_watch_code_range(&bus, ram, 1);
    bus_write8(&bus, ram, 0xaa);
    TEST_CHECK(bus_code_page_generation(&bus, ram) == 1);

    bus_clear_code_watches(&bus);
    bus_write8(&bus, ram, 0xbb);
    TEST_CHECK(bus_code_page_generation(&bus, ram) == 0);

    teardown_bus(&bus);
}

TEST_LIST = {
    {"plain_on_chip_ram_fast_path_stays_below_io_boundary", test_plain_on_chip_ram_fast_path_stays_below_io_boundary},
    {"on_chip_access_reaching_io_boundary_uses_slow_router", test_on_chip_access_reaching_io_boundary_uses_slow_router},
    {"io_boundary_byte_access_synchronizes", test_io_boundary_byte_access_synchronizes},
    {"plain_range_classifier_accepts_mapped_ram_and_rom", test_plain_range_classifier_accepts_mapped_ram_and_rom},
    {"plain_range_classifier_rejects_mmio_and_page_crossing", test_plain_range_classifier_rejects_mmio_and_page_crossing},
    {"plain_pointer_helpers_return_live_backing_storage", test_plain_pointer_helpers_return_live_backing_storage},
    {"plain_pointer_helpers_reject_slow_paths", test_plain_pointer_helpers_reject_slow_paths},
    {"code_page_generation_tracks_only_watched_writes", test_code_page_generation_tracks_only_watched_writes},
    {"code_page_generation_tracks_cross_page_writes", test_code_page_generation_tracks_cross_page_writes},
    {"code_page_generation_tracks_on_chip_ram_writes", test_code_page_generation_tracks_on_chip_ram_writes},
    {"code_page_watches_can_be_cleared", test_code_page_watches_can_be_cleared},
    {NULL, NULL}
};

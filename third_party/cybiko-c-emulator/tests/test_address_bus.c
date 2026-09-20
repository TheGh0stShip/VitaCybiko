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

TEST_LIST = {
    {"plain_on_chip_ram_fast_path_stays_below_io_boundary", test_plain_on_chip_ram_fast_path_stays_below_io_boundary},
    {"on_chip_access_reaching_io_boundary_uses_slow_router", test_on_chip_access_reaching_io_boundary_uses_slow_router},
    {"io_boundary_byte_access_synchronizes", test_io_boundary_byte_access_synchronizes},
    {"plain_range_classifier_accepts_mapped_ram_and_rom", test_plain_range_classifier_accepts_mapped_ram_and_rom},
    {"plain_range_classifier_rejects_mmio_and_page_crossing", test_plain_range_classifier_rejects_mmio_and_page_crossing},
    {NULL, NULL}
};

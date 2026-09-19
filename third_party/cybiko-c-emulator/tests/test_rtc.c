#include "acutest.h"
#include "core/rtc.h"
#include "core/address_bus.h"
#include <string.h>

static void start(rtc_t *r) {
    rtc_scl_w(r, false); rtc_sda_w(r, true);
    rtc_scl_w(r, true); rtc_sda_w(r, false); rtc_scl_w(r, false);
}
static void stop(rtc_t *r) {
    rtc_scl_w(r, false); rtc_sda_w(r, false);
    rtc_scl_w(r, true); rtc_sda_w(r, true);
}
static bool send(rtc_t *r, uint8_t b) {
    for (int bit = 7; bit >= 0; --bit) {
        rtc_sda_w(r, (b >> bit) & 1);
        rtc_scl_w(r, true); rtc_scl_w(r, false);
    }
    rtc_sda_w(r, true); rtc_scl_w(r, true);
    bool ack = !rtc_sda_r(r);
    rtc_scl_w(r, false);
    return ack;
}
static uint8_t receive(rtc_t *r, bool ack) {
    uint8_t b = 0;
    rtc_sda_w(r, true);
    for (int bit = 0; bit < 8; ++bit) {
        rtc_scl_w(r, true);
        b = (uint8_t)((b << 1) | rtc_sda_r(r));
        rtc_scl_w(r, false);
    }
    rtc_sda_w(r, !ack); rtc_scl_w(r, true); rtc_scl_w(r, false);
    return b;
}
static void test_i2c(void) {
    rtc_t r; rtc_init(&r);
    start(&r); TEST_CHECK(send(&r, 0xa2)); TEST_CHECK(send(&r, 2));
    TEST_CHECK(send(&r, 0x37)); TEST_CHECK(send(&r, 0x48)); stop(&r);
    TEST_CHECK(r.data[2] == 0x37 && r.data[3] == 0x48);
    start(&r); TEST_CHECK(send(&r, 0xa2)); TEST_CHECK(send(&r, 2));
    start(&r); TEST_CHECK(send(&r, 0xa3)); /* Repeated START. */
    TEST_CHECK(receive(&r, true) == 0x37);
    TEST_CHECK(receive(&r, false) == 0x48);
    TEST_CHECK(r.mode == RTC_MODE_IGNORE); stop(&r);
    start(&r); TEST_CHECK(!send(&r, 0xa0)); stop(&r);
    TEST_CHECK(r.data[2] == 0x37);
    /* More than the old 50-byte receive buffer; pointer wraps in hardware. */
    start(&r); TEST_CHECK(send(&r, 0xa2)); TEST_CHECK(send(&r, 0));
    for (int i = 0; i < 1024; ++i) TEST_CHECK(send(&r, (uint8_t)i));
    stop(&r);
    for (int i = 0; i < 16; ++i) TEST_CHECK(r.data[i] == (uint8_t)(1008+i));
}
static void test_calendar(void) {
    rtc_t r; rtc_init(&r); memset(r.data, 0, sizeof(r.data));
    r.data[2] = r.data[3] = 0x59; r.data[4] = 0x23;
    r.data[5] = 0x28; r.data[6] = 0x02; /* leap-year Feb 28 */
    rtc_advance(&r, 1000000000ULL);
    TEST_CHECK(r.data[5] == 0x29 && (r.data[6] & 31) == 2);
    rtc_advance(&r, 86400000000000ULL);
    TEST_CHECK(r.data[5] == 1 && (r.data[6] & 31) == 3);
    r.data[5] = 0x40 | 0x28; r.data[6] = 2;
    rtc_advance(&r, 86400000000000ULL);
    TEST_CHECK(r.data[5] == 0x41 && (r.data[6] & 31) == 3);
    r.data[5] = 0xc0 | 0x31; r.data[6] = 0x12;
    rtc_advance(&r, 86400000000000ULL);
    TEST_CHECK(r.data[5] == 1 && (r.data[6] & 31) == 1);
    r.data[0] = 0x80; uint8_t before = r.data[2];
    rtc_advance(&r, 1000000000ULL); TEST_CHECK(r.data[2] == before);
}
static void test_port_f_direction(void) {
    address_bus_t b; rtc_t r;
    bus_init(&b); b.machine = cybiko_machine(CYBIKO_CLASSIC_V2);
    memory_init(&b.on_chip_ram, 0x2400, true); rtc_init(&r); b.rtc = &r;
    bus_write8(&b, 0xFFFF6E, 2);
    bus_write8(&b, 0xFFFEBE, 2);
    TEST_CHECK(r.pin_scl == 1 && r.pin_sda == 1);
    TEST_CHECK(bus_read8(&b, 0xFFFF6E) == 2); /* DR is latch, not pins. */
    bus_write8(&b, 0xFFFEBE, 3); /* Drive SDA low while SCL high: START. */
    TEST_CHECK(r.active && r.pin_sda == 0);
    TEST_CHECK((bus_read8(&b, 0xFFFF5E) & 1) == 0);
    bus_write8(&b, 0xFFFEBE, 2);
    TEST_CHECK(!r.active && r.pin_sda == 1);
    bus_free(&b);
}
TEST_LIST = {
    {"i2c_transactions", test_i2c}, {"calendar", test_calendar},
    {"port_f_direction", test_port_f_direction}, {NULL, NULL}
};

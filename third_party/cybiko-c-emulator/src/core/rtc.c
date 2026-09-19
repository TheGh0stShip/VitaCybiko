/* PCF8593 calendar and open-drain I2C slave at address 0x51.
 * Sample on rising SCL; change slave output on falling SCL.
 * A bounded accumulator handles arbitrarily long and repeated transactions. */
#include "core/rtc.h"
#include <string.h>
#include <time.h>

static uint8_t to_bcd(int n) { return (uint8_t)((n / 10) * 16 + n % 10); }
static int from_bcd(uint8_t n) { return (n >> 4) * 10 + (n & 15); }
static uint64_t monotonic_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static void advance_day(rtc_t *r) {
    int year = r->data[5] >> 6;
    int month = from_bcd(r->data[6] & 0x1f);
    int weekday = ((r->data[6] >> 5) + 1) % 7;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) month = 1;
    int limit = days[month - 1] + (month == 2 && year == 0);
    int day = from_bcd(r->data[5] & 0x3f) + 1;
    if (day > limit) {
        day = 1;
        if (++month > 12) { month = 1; year = (year + 1) & 3; }
    }
    r->data[5] = (uint8_t)((year << 6) | to_bcd(day));
    r->data[6] = (uint8_t)((weekday << 5) | to_bcd(month));
}

void rtc_advance(rtc_t *r, uint64_t elapsed_ns) {
    if (r->data[0] & 0x80) return;
    uint64_t ticks = elapsed_ns / 10000000ULL;
    r->frac_ns += elapsed_ns % 10000000ULL;
    ticks += r->frac_ns / 10000000ULL;
    r->frac_ns %= 10000000ULL;
    ticks += (unsigned)from_bcd(r->data[1]);
    r->data[1] = to_bcd((int)(ticks % 100));
    uint64_t seconds = ticks / 100 + (unsigned)from_bcd(r->data[2] & 0x7f);
    r->data[2] = to_bcd((int)(seconds % 60));
    uint64_t minutes = seconds / 60 + (unsigned)from_bcd(r->data[3] & 0x7f);
    r->data[3] = to_bcd((int)(minutes % 60));
    uint64_t hours = minutes / 60 + (unsigned)from_bcd(r->data[4] & 0x3f);
    r->data[4] = to_bcd((int)(hours % 24));
    for (uint64_t days = hours / 24; days; --days) advance_day(r);
}
void rtc_init(rtc_t *r) {
    memset(r, 0, sizeof(*r));
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        r->data[2] = to_bcd(t->tm_sec);
        r->data[3] = to_bcd(t->tm_min);
        r->data[4] = to_bcd(t->tm_hour);
        r->data[5] = (uint8_t)(((t->tm_year % 4) << 6) | to_bcd(t->tm_mday));
        r->data[6] = (uint8_t)((t->tm_wday << 5) | to_bcd(t->tm_mon + 1));
    } else { r->data[5] = 1; r->data[6] = 1; }
    r->pin_scl = r->pin_sda = r->inp = 1;
    r->last_tick_ns = monotonic_ns();
}
void rtc_tick(rtc_t *r) {
    uint64_t now = monotonic_ns();
    uint64_t elapsed = now >= r->last_tick_ns ? now - r->last_tick_ns : 0;
    r->last_tick_ns = now;
    rtc_advance(r, elapsed);
}
bool rtc_sda_r(const rtc_t *r) { return r->inp && r->pin_sda; }

static void receive_byte(rtc_t *r) {
    if (r->recv_phase == 0) {
        r->ack = (r->recv_byte & 0xfe) == 0xa2;
        r->send_after_ack = r->ack && (r->recv_byte & 1);
        if (r->ack) r->recv_phase = 1;
    } else if (r->recv_phase == 1) {
        r->pos = r->recv_byte & 15;
        r->recv_phase = 2;
        r->ack = true;
    } else {
        r->data[r->pos] = r->recv_byte;
        r->pos = (r->pos + 1) & 15;
        r->ack = true;
    }
}
void rtc_scl_w(rtc_t *r, bool high) {
    int level = high ? 1 : 0;
    if (level == r->pin_scl) return;
    r->pin_scl = level;
    if (!r->active || r->mode == RTC_MODE_IGNORE) return;
    if (level) {
        if (r->bits < 8) {
            if (r->mode == RTC_MODE_RECV) {
                r->recv_byte = (uint8_t)((r->recv_byte << 1) | r->pin_sda);
                if (r->bits == 7) receive_byte(r);
            }
        } else if (r->mode == RTC_MODE_SEND) {
            r->ack = !r->pin_sda; /* Master owns the ninth clock. */
        }
        ++r->bits;
    } else {
        if (r->bits == 9) {
            r->bits = 0;
            r->recv_byte = 0;
            if (r->mode == RTC_MODE_SEND) r->pos = (r->pos + 1) & 15;
            if (!r->ack) r->mode = RTC_MODE_IGNORE;
            else if (r->send_after_ack) { r->mode = RTC_MODE_SEND; r->send_after_ack = false; }
        }
        if (r->mode == RTC_MODE_SEND)
            r->inp = r->bits < 8 ? (r->data[r->pos] >> (7 - r->bits)) & 1 : 1;
        else
            r->inp = r->mode == RTC_MODE_RECV && r->bits == 8 && r->ack ? 0 : 1;
    }
}
void rtc_sda_w(rtc_t *r, bool high) {
    int level = high ? 1 : 0;
    if (r->pin_scl && level != r->pin_sda) {
        if (!level) { /* START or repeated START preserves register pointer. */
            r->active = true;
            r->mode = RTC_MODE_RECV;
            r->bits = r->recv_phase = r->recv_byte = 0;
            r->ack = r->send_after_ack = false;
        } else r->active = false;
        r->inp = 1;
    }
    r->pin_sda = level;
}

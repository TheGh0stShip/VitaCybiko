#ifndef CYBIKO_RTC_H
#define CYBIKO_RTC_H
#include "types.h"

typedef enum { RTC_MODE_RECV, RTC_MODE_SEND, RTC_MODE_IGNORE } rtc_mode_t;

typedef struct {
    uint8_t    data[16];
    bool       active;
    rtc_mode_t mode;
    int        pin_scl, pin_sda, inp;
    int        bits, pos, recv_phase;
    uint8_t    recv_byte;
    bool       ack, send_after_ack;
    uint64_t   last_tick_ns, frac_ns;
} rtc_t;

void rtc_init(rtc_t *rtc);
void rtc_scl_w(rtc_t *rtc, bool high);
void rtc_sda_w(rtc_t *rtc, bool high);
bool rtc_sda_r(const rtc_t *rtc);
void rtc_tick(rtc_t *rtc);
/* Deterministic elapsed-time input, also used by the host-clock adapter. */
void rtc_advance(rtc_t *rtc, uint64_t elapsed_ns);

#endif

/*
 * H8S 8-bit timer (TMR) emulation.
 *
 * The H8S/2323 has two 8-bit timer channels (TMR0, TMR1).
 * Each has TCR, TCSR, TCORA, TCORB, TCNT registers.
 *
 * Interrupt vectors:
 *   Channel 0: CMIA=64, CMIB=65, OVI=66
 *   Channel 1: CMIA=68, CMIB=69, OVI=70
 */
#include "core/timer8.h"
#include "core/h8s_cpu.h"

#if defined(__GNUC__) || defined(__clang__)
#define TIMER8_INLINE static inline __attribute__((always_inline))
#define TIMER8_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define TIMER8_INLINE static inline
#define TIMER8_UNLIKELY(x) (x)
#endif

/* TCR bits */
#define TCR_CMIEB 0x80  /* Compare Match Interrupt Enable B */
#define TCR_CMIEA 0x40  /* Compare Match Interrupt Enable A */
#define TCR_OVIE  0x20  /* Overflow Interrupt Enable */

/* TCSR bits */
#define TCSR_CMFB 0x80  /* Compare Match Flag B */
#define TCSR_CMFA 0x40  /* Compare Match Flag A */
#define TCSR_OVF  0x20  /* Overflow Flag */

/* Clock divisors indexed by CKS (bits 2-0 of TCR) */
static const int clock_divisors[8] = {
    0, 8, 64, 8192, 256, 256, 256, 256
};

void timer8_init(timer8_t *t, int channel, h8s_cpu_t *cpu) {
    t->tcr   = 0;
    t->tcsr  = 0;
    t->tcora = 0xFF;
    t->tcorb = 0xFF;
    t->tcnt  = 0;
    t->prescale_counter = 0;
    t->cached_divisor   = 0;
    t->cpu = cpu;

    if (channel == 0) {
        t->vec_cmia = 64;
        t->vec_cmib = 65;
        t->vec_ovi  = 66;
    } else {
        t->vec_cmia = 68;
        t->vec_cmib = 69;
        t->vec_ovi  = 70;
    }
}

uint8_t timer8_read(const timer8_t *t, int reg) {
    switch (reg) {
    case 0: return t->tcr;
    case 2: return t->tcsr;
    case 4: return t->tcora;
    case 6: return t->tcorb;
    case 8: return t->tcnt;
    default: return 0;
    }
}

void timer8_write(timer8_t *t, int reg, uint8_t value) {
    switch (reg) {
    case 0:
        t->tcr = value;
        t->cached_divisor = clock_divisors[value & 0x07];
        break;
    case 2:
        /* Flags (bits 7-5): write-0-to-clear; lower 5 bits writable directly */
        t->tcsr = (t->tcsr & ~0x1F) | (value & 0x1F);
        t->tcsr &= value | 0x1F;
        break;
    case 4:
        t->tcora = value;
        break;
    case 6:
        t->tcorb = value;
        break;
    case 8:
        t->tcnt = value;
        break;
    default:
        break;
    }
}

void timer8_counter_tick(timer8_t *t) {
    /* Increment counter */
    uint8_t previous_count = t->tcnt;
    t->tcnt = (t->tcnt + 1) & 0xFF;

    /* Check compare match A */
    if (t->tcnt == t->tcora) {
        if (!(t->tcsr & TCSR_CMFA)) {
            t->tcsr |= TCSR_CMFA;
            if (t->tcr & TCR_CMIEA) {
                h8s_cpu_request_interrupt(t->cpu, t->vec_cmia);
            }
        }
        if (((t->tcr >> 3) & 0x03) == 1) {
            t->tcnt = 0;
        }
    }

    /* Check compare match B */
    if (t->tcnt == t->tcorb) {
        if (!(t->tcsr & TCSR_CMFB)) {
            t->tcsr |= TCSR_CMFB;
            if (t->tcr & TCR_CMIEB) {
                h8s_cpu_request_interrupt(t->cpu, t->vec_cmib);
            }
        }
        if (((t->tcr >> 3) & 0x03) == 2) {
            t->tcnt = 0;
        }
    }

    /* Check overflow (only on natural wrap 0xFF -> 0x00) */
    if (previous_count == 0xFF && t->tcnt == 0 && !(t->tcsr & TCSR_OVF)) {
        t->tcsr |= TCSR_OVF;
        if (t->tcr & TCR_OVIE) {
            h8s_cpu_request_interrupt(t->cpu, t->vec_ovi);
        }
    }
}

TIMER8_INLINE int timer8_cycles_until_counter_tick_inline(const timer8_t *t) {
    if (TIMER8_UNLIKELY(t->cached_divisor == 0)) return 0;
    int remaining = t->cached_divisor - t->prescale_counter;
    return remaining > 0 ? remaining : 1;
}

int timer8_cycles_until_counter_tick(const timer8_t *t) {
    return timer8_cycles_until_counter_tick_inline(t);
}

TIMER8_INLINE int timer8_counter_ticks_until_event(const timer8_t *t) {
    if (TIMER8_UNLIKELY(t->cached_divisor == 0)) return 0;
    /* The next event is the nearest comparator or natural wrap. Previously
     * every deadline query simulated up to 256 counter increments, including
     * millions of queries made before the counter had advanced at all. */
    int ticks = 256 - t->tcnt;
    int a = (uint8_t)(t->tcora - t->tcnt);
    int b = (uint8_t)(t->tcorb - t->tcnt);
    if (a && a < ticks) ticks = a;
    if (b && b < ticks) ticks = b;
    return ticks;
}

TIMER8_INLINE int timer8_counter_distance(uint8_t from, uint8_t target) {
    int distance = (uint8_t)(target - from);
    return distance == 0 ? 0x100 : distance;
}

int timer8_cycles_until_event(const timer8_t *t) {
    int first_tick = timer8_cycles_until_counter_tick_inline(t);
    if (first_tick <= 0) return 0;
    int ticks = timer8_counter_ticks_until_event(t);
    return first_tick + (ticks - 1) * t->cached_divisor;
}

int timer8_cycles_until_cpu_event(const timer8_t *t) {
    int first_tick = timer8_cycles_until_counter_tick_inline(t);
    if (first_tick <= 0) return 0;

    int dist_a = timer8_counter_distance(t->tcnt, t->tcora);
    int dist_b = timer8_counter_distance(t->tcnt, t->tcorb);
    int dist_o = timer8_counter_distance(t->tcnt, 0);
    bool obs_a = (t->tcr & TCR_CMIEA) && !(t->tcsr & TCSR_CMFA);
    bool obs_b = (t->tcr & TCR_CMIEB) && !(t->tcsr & TCSR_CMFB);
    bool obs_o = (t->tcr & TCR_OVIE) && !(t->tcsr & TCSR_OVF);

    int best = 0x101;
    if (obs_a && dist_a < best) best = dist_a;
    if (obs_b && dist_b < best) best = dist_b;
    if (obs_o && dist_o < best) best = dist_o;

    int clear_mode = (t->tcr >> 3) & 0x03;
    if (clear_mode == 1 && !obs_a && dist_a <= best) return 0;
    if (clear_mode == 2 && !obs_b && dist_b <= best) return 0;
    if (best == 0x101) return 0;
    return first_tick + (best - 1) * t->cached_divisor;
}

TIMER8_INLINE void timer8_advance_no_event(timer8_t *t, int cycles) {
    int total = t->prescale_counter + cycles;
    int ticks = total / t->cached_divisor;
    t->prescale_counter = total % t->cached_divisor;
    t->tcnt = (uint8_t)(t->tcnt + ticks);
}

void timer8_advance(timer8_t *t, int cycles) {
    /* I/O synchronization usually arrives before even one prescaler tick.
     * Do not rescan deadlines or invoke integer division for that case. */
    if (cycles > 0 && t->cached_divisor != 0 &&
        cycles < t->cached_divisor - t->prescale_counter) {
        t->prescale_counter += cycles;
        return;
    }
    while (cycles > 0 && t->cached_divisor != 0) {
        int remaining = timer8_cycles_until_event(t);
        if (remaining <= 0) return;
        if (cycles < remaining) {
            timer8_advance_no_event(t, cycles);
            return;
        }
        cycles -= remaining;
        if (remaining > 1) timer8_advance_no_event(t, remaining - 1);
        timer8_tick(t);
    }
}

bool timer8_is_running(const timer8_t *t) {
    return t->cached_divisor != 0;
}

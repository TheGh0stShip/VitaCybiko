#include "acutest.h"
#include "core/timer8.h"
#include "core/h8s_cpu.h"
#include <string.h>

/* Minimal CPU for receiving interrupt requests */
static h8s_cpu_t cpu;

static void cpu_init_stub(void) {
    memset(&cpu, 0, sizeof(cpu));
}

static void test_init_defaults(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    TEST_CHECK(t.tcr == 0);
    TEST_CHECK(t.tcsr == 0);
    TEST_CHECK(t.tcora == 0xFF);
    TEST_CHECK(t.tcorb == 0xFF);
    TEST_CHECK(t.tcnt == 0);
    TEST_CHECK(t.cached_divisor == 0);
}

static void test_init_channel_vectors(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    TEST_CHECK(t.vec_cmia == 64);
    TEST_CHECK(t.vec_cmib == 65);
    TEST_CHECK(t.vec_ovi == 66);

    timer8_init(&t, 1, &cpu);
    TEST_CHECK(t.vec_cmia == 68);
    TEST_CHECK(t.vec_cmib == 69);
    TEST_CHECK(t.vec_ovi == 70);
}

static void test_not_running_cks_zero(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    TEST_CHECK(!timer8_is_running(&t));
    /* Ticking does nothing when stopped */
    timer8_tick(&t);
    TEST_CHECK(t.tcnt == 0);
}

static void test_prescaler_div8(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    /* CKS=1 → divisor 8 */
    timer8_write(&t, 0, 0x01);
    TEST_CHECK(timer8_is_running(&t));
    for (int i = 0; i < 7; i++) timer8_tick(&t);
    TEST_CHECK(t.tcnt == 0);
    timer8_tick(&t);
    TEST_CHECK(t.tcnt == 1);
}

static void test_prescaler_div64(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    /* CKS=2 → divisor 64 */
    timer8_write(&t, 0, 0x02);
    for (int i = 0; i < 63; i++) timer8_tick(&t);
    TEST_CHECK(t.tcnt == 0);
    timer8_tick(&t);
    TEST_CHECK(t.tcnt == 1);
}

static void test_compare_match_a_sets_flag(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    timer8_write(&t, 0, 0x01); /* CKS=1 (div8) */
    timer8_write(&t, 4, 2);    /* TCORA=2 */
    /* Tick to tcnt=2 (2 * 8 = 16 ticks) */
    for (int i = 0; i < 16; i++) timer8_tick(&t);
    TEST_CHECK(t.tcnt == 2);
    TEST_CHECK((timer8_read(&t, 2) & 0x40) != 0); /* CMFA set */
}

static void test_compare_match_a_fires_interrupt(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    /* TCR: CMIEA=1 (bit 6), CKS=1 */
    timer8_write(&t, 0, 0x41);
    timer8_write(&t, 4, 1); /* TCORA=1 */
    for (int i = 0; i < 8; i++) timer8_tick(&t);
    TEST_CHECK(cpu.pending_irq_count > 0);
    TEST_CHECK(cpu.pending_irqs[0] == 64); /* vec_cmia for ch0 */
}

static void test_compare_match_b_sets_flag(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    timer8_write(&t, 0, 0x01); /* CKS=1 */
    timer8_write(&t, 6, 3);    /* TCORB=3 */
    for (int i = 0; i < 24; i++) timer8_tick(&t);
    TEST_CHECK((timer8_read(&t, 2) & 0x80) != 0); /* CMFB set */
}

static void test_clear_on_compare_a(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    /* TCR: CCLR=01 (bits 4:3), CKS=1 → 0x09 */
    timer8_write(&t, 0, 0x09);
    timer8_write(&t, 4, 3); /* TCORA=3 */
    /* Tick to match (3 * 8 = 24 ticks) */
    for (int i = 0; i < 24; i++) timer8_tick(&t);
    /* Counter should clear on match */
    TEST_CHECK(t.tcnt == 0);
    TEST_CHECK((t.tcsr & 0x20) == 0); /* Compare clear is not overflow. */
}

static void test_clear_on_compare_b(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    /* TCR: CCLR=10 (bits 4:3), CKS=1 → 0x11 */
    timer8_write(&t, 0, 0x11);
    timer8_write(&t, 6, 5); /* TCORB=5 */
    for (int i = 0; i < 40; i++) timer8_tick(&t);
    TEST_CHECK(t.tcnt == 0);
    TEST_CHECK((t.tcsr & 0x20) == 0);
}

static void test_overflow_sets_flag(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    timer8_write(&t, 0, 0x01); /* CKS=1 (div8) */
    timer8_write(&t, 8, 0xFE); /* TCNT=254 */
    /* Tick twice: 254→255, 255→0 (overflow) */
    for (int i = 0; i < 16; i++) timer8_tick(&t);
    TEST_CHECK(t.tcnt == 0);
    TEST_CHECK((timer8_read(&t, 2) & 0x20) != 0); /* OVF set */
}

static void test_tcsr_write_zero_to_clear(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    /* Manually set all flags */
    timer8_write(&t, 0, 0x01);
    timer8_write(&t, 4, 1); /* TCORA=1 */
    for (int i = 0; i < 8; i++) timer8_tick(&t);
    TEST_CHECK((timer8_read(&t, 2) & 0x40) != 0); /* CMFA set */
    /* Write 0 to bit 6 (CMFA) to clear it */
    timer8_write(&t, 2, 0x00);
    TEST_CHECK((timer8_read(&t, 2) & 0x40) == 0);
}

static void test_register_roundtrips(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    timer8_write(&t, 0, 0x07);
    TEST_CHECK(timer8_read(&t, 0) == 0x07);
    timer8_write(&t, 4, 0x42);
    TEST_CHECK(timer8_read(&t, 4) == 0x42);
    timer8_write(&t, 6, 0xAB);
    TEST_CHECK(timer8_read(&t, 6) == 0xAB);
    timer8_write(&t, 8, 0x99);
    TEST_CHECK(timer8_read(&t, 8) == 0x99);
}

static void test_advance_matches_single_ticks(void) {
    timer8_t stepped, advanced;
    h8s_cpu_t cpu_stepped, cpu_advanced;
    memset(&cpu_stepped, 0, sizeof(cpu_stepped));
    memset(&cpu_advanced, 0, sizeof(cpu_advanced));
    timer8_init(&stepped, 0, &cpu_stepped);
    timer8_init(&advanced, 0, &cpu_advanced);

    timer8_write(&stepped, 0, 0x61);  /* CMIEA + OVIE + /8 */
    timer8_write(&advanced, 0, 0x61);
    timer8_write(&stepped, 4, 3);
    timer8_write(&advanced, 4, 3);
    timer8_write(&stepped, 8, 0xFC);
    timer8_write(&advanced, 8, 0xFC);

    for (int i = 0; i < 43; ++i) timer8_tick(&stepped);
    timer8_advance(&advanced, 17);
    timer8_advance(&advanced, 26);

    TEST_CHECK(advanced.tcnt == stepped.tcnt);
    TEST_CHECK(advanced.tcsr == stepped.tcsr);
    TEST_CHECK(advanced.prescale_counter == stepped.prescale_counter);
    TEST_CHECK(cpu_advanced.pending_irq_count == cpu_stepped.pending_irq_count);
    for (int i = 0; i < cpu_stepped.pending_irq_count; ++i)
        TEST_CHECK(cpu_advanced.pending_irqs[i] == cpu_stepped.pending_irqs[i]);
}

static void test_deadline_matches_counter_scan(void) {
    timer8_t t;
    cpu_init_stub();
    timer8_init(&t, 0, &cpu);
    timer8_write(&t, 0, 3); /* /8192: includes nonzero prescaler debt. */
    for (int count = 0; count < 256; ++count) {
        for (int a = 0; a < 256; ++a) {
            t.tcnt = (uint8_t)count;
            t.tcora = (uint8_t)a;
            t.tcorb = (uint8_t)(a * 73 + count * 17);
            t.prescale_counter = (a * 31 + count) % 8192;
            uint8_t scan = t.tcnt;
            int ticks = 0;
            do {
                ++ticks;
                ++scan;
            } while (scan && scan != t.tcora && scan != t.tcorb);
            TEST_CHECK(timer8_cycles_until_event(&t) ==
                       ticks * 8192 - t.prescale_counter);
        }
    }
}

TEST_LIST = {
    { "deadline_matches_counter_scan", test_deadline_matches_counter_scan },
    { "init_defaults",                  test_init_defaults },
    { "init_channel_vectors",           test_init_channel_vectors },
    { "not_running_cks_zero",           test_not_running_cks_zero },
    { "prescaler_div8",                 test_prescaler_div8 },
    { "prescaler_div64",                test_prescaler_div64 },
    { "compare_match_a_sets_flag",      test_compare_match_a_sets_flag },
    { "compare_match_a_fires_interrupt", test_compare_match_a_fires_interrupt },
    { "compare_match_b_sets_flag",      test_compare_match_b_sets_flag },
    { "clear_on_compare_a",             test_clear_on_compare_a },
    { "clear_on_compare_b",             test_clear_on_compare_b },
    { "overflow_sets_flag",             test_overflow_sets_flag },
    { "tcsr_write_zero_to_clear",       test_tcsr_write_zero_to_clear },
    { "register_roundtrips",            test_register_roundtrips },
    { "advance_matches_single_ticks",   test_advance_matches_single_ticks },
    { NULL, NULL }
};

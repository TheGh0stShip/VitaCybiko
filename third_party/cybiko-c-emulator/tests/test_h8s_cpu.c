#include "acutest.h"
#include "core/h8s_cpu.h"
#include "core/address_bus.h"
#include <string.h>

/*
 * CPU test setup: Create a real address_bus with on_chip_ram initialized.
 * Write instruction bytes to on-chip RAM at 0xFFDC00 and set PC there.
 * Peripheral pointers are left NULL - we never access peripherals.
 *
 * H8S instruction encoding reference (from decode function):
 *   0xF_RR_II = MOV.B #imm, Rd  (Rd = RR[7:4]=H or [3:0]=L of ER[n])
 *   0x8_RR_II = ADD.B #imm, Rd
 *   0xA_RR_II = CMP.B #imm, Rd
 *   0xB_RR_II = SUBX.B #imm, Rd
 *   0xC_RR_II = OR.B #imm, Rd
 *   0xD_RR_II = XOR.B #imm, Rd
 *   0xE_RR_II = AND.B #imm, Rd
 *   0x4_CC_DD = Bcc d:8 (CC=condition, DD=signed displacement)
 *   0x0000    = NOP
 *   Reg byte: R0H=0, R1H=1, ..., R7H=7, R0L=8, R1L=9, ..., R7L=F
 */

#define CODE_BASE 0xFFDC00
#define CODE_OFFSET 0  /* Offset within on_chip_ram (mapped at 0xFFDC00) */

static address_bus_t bus;
static h8s_cpu_t cpu;

/* Dummy peripheral structs to prevent null pointer issues on bus read/write */
static hd66421_t  lcd_stub;
static timer8_t   timer8_stub[2];
static timer16_t  timer16_stub[6];
static rtc_t      rtc_stub;
static speaker_t  speaker_stub;
static keyboard_t keyboard_stub;

static void setup(void) {
    bus_init(&bus);

    /* Initialize on-chip RAM (0x2400 bytes mapped at 0xFFDC00) */
    memory_init(&bus.on_chip_ram, 0x2400, true);

    /* Wire dummy peripherals so bus reads don't crash */
    bus.lcd = &lcd_stub;
    bus.timer8[0] = &timer8_stub[0];
    bus.timer8[1] = &timer8_stub[1];
    for (int i = 0; i < 6; i++) bus.timer16[i] = &timer16_stub[i];
    bus.rtc = &rtc_stub;
    bus.speaker = &speaker_stub;
    bus.keyboard = &keyboard_stub;
    bus.cpu = &cpu;

    memset(&lcd_stub, 0, sizeof(lcd_stub));
    memset(timer8_stub, 0, sizeof(timer8_stub));
    memset(timer16_stub, 0, sizeof(timer16_stub));
    memset(&rtc_stub, 0, sizeof(rtc_stub));
    memset(&speaker_stub, 0, sizeof(speaker_stub));
    memset(&keyboard_stub, 0, sizeof(keyboard_stub));

    h8s_cpu_init(&cpu, &bus);
    /* Set PC to on-chip RAM start, disable interrupts */
    cpu.pc = CODE_BASE;
    cpu.ccr = CCR_I; /* I=1 blocks interrupts so process_interrupts returns early */
}

static void teardown(void) {
    memory_free(&bus.on_chip_ram);
    bus_free(&bus);
}

/* Write a 16-bit instruction word at offset within on-chip RAM */
static void write_code16(int offset, uint16_t word) {
    memory_write16(&bus.on_chip_ram, (uint32_t)offset, word);
}

/* ---- Tests ---- */

static void test_nop(void) {
    setup();
    write_code16(CODE_OFFSET, 0x0000); /* NOP */
    uint32_t pc_before = cpu.pc;
    h8s_cpu_step(&cpu);
    TEST_CHECK_(cpu.pc == pc_before + 2, "PC should advance by 2, got 0x%06X", cpu.pc);
    teardown();
}

static void test_mov_b_imm(void) {
    setup();
    /* MOV.B #0x42, R0L → opcode 0xF842 (Rd=8=R0L, imm=0x42) */
    write_code16(CODE_OFFSET, 0xF842);
    h8s_cpu_step(&cpu);
    TEST_CHECK_((cpu.er[0] & 0xFF) == 0x42,
        "R0L should be 0x42, got 0x%02X", (uint8_t)(cpu.er[0] & 0xFF));
    /* N=0, Z=0 (0x42 is positive, non-zero) */
    TEST_CHECK(!(cpu.ccr & CCR_N));
    TEST_CHECK(!(cpu.ccr & CCR_Z));
    teardown();
}

static void test_mov_b_imm_zero(void) {
    setup();
    /* MOV.B #0, R0L → 0xF800 */
    write_code16(CODE_OFFSET, 0xF800);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0);
    TEST_CHECK(cpu.ccr & CCR_Z);
    TEST_CHECK(!(cpu.ccr & CCR_N));
    teardown();
}

static void test_mov_b_imm_negative(void) {
    setup();
    /* MOV.B #0x80, R0L → 0xF880 */
    write_code16(CODE_OFFSET, 0xF880);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0x80);
    TEST_CHECK(cpu.ccr & CCR_N);
    TEST_CHECK(!(cpu.ccr & CCR_Z));
    teardown();
}

static void test_add_b_imm(void) {
    setup();
    /* Load R0L with 0x10, then ADD.B #0x20, R0L */
    write_code16(CODE_OFFSET, 0xF810);     /* MOV.B #0x10, R0L */
    write_code16(CODE_OFFSET + 2, 0x8820); /* ADD.B #0x20, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK_((cpu.er[0] & 0xFF) == 0x30,
        "R0L should be 0x30, got 0x%02X", (uint8_t)(cpu.er[0] & 0xFF));
    TEST_CHECK(!(cpu.ccr & CCR_C));
    TEST_CHECK(!(cpu.ccr & CCR_V));
    TEST_CHECK(!(cpu.ccr & CCR_Z));
    TEST_CHECK(!(cpu.ccr & CCR_N));
    teardown();
}

static void test_add_b_overflow(void) {
    setup();
    /* 0x7F + 1 = 0x80 → V=1 (positive overflow to negative) */
    write_code16(CODE_OFFSET, 0xF87F);     /* MOV.B #0x7F, R0L */
    write_code16(CODE_OFFSET + 2, 0x8801); /* ADD.B #0x01, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK_((cpu.er[0] & 0xFF) == 0x80,
        "R0L should be 0x80, got 0x%02X", (uint8_t)(cpu.er[0] & 0xFF));
    TEST_CHECK(cpu.ccr & CCR_V);
    TEST_CHECK(cpu.ccr & CCR_N);
    TEST_CHECK(!(cpu.ccr & CCR_C));
    teardown();
}

static void test_add_b_carry(void) {
    setup();
    /* 0xFF + 1 = 0x00 → C=1, Z=1 */
    write_code16(CODE_OFFSET, 0xF8FF);     /* MOV.B #0xFF, R0L */
    write_code16(CODE_OFFSET + 2, 0x8801); /* ADD.B #0x01, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0x00);
    TEST_CHECK(cpu.ccr & CCR_C);
    TEST_CHECK(cpu.ccr & CCR_Z);
    teardown();
}

static void test_cmp_b_imm(void) {
    setup();
    /* CMP.B #0x42, R0L (with R0L=0x42) → Z=1, register unchanged */
    write_code16(CODE_OFFSET, 0xF842);     /* MOV.B #0x42, R0L */
    write_code16(CODE_OFFSET + 2, 0xA842); /* CMP.B #0x42, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0x42); /* Unchanged */
    TEST_CHECK(cpu.ccr & CCR_Z);
    TEST_CHECK(!(cpu.ccr & CCR_N));
    TEST_CHECK(!(cpu.ccr & CCR_C));
    teardown();
}

static void test_subx_b_imm(void) {
    setup();
    /* SUBX.B #0x01, R0L (with R0L=0x10, C=0) → 0x0F */
    write_code16(CODE_OFFSET, 0xF810);     /* MOV.B #0x10, R0L */
    write_code16(CODE_OFFSET + 2, 0xB801); /* SUBX.B #0x01, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK_((cpu.er[0] & 0xFF) == 0x0F,
        "R0L should be 0x0F, got 0x%02X", (uint8_t)(cpu.er[0] & 0xFF));
    teardown();
}

static void test_or_b_imm(void) {
    setup();
    write_code16(CODE_OFFSET, 0xF80F);     /* MOV.B #0x0F, R0L */
    write_code16(CODE_OFFSET + 2, 0xC8F0); /* OR.B  #0xF0, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0xFF);
    teardown();
}

static void test_xor_b_imm(void) {
    setup();
    write_code16(CODE_OFFSET, 0xF8FF);     /* MOV.B #0xFF, R0L */
    write_code16(CODE_OFFSET + 2, 0xD80F); /* XOR.B #0x0F, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0xF0);
    teardown();
}

static void test_and_b_imm(void) {
    setup();
    write_code16(CODE_OFFSET, 0xF8FF);     /* MOV.B #0xFF, R0L */
    write_code16(CODE_OFFSET + 2, 0xE80F); /* AND.B #0x0F, R0L */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 0xFF) == 0x0F);
    teardown();
}

static void test_bra(void) {
    setup();
    /* BRA +4 → opcode 0x4004 (cond=0=always, disp=+4) */
    /* PC after fetch = CODE_BASE+2, target = CODE_BASE+2+4 = CODE_BASE+6 */
    write_code16(CODE_OFFSET, 0x4004);
    h8s_cpu_step(&cpu);
    TEST_CHECK_(cpu.pc == CODE_BASE + 6,
        "expected PC=0x%06X, got 0x%06X", CODE_BASE + 6, cpu.pc);
    teardown();
}

static void test_beq_taken(void) {
    setup();
    /* Set Z flag, then BEQ +4 */
    write_code16(CODE_OFFSET, 0xF800);     /* MOV.B #0, R0L → sets Z */
    write_code16(CODE_OFFSET + 2, 0x4704); /* BEQ +4 (cond=7=Z) */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    /* PC after BEQ fetch = CODE_BASE+4, target = CODE_BASE+4+4 = CODE_BASE+8 */
    TEST_CHECK_(cpu.pc == CODE_BASE + 8,
        "expected PC=0x%06X, got 0x%06X", CODE_BASE + 8, cpu.pc);
    teardown();
}

static void test_beq_not_taken(void) {
    setup();
    /* Clear Z flag, then BEQ +4 */
    write_code16(CODE_OFFSET, 0xF842);     /* MOV.B #0x42, R0L → Z=0 */
    write_code16(CODE_OFFSET + 2, 0x4704); /* BEQ +4 */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    /* Not taken: PC = CODE_BASE+4 */
    TEST_CHECK_(cpu.pc == CODE_BASE + 4,
        "expected PC=0x%06X, got 0x%06X", CODE_BASE + 4, cpu.pc);
    teardown();
}

static void test_bne_taken(void) {
    setup();
    /* Clear Z flag, then BNE +4 */
    write_code16(CODE_OFFSET, 0xF842);     /* MOV.B #0x42, R0L → Z=0 */
    write_code16(CODE_OFFSET + 2, 0x4604); /* BNE +4 (cond=6=!Z) */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK_(cpu.pc == CODE_BASE + 8,
        "expected PC=0x%06X, got 0x%06X", CODE_BASE + 8, cpu.pc);
    teardown();
}

static void test_bne_not_taken(void) {
    setup();
    write_code16(CODE_OFFSET, 0xF800);     /* MOV.B #0, R0L → Z=1 */
    write_code16(CODE_OFFSET + 2, 0x4604); /* BNE +4 */
    h8s_cpu_step(&cpu);
    h8s_cpu_step(&cpu);
    TEST_CHECK_(cpu.pc == CODE_BASE + 4,
        "expected PC=0x%06X, got 0x%06X", CODE_BASE + 4, cpu.pc);
    teardown();
}

static void test_cycle_count(void) {
    setup();
    TEST_CHECK(cpu.cycle_count == 0);
    write_code16(CODE_OFFSET, 0x0000); /* NOP */
    h8s_cpu_step(&cpu);
    TEST_CHECK_(cpu.cycle_count == 1, "expected 1, got %llu",
        (unsigned long long)cpu.cycle_count);
    teardown();
}

static void test_long_shift_sign_and_overflow(void) {
    setup();
    cpu.er[0] = 0xFFFFFF7F; /* Value reached by the Classic V2 firmware. */
    write_code16(0, 0x1030); /* SHLL.L ER0 */
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[0] == 0xFFFFFEFE);
    TEST_CHECK(cpu.ccr & CCR_C);
    TEST_CHECK(!(cpu.ccr & CCR_V));
    cpu.er[0] = 0x80000001;
    write_code16(2, 0x11B0); /* SHAR.L ER0 */
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[0] == 0xC0000000);
    TEST_CHECK(cpu.ccr & CCR_C);
    cpu.er[0] = 0x80000000;
    write_code16(4, 0x10B0); /* SHAL.L ER0 */
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[0] == 0);
    TEST_CHECK(cpu.ccr & CCR_C);
    TEST_CHECK(cpu.ccr & CCR_V);
    TEST_CHECK(cpu.ccr & CCR_Z);
    teardown();
}

static void test_instruction_mapping_cache(void) {
    setup();
    write_code16(0, 0xF812);
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 255) == 0x12);
    write_code16(0, 0xF834); /* No stale opcode after self modification. */
    cpu.pc = CODE_BASE;
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 255) == 0x34);
    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF856);
    cpu.pc = 0x8100; /* Mirrored boot-ROM window. */
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 255) == 0x56);
    TEST_CHECK(cpu.pc == 0x8102);
    /* Alternating regions must not retain stale instruction bytes. */
    cpu.pc = CODE_BASE;
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 255) == 0x34);
    memory_write16(&bus.boot_rom, 0x100, 0xF878);
    cpu.pc = 0x8100;
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 255) == 0x78);
    memory_write16(&bus.boot_rom, 0x0fff, 0xF89A);
    cpu.pc = 0x8fff; /* Word spans a 4 KiB page boundary. */
    h8s_cpu_step(&cpu);
    TEST_CHECK((cpu.er[0] & 255) == 0x9a);
    h8s_cpu_reset(&cpu);
    TEST_CHECK(cpu.fetch_data == NULL);
    teardown();
}

static void test_long_displacement_store(void) {
    setup();
    cpu.er[1] = CODE_BASE + 0x100;
    cpu.er[2] = 0x87654321;
    write_code16(0, 0x0100);
    write_code16(2, 0x7810);
    write_code16(4, 0x6BA2); /* MOV.L ER2, @(0x20:32, ER1) */
    write_code16(6, 0);
    write_code16(8, 0x20);
    h8s_cpu_step(&cpu);
    TEST_CHECK(bus_read32(&bus, CODE_BASE + 0x120) == 0x87654321);
    TEST_CHECK(cpu.er[1] == CODE_BASE + 0x100);
    TEST_CHECK(cpu.er[2] == 0x87654321);
    TEST_CHECK(cpu.pc == CODE_BASE + 10);
    TEST_CHECK(cpu.ccr & CCR_N);
    teardown();
}

static void test_interrupt_frame(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write32(&bus.boot_rom, 18 * 4, CODE_BASE + 0x20);
    write_code16(0x20, 0x5670); /* RTE */
    cpu.er[7] = CODE_BASE + 0x100;
    cpu.ccr = CCR_C | CCR_H;
    cpu.halted = true;
    h8s_cpu_request_interrupt(&cpu, 18);
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE + 0x20);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0xFC);
    TEST_CHECK(bus_read32(&bus, cpu.er[7]) ==
               ((uint32_t)(CCR_C | CCR_H) << 24 | CODE_BASE));
    TEST_CHECK(cpu.ccr & CCR_I);
    TEST_CHECK(!cpu.halted);
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0x100);
    TEST_CHECK(cpu.ccr == (CCR_C | CCR_H));
    teardown();
}

static void test_trap_and_task_frame(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write32(&bus.boot_rom, 0x24, CODE_BASE + 0x20);
    write_code16(0, 0x5710); /* TRAPA #1 */
    write_code16(0x20, 0x5670); /* RTE */
    cpu.er[7] = CODE_BASE + 0x100;
    cpu.ccr = CCR_Z;
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE + 0x20);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0xFC);
    TEST_CHECK(bus_read32(&bus, cpu.er[7]) ==
               ((uint32_t)CCR_Z << 24 | (CODE_BASE + 2)));
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE + 2);
    TEST_CHECK(cpu.ccr == CCR_Z);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0x100);
    /* A task's initial context is built by software, not interrupt entry. */
    cpu.pc = CODE_BASE + 0x20;
    cpu.er[7] -= 4;
    bus_write32(&bus, cpu.er[7], 0xA5123456);
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == 0x123456);
    TEST_CHECK(cpu.ccr == 0xA5);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0x100);
    teardown();
}

static void test_signed_multiply_word_destination(void) {
    setup();
    for (int rs = 0; rs < 16; ++rs) {
        for (int rd = 0; rd < 16; ++rd) {
            for (int i = 0; i < 8; ++i) cpu.er[i] = 0x81fd03feu + i * 0x03050307u;
            uint32_t before = cpu.er[rd & 7];
            int8_t source = (int8_t)(cpu.er[rs & 7] >> (rs < 8 ? 8 : 0));
            int8_t dest = (int8_t)(before >> (rd < 8 ? 0 : 16));
            uint16_t product = (uint16_t)((int)source * dest);
            uint32_t expected = rd < 8 ? (before & 0xffff0000u) | product :
                                        (before & 0xffffu) | ((uint32_t)product << 16);
            write_code16(0, 0x01c0);
            write_code16(2, 0x5000 | (rs << 4) | rd);
            cpu.pc = CODE_BASE;
            cpu.ccr = CCR_I | CCR_C | CCR_V | CCR_H;
            h8s_cpu_step(&cpu);
            TEST_CHECK_(cpu.er[rd & 7] == expected, "MULXS.B rs=%d rd=%d", rs, rd);
            TEST_CHECK(!!(cpu.ccr & CCR_N) == !!(product & 0x8000));
            TEST_CHECK(!!(cpu.ccr & CCR_Z) == (product == 0));
            TEST_CHECK((cpu.ccr & (CCR_C | CCR_V | CCR_H)) == (CCR_C | CCR_V | CCR_H));
        }
    }
    teardown();
}

static void test_signed_divide_registers_and_flags(void) {
    setup();
    /* DIVXS.B R0L,E1: destination includes the upper word register bank. */
    write_code16(0, 0x01d0);
    write_code16(2, 0x5189);
    cpu.er[0] = 3;
    cpu.er[1] = 0xffecabcd; /* -20 / 3 = -6, remainder -2 */
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[1] == 0xfefaabcd);
    TEST_CHECK(cpu.ccr & CCR_N);
    TEST_CHECK(!(cpu.ccr & CCR_Z));
    cpu.pc = CODE_BASE;
    cpu.er[1] = 0x0001abcd; /* Zero quotient does not set Z; zero divisor does. */
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[1] == 0x0100abcd);
    TEST_CHECK(!(cpu.ccr & (CCR_N | CCR_Z)));
    cpu.pc = CODE_BASE;
    cpu.er[0] = 0;
    cpu.ccr |= CCR_N;
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[1] == 0x0100abcd);
    TEST_CHECK(cpu.ccr & CCR_Z);
    TEST_CHECK(!(cpu.ccr & CCR_N));
    /* The largest signed quotient must not cause host division overflow. */
    write_code16(2, 0x5301); /* DIVXS.W R0,ER1 */
    cpu.pc = CODE_BASE;
    cpu.er[0] = 0xffff;
    cpu.er[1] = 0x80000000;
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.er[1] == 0);
    TEST_CHECK(!(cpu.ccr & CCR_Z));
    teardown();
}

static void test_ccr_interrupt_deferral(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write32(&bus.boot_rom, 18 * 4, CODE_BASE + 0x40);
    write_code16(0, 0x067f); /* ANDC #0x7f,CCR */
    write_code16(2, 0x0f97); /* MOV.L ER1,ER7: CyOS task-switch pattern */
    write_code16(4, 0x0000);
    cpu.er[7] = CODE_BASE + 0x100;
    cpu.er[1] = CODE_BASE + 0x200;
    h8s_cpu_request_interrupt(&cpu, 18);
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE + 2);
    TEST_CHECK(cpu.irq_deferred);
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE + 4);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0x200);
    TEST_CHECK(cpu.pending_irq_count == 1);
    h8s_cpu_step(&cpu);
    TEST_CHECK(cpu.pc == CODE_BASE + 0x40);
    TEST_CHECK(cpu.er[7] == CODE_BASE + 0x1fc);
    TEST_CHECK((bus_read32(&bus, cpu.er[7]) & 0xffffff) == CODE_BASE + 4);
    teardown();
}

TEST_LIST = {
    {"ccr_interrupt_deferral", test_ccr_interrupt_deferral},
    {"signed_multiply_word_destination", test_signed_multiply_word_destination},
    {"signed_divide_registers_and_flags", test_signed_divide_registers_and_flags},
    {"instruction_mapping_cache", test_instruction_mapping_cache},
    {"long_displacement_store", test_long_displacement_store},
    {"interrupt_frame", test_interrupt_frame},
    {"trap_and_task_frame", test_trap_and_task_frame},
    {"long_shift_sign_and_overflow", test_long_shift_sign_and_overflow},
    { "nop",              test_nop },
    { "mov_b_imm",        test_mov_b_imm },
    { "mov_b_imm_zero",   test_mov_b_imm_zero },
    { "mov_b_imm_negative", test_mov_b_imm_negative },
    { "add_b_imm",        test_add_b_imm },
    { "add_b_overflow",   test_add_b_overflow },
    { "add_b_carry",      test_add_b_carry },
    { "cmp_b_imm",        test_cmp_b_imm },
    { "subx_b_imm",       test_subx_b_imm },
    { "or_b_imm",         test_or_b_imm },
    { "xor_b_imm",        test_xor_b_imm },
    { "and_b_imm",        test_and_b_imm },
    { "bra",              test_bra },
    { "beq_taken",        test_beq_taken },
    { "beq_not_taken",    test_beq_not_taken },
    { "bne_taken",        test_bne_taken },
    { "bne_not_taken",    test_bne_not_taken },
    { "cycle_count",      test_cycle_count },
    { NULL, NULL }
};

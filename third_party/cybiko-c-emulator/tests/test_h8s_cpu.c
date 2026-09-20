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
    bus_build_memory_map(&bus);
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

static void test_immutable_fetch_window_accepts_boot_and_flash(void) {
    setup();
    const uint8_t *data = NULL;
    uint32_t base = 0, size = 0;

    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF856);
    bus_build_memory_map(&bus);
    cpu.pc = 0x8100; /* Mirrored boot-ROM window. */
    TEST_CHECK(h8s_cpu_get_immutable_fetch_window(&cpu, &data, &base, &size));
    TEST_CHECK(data == bus.boot_rom.data);
    TEST_CHECK(base == 0x8000);
    TEST_CHECK(size == 32768);

    memory_init(&bus.flash_rom, bus.machine->flash_size, true);
    memory_write16(&bus.flash_rom, 0x200, 0xF878);
    bus_build_memory_map(&bus);
    cpu.pc = bus.machine->flash_base + 0x200;
    TEST_CHECK(h8s_cpu_get_immutable_fetch_window(&cpu, &data, &base, &size));
    TEST_CHECK(data == bus.flash_rom.data);
    TEST_CHECK(base == bus.machine->flash_base);
    TEST_CHECK(size == bus.machine->flash_size);
    teardown();
}

static void test_immutable_fetch_window_rejects_ram_and_io(void) {
    setup();
    const uint8_t *data = NULL;
    uint32_t base = 0, size = 0;

    cpu.pc = CODE_BASE;
    TEST_CHECK(!h8s_cpu_get_immutable_fetch_window(&cpu, &data, &base, &size));
    cpu.pc = 0xFFFF00;
    TEST_CHECK(!h8s_cpu_get_immutable_fetch_window(&cpu, &data, &base, &size));
    teardown();
}

static void test_semantic_rom_block_fast_path_executes_bcc(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF800); /* MOV.B #0,R0L -> Z */
    memory_write16(&bus.boot_rom, 0x102, 0x4604); /* BNE 0x108; should fall through. */
    memory_write16(&bus.boot_rom, 0x104, 0x0B01); /* Fall-through target. */
    memory_write16(&bus.boot_rom, 0x106, 0x5470);
    memory_write16(&bus.boot_rom, 0x108, 0x0B02);
    bus_build_memory_map(&bus);

    cpu.pc = 0x8100;
    cpu.ccr = CCR_I;
    cpu.er[0] = 0xffffffffu;
    int cycles = 0;
    TEST_CHECK(h8s_cpu_try_execute_semantic_rom_block(&cpu, 8, &cycles));
    TEST_CHECK(cycles == 2);
    TEST_CHECK(cpu.pc == 0x8104);
    TEST_CHECK((cpu.er[0] & 0xff) == 0);
    TEST_CHECK(cpu.ccr & CCR_Z);
    TEST_CHECK(cpu.cycle_count == 2);
    TEST_CHECK(cpu.semantic_fast_blocks == 1);
    TEST_CHECK(cpu.semantic_fast_cycles == 2);
    TEST_CHECK(cpu.semantic_fast_rejects == 0);
    teardown();
}

static void test_semantic_rom_block_fast_path_rejects_guards(void) {
    setup();
    write_code16(0, 0xF800);
    write_code16(2, 0x4602);
    int cycles = 0x1234;
    TEST_CHECK(!h8s_cpu_try_execute_semantic_rom_block(&cpu, 8, &cycles));
    TEST_CHECK(cycles == 0x1234);
    TEST_CHECK(cpu.pc == CODE_BASE);
    TEST_CHECK(cpu.cycle_count == 0);
    TEST_CHECK(cpu.semantic_fast_rejects == 1);

    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF800);
    memory_write16(&bus.boot_rom, 0x102, 0x4602);
    bus_build_memory_map(&bus);
    cpu.pc = 0x8100;
    TEST_CHECK(!h8s_cpu_try_execute_semantic_rom_block(&cpu, 1, &cycles));
    TEST_CHECK(cpu.pc == 0x8100);
    TEST_CHECK(cpu.semantic_fast_rejects == 2);

    cpu.pending_irqs[0] = 12;
    cpu.pending_irq_count = 1;
    cpu.ccr = 0;
    TEST_CHECK(!h8s_cpu_try_execute_semantic_rom_block(&cpu, 8, &cycles));
    TEST_CHECK(cpu.pc == 0x8100);
    TEST_CHECK(cpu.semantic_fast_rejects == 3);
    teardown();
}

static void test_cpu_run_semantic_fast_path_matches_steps(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF800); /* MOV.B #0,R0L -> Z */
    memory_write16(&bus.boot_rom, 0x102, 0x4604); /* BNE 0x108; should fall through. */
    memory_write16(&bus.boot_rom, 0x104, 0x0B01); /* ADDS #2, ER1 */
    memory_write16(&bus.boot_rom, 0x106, 0x5470); /* RTS boundary */
    memory_write16(&bus.boot_rom, 0x108, 0x0B02);
    bus_build_memory_map(&bus);

    h8s_cpu_t stepped;
    h8s_cpu_init(&stepped, &bus);
    stepped.pc = 0x8100;
    stepped.ccr = CCR_I;
    stepped.er[0] = 0xffffffffu;
    stepped.er[1] = 0x100;
    h8s_cpu_step(&stepped);
    h8s_cpu_step(&stepped);

    cpu.pc = 0x8100;
    cpu.ccr = CCR_I;
    cpu.er[0] = 0xffffffffu;
    cpu.er[1] = 0x100;
    int timer_debt = 0, completion_debt = 0;
    bool io_access = false;
    int done = h8s_cpu_run(&cpu, 2, 123, &timer_debt, &completion_debt, &io_access);

    TEST_CHECK(done == 2);
    TEST_CHECK(timer_debt == 2);
    TEST_CHECK(completion_debt == 2);
    TEST_CHECK(!io_access);
    TEST_CHECK(cpu.semantic_fast_blocks == 1);
    TEST_CHECK(cpu.semantic_fast_cycles == 2);
    TEST_CHECK(cpu.pc == stepped.pc);
    TEST_CHECK(cpu.ccr == stepped.ccr);
    TEST_CHECK(cpu.cycle_count == stepped.cycle_count);
    for (unsigned i = 0; i < 8; ++i)
        TEST_CHECK(cpu.er[i] == stepped.er[i]);
    teardown();
}

static void count_sync_callback(void *ctx)
{
    int *count = (int *)ctx;
    ++*count;
}

static void test_cpu_run_fast_path_preserves_sync_boundary(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF800); /* MOV.B #0,R0L -> Z */
    memory_write16(&bus.boot_rom, 0x102, 0x4604); /* BNE fall-through */
    memory_write16(&bus.boot_rom, 0x104, 0x0B01);
    bus_build_memory_map(&bus);

    int sync_count = 0;
    bus.sync_peripherals = count_sync_callback;
    bus.sync_ctx = &sync_count;
    cpu.pc = 0x8100;
    cpu.ccr = CCR_I;

    int timer_debt = 0, completion_debt = 0;
    bool io_access = false;
    int done = h8s_cpu_run(&cpu, 2, 123, &timer_debt, &completion_debt, &io_access);

    TEST_CHECK(done == 2);
    TEST_CHECK(sync_count == 1);
    TEST_CHECK(timer_debt == 2);
    TEST_CHECK(completion_debt == 2);
    TEST_CHECK(cpu.pc == 0x8104);
    teardown();
}

static void test_cpu_run_semantic_fast_path_matches_jmp_steps(void) {
    setup();
    memory_init(&bus.boot_rom, 32768, true);
    memory_write16(&bus.boot_rom, 0x100, 0xF812); /* MOV.B #0x12,R0L */
    memory_write16(&bus.boot_rom, 0x102, 0x5A00); /* JMP @0x000108 */
    memory_write16(&bus.boot_rom, 0x104, 0x0108);
    memory_write16(&bus.boot_rom, 0x108, 0x0B01); /* ADDS #2, ER1 */
    bus_build_memory_map(&bus);

    h8s_cpu_t stepped;
    h8s_cpu_init(&stepped, &bus);
    stepped.pc = 0x8100;
    stepped.ccr = CCR_I;
    stepped.er[1] = 0x200;
    h8s_cpu_step(&stepped);
    h8s_cpu_step(&stepped);

    cpu.pc = 0x8100;
    cpu.ccr = CCR_I;
    cpu.er[1] = 0x200;
    int timer_debt = 0, completion_debt = 0;
    bool io_access = false;
    int done = h8s_cpu_run(&cpu, 2, 456, &timer_debt, &completion_debt, &io_access);

    TEST_CHECK(done == 2);
    TEST_CHECK(timer_debt == 2);
    TEST_CHECK(completion_debt == 2);
    TEST_CHECK(!io_access);
    TEST_CHECK(cpu.pc == stepped.pc);
    TEST_CHECK(cpu.ccr == stepped.ccr);
    TEST_CHECK(cpu.cycle_count == stepped.cycle_count);
    for (unsigned i = 0; i < 8; ++i)
        TEST_CHECK(cpu.er[i] == stepped.er[i]);
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

static void test_unsigned_multiply_word_destination(void) {
    setup();
    for (int rs = 0; rs < 16; ++rs) {
        for (int rd = 0; rd < 16; ++rd) {
            for (int i = 0; i < 8; ++i) cpu.er[i] = 0x81fd03feu + i * 0x03050307u;
            uint32_t before = cpu.er[rd & 7];
            uint8_t source = (uint8_t)(cpu.er[rs & 7] >> (rs < 8 ? 8 : 0));
            uint8_t dest = (uint8_t)(before >> (rd < 8 ? 0 : 16));
            uint16_t product = (uint16_t)((unsigned)source * dest);
            uint32_t expected = rd < 8 ? (before & 0xffff0000u) | product :
                                        (before & 0xffffu) | ((uint32_t)product << 16);
            write_code16(0, 0x5000 | (rs << 4) | rd);
            cpu.pc = CODE_BASE;
            cpu.ccr = 0xff;
            h8s_cpu_step(&cpu);
            TEST_CHECK_(cpu.er[rd & 7] == expected, "MULXU.B rs=%d rd=%d", rs, rd);
            TEST_CHECK(cpu.ccr == 0xff);
        }
    }
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

static void test_move_byte_flags_exhaustive(void)
{
    setup();
    for (unsigned initial = 0; initial < 256; ++initial) {
        for (unsigned value = 0; value < 256; ++value) {
            cpu.pc = CODE_BASE;
            cpu.ccr = (uint8_t)initial;
            write_code16(0, 0xf800 | value);
            h8s_cpu_step(&cpu);
            uint8_t expected = (uint8_t)(initial & ~(CCR_N | CCR_Z | CCR_V));
            if (!value) expected |= CCR_Z;
            if (value & 0x80) expected |= CCR_N;
            TEST_CHECK(cpu.ccr == expected);
            TEST_CHECK((cpu.er[0] & 255) == value);
        }
    }
    teardown();
}

static void test_long_arithmetic_flags(void)
{
    setup();
    const uint32_t edges[] = {0, 1, 0x0fffffff, 0x10000000, 0x7fffffff,
                             0x80000000, 0xfffffffe, 0xffffffff};
    uint32_t random = 0x41523132;
    for (unsigned trial = 0; trial < 4096; ++trial) {
        random = random * 1664525u + 1013904223u;
        uint32_t d = trial < 64 ? edges[trial / 8] : random;
        random = random * 1664525u + 1013904223u;
        uint32_t s = trial < 64 ? edges[trial % 8] : random;
        for (unsigned form = 0; form < 6; ++form) {
            bool subtract = form % 3 != 0;
            bool compare = form % 3 == 2;
            uint64_t wide = subtract ? (uint64_t)d - s : (uint64_t)d + s;
            uint32_t result = (uint32_t)wide;
            uint32_t overflow = subtract ? ((d ^ s) & (d ^ result))
                                         : ((d ^ result) & (s ^ result));
            uint8_t initial = (uint8_t)trial;
            uint8_t expected = initial & (CCR_I | CCR_UI | CCR_U);
            if (wide & 0x100000000ULL) expected |= CCR_C;
            if (result == 0) expected |= CCR_Z;
            if (result & 0x80000000u) expected |= CCR_N;
            if (overflow & 0x80000000u) expected |= CCR_V;
            if ((d ^ s ^ result) & 0x10000000u) expected |= CCR_H;
            cpu.pc = CODE_BASE; cpu.ccr = initial;
            cpu.er[0] = d; cpu.er[1] = s;
            if (form < 3) {
                write_code16(0, form == 0 ? 0x0a90 : form == 1 ? 0x1a90 : 0x1f90);
            } else {
                write_code16(0, form == 3 ? 0x7a10 : form == 4 ? 0x7a30 : 0x7a20);
                write_code16(2, s >> 16); write_code16(4, s);
            }
            h8s_cpu_step(&cpu);
            TEST_CHECK_(cpu.ccr == expected, "trial=%u form=%u flags=%02x expected=%02x",
                        trial, form, cpu.ccr, expected);
            TEST_CHECK(cpu.er[0] == (compare ? d : result));
            TEST_CHECK(cpu.er[1] == s);
        }
    }
    teardown();
}

TEST_LIST = {
    {"unsigned_multiply_word_destination", test_unsigned_multiply_word_destination},
    {"move_byte_flags_exhaustive", test_move_byte_flags_exhaustive},
    {"long_arithmetic_flags", test_long_arithmetic_flags},
    {"ccr_interrupt_deferral", test_ccr_interrupt_deferral},
    {"signed_multiply_word_destination", test_signed_multiply_word_destination},
    {"signed_divide_registers_and_flags", test_signed_divide_registers_and_flags},
    {"instruction_mapping_cache", test_instruction_mapping_cache},
    {"immutable_fetch_window_accepts_boot_and_flash", test_immutable_fetch_window_accepts_boot_and_flash},
    {"immutable_fetch_window_rejects_ram_and_io", test_immutable_fetch_window_rejects_ram_and_io},
    {"semantic_rom_block_fast_path_executes_bcc", test_semantic_rom_block_fast_path_executes_bcc},
    {"semantic_rom_block_fast_path_rejects_guards", test_semantic_rom_block_fast_path_rejects_guards},
    {"cpu_run_semantic_fast_path_matches_steps", test_cpu_run_semantic_fast_path_matches_steps},
    {"cpu_run_fast_path_preserves_sync_boundary", test_cpu_run_fast_path_preserves_sync_boundary},
    {"cpu_run_semantic_fast_path_matches_jmp_steps", test_cpu_run_semantic_fast_path_matches_jmp_steps},
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

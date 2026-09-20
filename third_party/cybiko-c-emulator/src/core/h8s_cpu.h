#ifndef CYBIKO_H8S_CPU_H
#define CYBIKO_H8S_CPU_H

#include "types.h"

typedef struct address_bus address_bus_t;

#define CCR_C  0x01
#define CCR_V  0x02
#define CCR_Z  0x04
#define CCR_N  0x08
#define CCR_U  0x10
#define CCR_H  0x20
#define CCR_UI 0x40
#define CCR_I  0x80

#define MAX_PENDING_IRQS 32

typedef struct h8s_cpu {
    uint32_t er[8];
    uint32_t pc;
    uint8_t  ccr;
    uint8_t  exr;
    bool     halted;
    bool     irq_deferred;

    int      pending_irqs[MAX_PENDING_IRQS];
    int      pending_irq_count;

    address_bus_t *bus;
    uint64_t cycle_count;

    bool     tracing;
    uint32_t last_start_pc;
    /* Cache only the instruction memory mapping, never decoded bytes: guest
     * self-modifying RAM remains immediately visible. I/O uses the slow bus. */
    const uint8_t *fetch_data;
    uint32_t fetch_base, fetch_end;
    uint32_t prefetch_pc;
    uint16_t prefetch_word;
    bool prefetch_valid;
    bool fetch_immutable;
} h8s_cpu_t;

void     h8s_cpu_init(h8s_cpu_t *cpu, address_bus_t *bus);
void     h8s_cpu_reset(h8s_cpu_t *cpu);
void     h8s_cpu_step(h8s_cpu_t *cpu);
/* Execute an event-bounded Classic batch without a host call per guest
 * instruction. Debts and I/O exits preserve the single-step ordering. */
int      h8s_cpu_run(h8s_cpu_t *cpu, int limit, int frame_cycle,
                     int *timer_debt, int *completion_debt, bool *io_access);
void     h8s_cpu_request_interrupt(h8s_cpu_t *cpu, int vector);
void     h8s_cpu_cancel_interrupt(h8s_cpu_t *cpu, int vector);
uint32_t h8s_cpu_get_er(const h8s_cpu_t *cpu, int n);
void     h8s_cpu_set_er(h8s_cpu_t *cpu, int n, uint32_t value);
void     h8s_cpu_dump_registers(const h8s_cpu_t *cpu);

#endif

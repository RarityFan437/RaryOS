#include "lapic.h"
#include "idt.h"
#include "irq.h"
#include "stdio.h"

#define LAPIC_BASE       0xFEE00000UL
#define LAPIC_EOI_REG    0x0B0
#define LAPIC_SVR        0x0F0
#define LAPIC_TPR        0x080
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

extern volatile uint64_t pit_ticks;

static volatile uint64_t lapic_ticks = 0;

static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(LAPIC_BASE + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(LAPIC_BASE + reg) = val;
}

void lapic_init(void) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= (1u << 11);
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

    uint32_t svr = lapic_read(LAPIC_SVR);
    if ((svr & 0xFF) == 0) svr = (svr & ~0xFFu) | 0xFFu;
    svr |= (1u << 8);
    lapic_write(LAPIC_SVR, svr);
    lapic_write(LAPIC_TPR, 0);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI_REG, 0);
}

static void lapic_timer_handler(struct regs* r) {
    (void)r;
    lapic_ticks++;
    lapic_eoi();
}

void lapic_timer_init(uint32_t hz) {
    lapic_write(LAPIC_TIMER_DIV, 0x3);

    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFFu);

    uint64_t start = pit_ticks;
    while (pit_ticks - start < 5) asm volatile("hlt");

    uint32_t remaining = lapic_read(LAPIC_TIMER_CUR);
    uint32_t elapsed   = 0xFFFFFFFFu - remaining;

    uint32_t ticks_per_period = elapsed / (50u * hz / 1000u);
    if (ticks_per_period < 32) ticks_per_period = 32;

    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_VECTOR | (1u << 16));
    lapic_write(LAPIC_TIMER_INIT, ticks_per_period);

    irq_register(LAPIC_TIMER_VECTOR, lapic_timer_handler, "LAPIC Timer");

    printf("LAPIC: timer at %d Hz (init=%d)\n", (int)hz, (int)ticks_per_period);
}

uint64_t lapic_get_ticks(void) {
    return lapic_ticks;
}

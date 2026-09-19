#include "irq.h"
#include "stdio.h"

#define IRQ_TABLE_SIZE 256

typedef struct {
    irq_handler_t handler;
    const char*   name;
    uint64_t      count;
} irq_entry_t;

static irq_entry_t irq_table[IRQ_TABLE_SIZE];

static inline void outb_irq(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void irq_init(void) {
    for (int i = 0; i < IRQ_TABLE_SIZE; i++) {
        irq_table[i].handler = 0;
        irq_table[i].name = 0;
        irq_table[i].count = 0;
    }
}

int irq_register(uint8_t vector, irq_handler_t handler, const char* name) {
    if (irq_table[vector].handler != 0) return -1;
    irq_table[vector].handler = handler;
    irq_table[vector].name = name;
    irq_table[vector].count = 0;
    return 0;
}

void irq_unregister(uint8_t vector) {
    irq_table[vector].handler = 0;
    irq_table[vector].name = 0;
}

void irq_dispatch(struct regs* r) {
    uint8_t v = (uint8_t)r->vector;

    if (v >= 32 && v < 48) {
        if (v >= 40) outb_irq(0xA0, 0x20);
        outb_irq(0x20, 0x20);
    } else if (v >= 48) {
        *(volatile uint32_t*)0xFEE000B0 = 0;
    }

    if (irq_table[v].handler) {
        irq_table[v].count++;
        irq_table[v].handler(r);
    }
}

uint64_t irq_get_count(uint8_t vector) {
    return irq_table[vector].count;
}

const char* irq_get_name(uint8_t vector) {
    return irq_table[vector].name;
}
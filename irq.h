#pragma once
#include <stdint.h>
#include "idt.h"

typedef void (*irq_handler_t)(struct regs* r);

void irq_init(void);
int  irq_register(uint8_t vector, irq_handler_t handler, const char* name);
void irq_unregister(uint8_t vector);
void irq_dispatch(struct regs* r);
uint64_t irq_get_count(uint8_t vector);
const char* irq_get_name(uint8_t vector);
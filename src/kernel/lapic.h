#pragma once
#include <stdint.h>

#define LAPIC_TIMER_VECTOR 48

void lapic_init(void);
void lapic_eoi(void);
void lapic_timer_init(uint32_t hz);
uint64_t lapic_get_ticks(void);

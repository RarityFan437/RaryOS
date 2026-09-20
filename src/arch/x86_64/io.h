#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void outl(uint16_t port, uint32_t val);
uint32_t inl(uint16_t port);
void io_wait(void);

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#ifdef __cplusplus
}
#endif

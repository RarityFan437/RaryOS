#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define forced_inline static inline __attribute__((always_inline))

forced_inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "dN"(port) : "memory");
}

forced_inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "dN"(port) : "memory");
    return ret;
}

forced_inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "dN"(port) : "memory");
}

forced_inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "dN"(port) : "memory");
    return ret;
}

forced_inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

forced_inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

forced_inline void io_wait(void) {
    asm volatile ("outb %%al, $0x80" : : "a"(0) : "memory");
}

#undef forced_inline

#ifdef __cplusplus
}
#endif

void insw(uint16_t port, void* addr, uint32_t count);

#include "cpuinfo.h"
#include <stdint.h>

static inline void cpuid(uint32_t leaf,
                         uint32_t* a, uint32_t* b,
                         uint32_t* c, uint32_t* d) {
    asm volatile("cpuid"
                 : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                 : "a"(leaf), "c"(0));
}

void cpu_get_brand(char* out, size_t max) {
    if (max < 49) { if (max) out[0] = 0; return; }

    uint32_t a, b, c, d;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a < 0x80000004) { out[0] = 0; return; }

    uint32_t* p = (uint32_t*)out;
    cpuid(0x80000002, &p[0], &p[1], &p[2], &p[3]);
    cpuid(0x80000003, &p[4], &p[5], &p[6], &p[7]);
    cpuid(0x80000004, &p[8], &p[9], &p[10], &p[11]);
    out[48] = 0;

    int i = 0;
    while (out[i] == ' ') i++;
    if (i > 0) {
        int j = 0;
        while (out[i]) out[j++] = out[i++];
        out[j] = 0;
    }
}

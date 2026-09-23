#include "stdlib.h"
#include "string.h"

void itoa(long value, char* buf, int base) {
    int i = 0;
    char digits[] = "0123456789abcdef";
    int neg = 0;
    unsigned long u;

    if (base == 10 && value < 0) {
        neg = 1;
        u = (unsigned long)(-value);
    } else {
        u = (unsigned long)value;
    }

    if (u == 0) {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }

    while (u) {
        buf[i++] = digits[u % (unsigned long)base];
        u /= (unsigned long)base;
    }

    if (neg) buf[i++] = '-';
    buf[i] = '\0';
    reverse(buf);
}

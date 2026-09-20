#include "string.h"
#include <stddef.h>

void* itoa(long value, char* buffer, int base) {
    if (buffer == NULL)
    {
        int c = 0;
        while (value > 0)
        {
            c++;
            value /= 10;
        }

        return (void*)(long)c;
    }

    int i = 0;
    char* digits = "0123456789ABCDEF";
    int is_negative = 0;
    unsigned long uvalue;

    if (base == 10 && value < 0) {
        is_negative = 1;
        uvalue = (unsigned long)(-value);
    } else {
        uvalue = (unsigned long)value;
    }

    if (uvalue == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return 0;
    }

    while (uvalue != 0) {
        buffer[i++] = digits[uvalue % (unsigned long)base];
        uvalue = uvalue / (unsigned long)base;
    }

    if (is_negative) {
        buffer[i++] = '-';
    }

    buffer[i] = '\0';

    reverse(buffer);

    return 0;
}

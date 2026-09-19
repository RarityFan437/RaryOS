#include "string.h"
#include <stddef.h>

void* itoa(int value, char* buffer, int base) {
    if (buffer == NULL)
    {
        int c = 0;
        while (value > 0)
        {
            c++;
            value /= 10;
        }

        return (void*)c;
    }

    int i = 0;
    char* digits = "0123456789ABCDEF";
    int is_negative = 0;

    if (value == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return 0;
    }

    if (value < 0 && base == 10) {
        is_negative = 1;
        value = -value;
    }

    while (value != 0) {
        buffer[i++] = digits[value % base];
        value = value / base;
    }

    if (is_negative) {
        buffer[i++] = '-';
    }

    buffer[i] = '\0';

    reverse(buffer);
}
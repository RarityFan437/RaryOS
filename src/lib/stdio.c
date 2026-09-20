#include <stdarg.h>
#include "stdlib.h"
#include "syscall.h"

void printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            char c[2] = {format[i], '\0'};
            sys_write(c);
            continue;
        }

        i++;
        char buffer[32];

        if (format[i] == 'l') {
            i++;
            switch (format[i]) {
                case 'd': {
                    long d = va_arg(args, long);
                    itoa(d, buffer, 10);
                    sys_write(buffer);
                    break;
                }
                case 'x': {
                    unsigned long x = va_arg(args, unsigned long);
                    itoa((long)x, buffer, 16);
                    sys_write("0x");
                    sys_write(buffer);
                    break;
                }
                case 'u': {
                    unsigned long u = va_arg(args, unsigned long);
                    itoa((long)u, buffer, 10);
                    sys_write(buffer);
                    break;
                }
                default: {
                    char unknown[4] = {'%', 'l', format[i], '\0'};
                    sys_write(unknown);
                    break;
                }
            }
            continue;
        }

        switch (format[i]) {
            case 's': {
                char* s = va_arg(args, char*);
                sys_write(s);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                itoa((long)d, buffer, 10);
                sys_write(buffer);
                break;
            }
            case 'x': {
                unsigned int x = va_arg(args, unsigned int);
                itoa((long)x, buffer, 16);
                sys_write("0x");
                sys_write(buffer);
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                itoa((long)u, buffer, 10);
                sys_write(buffer);
                break;
            }
            case 'p': {
                void* p = va_arg(args, void*);
                itoa((long)(unsigned long)p, buffer, 16);
                sys_write("0x");
                sys_write(buffer);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                char str_c[2] = {c, '\0'};
                sys_write(str_c);
                break;
            }
            case '%': {
                sys_write("%");
                break;
            }
            default: {
                char unknown[3] = {'%', format[i], '\0'};
                sys_write(unknown);
                break;
            }
        }
    }

    va_end(args);
}

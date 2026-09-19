#include <stdarg.h>
#include "stdlib.h"

extern void terminal_write_string(const char* data);

void printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            char c[2] = {format[i], '\0'};
            terminal_write_string(c);
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
                    terminal_write_string(buffer);
                    break;
                }
                case 'x': {
                    unsigned long x = va_arg(args, unsigned long);
                    itoa((long)x, buffer, 16);
                    terminal_write_string("0x");
                    terminal_write_string(buffer);
                    break;
                }
                case 'u': {
                    unsigned long u = va_arg(args, unsigned long);
                    itoa((long)u, buffer, 10);
                    terminal_write_string(buffer);
                    break;
                }
                default: {
                    char unknown[4] = {'%', 'l', format[i], '\0'};
                    terminal_write_string(unknown);
                    break;
                }
            }
            continue;
        }

        switch (format[i]) {
            case 's': {
                char* s = va_arg(args, char*);
                terminal_write_string(s);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                itoa((long)d, buffer, 10);
                terminal_write_string(buffer);
                break;
            }
            case 'x': {
                unsigned int x = va_arg(args, unsigned int);
                itoa((long)x, buffer, 16);
                terminal_write_string("0x");
                terminal_write_string(buffer);
                break;
            }
            case 'p': {
                void* p = va_arg(args, void*);
                itoa((long)(unsigned long)p, buffer, 16);
                terminal_write_string("0x");
                terminal_write_string(buffer);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                char str_c[2] = {c, '\0'};
                terminal_write_string(str_c);
                break;
            }
            case '%': {
                terminal_write_string("%");
                break;
            }
            default: {
                char unknown[3] = {'%', format[i], '\0'};
                terminal_write_string(unknown);
                break;
            }
        }
    }

    va_end(args);
}

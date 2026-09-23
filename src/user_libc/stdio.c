#include <stdarg.h>
#include <stdint.h>

#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "syscall.h"

void putchar(char c) {
    sys_write(&c, 1);
}

void puts(const char* s) {
    sys_write(s, strlen(s));
    sys_write("\n", 1);
}

int getchar(void) {
    char c;
    long n = sys_read(&c, 1);
    if (n <= 0) return -1;
    return (unsigned char)c;
}

void clear_screen(void) {
    sys_ioctl(IOCTL_CLEAR, 0);
}

uint64_t uptime_ticks(void) {
    return (uint64_t)sys_ioctl(IOCTL_GET_TICKS, 0);
}

void shutdown(void) {
    sys_ioctl(IOCTL_SHUTDOWN, 0);
}

void reboot(void) {
    sys_ioctl(IOCTL_REBOOT, 0);
}

void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buf[64];

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            putchar(fmt[i]);
            continue;
        }

        i++;
        switch (fmt[i]) {
            case 's': {
                const char* s = va_arg(args, const char*);
                sys_write(s, strlen(s));
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                putchar(c);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                itoa((long)d, buf, 10);
                sys_write(buf, strlen(buf));
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                itoa((long)u, buf, 10);
                sys_write(buf, strlen(buf));
                break;
            }
            case 'x': {
                unsigned int x = va_arg(args, unsigned int);
                itoa((long)x, buf, 16);
                sys_write(buf, strlen(buf));
                break;
            }
            case 'l': {
                i++;
                if (fmt[i] == 'd') {
                    long d = va_arg(args, long);
                    itoa(d, buf, 10);
                    sys_write(buf, strlen(buf));
                } else if (fmt[i] == 'x') {
                    unsigned long x = va_arg(args, unsigned long);
                    itoa((long)x, buf, 16);
                    sys_write(buf, strlen(buf));
                } else if (fmt[i] == 'u') {
                    unsigned long u = va_arg(args, unsigned long);
                    itoa((long)u, buf, 10);
                    sys_write(buf, strlen(buf));
                }
                break;
            }
            case '%':
                putchar('%');
                break;
            default:
                putchar('%');
                putchar(fmt[i]);
                break;
        }
    }

    va_end(args);
}

int get_sys_info(struct sys_info* info) {
    return (int)sys_ioctl2(IOCTL_GET_INFO, (long)info, 0);
}
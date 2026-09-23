#pragma once
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

void putchar(char c);
void puts(const char* s);
void printf(const char* fmt, ...);
int  getchar(void);
void clear_screen(void);
uint64_t uptime_ticks(void);
void shutdown(void);
void reboot(void);
int  get_sys_info(struct sys_info* info);

#ifdef __cplusplus
}
#endif

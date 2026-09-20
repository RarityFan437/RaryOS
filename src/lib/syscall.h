#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Консоль */
void sys_write(const char* s);
void sys_putchar(char c);
void sys_clear(void);

/* Система */
void sys_shutdown(void);
void sys_reboot(void);
void sys_halt(void);

/* Время */
uint64_t sys_ticks(void);
uint64_t sys_lapic_ticks(void);

/* Ввод */
int sys_input_available(void);
int sys_read_char(void);

/* Память */
void* sys_malloc(size_t size);
void  sys_free(void* p);

#ifdef __cplusplus
}
#endif

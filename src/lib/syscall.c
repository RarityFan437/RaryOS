#include "syscall.h"
#include "acpi.h"
#include "lapic.h"
#include "input.h"
#include "malloc.h"

extern void terminal_write_string(const char* data);
extern void terminal_putchar(char c);
extern void clear_terminal(void);
extern volatile uint64_t pit_ticks;

void sys_write(const char* s) {
    terminal_write_string(s);
}

void sys_putchar(char c) {
    terminal_putchar(c);
}

void sys_clear(void) {
    clear_terminal();
}

void sys_shutdown(void) {
    acpi_shutdown();
}

void sys_reboot(void) {
    acpi_reboot();
}

void sys_halt(void) {
    asm volatile("hlt");
}

uint64_t sys_ticks(void) {
    return pit_ticks;
}

uint64_t sys_lapic_ticks(void) {
    return lapic_get_ticks();
}

int sys_input_available(void) {
    return input_available();
}

int sys_read_char(void) {
    return input_pop();
}

void* sys_malloc(size_t size) {
    return malloc(size);
}

void sys_free(void* p) {
    free(p);
}

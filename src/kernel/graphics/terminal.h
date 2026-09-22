#include <stdint.h>
#include <stddef.h>

extern size_t terminal_row;
extern size_t terminal_column;
extern uint8_t terminal_color;

void clear_terminal(void);
void terminal_initialize(void);
void terminal_setcolor(uint8_t color);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_write_string(const char* data);

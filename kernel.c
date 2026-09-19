#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "irq.h"
#include "input.h"
#include "stdio.h"
#include "string.h"
#include "multiboot.h"
#include "usb.h"

#define RARYOS_VERSION "v0.1.13"

void init_idt(void);
void pic_remap(void);
void init_pit(void);
void* malloc(size_t size);
void free(void* ptr);

extern int usb_irq_active;

volatile uint64_t pit_ticks = 0;

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;

extern uint8_t kernel_end;
static uint8_t* heap_end = &kernel_end;
static char* heap_limit = &kernel_end;

void init_heap(multiboot_info_t* mbd) {
    if (!(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        heap_limit = (char*)&kernel_end + (16 * 1024 * 1024);
        return;
    }

    multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)(uintptr_t)mbd->mmap_addr;

    uint32_t mmap_end_addr = mbd->mmap_addr + mbd->mmap_length;

    uintptr_t kernel_addr = (uintptr_t)&kernel_end;

    while ((uint32_t)(uintptr_t)mmap < mmap_end_addr) {

        if (mmap->type == 1) {
            if (kernel_addr >= mmap->addr && kernel_addr < (mmap->addr + mmap->len)) {
                heap_limit = (char*)(uintptr_t)(mmap->addr + mmap->len);
                break;
            }
        }

        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }
}

void* sbrk(ptrdiff_t increment) {
    char* prev_heap_end = heap_end;

    uintptr_t current_next = (uintptr_t)(heap_end + increment);
    uintptr_t limit = (uintptr_t)heap_limit;
    uintptr_t start = (uintptr_t)&kernel_end;

    if (current_next > limit || current_next < start) {
        return (void*)-1;
    }

    heap_end += increment;
    return (void*)prev_heap_end;
}

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = (uint16_t*) 0xB8000;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, color);
}

void shift_rows_up()
{
    for (size_t i=1; i<VGA_HEIGHT; i++)
    {
        for (size_t j=0; j<VGA_WIDTH; j++)
        {
            terminal_buffer[(i-1)*VGA_WIDTH + j] = terminal_buffer[(i)*VGA_WIDTH + j];
        }
    }
    return;
}

void clear_down_row()
{
    for (size_t i=0; i<VGA_WIDTH; i++)
    {
        terminal_buffer[(VGA_HEIGHT-1)*VGA_WIDTH+i] = vga_entry(' ', terminal_color);;
    }
}

void scrolling()
{
    shift_rows_up();
    clear_down_row();
    terminal_row = VGA_HEIGHT-1;
    terminal_column = 0;
}

void clear_terminal()
{
    for (size_t i=0; i<VGA_HEIGHT*VGA_WIDTH; i++)
    {
        terminal_buffer[i] = vga_entry(' ', terminal_color);
    }
    terminal_column = 0;
    terminal_row = 0;
}

void terminal_putchar(char c) {
    if (c == '\n')
    {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            scrolling();
        }
        return;
    }
    if (c == '\b')
    {
        if (terminal_column > 0) {
            terminal_column--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
        }
        terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        return;
    }
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            scrolling();
        }
    }
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_write_string(const char* data) {
    terminal_write(data, strlen(data));
}

static inline void outb_main(uint16_t port, uint8_t val) {
    asm volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory" );
}

void init_pit(void) {
    uint32_t divisor = 1193182 / 100;
    outb_main(0x43, 0x36);
    outb_main(0x40, (uint8_t)(divisor & 0xFF));
    outb_main(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler(struct regs* r) {
    (void)r;
    pit_ticks++;
}

extern void ps2_keyboard_handler(struct regs* r);

void kernel_main(uint32_t magic, multiboot_info_t* mbd) {
    terminal_initialize();
    input_init();

    irq_init();
    pic_remap();
    init_pit();
    init_idt();

    irq_register(32, pit_handler, "PIT");
    irq_register(33, ps2_keyboard_handler, "PS/2");

    outb_main(0x21, 0xFC);
    asm volatile("sti");

    init_heap(mbd);

    char* loader_name = "Unknown Bootloader";
    char* kernel_args = "None";

    if (mbd->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) {
        loader_name = (char*)(uintptr_t)mbd->boot_loader_name;
    }

    printf("========================================\n");
    printf("      Welcome to RaryOS %s        \n", RARYOS_VERSION);
    printf("========================================\n\n");

    printf("Loader Name : %s\n", loader_name);

    if (mbd->flags & MULTIBOOT_INFO_MEMORY) {
        uint32_t extended_ram_mb = mbd->mem_upper / 1024;

        printf("Avaible RAM: %d MB\n", extended_ram_mb);
    } else {
        printf("RAM Information: Not provided by bootloader\n");
    }

    printf("\n----------------------------------------\n");

    usb_init();

    printf("----------------------------------------\n");

    uint64_t last_isr = 0;

    while (1) {
        asm volatile("hlt");

        if (usb_get_isr_count() != last_isr) {
            last_isr = usb_get_isr_count();
        }

        while (input_available()) {
            int c = input_pop();
            if (c < 0) break;
            terminal_putchar((char)c);
        }
    }
}